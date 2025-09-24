#include <gtest/gtest.h>
#include "../lib/http_client.hpp"
#include <string>
#include <thread>
#include <chrono>
#include <fstream>

class HttpClientTest : public ::testing::Test {
protected:
    blaze::HttpClient client;
    
    void SetUp() override {
        client.setLogLevel(blaze::LogLevel::Error);
        client.setTimeout(10000);
    }
};

TEST_F(HttpClientTest, GetRequest) {
    auto response = client.get("https://httpbin.org/get");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.isSuccess());
    EXPECT_FALSE(response.isHttpError());
    EXPECT_FALSE(response.body.empty());
    EXPECT_TRUE(response.body.find("\"url\": \"https://httpbin.org/get\"") != std::string::npos);
    EXPECT_FALSE(response.request_id.empty());
}

TEST_F(HttpClientTest, PostRequest) {
    std::string body = "test=value&foo=bar";
    std::map<std::string, std::string> headers = {
        {"Content-Type", "application/x-www-form-urlencoded"}
    };
    
    auto response = client.post("https://httpbin.org/post", body, headers);
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.isSuccess());
    EXPECT_FALSE(response.body.empty());
    EXPECT_TRUE(response.body.find("\"form\": {") != std::string::npos);
    EXPECT_TRUE(response.body.find("\"test\": \"value\"") != std::string::npos);
}

TEST_F(HttpClientTest, PostJsonRequest) {
    std::string json_body = R"({"name": "test", "value": 123})";
    
    auto response = client.post("https://httpbin.org/post", json_body);
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.body.find("\"json\": {") != std::string::npos);
    EXPECT_TRUE(response.body.find("\"name\": \"test\"") != std::string::npos);
}

TEST_F(HttpClientTest, PutRequest) {
    std::string body = "{\"name\": \"test\"}";
    std::map<std::string, std::string> headers = {
        {"Content-Type", "application/json"}
    };
    
    auto response = client.put("https://httpbin.org/put", body, headers);
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.body.find("\"json\": {") != std::string::npos);
    EXPECT_TRUE(response.body.find("\"name\": \"test\"") != std::string::npos);
}

TEST_F(HttpClientTest, PatchRequest) {
    std::string body = "{\"updated\": true}";
    
    auto response = client.patch("https://httpbin.org/patch", body);
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.body.find("\"json\": {") != std::string::npos);
    EXPECT_TRUE(response.body.find("\"updated\": true") != std::string::npos);
}

TEST_F(HttpClientTest, DeleteRequest) {
    auto response = client.del("https://httpbin.org/delete");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.body.find("\"url\": \"https://httpbin.org/delete\"") != std::string::npos);
}

TEST_F(HttpClientTest, HeadRequest) {
    auto response = client.head("https://httpbin.org/");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.body.empty());
    EXPECT_FALSE(response.headers.empty());
}

TEST_F(HttpClientTest, OptionsRequest) {
    auto response = client.options("https://httpbin.org/");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
}

TEST_F(HttpClientTest, StatusCodeHelpers) {
    auto response = client.get("https://httpbin.org/status/404");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(404, response.status_code);
    EXPECT_FALSE(response.isSuccess());
    EXPECT_TRUE(response.isClientError());
    EXPECT_FALSE(response.isServerError());
    EXPECT_TRUE(response.isHttpError());
}

TEST_F(HttpClientTest, ServerErrorStatus) {
    auto response = client.get("https://httpbin.org/status/500");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(500, response.status_code);
    EXPECT_FALSE(response.isSuccess());
    EXPECT_FALSE(response.isClientError());
    EXPECT_TRUE(response.isServerError());
    EXPECT_TRUE(response.isHttpError());
}

TEST_F(HttpClientTest, RedirectStatus) {
    client.setFollowRedirects(false);
    auto response = client.get("https://httpbin.org/redirect/1");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(302, response.status_code);
    EXPECT_TRUE(response.isRedirect());
    EXPECT_FALSE(response.isSuccess());
}

TEST_F(HttpClientTest, DefaultHeaders) {
    client.setDefaultHeader("X-Test-Header", "test-value");
    auto response = client.get("https://httpbin.org/headers");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.body.find("\"X-Test-Header\": \"test-value\"") != std::string::npos);
}

TEST_F(HttpClientTest, RemoveDefaultHeader) {
    client.setDefaultHeader("X-Test-Header", "test-value");
    client.removeDefaultHeader("X-Test-Header");
    auto response = client.get("https://httpbin.org/headers");
    ASSERT_TRUE(response.success);
    EXPECT_FALSE(response.body.find("\"X-Test-Header\": \"test-value\"") != std::string::npos);
}

TEST_F(HttpClientTest, UserAgent) {
    client.setUserAgent("TestAgent/1.0");
    auto response = client.get("https://httpbin.org/headers");
    ASSERT_TRUE(response.success);
    EXPECT_TRUE(response.body.find("\"User-Agent\": \"TestAgent/1.0\"") != std::string::npos);
}

TEST_F(HttpClientTest, BasicAuth) {
    client.setBasicAuth("user", "pass");
    auto response = client.get("https://httpbin.org/basic-auth/user/pass");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.body.find("\"authenticated\": true") != std::string::npos);
}

TEST_F(HttpClientTest, BearerToken) {
    client.setBearerToken("test-token");
    auto response = client.get("https://httpbin.org/bearer");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.body.find("\"authenticated\": true") != std::string::npos);
    EXPECT_TRUE(response.body.find("\"token\": \"test-token\"") != std::string::npos);
}

TEST_F(HttpClientTest, ApiKey) {
    client.setApiKey("test-api-key", "X-API-Key");
    auto response = client.get("https://httpbin.org/headers");
    ASSERT_TRUE(response.success);
    EXPECT_TRUE(response.body.find("\"X-Api-Key\": \"test-api-key\"") != std::string::npos);
}

TEST_F(HttpClientTest, Timeout) {
    client.setTimeout(100);
    auto response = client.get("https://httpbin.org/delay/2");
    EXPECT_FALSE(response.success);
    EXPECT_EQ(blaze::ErrorType::TimeoutError, response.error_type);
}

TEST_F(HttpClientTest, ConnectTimeout) {
    client.setConnectTimeout(100);
    auto response = client.get("http://10.255.255.1/");
    EXPECT_FALSE(response.success);
}

TEST_F(HttpClientTest, FollowRedirects) {
    client.setFollowRedirects(true);
    auto response = client.get("https://httpbin.org/redirect/2");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    
    client.setFollowRedirects(false);
    response = client.get("https://httpbin.org/redirect/2");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(302, response.status_code);
}

TEST_F(HttpClientTest, MaxRedirects) {
    client.setFollowRedirects(true);
    client.setMaxRedirects(1);
    auto response = client.get("https://httpbin.org/redirect/3");
    EXPECT_FALSE(response.success);
}

TEST_F(HttpClientTest, AsyncRequest) {
    auto future = client.sendAsync({"https://httpbin.org/get", "GET"});
    
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto response = future.get();
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_FALSE(response.body.empty());
}

TEST_F(HttpClientTest, StreamResponse) {
    std::string streamed_data;
    
    blaze::HttpRequest request;
    request.url = "https://httpbin.org/stream/3";
    request.method = "GET";
    
    auto response = client.streamResponse(request, [&streamed_data](const char* data, size_t size) {
        streamed_data.append(data, size);
        return true;
    });
    
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_FALSE(streamed_data.empty());
    EXPECT_TRUE(response.body.empty());
}

TEST_F(HttpClientTest, StreamResponseCancellation) {
    blaze::HttpRequest request;
    request.url = "https://httpbin.org/stream/10";
    request.method = "GET";
    
    int chunks_received = 0;
    auto response = client.streamResponse(request, [&chunks_received](const char* data, size_t size) {
        chunks_received++;
        return chunks_received < 2;
    });
    
    EXPECT_FALSE(response.success);
    EXPECT_EQ(2, chunks_received);
}

TEST_F(HttpClientTest, InvalidUrl) {
    auto response = client.get("not-a-url");
    EXPECT_FALSE(response.success);
    EXPECT_EQ(blaze::ErrorType::InvalidUrl, response.error_type);
    EXPECT_FALSE(response.error_message.empty());
}

TEST_F(HttpClientTest, NonExistentDomain) {
    auto response = client.get("https://thisdoesnotexist12345.com");
    EXPECT_FALSE(response.success);
    EXPECT_EQ(blaze::ErrorType::NetworkError, response.error_type);
}

TEST_F(HttpClientTest, HttpMetrics) {
    blaze::HttpRequest request;
    request.url = "https://httpbin.org/get";
    request.enable_metrics = true;
    
    auto response = client.send(request);
    ASSERT_TRUE(response.success);
    
    EXPECT_GT(response.metrics.total_time.count(), 0);
    EXPECT_GE(response.metrics.download_size, 0);
    EXPECT_GE(response.metrics.upload_size, 0);
}

TEST_F(HttpClientTest, RequestInterceptor) {
    bool interceptor_called = false;
    client.addRequestInterceptor([&interceptor_called](blaze::HttpRequest& req) {
        interceptor_called = true;
        req.headers["X-Intercepted"] = "true";
    });
    
    auto response = client.get("https://httpbin.org/headers");
    ASSERT_TRUE(response.success);
    EXPECT_TRUE(interceptor_called);
    EXPECT_TRUE(response.body.find("\"X-Intercepted\": \"true\"") != std::string::npos);
}

TEST_F(HttpClientTest, ResponseInterceptor) {
    bool interceptor_called = false;
    client.addResponseInterceptor([&interceptor_called](blaze::HttpResponse& resp) {
        interceptor_called = true;
        resp.headers["X-Response-Intercepted"] = "true";
    });
    
    auto response = client.get("https://httpbin.org/get");
    ASSERT_TRUE(response.success);
    EXPECT_TRUE(interceptor_called);
    EXPECT_EQ("true", response.headers["X-Response-Intercepted"]);
}

TEST_F(HttpClientTest, RetryOnFailure) {
    client.enableRetry(3);
    
    int attempt_count = 0;
    client.addRequestInterceptor([&attempt_count](blaze::HttpRequest& req) {
        attempt_count++;
    });
    
    auto response = client.get("https://httpbin.org/status/429");
    EXPECT_GE(attempt_count, 1);
}

TEST_F(HttpClientTest, MaxResponseSize) {
    client.setMaxResponseSize(1024);
    auto response = client.get("https://httpbin.org/bytes/2048");
    EXPECT_FALSE(response.success);
    EXPECT_EQ(blaze::ErrorType::ResponseTooLarge, response.error_type);
}

TEST_F(HttpClientTest, CustomConfig) {
    blaze::HttpConfig config;
    config.timeout_ms = 5000;
    config.user_agent = "CustomAgent/1.0";
    config.max_response_size = 1024 * 1024;
    config.follow_redirects = false;
    
    blaze::HttpClient custom_client(config);
    auto response = custom_client.get("https://httpbin.org/headers");
    ASSERT_TRUE(response.success);
    EXPECT_TRUE(response.body.find("\"User-Agent\": \"CustomAgent/1.0\"") != std::string::npos);
}

TEST_F(HttpClientTest, BuilderPattern) {
    auto response = blaze::HttpClient::builder()
        .url("https://httpbin.org/post")
        .method("POST")
        .jsonBody(R"({"test": "value"})")
        .header("X-Custom", "test")
        .timeout(5000)
        .send();
    
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.body.find("\"test\": \"value\"") != std::string::npos);
    EXPECT_TRUE(response.body.find("\"X-Custom\": \"test\"") != std::string::npos);
}

TEST_F(HttpClientTest, BuilderWithAuth) {
    auto response = blaze::HttpClient::builder()
        .url("https://httpbin.org/basic-auth/user/pass")
        .basicAuth("user", "pass")
        .send();
    
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.body.find("\"authenticated\": true") != std::string::npos);
}

TEST_F(HttpClientTest, BuilderFormData) {
    auto response = blaze::HttpClient::builder()
        .url("https://httpbin.org/post")
        .method("POST")
        .formBody({{"key1", "value1"}, {"key2", "value2"}})
        .send();
    
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.body.find("\"form\": {") != std::string::npos);
    EXPECT_TRUE(response.body.find("\"key1\": \"value1\"") != std::string::npos);
}

TEST_F(HttpClientTest, UtilityFunctions) {
    EXPECT_EQ("Hello%20World", blaze::utils::urlEncode("Hello World"));
    EXPECT_EQ("Hello World", blaze::utils::urlDecode("Hello%20World"));
    EXPECT_EQ("SGVsbG8gV29ybGQ=", blaze::utils::base64Encode("Hello World"));
    EXPECT_EQ("Hello World", blaze::utils::base64Decode("SGVsbG8gV29ybGQ="));
    
    auto params = blaze::utils::parseQueryString("key1=value1&key2=value%202");
    EXPECT_EQ("value1", params["key1"]);
    EXPECT_EQ("value 2", params["key2"]);
    
    std::string query = blaze::utils::buildQueryString({{"key1", "value1"}, {"key2", "value 2"}});
    EXPECT_TRUE(query.find("key1=value1") != std::string::npos);
    EXPECT_TRUE(query.find("key2=value%202") != std::string::npos);
    
    EXPECT_TRUE(blaze::utils::isValidUrl("https://example.com"));
    EXPECT_FALSE(blaze::utils::isValidUrl("not-a-url"));
    
    std::string request_id = blaze::utils::generateRequestId();
    EXPECT_FALSE(request_id.empty());
    EXPECT_EQ(36, request_id.length());
}

TEST_F(HttpClientTest, AuthHelpers) {
    auto basic_auth = blaze::auth::basic("user", "pass");
    EXPECT_EQ(blaze::AuthType::Basic, basic_auth.type);
    EXPECT_EQ("user", basic_auth.username);
    EXPECT_EQ("pass", basic_auth.password);
    
    auto bearer_auth = blaze::auth::bearer("token123");
    EXPECT_EQ(blaze::AuthType::Bearer, bearer_auth.type);
    EXPECT_EQ("token123", bearer_auth.token);
    
    auto api_key_auth = blaze::auth::apiKey("key123", "X-API-Key");
    EXPECT_EQ(blaze::AuthType::ApiKey, api_key_auth.type);
    EXPECT_EQ("key123", api_key_auth.token);
    EXPECT_EQ("X-API-Key", api_key_auth.api_key_header);
}

TEST_F(HttpClientTest, ClearAuth) {
    client.setBasicAuth("user", "pass");
    client.clearAuth();
    
    auto response = client.get("https://httpbin.org/basic-auth/user/pass");
    EXPECT_EQ(401, response.status_code);
}

TEST_F(HttpClientTest, SSLVerification) {
    client.setSSLVerification(false);
    auto response = client.get("https://httpbin.org/get");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    
    client.setSSLVerification(true);
    response = client.get("https://httpbin.org/get");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
}

TEST_F(HttpClientTest, ConnectionMetrics) {
    auto metrics = client.getConnectionMetrics();
    client.resetMetrics();
    auto reset_metrics = client.getConnectionMetrics();
    
    EXPECT_EQ(std::chrono::milliseconds(0), reset_metrics.total_time);
}

class HttpClientPerformanceTest : public ::testing::Test {
protected:
    blaze::HttpClient client;
};

TEST_F(HttpClientPerformanceTest, ConcurrentRequests) {
    const int num_requests = 10;
    std::vector<std::future<blaze::HttpResponse>> futures;
    
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < num_requests; ++i) {
        futures.push_back(client.sendAsync({"https://httpbin.org/get", "GET"}));
    }
    
    int success_count = 0;
    for (auto& future : futures) {
        auto response = future.get();
        if (response.success) {
            success_count++;
        }
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    EXPECT_EQ(num_requests, success_count);
    EXPECT_LT(duration.count(), 30000);
}

TEST_F(HttpClientPerformanceTest, ConnectionPooling) {
    client.enableConnectionPooling(5);
    
    auto start = std::chrono::steady_clock::now();
    
    for (int i = 0; i < 10; ++i) {
        auto response = client.get("https://httpbin.org/get");
        EXPECT_TRUE(response.success);
    }
    
    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
    
    EXPECT_LT(duration.count(), 20000);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}