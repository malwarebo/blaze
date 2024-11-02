#include <gtest/gtest.h>
#include "http_client.hpp"

class HTTPClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup code before each test
    }

    void TearDown() override {
        // Cleanup code after each test
    }

    HTTPClient client;
};

// Test GET request
TEST_F(HTTPClientTest, BasicGetRequest) {
    auto response = client.get("https://api.github.com/users/octocat");
    EXPECT_TRUE(response.success);
    EXPECT_EQ(response.status_code, 200);
    EXPECT_FALSE(response.body.empty());
}

// Test invalid URL
TEST_F(HTTPClientTest, InvalidURL) {
    auto response = client.get("https://invalid.url.that.does.not.exist");
    EXPECT_FALSE(response.success);
}

// Test headers
TEST_F(HTTPClientTest, CustomHeaders) {
    client.setHeader("User-Agent", "Blaze-Test");
    client.setHeader("Accept", "application/json");
    
    auto response = client.get("https://api.github.com/users/octocat");
    EXPECT_TRUE(response.success);
}

// Test POST request
TEST_F(HTTPClientTest, BasicPostRequest) {
    std::string data = R"({"test": "data"})";
    client.setContentType("application/json");
    
    auto response = client.post("https://postman-echo.com/post", data);
    EXPECT_TRUE(response.success);
    EXPECT_EQ(response.status_code, 200);
}

// Test timeout
TEST_F(HTTPClientTest, Timeout) {
    client.setTimeout(1); // 1 second timeout
    auto response = client.get("https://httpbin.org/delay/2");
    EXPECT_FALSE(response.success);
}