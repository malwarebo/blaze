# Blaze HTTP Client

A modern, lightweight, and easy-to-use C++ HTTP client library built on top of libcurl. Designed for simplicity and efficiency, Blaze provides a clean interface for making HTTP requests while handling all the complexity of HTTP communications under the hood.

## Features

- 🚀 Simple and intuitive API
- 🔒 Built-in SSL/TLS support
- 📡 Support for all standard HTTP methods (GET, POST, PUT, DELETE)
- ⚙️ Configurable timeout settings
- 🎯 Custom header management
- ⚡ Thread-safe design
- 🛡️ Exception-safe operations
- 📝 Comprehensive error handling
- 🔄 Automatic memory management

## Requirements

- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.12 or higher
- libcurl development files
- Google Test (for testing)

### Installing Dependencies

#### Ubuntu/Debian
```bash
sudo apt-get update
sudo apt-get install build-essential cmake libcurl4-openssl-dev
```

#### Fedora
```bash
sudo dnf install gcc-c++ cmake libcurl-devel
```

#### macOS
```bash
brew install cmake curl
```

#### Windows
Install [vcpkg](https://github.com/Microsoft/vcpkg) and then:
```bash
vcpkg install curl:x64-windows
```

## Building

```bash
# Clone the repository
git clone https://github.com/yourusername/blaze-http.git
cd blaze-http

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make

# Optionally build examples
cmake -DBUILD_EXAMPLES=ON ..
make
```

## Usage

### Basic GET Request
```cpp
#include <blaze/http_client.hpp>
#include <iostream>

int main() {
    HTTPClient client;
    
    auto response = client.get("https://api.example.com/data");
    if (response.success) {
        std::cout << "Status: " << response.status_code << std::endl;
        std::cout << "Body: " << response.body << std::endl;
    }
}
```

### POST Request with Headers
```cpp
HTTPClient client;

// Configure client
client.setContentType("application/json");
client.setHeader("Authorization", "Bearer your-token-here");

// Make POST request
std::string data = R"({"key": "value"})";
auto response = client.post("https://api.example.com/create", data);

if (response.success) {
    std::cout << "Created successfully!" << std::endl;
} else {
    std::cerr << "Error: " << response.error_message << std::endl;
}
```

### Handling Timeouts
```cpp
HTTPClient client;
client.setTimeout(30); // 30 seconds timeout

auto response = client.get("https://api.example.com/data");
```

## API Reference

### HTTPClient Class
```cpp
class HTTPClient {
public:
    // Constructor and destructor
    HTTPClient();
    ~HTTPClient();

    // Configuration methods
    void setTimeout(long seconds);
    void setHeader(const std::string& key, const std::string& value);
    void setContentType(const std::string& content_type);

    // HTTP methods
    Response get(const std::string& url);
    Response post(const std::string& url, const std::string& data);
    Response put(const std::string& url, const std::string& data);
    Response del(const std::string& url);
};
```

### Response Structure
```cpp
struct Response {
    std::string body;        // Response body
    long status_code;        // HTTP status code
    std::string error_message; // Error message if any
    bool success;            // True if request was successful
};
```

## Error Handling

The library uses a combination of error codes and messages to handle errors:
- HTTP status codes are available in `response.status_code`
- Detailed error messages are provided in `response.error_message`
- Quick success check via `response.success`

```cpp
auto response = client.get("https://api.example.com/data");
if (!response.success) {
    std::cerr << "Request failed with status " << response.status_code << std::endl;
    std::cerr << "Error: " << response.error_message << std::endl;
}
```

## Thread Safety

The `HTTPClient` class is designed to be thread-safe for different instances. However, using the same instance across multiple threads requires external synchronization.

## Acknowledgments

- Built with [libcurl](https://curl.se/libcurl/)
- Inspired by modern C++ design principles

## Support

If you encounter any issues or have questions, please file an issue on the GitHub repository.
