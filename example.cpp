#include "http_client.hpp"
#include <iostream>

int main() {
    HTTPClient client;

    // Example usage
    std::string response = client.get("https://example.com");
    std::cout << "Response: " << response << std::endl;

    response = client.put("https://example.com/resource", "Data to PUT");
    std::cout << "Response: " << response << std::endl;

    response = client.update("https://example.com/resource", "Data to UPDATE");
    std::cout << "Response: " << response << std::endl;

    response = client.del("https://example.com/resource");
    std::cout << "Response: " << response << std::endl;

    response = client.post("https://example.com/resource", "Data to POST");
    std::cout << "Response: " << response << std::endl;

    return 0;
}
