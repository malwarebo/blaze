#pragma once

#include <string>
#include <string_view>
#include <map>
#include <functional>
#include <vector>
#include <chrono>
#include <optional>
#include <cctype>

namespace blaze {

/// HTTP field names are case-insensitive (RFC 9110 5.1); HTTP/2 lowercases them on the
/// wire.
struct CaseInsensitiveLess {
    using is_transparent = void;

    bool operator()(std::string_view a, std::string_view b) const noexcept {
        size_t n = a.size() < b.size() ? a.size() : b.size();
        for (size_t i = 0; i < n; ++i) {
            unsigned char ca = static_cast<unsigned char>(a[i]);
            unsigned char cb = static_cast<unsigned char>(b[i]);
            ca = static_cast<unsigned char>(std::tolower(ca));
            cb = static_cast<unsigned char>(std::tolower(cb));
            if (ca != cb) return ca < cb;
        }
        return a.size() < b.size();
    }
};

using Headers = std::map<std::string, std::string, CaseInsensitiveLess>;

enum class ErrorType {
    None,
    NetworkError,
    TimeoutError,
    SSLError,
    InvalidUrl,
    ResponseTooLarge,
    Cancelled,
    Unknown
};

enum class HttpVersion { Default, Http1_1, Http2, Http2TLS, Http3 };

enum class LogLevel { None, Error, Warn, Info, Debug };

enum class AuthType { None, Basic, Bearer, ApiKey };

struct HttpError {
    ErrorType type{ErrorType::Unknown};
    std::string message;
};

struct HttpMetrics {
    std::chrono::milliseconds total_time{0};
    std::chrono::milliseconds connect_time{0};
    std::chrono::milliseconds dns_time{0};
    size_t upload_size{0};
    size_t download_size{0};
    double upload_speed{0.0};
    double download_speed{0.0};
};

struct HttpResponse {
    int status_code{0};
    Headers headers;
    std::string body;
    HttpMetrics metrics;
    std::string request_id;

    /// Empty unless the transfer itself failed. A 4xx/5xx is a completed transfer, not an
    /// error.
    std::optional<HttpError> error;

    bool ok() const { return !error.has_value(); }
    explicit operator bool() const { return ok(); }

    ErrorType error_type() const { return error ? error->type : ErrorType::None; }
    const std::string& error_message() const {
        static const std::string empty;
        return error ? error->message : empty;
    }

    bool is_success() const { return ok() && status_code >= 200 && status_code < 300; }
    bool is_redirect() const { return ok() && status_code >= 300 && status_code < 400; }
    bool is_client_error() const { return ok() && status_code >= 400 && status_code < 500; }
    bool is_server_error() const { return ok() && status_code >= 500 && status_code < 600; }
    bool is_http_error() const { return is_client_error() || is_server_error(); }
};

struct Auth {
    AuthType type{AuthType::None};
    std::string username;
    std::string password;
    std::string token;
    std::string api_key_header{"X-API-Key"};
};

struct ProxyConfig {
    std::string url;
    std::string username;
    std::string password;
    bool enabled{false};
};

struct SSLConfig {
    bool verify_peer{true};
    bool verify_host{true};
    std::string ca_cert_path;
    std::string client_cert_path;
    std::string client_key_path;
    std::string ciphers;
    long ssl_version{0};
};

struct RetryConfig {
    int max_attempts{3};
    std::chrono::milliseconds initial_delay{1000};
    double backoff_multiplier{2.0};
    std::chrono::milliseconds max_delay{30000};
    std::vector<int> retry_status_codes{429, 502, 503, 504};
};

struct HttpConfig {
    int timeout_ms{30000};
    int connect_timeout_ms{10000};
    bool follow_redirects{true};
    int max_redirects{5};
    std::string user_agent{"Blaze/2.0"};
    size_t max_response_size{100 * 1024 * 1024};
    bool enable_compression{true};
    bool keep_alive{true};
    int max_connections{10};
    Headers default_headers;
    Auth auth;
    ProxyConfig proxy;
    SSLConfig ssl;
    RetryConfig retry;
    LogLevel log_level{LogLevel::Error};
    HttpVersion http_version{HttpVersion::Default};
};

struct HttpRequest {
    std::string url;
    std::string method{"GET"};
    Headers headers;
    std::string body;
    std::optional<int> timeout_ms;
    std::optional<bool> follow_redirects;
    std::optional<int> max_redirects;
    std::optional<Auth> auth;
    std::string request_id;
    bool enable_metrics{true};
};

using StreamCallback = std::function<bool(const char* data, size_t size)>;
using ResponseCallback = std::function<void(const HttpResponse&)>;
using ProgressCallback = std::function<bool(size_t downloaded, size_t total)>;
using RequestInterceptor = std::function<void(HttpRequest&)>;
using ResponseInterceptor = std::function<void(HttpResponse&)>;
using LogCallback = std::function<void(LogLevel level, const std::string& message)>;

}  // namespace blaze
