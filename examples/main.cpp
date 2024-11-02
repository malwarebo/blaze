#include "http_client.hpp"
#include <iostream>

int main() {
    HTTPClient client;
    client.setTimeout(60);  // 60 seconds timeout
    client.setContentType("application/json");
    client.setHeader("Authorization", "Bearer token123");

    auto response = client.post("https://api.example.com/data", "{\"key\":\"value\"}");
    if (response.success) {
        std::cout << "Response: " << response.body << std::endl;
    } else {
        std::cerr << "Error: " << response.error_message << std::endl;
        std::cerr << "Status code: " << response.status_code << std::endl;
    }

    return 0;
}
