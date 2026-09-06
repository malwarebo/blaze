#include <gtest/gtest.h>

#include "blaze.hpp"
#include "test_server.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>

namespace {

class SyncTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() { server = new blaze_test::TestServer(); }
    static void TearDownTestSuite() {
        delete server;
        server = nullptr;
    }

    void SetUp() override {
        client.set_log_level(blaze::LogLevel::Error);
        client.set_timeout(10000);
    }

    std::string url(const std::string& path) const { return server->url(path); }

    static blaze_test::TestServer* server;
    blaze::HttpClient client;
};

blaze_test::TestServer* SyncTest::server = nullptr;

TEST_F(SyncTest, Get) {
    auto response = client.get(url("/get"));
    ASSERT_TRUE(response.ok()) << response.error_message();
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.is_success());
    EXPECT_FALSE(response.is_http_error());
    EXPECT_NE(response.body.find("\"method\": \"GET\""), std::string::npos);
    EXPECT_FALSE(response.request_id.empty());
}

TEST_F(SyncTest, PostFormBody) {
    auto response = client.post(url("/post"),
                                "test=value&foo=bar",
                                {{"Content-Type", "application/x-www-form-urlencoded"}});
    ASSERT_TRUE(response.ok()) << response.error_message();
    EXPECT_EQ(200, response.status_code);
    EXPECT_NE(response.body.find("test=value&foo=bar"), std::string::npos);
}

TEST_F(SyncTest, PostInfersJsonContentType) {
    auto response = client.post(url("/post"), R"({"name":"test"})");
    ASSERT_TRUE(response.ok()) << response.error_message();
    EXPECT_NE(response.body.find("application/json"), std::string::npos);
}

TEST_F(SyncTest, PutPatchDelete) {
    auto put = client.put(url("/put"), "{\"a\":1}");
    ASSERT_TRUE(put.ok());
    EXPECT_NE(put.body.find("\"method\": \"PUT\""), std::string::npos);

    auto patch = client.patch(url("/patch"), "{\"b\":2}");
    ASSERT_TRUE(patch.ok());
    EXPECT_NE(patch.body.find("\"method\": \"PATCH\""), std::string::npos);

    auto del = client.del(url("/delete"));
    ASSERT_TRUE(del.ok());
    EXPECT_NE(del.body.find("\"method\": \"DELETE\""), std::string::npos);
}

TEST_F(SyncTest, HeadHasNoBody) {
    auto response = client.head(url("/"));
    ASSERT_TRUE(response.ok()) << response.error_message();
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.body.empty());
    EXPECT_FALSE(response.headers.empty());
}

TEST_F(SyncTest, Options) {
    auto response = client.options(url("/"));
    ASSERT_TRUE(response.ok());
    EXPECT_EQ(200, response.status_code);
}

TEST_F(SyncTest, StatusClassification) {
    auto not_found = client.get(url("/status/404"));
    ASSERT_TRUE(not_found.ok());
    EXPECT_EQ(404, not_found.status_code);
    EXPECT_FALSE(not_found.is_success());
    EXPECT_TRUE(not_found.is_client_error());
    EXPECT_FALSE(not_found.is_server_error());
    EXPECT_TRUE(not_found.is_http_error());

    auto server_error = client.get(url("/status/500"));
    ASSERT_TRUE(server_error.ok());
    EXPECT_EQ(500, server_error.status_code);
    EXPECT_TRUE(server_error.is_server_error());
}

TEST_F(SyncTest, HttpErrorIsNotATransportError) {
    auto response = client.get(url("/status/500"));
    EXPECT_TRUE(response.ok());
    EXPECT_EQ(blaze::ErrorType::None, response.error_type());
}

TEST_F(SyncTest, FollowRedirects) {
    client.set_follow_redirects(true);
    auto followed = client.get(url("/redirect/2"));
    ASSERT_TRUE(followed.ok()) << followed.error_message();
    EXPECT_EQ(200, followed.status_code);

    client.set_follow_redirects(false);
    auto stopped = client.get(url("/redirect/1"));
    ASSERT_TRUE(stopped.ok());
    EXPECT_TRUE(stopped.is_redirect());
    EXPECT_EQ(302, stopped.status_code);
}

TEST_F(SyncTest, MaxRedirectsExceeded) {
    client.set_follow_redirects(true);
    client.set_max_redirects(1);
    auto response = client.get(url("/redirect/5"));
    EXPECT_FALSE(response.ok());
}

TEST_F(SyncTest, DefaultHeaders) {
    client.set_default_header("X-Custom-Header", "custom-value");
    auto response = client.get(url("/headers"));
    ASSERT_TRUE(response.ok());
    EXPECT_NE(response.body.find("custom-value"), std::string::npos);

    client.remove_default_header("X-Custom-Header");
    auto without = client.get(url("/headers"));
    ASSERT_TRUE(without.ok());
    EXPECT_EQ(without.body.find("custom-value"), std::string::npos);
}

TEST_F(SyncTest, RequestHeaderOverridesDefault) {
    client.set_default_header("X-Origin", "default");
    auto response = client.get(url("/headers"), {{"X-Origin", "explicit"}});
    ASSERT_TRUE(response.ok());
    EXPECT_NE(response.body.find("explicit"), std::string::npos);
    EXPECT_EQ(response.body.find("default"), std::string::npos);
}

TEST_F(SyncTest, UserAgent) {
    client.set_user_agent("BlazeTest/1.0");
    auto response = client.get(url("/headers"));
    ASSERT_TRUE(response.ok());
    EXPECT_NE(response.body.find("BlazeTest/1.0"), std::string::npos);
}

TEST_F(SyncTest, BasicAuth) {
    client.set_basic_auth("user", "pass");
    auto response = client.get(url("/basic-auth/user/pass"));
    ASSERT_TRUE(response.ok());
    EXPECT_EQ(200, response.status_code);
    EXPECT_NE(response.body.find("\"authenticated\": true"), std::string::npos);
}

TEST_F(SyncTest, ClearAuthRevokesAccess) {
    client.set_basic_auth("user", "pass");
    client.clear_auth();
    auto response = client.get(url("/basic-auth/user/pass"));
    ASSERT_TRUE(response.ok());
    EXPECT_EQ(401, response.status_code);
}

TEST_F(SyncTest, BearerToken) {
    client.set_bearer_token("token123");
    auto response = client.get(url("/bearer"));
    ASSERT_TRUE(response.ok());
    EXPECT_EQ(200, response.status_code);
    EXPECT_NE(response.body.find("token123"), std::string::npos);
}

TEST_F(SyncTest, ApiKey) {
    client.set_api_key("key123", "X-API-Key");
    auto response = client.get(url("/headers"));
    ASSERT_TRUE(response.ok());
    EXPECT_NE(response.body.find("key123"), std::string::npos);
}

TEST_F(SyncTest, Timeout) {
    client.set_timeout(200);
    auto response = client.get(url("/delay/3"));
    EXPECT_FALSE(response.ok());
    EXPECT_EQ(blaze::ErrorType::TimeoutError, response.error_type());
}

TEST_F(SyncTest, InvalidUrl) {
    auto response = client.get("not-a-url");
    EXPECT_FALSE(response.ok());
    EXPECT_EQ(blaze::ErrorType::InvalidUrl, response.error_type());
}

TEST_F(SyncTest, ConnectionRefused) {
    auto response = client.get("http://127.0.0.1:9/nothing");
    EXPECT_FALSE(response.ok());
    EXPECT_EQ(blaze::ErrorType::NetworkError, response.error_type());
}

TEST_F(SyncTest, MaxResponseSizeEnforced) {
    client.set_max_response_size(1024);
    auto response = client.get(url("/bytes/4096"));
    EXPECT_FALSE(response.ok());
    EXPECT_EQ(blaze::ErrorType::ResponseTooLarge, response.error_type());
}

TEST_F(SyncTest, MaxResponseSizeAllowsSmallerBodies) {
    client.set_max_response_size(8192);
    auto response = client.get(url("/bytes/2048"));
    ASSERT_TRUE(response.ok());
    EXPECT_EQ(2048u, response.body.size());
}

TEST_F(SyncTest, Metrics) {
    blaze::HttpRequest request;
    request.url = url("/get");
    request.enable_metrics = true;

    auto response = client.send(request);
    ASSERT_TRUE(response.ok());
    EXPECT_GT(response.metrics.download_size, 0u);
}

TEST_F(SyncTest, ResetMetrics) {
    (void)client.get(url("/get"));
    client.reset_metrics();
    EXPECT_EQ(std::chrono::milliseconds(0), client.connection_metrics().total_time);
}

TEST_F(SyncTest, RequestInterceptor) {
    client.add_request_interceptor(
        [](blaze::HttpRequest& request) { request.headers["X-Intercepted"] = "yes"; });
    auto response = client.get(url("/headers"));
    ASSERT_TRUE(response.ok());
    EXPECT_NE(response.body.find("\"yes\""), std::string::npos);
}

TEST_F(SyncTest, ResponseInterceptor) {
    bool called = false;
    client.add_response_interceptor([&called](blaze::HttpResponse&) { called = true; });
    (void)client.get(url("/get"));
    EXPECT_TRUE(called);
}

TEST_F(SyncTest, RetriesConfiguredStatusCodes) {
    blaze::RetryConfig retry;
    retry.max_attempts = 3;
    retry.initial_delay = std::chrono::milliseconds(1);
    retry.retry_status_codes = {429};
    client.set_retry_config(retry);

    int attempts = 0;
    client.add_request_interceptor([&attempts](blaze::HttpRequest&) { ++attempts; });

    auto response = client.get(url("/status/429"));
    ASSERT_TRUE(response.ok());
    EXPECT_EQ(429, response.status_code);
    EXPECT_EQ(1, attempts);
}

TEST_F(SyncTest, DoesNotRetrySuccessfulStatus) {
    blaze::RetryConfig retry;
    retry.max_attempts = 3;
    retry.initial_delay = std::chrono::milliseconds(1);
    client.set_retry_config(retry);

    auto start = std::chrono::steady_clock::now();
    auto response = client.get(url("/get"));
    auto elapsed = std::chrono::steady_clock::now() - start;

    ASSERT_TRUE(response.ok());
    EXPECT_LT(std::chrono::duration_cast<std::chrono::seconds>(elapsed).count(), 2);
}

TEST_F(SyncTest, StreamResponse) {
    blaze::HttpRequest request;
    request.url = url("/stream/3");

    std::string collected;
    auto response =
        client.stream_response(request, [&collected](const char* data, size_t size) {
            collected.append(data, size);
            return true;
        });

    ASSERT_TRUE(response.ok()) << response.error_message();
    EXPECT_NE(collected.find("\"index\": 0"), std::string::npos);
    EXPECT_NE(collected.find("\"index\": 2"), std::string::npos);
}

TEST_F(SyncTest, StreamResponseCancellation) {
    blaze::HttpRequest request;
    request.url = url("/stream/10");

    int chunks = 0;
    auto response = client.stream_response(request, [&chunks](const char*, size_t) {
        ++chunks;
        return chunks < 2;
    });

    EXPECT_FALSE(response.ok());
    EXPECT_LT(chunks, 10);
}

TEST_F(SyncTest, SendWithProgress) {
    blaze::HttpRequest request;
    request.url = url("/bytes/4096");

    bool progressed = false;
    auto response = client.send_with_progress(request, [&progressed](size_t, size_t) {
        progressed = true;
        return true;
    });

    ASSERT_TRUE(response.ok()) << response.error_message();
    EXPECT_TRUE(progressed);
}

TEST_F(SyncTest, ProgressCallbackCanCancel) {
    blaze::HttpRequest request;
    request.url = url("/delay/2");

    auto response =
        client.send_with_progress(request, [](size_t, size_t) { return false; });

    EXPECT_FALSE(response.ok());
    EXPECT_EQ(blaze::ErrorType::Cancelled, response.error_type());
}

TEST_F(SyncTest, DownloadFile) {
    std::string path = std::string(::testing::TempDir()) + "blaze_download.bin";
    auto response = client.download_file(url("/bytes/1024"), path);

    ASSERT_TRUE(response.ok()) << response.error_message();
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(file.good());
    EXPECT_EQ(1024, static_cast<int>(file.tellg()));
    file.close();
    std::remove(path.c_str());
}

TEST_F(SyncTest, Builder) {
    auto response = blaze::HttpClient::builder()
                        .url(url("/post"))
                        .method("POST")
                        .json_body(R"({"builder":true})")
                        .header("X-Builder", "yes")
                        .send();

    ASSERT_TRUE(response.ok()) << response.error_message();
    EXPECT_EQ(200, response.status_code);
    EXPECT_NE(response.body.find("builder"), std::string::npos);
}

TEST_F(SyncTest, BuilderFormBody) {
    auto response = blaze::HttpClient::builder()
                        .url(url("/post"))
                        .method("POST")
                        .form_body({{"key1", "value1"}})
                        .send();

    ASSERT_TRUE(response.ok());
    EXPECT_NE(response.body.find("key1=value1"), std::string::npos);
}

TEST_F(SyncTest, BuilderBuildReturnsRequest) {
    auto request = blaze::HttpClient::builder()
                       .url("http://example.com")
                       .method("PUT")
                       .body("payload")
                       .build();

    EXPECT_EQ("http://example.com", request.url);
    EXPECT_EQ("PUT", request.method);
    EXPECT_EQ("payload", request.body);
}

TEST_F(SyncTest, CustomConfig) {
    blaze::HttpConfig config;
    config.timeout_ms = 5000;
    config.user_agent = "CustomAgent/2.0";
    config.default_headers["X-Config"] = "configured";

    blaze::HttpClient configured(config);
    auto response = configured.get(url("/headers"));

    ASSERT_TRUE(response.ok());
    EXPECT_NE(response.body.find("CustomAgent/2.0"), std::string::npos);
    EXPECT_NE(response.body.find("configured"), std::string::npos);
}

TEST_F(SyncTest, ConfigRoundTrips) {
    blaze::HttpConfig config;
    config.timeout_ms = 1234;
    client.set_config(config);
    EXPECT_EQ(1234, client.config().timeout_ms);
}

}  // namespace
