#include <gtest/gtest.h>

#include "blaze.hpp"
#include "test_server.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace {

using namespace std::chrono_literals;

class AsyncTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { server = new blaze_test::TestServer(); }
    static void TearDownTestSuite() {
        delete server;
        server = nullptr;
    }

    void SetUp() override { client.set_log_level(blaze::LogLevel::Error); }

    std::string url(const std::string& path) const { return server->url(path); }

    static blaze_test::TestServer* server;
    blaze::HttpClient client;
};

blaze_test::TestServer* AsyncTest::server = nullptr;

TEST_F(AsyncTest, AsyncGet) {
    auto response = blaze::sync_wait(client.async_get(url("/get")));
    ASSERT_TRUE(response.ok()) << response.error_message();
    EXPECT_EQ(200, response.status_code);
    EXPECT_NE(response.body.find("\"method\": \"GET\""), std::string::npos);
}

TEST_F(AsyncTest, AsyncPost) {
    auto response = blaze::sync_wait(client.async_post(url("/post"), R"({"async":true})"));
    ASSERT_TRUE(response.ok()) << response.error_message();
    EXPECT_NE(response.body.find("async"), std::string::npos);
}

TEST_F(AsyncTest, AsyncPutPatchDelete) {
    EXPECT_TRUE(blaze::sync_wait(client.async_put(url("/put"), "x")).ok());
    EXPECT_TRUE(blaze::sync_wait(client.async_patch(url("/patch"), "x")).ok());
    EXPECT_TRUE(blaze::sync_wait(client.async_del(url("/delete"))).ok());
}

TEST_F(AsyncTest, AsyncInvalidUrl) {
    auto response = blaze::sync_wait(client.async_get("not-a-url"));
    EXPECT_FALSE(response.ok());
    EXPECT_EQ(blaze::ErrorType::InvalidUrl, response.error_type());
}

TEST_F(AsyncTest, TaskIsLazy) {
    std::atomic<bool> ran{false};

    auto factory = [&]() -> blaze::Task<int> {
        ran = true;
        co_return 7;
    };

    {
        auto task = factory();
        EXPECT_FALSE(ran.load());
    }
    EXPECT_FALSE(ran.load());

    EXPECT_EQ(7, blaze::sync_wait(factory()));
    EXPECT_TRUE(ran.load());
}

TEST_F(AsyncTest, SyncWaitPropagatesException) {
    auto factory = []() -> blaze::Task<int> {
        throw std::runtime_error("boom");
        co_return 0;
    };

    EXPECT_THROW(blaze::sync_wait(factory()), std::runtime_error);
}

TEST_F(AsyncTest, SyncWaitVoidTask) {
    std::atomic<bool> ran{false};

    auto factory = [&]() -> blaze::Task<void> {
        ran = true;
        co_return;
    };

    blaze::sync_wait(factory());
    EXPECT_TRUE(ran.load());
}

TEST_F(AsyncTest, WhenAllRunsConcurrently) {
    auto start = std::chrono::steady_clock::now();

    auto [a, b, c] = blaze::sync_wait(blaze::when_all(client.async_get(url("/delay/1")),
                                                      client.async_get(url("/delay/1")),
                                                      client.async_get(url("/delay/1"))));

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    ASSERT_TRUE(a.ok()) << a.error_message();
    ASSERT_TRUE(b.ok());
    ASSERT_TRUE(c.ok());
    EXPECT_EQ(200, a.status_code);

    // Three 300ms requests in parallel must beat running them back to back.
    EXPECT_LT(elapsed.count(), 750);
}

TEST_F(AsyncTest, WhenAllAwaitsSiblingsWhenOneThrows) {
    std::atomic<bool> sibling_finished{false};

    auto failing = [&]() -> blaze::Task<int> {
        co_await client.async_get(url("/get"));
        throw std::runtime_error("boom");
        co_return 0;
    };

    auto sibling = [&]() -> blaze::Task<int> {
        auto response = co_await client.async_get(url("/delay/1"));
        sibling_finished = true;
        co_return response.status_code;
    };

    EXPECT_THROW(blaze::sync_wait(blaze::when_all(failing(), sibling())),
                 std::runtime_error);

    // The sibling must have been awaited to completion rather than abandoned.
    EXPECT_TRUE(sibling_finished.load());
}

TEST_F(AsyncTest, WhenAllPropagatesLeftmostException) {
    auto first = []() -> blaze::Task<int> {
        throw std::logic_error("first");
        co_return 0;
    };
    auto second = []() -> blaze::Task<int> {
        throw std::runtime_error("second");
        co_return 0;
    };

    EXPECT_THROW(blaze::sync_wait(blaze::when_all(first(), second())), std::logic_error);
}

TEST_F(AsyncTest, RaceReturnsFastest) {
    std::vector<blaze::HttpRequest> requests;
    for (int i = 3; i >= 1; --i) {
        blaze::HttpRequest request;
        request.url = url("/delay/" + std::to_string(i));
        request.method = "GET";
        requests.push_back(std::move(request));
    }

    auto start = std::chrono::steady_clock::now();
    auto [winner, response] = blaze::sync_wait(client.async_race(std::move(requests)));
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    ASSERT_TRUE(response.ok()) << response.error_message();
    EXPECT_EQ(2u, winner);
    EXPECT_LT(elapsed.count(), 750);
}

TEST_F(AsyncTest, RaceWithNoRequests) {
    auto [winner, response] = blaze::sync_wait(client.async_race({}));
    EXPECT_EQ(0u, winner);
    EXPECT_FALSE(response.ok());
}

TEST_F(AsyncTest, AbandoningRunningTaskIsSafe) {
    // Regression: destroying a Task whose frame is still owned by the engine used
    // to free it underneath the loop thread, and the resumed frame then touched a
    // client that had already gone away.
    blaze::HttpClient local;
    for (int i = 0; i < 20; ++i) {
        auto task = local.async_get(url("/delay/2"));
        task.start();
    }
    // No sleep: ~HttpClient cancels and drains the abandoned transfers.
}

TEST_F(AsyncTest, ClientDestructorDrainsPromptly) {
    auto start = std::chrono::steady_clock::now();
    {
        blaze::HttpClient local;
        for (int i = 0; i < 5; ++i) {
            auto task = local.async_get(url("/delay/3"));
            task.start();
        }
    }
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    // Cancellation must beat letting the 900ms requests run to completion.
    EXPECT_LT(elapsed.count(), 700);
}

TEST_F(AsyncTest, AbandoningUnstartedTaskIsSafe) {
    for (int i = 0; i < 20; ++i) {
        auto task = client.async_get(url("/get"));
        (void)task;
    }
}

TEST_F(AsyncTest, TaskIsMovable) {
    auto factory = []() -> blaze::Task<int> { co_return 42; };

    auto task = factory();
    auto moved = std::move(task);
    EXPECT_EQ(42, blaze::sync_wait(std::move(moved)));
}

TEST_F(AsyncTest, ContinuationsRunOffTheEventLoop) {
    // Two continuations must be able to overlap. If resumption happened on the
    // single curl loop thread they would serialise and this would time out.
    std::mutex mutex;
    std::condition_variable cv;
    int arrived = 0;
    bool both_arrived = false;

    auto arrive_and_wait = [&]() -> blaze::Task<bool> {
        co_await client.async_get(url("/get"));

        std::unique_lock<std::mutex> lock(mutex);
        if (++arrived == 2) {
            both_arrived = true;
            cv.notify_all();
        } else {
            cv.wait_for(lock, 3s, [&] { return both_arrived; });
        }
        co_return both_arrived;
    };

    auto [first, second] =
        blaze::sync_wait(blaze::when_all(arrive_and_wait(), arrive_and_wait()));

    EXPECT_TRUE(first);
    EXPECT_TRUE(second);
}

TEST_F(AsyncTest, ManyConcurrentRequests) {
    std::vector<blaze::Task<blaze::HttpResponse>> tasks;
    for (int i = 0; i < 25; ++i) tasks.push_back(client.async_get(url("/get")));

    auto gather = [&]() -> blaze::Task<int> {
        int ok = 0;
        for (auto& task : tasks) {
            task.start();
        }
        for (auto& task : tasks) {
            auto response = co_await task;
            if (response.is_success()) ++ok;
        }
        co_return ok;
    };

    EXPECT_EQ(25, blaze::sync_wait(gather()));
}

TEST_F(AsyncTest, InterceptorsRunForAsyncRequests) {
    std::atomic<int> request_hits{0};
    std::atomic<int> response_hits{0};

    client.add_request_interceptor([&](blaze::HttpRequest&) { ++request_hits; });
    client.add_response_interceptor([&](blaze::HttpResponse&) { ++response_hits; });

    auto response = blaze::sync_wait(client.async_get(url("/get")));

    ASSERT_TRUE(response.ok());
    EXPECT_EQ(1, request_hits.load());
    EXPECT_EQ(1, response_hits.load());
}

}  // namespace
