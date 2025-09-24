#include "../lib/http_client.hpp"
#include <iostream>
#include <chrono>
#include <thread>
#include <iomanip>

void printSeparator(const std::string& title) {
    std::cout << "\n" << std::string(50, '=') << std::endl;
    std::cout << "  " << title << std::endl;
    std::cout << std::string(50, '=') << std::endl;
}

void printResponse(const blaze::HttpResponse& response) {
    std::cout << "Status: " << response.status_code;
    if (response.isSuccess()) {
        std::cout << " (Success)";
    } else if (response.isClientError()) {
        std::cout << " (Client Error)";
    } else if (response.isServerError()) {
        std::cout << " (Server Error)";
    }
    std::cout << std::endl;
    
    if (response.success) {
        std::cout << "Response size: " << response.body.size() << " bytes" << std::endl;
        std::cout << "Request ID: " << response.request_id << std::endl;
        
        if (response.metrics.total_time.count() > 0) {
            std::cout << "Metrics:" << std::endl;
            std::cout << "  Total time: " << response.metrics.total_time.count() << "ms" << std::endl;
            std::cout << "  Download size: " << response.metrics.download_size << " bytes" << std::endl;
        }
        
        if (!response.body.empty()) {
            std::string preview = response.body.substr(0, 200);
            if (response.body.length() > 200) preview += "...";
            std::cout << "Response preview:\n" << preview << std::endl;
        }
    } else {
        std::cout << "Error: " << response.error_message << std::endl;
    }
}

int main() {
    std::cout << "Blaze HTTP Client v2.0 - Comprehensive Examples" << std::endl;
    
    blaze::HttpClient client;
    client.setLogLevel(blaze::LogLevel::Info);
    client.setTimeout(10000);
    client.setUserAgent("BlazeExample/2.0");
    
    printSeparator("Basic GET Request");
    {
        auto response = client.get("https://httpbin.org/get");
        printResponse(response);
    }
    
    printSeparator("POST with JSON Auto-Detection");
    {
        std::string json_body = R"({"name": "Blaze", "version": "2.0"})";
        auto response = client.post("https://httpbin.org/post", json_body);
        printResponse(response);
    }
    
    printSeparator("Builder Pattern Example");
    {
        auto response = blaze::HttpClient::builder()
            .url("https://httpbin.org/post")
            .method("POST")
            .jsonBody(R"({"builder": "pattern", "easy": true})")
            .header("X-Custom-Header", "builder-example")
            .timeout(5000)
            .send();
        printResponse(response);
    }
    
    printSeparator("Authentication Examples");
    {
        std::cout << "Testing Basic Auth:" << std::endl;
        auto response = blaze::HttpClient::builder()
            .url("https://httpbin.org/basic-auth/user/pass")
            .basicAuth("user", "pass")
            .send();
        printResponse(response);
    }
    
    printSeparator("Error Handling Examples");
    {
        std::cout << "Invalid URL:" << std::endl;
        auto response = client.get("not-a-valid-url");
        printResponse(response);
        
        std::cout << "\n404 Not Found:" << std::endl;
        response = client.get("https://httpbin.org/status/404");
        printResponse(response);
    }
    
    printSeparator("Asynchronous Requests");
    {
        std::cout << "Starting async request..." << std::endl;
        
        auto future = client.sendAsync({"https://httpbin.org/delay/1", "GET"});
        std::cout << "Waiting for response..." << std::endl;
        
        auto response = future.get();
        std::cout << "Async request completed!" << std::endl;
        printResponse(response);
    }
    
    printSeparator("Streaming Response");
    {
        std::cout << "Streaming response from server..." << std::endl;
        
        size_t total_received = 0;
        int chunk_count = 0;
        
        blaze::HttpRequest request;
        request.url = "https://httpbin.org/stream/3";
        request.method = "GET";
        
        auto response = client.streamResponse(request, [&](const char* data, size_t size) {
            total_received += size;
            chunk_count++;
            std::cout << "Received chunk " << chunk_count << ": " << size << " bytes" << std::endl;
            return chunk_count < 2;
        });
        
        std::cout << "Streaming stopped after " << chunk_count << " chunks" << std::endl;
        std::cout << "Total received: " << total_received << " bytes" << std::endl;
    }
    
    printSeparator("Utility Functions Demo");
    {
        std::string original = "Hello World!";
        std::string encoded = blaze::utils::urlEncode(original);
        std::string decoded = blaze::utils::urlDecode(encoded);
        
        std::cout << "URL Encoding:" << std::endl;
        std::cout << "Original: " << original << std::endl;
        std::cout << "Encoded:  " << encoded << std::endl;
        std::cout << "Decoded:  " << decoded << std::endl;
        
        std::string base64_encoded = blaze::utils::base64Encode("Hello Base64!");
        std::string base64_decoded = blaze::utils::base64Decode(base64_encoded);
        
        std::cout << "\nBase64 Encoding:" << std::endl;
        std::cout << "Encoded: " << base64_encoded << std::endl;
        std::cout << "Decoded: " << base64_decoded << std::endl;
        
        std::cout << "\nURL Validation:" << std::endl;
        std::cout << "Valid: https://example.com -> " << blaze::utils::isValidUrl("https://example.com") << std::endl;
        std::cout << "Invalid: not-a-url -> " << blaze::utils::isValidUrl("not-a-url") << std::endl;
    }
    
    std::cout << "\n" << std::string(60, '=') << std::endl;
    std::cout << "All examples completed successfully!" << std::endl;
    std::cout << "Blaze HTTP Client v2.0 features demonstrated:" << std::endl;
    std::cout << "✓ Enhanced error handling and status categorization" << std::endl;
    std::cout << "✓ Streaming response support" << std::endl;
    std::cout << "✓ Multiple authentication methods" << std::endl;
    std::cout << "✓ Builder pattern for request construction" << std::endl;
    std::cout << "✓ Advanced configuration options" << std::endl;
    std::cout << "✓ Comprehensive utility functions" << std::endl;
    std::cout << "✓ Async request support" << std::endl;
    std::cout << std::string(60, '=') << std::endl;
    
    return 0;
}
