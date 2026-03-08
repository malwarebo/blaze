#pragma once

#include <string>
#include <map>
#include <functional>
#include <vector>
#include <chrono>
#include <optional>
#include <variant>

namespace blaze {

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

enum class HttpVersion {
    Default,
    Http1_1,
    Http2,
    Http2TLS,
    Http3
};

enum class LogLevel {
    None,
    Error,
    Warn,
    Info,
    Debug
};

enum class AuthType {
    None,
    Basic,
    Bearer,
    ApiKey
};

struct HttpError {
    ErrorType type{ErrorType::Unknown};
    std::string message;
};

template<typename E>
class Unexpected {
    E error_;
public:
    explicit Unexpected(E e) : error_(std::move(e)) {}
    const E& error() const& { return error_; }
    E&& error() && { return std::move(error_); }
};

template<typename T, typename E = HttpError>
class Expected {
    std::variant<T, E> data_;
public:
    Expected(T val) : data_(std::in_place_index<0>, std::move(val)) {}
    Expected(Unexpected<E> err) : data_(std::in_place_index<1>, std::move(err).error()) {}

    bool has_value() const { return data_.index() == 0; }
    explicit operator bool() const { return has_value(); }

    T& value() & { return std::get<0>(data_); }
    const T& value() const& { return std::get<0>(data_); }
    T&& value() && { return std::get<0>(std::move(data_)); }

    E& error() & { return std::get<1>(data_); }
    const E& error() const& { return std::get<1>(data_); }

    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }
    T* operator->() { return &std::get<0>(data_); }
    const T* operator->() const { return &std::get<0>(data_); }
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
    std::map<std::string, std::string> headers;
    std::string body;
    bool success{false};
    std::string error_message;
    ErrorType error_type{ErrorType::None};
    HttpMetrics metrics;
    std::string request_id;

    bool isSuccess() const { return status_code >= 200 && status_code < 300; }
    bool isRedirect() const { return status_code >= 300 && status_code < 400; }
    bool isClientError() const { return status_code >= 400 && status_code < 500; }
    bool isServerError() const { return status_code >= 500 && status_code < 600; }
    bool isHttpError() const { return isClientError() || isServerError(); }
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
    std::map<std::string, std::string> default_headers;
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
    std::map<std::string, std::string> headers;
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

} // namespace blaze
