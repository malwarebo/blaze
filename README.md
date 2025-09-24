# blaze

A modern, comprehensive, and high-performance C++ HTTP client library built on top of libcurl. Designed for production use with enterprise-grade features including connection pooling, authentication, streaming, monitoring, and advanced error handling.

## Features

- **High Performance**: Connection pooling, compression, keep-alive
- **Security**: SSL/TLS configuration, multiple authentication methods
- **Developer Friendly**: Builder pattern, fluent API, comprehensive error handling
- **Monitoring**: Request metrics, logging, progress tracking
- **Reliability**: Retry logic with exponential backoff
- **Streaming**: Response streaming for large data
- **Flexible**: Interceptors, custom configuration, proxy support
- **Utilities**: URL encoding, Base64, query string parsing

## Requirements

- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.14 or higher
- libcurl development files
- Google Test (for testing, optional)

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
git clone https://github.com/your-repo/blaze.git
cd blaze

# Create build directory
mkdir build && cd build

# Configure and build
cmake ..
make -j4

# Run tests (optional)
./blaze_tests

# Run comprehensive example
./blaze_example
```

## Quick Start

### Basic GET Request

```cpp
#include <blaze/http_client.hpp>
#include <iostream>

int main() {
    blaze::HttpClient client;
    
    auto response = client.get("https://api.example.com/data");
    
    if (response.isSuccess()) {
        std::cout << "Status: " << response.status_code << std::endl;
        std::cout << "Body: " << response.body << std::endl;
        std::cout << "Request took: " << response.metrics.total_time.count() << "ms" << std::endl;
    } else {
        std::cerr << "Error: " << response.error_message << std::endl;
    }
}
```

### Builder Pattern (Recommended)

```cpp
auto response = blaze::HttpClient::builder()
    .url("https://api.example.com/users")
    .method("POST")
    .jsonBody(R"({"name": "John", "email": "john@example.com"})")
    .bearerToken("your-auth-token")
    .timeout(5000)
    .enableMetrics()
    .send();

if (response.isSuccess()) {
    std::cout << "User created successfully!" << std::endl;
}
```

## Examples

### Authentication

#### Basic Authentication
```cpp
blaze::HttpClient client;
client.setBasicAuth("username", "password");
auto response = client.get("https://api.example.com/protected");
```

#### Bearer Token
```cpp
auto response = blaze::HttpClient::builder()
    .url("https://api.example.com/data")
    .bearerToken("your-jwt-token")
    .send();
```

#### API Key
```cpp
blaze::HttpClient client;
client.setApiKey("your-api-key", "X-API-Key");
auto response = client.get("https://api.example.com/data");
```

### Streaming Large Responses

```cpp
blaze::HttpRequest request;
request.url = "https://api.example.com/large-dataset";

size_t total_received = 0;
auto response = client.streamResponse(request, [&](const char* data, size_t size) {
    // Process data chunk
    total_received += size;
    std::cout << "Received: " << total_received << " bytes\r" << std::flush;
    
    // Return false to cancel stream
    return total_received < 10 * 1024 * 1024; // Stop at 10MB
});
```

### Progress Tracking

```cpp
auto response = client.sendWithProgress(request, [](size_t downloaded, size_t total) {
    if (total > 0) {
        double percent = (double)downloaded / total * 100.0;
        std::cout << "Progress: " << std::fixed << std::setprecision(1) 
                  << percent << "%\r" << std::flush;
    }
    return true; // Continue download
});
```

### Advanced Configuration

```cpp
blaze::HttpConfig config;
config.timeout_ms = 30000;
config.connect_timeout_ms = 5000;
config.max_response_size = 100 * 1024 * 1024; // 100MB
config.enable_compression = true;
config.keep_alive = true;
config.max_connections = 10;

// SSL Configuration
config.ssl.verify_peer = true;
config.ssl.ca_cert_path = "/path/to/ca-cert.pem";

// Retry Configuration
config.retry.max_attempts = 3;
config.retry.initial_delay = std::chrono::milliseconds(1000);
config.retry.backoff_multiplier = 2.0;

// Proxy Configuration
config.proxy.enabled = true;
config.proxy.url = "http://proxy.company.com:8080";
config.proxy.username = "proxy_user";
config.proxy.password = "proxy_pass";

blaze::HttpClient client(config);
```

### Request/Response Interceptors

```cpp
blaze::HttpClient client;

// Add request interceptor
client.addRequestInterceptor([](blaze::HttpRequest& req) {
    req.headers["X-Request-ID"] = blaze::utils::generateRequestId();
    req.headers["X-Timestamp"] = std::to_string(std::time(nullptr));
});

// Add response interceptor
client.addResponseInterceptor([](blaze::HttpResponse& resp) {
    std::cout << "Response received: " << resp.status_code 
              << " (" << resp.metrics.total_time.count() << "ms)" << std::endl;
});
```

### File Operations

#### Upload File
```cpp
auto response = client.uploadFile(
    "https://api.example.com/upload",
    "/path/to/file.jpg",
    "file", // form field name
    {{"Authorization", "Bearer token"}}
);
```

#### Download File
```cpp
auto response = client.downloadFile(
    "https://api.example.com/download/file.zip",
    "/path/to/save/file.zip"
);
```

### Error Handling with Status Code Helpers

```cpp
auto response = client.get("https://api.example.com/data");

if (response.isSuccess()) {
    // 2xx status codes
    std::cout << "Success: " << response.body << std::endl;
} else if (response.isClientError()) {
    // 4xx status codes
    std::cerr << "Client error: " << response.status_code << std::endl;
} else if (response.isServerError()) {
    // 5xx status codes
    std::cerr << "Server error: " << response.status_code << std::endl;
} else {
    // Network or other errors
    std::cerr << "Network error: " << response.error_message << std::endl;
    std::cerr << "Error type: " << static_cast<int>(response.error_type) << std::endl;
}
```

### Utility Functions

```cpp
// URL encoding/decoding
std::string encoded = blaze::utils::urlEncode("Hello World!");
std::string decoded = blaze::utils::urlDecode("Hello%20World%21");

// Base64 encoding/decoding
std::string base64 = blaze::utils::base64Encode("Hello World!");
std::string original = blaze::utils::base64Decode(base64);

// Query string handling
auto params = blaze::utils::parseQueryString("key1=value1&key2=value2");
std::string query = blaze::utils::buildQueryString({{"key", "value"}, {"foo", "bar"}});

// URL validation
bool valid = blaze::utils::isValidUrl("https://example.com");

// Generate request ID
std::string id = blaze::utils::generateRequestId();
```

## API Reference

### HttpClient Class

```cpp
namespace blaze {
class HttpClient {
public:
    // Constructors
    HttpClient();
    explicit HttpClient(const HttpConfig& config);
    ~HttpClient();
    
    // HTTP Methods
    HttpResponse get(const std::string& url, const Headers& headers = {});
    HttpResponse post(const std::string& url, const std::string& body, const Headers& headers = {});
    HttpResponse put(const std::string& url, const std::string& body, const Headers& headers = {});
    HttpResponse patch(const std::string& url, const std::string& body, const Headers& headers = {});
    HttpResponse del(const std::string& url, const Headers& headers = {});
    HttpResponse head(const std::string& url, const Headers& headers = {});
    HttpResponse options(const std::string& url, const Headers& headers = {});
    
    // Advanced Methods
    HttpResponse send(const HttpRequest& request);
    std::future<HttpResponse> sendAsync(const HttpRequest& request);
    HttpResponse sendWithProgress(const HttpRequest& request, ProgressCallback callback);
    HttpResponse streamResponse(const HttpRequest& request, StreamCallback callback);
    
    // File Operations
    HttpResponse uploadFile(const std::string& url, const std::string& file_path,
                           const std::string& field_name = "file", const Headers& headers = {});
    HttpResponse downloadFile(const std::string& url, const std::string& file_path,
                             const Headers& headers = {});
    
    // Configuration
    void setConfig(const HttpConfig& config);
    HttpConfig getConfig() const;
    void setDefaultHeader(const std::string& name, const std::string& value);
    void removeDefaultHeader(const std::string& name);
    void setTimeout(int timeout_ms);
    void setUserAgent(const std::string& user_agent);
    
    // Authentication
    void setBasicAuth(const std::string& username, const std::string& password);
    void setBearerToken(const std::string& token);
    void setApiKey(const std::string& key, const std::string& header = "X-API-Key");
    void clearAuth();
    
    // SSL/TLS
    void setSSLVerification(bool verify_peer, bool verify_host = true);
    void setSSLCACert(const std::string& ca_cert_path);
    void setSSLClientCert(const std::string& cert_path, const std::string& key_path);
    
    // Proxy
    void setProxy(const ProxyConfig& proxy);
    void clearProxy();
    
    // Retry & Resilience
    void enableRetry(int max_attempts = 3);
    void disableRetry();
    
    // Interceptors
    void addRequestInterceptor(RequestInterceptor interceptor);
    void addResponseInterceptor(ResponseInterceptor interceptor);
    void clearInterceptors();
    
    // Monitoring
    void setLogLevel(LogLevel level);
    void setLogCallback(LogCallback callback);
    HttpMetrics getConnectionMetrics() const;
    void resetMetrics();
    
    // Static Builder
    static HttpClientBuilder builder();
};
}
```

### Key Structures

#### HttpResponse
```cpp
struct HttpResponse {
    int status_code{0};
    std::map<std::string, std::string> headers;
    std::string body;
    bool success{false};
    std::string error_message;
    ErrorType error_type{ErrorType::None};
    HttpMetrics metrics;
    std::string request_id;
    
    // Helper methods
    bool isSuccess() const;      // 2xx
    bool isRedirect() const;     // 3xx
    bool isClientError() const;  // 4xx
    bool isServerError() const;  // 5xx
    bool isHttpError() const;    // 4xx or 5xx
};
```

#### HttpRequest
```cpp
struct HttpRequest {
    std::string url;
    std::string method{"GET"};
    std::map<std::string, std::string> headers;
    std::string body;
    std::optional<int> timeout_ms;
    std::optional<bool> follow_redirects;
    std::optional<int> max_redirects;
    std::optional<Auth> auth;
    std::string request_id;
    bool enable_metrics{true};
};
```

#### HttpMetrics
```cpp
struct HttpMetrics {
    std::chrono::milliseconds total_time{0};
    std::chrono::milliseconds connect_time{0};
    std::chrono::milliseconds dns_time{0};
    size_t upload_size{0};
    size_t download_size{0};
    double upload_speed{0.0};
    double download_speed{0.0};
};
```

### Builder Pattern

```cpp
class HttpClientBuilder {
public:
    HttpClientBuilder& url(const std::string& url);
    HttpClientBuilder& method(const std::string& method);
    HttpClientBuilder& header(const std::string& name, const std::string& value);
    HttpClientBuilder& body(const std::string& body);
    HttpClientBuilder& jsonBody(const std::string& json);
    HttpClientBuilder& formBody(const std::map<std::string, std::string>& form);
    HttpClientBuilder& timeout(int timeout_ms);
    HttpClientBuilder& basicAuth(const std::string& username, const std::string& password);
    HttpClientBuilder& bearerToken(const std::string& token);
    HttpClientBuilder& apiKey(const std::string& key, const std::string& header = "X-API-Key");
    HttpClientBuilder& userAgent(const std::string& user_agent);
    HttpClientBuilder& enableMetrics(bool enable = true);
    
    HttpRequest build();
    HttpResponse send();
    std::future<HttpResponse> sendAsync();
};
```

## Error Types

```cpp
enum class ErrorType {
    None,
    NetworkError,    // DNS resolution, connection failures
    TimeoutError,    // Request timeout
    SSLError,        // SSL/TLS certificate issues
    InvalidUrl,      // Malformed URL
    ResponseTooLarge, // Response exceeds size limit
    Unknown          // Other errors
};
```

## Thread Safety

- **Different instances**: Fully thread-safe
- **Same instance**: Requires external synchronization
- **Connection pooling**: Thread-safe when enabled
- **Async operations**: Safe to use concurrently

## Performance Features

- **Connection Pooling**: Reuses connections for better performance
- **Compression**: Automatic gzip/deflate support
- **Keep-Alive**: Persistent connections
- **Async Operations**: Non-blocking requests
- **Streaming**: Memory-efficient for large responses
- **Metrics**: Built-in performance monitoring

## Best Practices

1. **Use Builder Pattern** for complex requests
2. **Enable Connection Pooling** for high-throughput applications
3. **Set Appropriate Timeouts** for your use case
4. **Handle Errors Properly** using status code helpers
5. **Use Streaming** for large responses
6. **Monitor Performance** with built-in metrics
7. **Configure Retry Logic** for resilient applications
8. **Use Interceptors** for cross-cutting concerns

## Examples in the Wild

Run the comprehensive example to see all features in action:

```bash
cd build
./blaze_example
```

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests for new functionality
5. Run the test suite: `./blaze_tests`
6. Submit a pull request

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Acknowledgments

- Built with [libcurl](https://curl.se/libcurl/) - The multiprotocol file transfer library
- Testing with [Google Test](https://github.com/google/googletest) - Google's C++ test framework
- Inspired by modern HTTP client libraries and C++17 design principles
- Performance optimizations based on production use cases

## Changelog

### v2.0.0

- Complete rewrite with enterprise-grade features
- Builder pattern for fluent API
- Enhanced authentication and SSL support
- Connection pooling and performance optimizations
- Comprehensive metrics and monitoring
- Response streaming capabilities
- Retry logic with exponential backoff
- Request/response interceptors
- Utility functions for common tasks
- 42 comprehensive test cases

### v1.0.0

- Initial release with basic HTTP functionality