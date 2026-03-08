#include "blaze.hpp"
#include <iostream>
#include <chrono>

void syncExamples() {
    std::cout << "=== Sync Examples ===" << std::endl;

    blaze::HttpClient client;

    auto response = client.get("https://httpbin.org/get");
    std::cout << "[GET] " << response.status_code << " (" << response.body.size() << " bytes)\n";

    auto post_resp = client.post("https://httpbin.org/post",
                                 R"({"key":"value"})",
                                 {{"Content-Type", "application/json"}});
    std::cout << "[POST] " << post_resp.status_code << "\n";

    auto builder_resp = blaze::HttpClient::builder()
        .url("https://httpbin.org/headers")
        .method("GET")
        .header("X-Custom", "blaze")
        .send();
    std::cout << "[Builder] " << builder_resp.status_code << "\n";
}

blaze::Task<void> asyncExamples() {
    std::cout << "\n=== Async Examples ===" << std::endl;

    blaze::HttpClient client;

    auto response = co_await client.async_get("https://httpbin.org/get");
    std::cout << "[async GET] " << response.status_code
              << " (" << response.body.size() << " bytes)\n";

    auto post_resp = co_await client.async_post("https://httpbin.org/post",
                                                 R"({"async":true})");
    std::cout << "[async POST] " << post_resp.status_code << "\n";
}

blaze::Task<void> parallelExample() {
    std::cout << "\n=== Parallel (when_all) ===" << std::endl;

    blaze::HttpClient client;

    auto start = std::chrono::steady_clock::now();

    auto [r1, r2, r3] = co_await blaze::when_all(
        client.async_get("https://httpbin.org/delay/1"),
        client.async_get("https://httpbin.org/delay/1"),
        client.async_get("https://httpbin.org/delay/1")
    );

    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    std::cout << "3 requests (1s each) completed in " << elapsed.count() << "ms\n";
    std::cout << "Status codes: " << r1.status_code << ", "
              << r2.status_code << ", " << r3.status_code << "\n";
}

blaze::Task<void> raceExample() {
    std::cout << "\n=== Race (async_race) ===" << std::endl;

    blaze::HttpClient client;

    std::vector<blaze::HttpRequest> requests;
    for (int i = 0; i < 3; ++i) {
        blaze::HttpRequest req;
        req.url = "https://httpbin.org/delay/" + std::to_string(i + 1);
        req.method = "GET";
        requests.push_back(std::move(req));
    }

    auto start = std::chrono::steady_clock::now();
    auto [winner, response] = co_await client.async_race(std::move(requests));
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);

    std::cout << "Winner: request " << winner << " in " << elapsed.count() << "ms"
              << " (status " << response.status_code << ")\n";
}

void expectedExample() {
    std::cout << "\n=== Expected<T, E> ===" << std::endl;

    auto doRequest = []() -> blaze::Expected<blaze::HttpResponse, blaze::HttpError> {
        blaze::HttpClient client;
        auto resp = client.get("https://httpbin.org/status/404");
        if (!resp.success)
            return blaze::Unexpected(blaze::HttpError{resp.error_type, resp.error_message});
        return resp;
    };

    auto result = doRequest();
    if (result) {
        std::cout << "Success: " << result->status_code << "\n";
    } else {
        std::cout << "Error: " << result.error().message << "\n";
    }
}

int main() {
    try {
        syncExamples();
        blaze::sync_wait(asyncExamples());
        blaze::sync_wait(parallelExample());
        blaze::sync_wait(raceExample());
        expectedExample();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
