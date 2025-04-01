#include <gtest/gtest.h>
#include "../lib/http_client.hpp"
#include <string>
#include <thread>
#include <chrono>

class HttpClientTest : public ::testing::Test {
protected:
    blaze::HttpClient client;
};

TEST_F(HttpClientTest, GetRequest) {
    auto response = client.get("https://httpbin.org/get");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_FALSE(response.body.empty());
    EXPECT_TRUE(response.body.find("\"url\": \"https://httpbin.org/get\"") != std::string::npos);
}

TEST_F(HttpClientTest, PostRequest) {
    std::string body = "test=value&foo=bar";
    std::map<std::string, std::string> headers = {
        {"Content-Type", "application/x-www-form-urlencoded"}
    };
    
    auto response = client.post("https://httpbin.org/post", body, headers);
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_FALSE(response.body.empty());
    EXPECT_TRUE(response.body.find("\"form\": {") != std::string::npos);
    EXPECT_TRUE(response.body.find("\"test\": \"value\"") != std::string::npos);
}

TEST_F(HttpClientTest, PutRequest) {
    std::string body = "{\"name\": \"test\"}";
    std::map<std::string, std::string> headers = {
        {"Content-Type", "application/json"}
    };
    
    auto response = client.put("https://httpbin.org/put", body, headers);
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_FALSE(response.body.empty());
    EXPECT_TRUE(response.body.find("\"json\": {") != std::string::npos);
    EXPECT_TRUE(response.body.find("\"name\": \"test\"") != std::string::npos);
}

TEST_F(HttpClientTest, DeleteRequest) {
    auto response = client.del("https://httpbin.org/delete");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_FALSE(response.body.empty());
    EXPECT_TRUE(response.body.find("\"url\": \"https://httpbin.org/delete\"") != std::string::npos);
}

TEST_F(HttpClientTest, NotFoundStatus) {
    auto response = client.get("https://httpbin.org/status/404");
    ASSERT_TRUE(response.success); // Request succeeded, even though we got 404
    EXPECT_EQ(404, response.status_code);
}

TEST_F(HttpClientTest, DefaultHeaders) {
    client.setDefaultHeader("X-Test-Header", "test-value");
    auto response = client.get("https://httpbin.org/headers");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_TRUE(response.body.find("\"X-Test-Header\": \"test-value\"") != std::string::npos);
}

TEST_F(HttpClientTest, Timeout) {
    // Set a very short timeout
    client.setTimeout(50); // 50ms
    
    auto response = client.get("https://httpbin.org/delay/2");
    EXPECT_FALSE(response.success);
    EXPECT_TRUE(response.error_message.find("timeout") != std::string::npos);
}

TEST_F(HttpClientTest, FollowRedirects) {
    // Enable redirect following
    client.setFollowRedirects(true);
    
    auto response = client.get("https://httpbin.org/redirect/2");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    
    // Disable redirect following
    client.setFollowRedirects(false);
    
    response = client.get("https://httpbin.org/redirect/2");
    ASSERT_TRUE(response.success);
    EXPECT_EQ(302, response.status_code); // Should receive redirect status
}

TEST_F(HttpClientTest, AsyncRequest) {
    auto future = client.sendAsync({"https://httpbin.org/get", "GET"});
    
    // Do other work...
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto response = future.get();
    ASSERT_TRUE(response.success);
    EXPECT_EQ(200, response.status_code);
    EXPECT_FALSE(response.body.empty());
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}