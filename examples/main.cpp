#include "../lib/http_client.hpp"
#include <iostream>
#include <chrono>
#include <thread>

int main() {
    // Create HTTP client
    blaze::HttpClient client;
    
    // Set some global options
    client.setTimeout(5000); // 5 seconds timeout
    client.setFollowRedirects(true);
    client.setDefaultHeader("X-Custom-Header", "CustomValue");
    
    // Make a simple GET request
    std::cout << "Performing GET request to httpbin.org...\n";
    auto response = client.get("https://httpbin.org/get");
    
    if (response.success) {
        std::cout << "Status Code: " << response.status_code << std::endl;
        std::cout << "Response Body:\n" << response.body << std::endl << std::endl;
    } else {
        std::cerr << "Error: " << response.error_message << std::endl;
    }
    
    // Make a POST request
    std::cout << "Performing POST request to httpbin.org...\n";
    std::map<std::string, std::string> headers = {
        {"Content-Type", "application/json"}
    };
    std::string json_body = R"({"name": "Blaze", "type": "HTTP Client"})";
    
    response = client.post("https://httpbin.org/post", json_body, headers);
    
    if (response.success) {
        std::cout << "Status Code: " << response.status_code << std::endl;
        std::cout << "Response Body:\n" << response.body << std::endl << std::endl;
    } else {
        std::cerr << "Error: " << response.error_message << std::endl;
    }
    
    // Example of asynchronous request
    std::cout << "Performing asynchronous GET request...\n";
    auto future_response = client.sendAsync({"https://httpbin.org/delay/2", "GET"});
    
    // Do other work while request is processing
    std::cout << "Waiting for async response...\n";
    
    // Wait for response
    response = future_response.get();
    
    if (response.success) {
        std::cout << "Async Status Code: " << response.status_code << std::endl;
        std::cout << "Async Response Body:\n" << response.body << std::endl;
    } else {
        std::cerr << "Async Error: " << response.error_message << std::endl;
    }
    
    // Custom request with all options
    blaze::HttpRequest custom_request;
    custom_request.url = "https://httpbin.org/anything";
    custom_request.method = "PATCH";
    custom_request.body = "custom data";
    custom_request.headers = {{"X-Test-Header", "test-value"}};
    custom_request.timeout_ms = 10000;
    custom_request.follow_redirects = true;
    
    std::cout << "Performing custom request...\n";
    response = client.send(custom_request);
    
    if (response.success) {
        std::cout << "Custom Status Code: " << response.status_code << std::endl;
        std::cout << "Custom Response Body:\n" << response.body << std::endl;
    } else {
        std::cerr << "Custom Error: " << response.error_message << std::endl;
    }
    
    return 0;
}
