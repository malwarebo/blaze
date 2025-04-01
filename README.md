# blaze

A modern, lightweight, and easy-to-use C++ HTTP client library built on top of libcurl. Designed for simplicity and efficiency, blaze provides a clean interface for making HTTP requests while handling all the complexity of HTTP communications under the hood.

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
    blaze::HttpClient client;
    
    auto response = client.get("https://api.example.com/data");
    if (response.success) {
        std::cout << "Status: " << response.status_code << std::endl;
        std::cout << "Body: " << response.body << std::endl;
    }
}
```

### POST Request with Headers

```cpp
#include <blaze/http_client.hpp>
#include <iostream>

int main() {
    blaze::HttpClient client;
    
    // Set default headers
    client.setDefaultHeader("Authorization", "Bearer your-token-here");
    
    // Make POST request
    std::string data = R"({"key": "value"})";
    std::map<std::string, std::string> headers = {{"Content-Type", "application/json"}};
    
    auto response = client.post("https://api.example.com/create", data, headers);
    
    if (response.success) {
        std::cout << "Created successfully!" << std::endl;
    } else {
        std::cerr << "Error: " << response.error_message << std::endl;
    }
}
```

### Handling Timeouts

```cpp
blaze::HttpClient client;
client.setTimeout(30000); // 30 seconds timeout (in milliseconds)
auto response = client.get("https://api.example.com/data");
```

### Asynchronous Requests

```cpp
#include <blaze/http_client.hpp>
#include <iostream>
#include <future>

int main() {
    blaze::HttpClient client;
    
    // Start an asynchronous request
    auto future_response = client.sendAsync({"https://api.example.com/data", "GET"});
    
    // Do other work while request is processing
    std::cout << "Request started, doing other work..." << std::endl;
    
    // Wait for and process the response
    auto response = future_response.get();
    
    if (response.success) {
        std::cout << "Got async response: " << response.body << std::endl;
    }
}
```

### Using the Request Structure for Complex Requests

```cpp
blaze::HttpRequest request;
request.url = "https://api.example.com/data";
request.method = "PUT";
request.body = R"({"updated": true})";
request.headers = {
    {"Content-Type", "application/json"},
    {"Authorization", "Bearer token"}
};
request.timeout_ms = 5000;      // 5 seconds
request.follow_redirects = true;
request.max_redirects = 3;

auto response = client.send(request);
```

## API Reference

### HttpClient Class

```cpp
namespace blaze {
class HttpClient {
public:
    // Constructor and destructor
    HttpClient();
    ~HttpClient();
    
    // Synchronous HTTP methods
    HttpResponse get(const std::string& url, 
                    const std::map<std::string, std::string>& headers = {});
    
    HttpResponse post(const std::string& url, 
                    const std::string& body,
                    const std::map<std::string, std::string>& headers = {});
    
    HttpResponse put(const std::string& url, 
                    const std::string& body,
                    const std::map<std::string, std::string>& headers = {});
    
    HttpResponse del(const std::string& url, 
                    const std::map<std::string, std::string>& headers = {});
    
    // General request method
    HttpResponse send(const HttpRequest& request);
    
    // Asynchronous request method
    std::future<HttpResponse> sendAsync(const HttpRequest& request);
    
    // Configuration methods
    void setDefaultHeader(const std::string& name, const std::string& value);
    void setTimeout(int timeout_ms);
    void setFollowRedirects(bool follow);
    void setMaxRedirects(int max_redirects);
};
}
```

### HttpRequest Structure

```cpp
namespace blaze {
struct HttpRequest {
    std::string url;
    std::string method = "GET";
    std::map<std::string, std::string> headers;
    std::string body;
    int timeout_ms = 30000; // 30 seconds default timeout
    bool follow_redirects = true;
    int max_redirects = 5;
};
}
```

### HttpResponse Structure

```cpp
namespace blaze {
struct HttpResponse {
    int status_code;
    std::map<std::string, std::string> headers;
    std::string body;
    bool success;
    std::string error_message;
};
}
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

The `HttpClient` class is designed to be thread-safe for different instances. However, using the same instance across multiple threads requires external synchronization.

## Asynchronous Operations

The library supports asynchronous operations through the `sendAsync` method, which returns a `std::future<HttpResponse>`. This allows you to continue execution while the HTTP request is being processed in the background.

## Acknowledgments

- Built with [libcurl](https://curl.se/libcurl/)
- Testing with [Google Test](https://github.com/google/googletest)
- Inspired by modern C++ design principles