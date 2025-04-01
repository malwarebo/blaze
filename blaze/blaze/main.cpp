#include "lib/blaze.h"
#include <iostream>

int main(int argc, char** argv) {
    std::cout << "Blaze HTTP Client Library v" << blaze::getVersion() << std::endl;
    std::cout << "Usage example: Include blaze.h in your project and use the blaze::HttpClient class." << std::endl;
    
    // Example of GET request
    if (argc > 1) {
        std::string url = argv[1];
        std::cout << "Performing GET request to: " << url << std::endl;
        
        blaze::HttpClient client;
        auto response = client.get(url);
        
        if (response.success) {
            std::cout << "Status: " << response.status_code << std::endl;
            std::cout << "Response size: " << response.body.size() << " bytes" << std::endl;
            std::cout << "First 100 chars of response:" << std::endl;
            std::cout << response.body.substr(0, 100) << "..." << std::endl;
        } else {
            std::cerr << "Error: " << response.error_message << std::endl;
        }
    }
    
    return 0;
}
