#pragma once

#include <string>
#include <map>
#include <memory>
#include <functional>
#include <vector>
#include <future>
#include <chrono>
#include <optional>

namespace blaze {

enum class ErrorType {
    None,
    NetworkError,
    TimeoutError,
    SSLError,
    InvalidUrl,
    ResponseTooLarge,
    Unknown
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

class HttpClientBuilder;

class HttpClient {
public:
    HttpClient();
    explicit HttpClient(const HttpConfig& config);
    ~HttpClient();

    HttpResponse get(const std::string& url, 
                    const std::map<std::string, std::string>& headers = {});
    
    HttpResponse post(const std::string& url, 
                     const std::string& body,
                     const std::map<std::string, std::string>& headers = {});
    
    HttpResponse put(const std::string& url, 
                    const std::string& body,
                    const std::map<std::string, std::string>& headers = {});
    
    HttpResponse patch(const std::string& url, 
                      const std::string& body,
                      const std::map<std::string, std::string>& headers = {});
    
    HttpResponse del(const std::string& url, 
                    const std::map<std::string, std::string>& headers = {});
    
    HttpResponse head(const std::string& url, 
                     const std::map<std::string, std::string>& headers = {});
    
    HttpResponse options(const std::string& url, 
                        const std::map<std::string, std::string>& headers = {});

    HttpResponse send(const HttpRequest& request);
    
    std::future<HttpResponse> sendAsync(const HttpRequest& request);
    
    HttpResponse sendWithProgress(const HttpRequest& request, ProgressCallback callback);
    
    HttpResponse streamResponse(const HttpRequest& request, StreamCallback callback);
    
    HttpResponse uploadFile(const std::string& url, const std::string& file_path,
                           const std::string& field_name = "file",
                           const std::map<std::string, std::string>& headers = {});
    
    HttpResponse downloadFile(const std::string& url, const std::string& file_path,
                             const std::map<std::string, std::string>& headers = {});

    void setConfig(const HttpConfig& config);
    HttpConfig getConfig() const;
    
    void setDefaultHeader(const std::string& name, const std::string& value);
    void removeDefaultHeader(const std::string& name);
    void clearDefaultHeaders();
    
    void setTimeout(int timeout_ms);
    void setConnectTimeout(int timeout_ms);
    void setFollowRedirects(bool follow);
    void setMaxRedirects(int max_redirects);
    void setUserAgent(const std::string& user_agent);
    void setMaxResponseSize(size_t max_size);
    
    void setAuth(const Auth& auth);
    void setBasicAuth(const std::string& username, const std::string& password);
    void setBearerToken(const std::string& token);
    void setApiKey(const std::string& key, const std::string& header = "X-API-Key");
    void clearAuth();
    
    void setProxy(const ProxyConfig& proxy);
    void clearProxy();
    
    void setSSLConfig(const SSLConfig& ssl);
    void setSSLVerification(bool verify_peer, bool verify_host = true);
    void setSSLCACert(const std::string& ca_cert_path);
    void setSSLClientCert(const std::string& cert_path, const std::string& key_path);
    
    void setRetryConfig(const RetryConfig& retry);
    void enableRetry(int max_attempts = 3);
    void disableRetry();
    
    void addRequestInterceptor(RequestInterceptor interceptor);
    void addResponseInterceptor(ResponseInterceptor interceptor);
    void clearInterceptors();
    
    void setLogLevel(LogLevel level);
    void setLogCallback(LogCallback callback);
    
    void enableConnectionPooling(int max_connections = 10);
    void disableConnectionPooling();
    
    void clearCookies();
    void setCookie(const std::string& name, const std::string& value, const std::string& domain = "");
    
    HttpMetrics getConnectionMetrics() const;
    void resetMetrics();
    
    static HttpClientBuilder builder();

private:
    class Impl;
    std::unique_ptr<Impl> pimpl;
};

class HttpClientBuilder {
public:
    HttpClientBuilder& url(const std::string& url);
    HttpClientBuilder& method(const std::string& method);
    HttpClientBuilder& header(const std::string& name, const std::string& value);
    HttpClientBuilder& headers(const std::map<std::string, std::string>& headers);
    HttpClientBuilder& body(const std::string& body);
    HttpClientBuilder& jsonBody(const std::string& json);
    HttpClientBuilder& formBody(const std::map<std::string, std::string>& form);
    HttpClientBuilder& timeout(int timeout_ms);
    HttpClientBuilder& auth(const Auth& auth);
    HttpClientBuilder& basicAuth(const std::string& username, const std::string& password);
    HttpClientBuilder& bearerToken(const std::string& token);
    HttpClientBuilder& apiKey(const std::string& key, const std::string& header = "X-API-Key");
    HttpClientBuilder& followRedirects(bool follow = true);
    HttpClientBuilder& maxRedirects(int max_redirects);
    HttpClientBuilder& userAgent(const std::string& user_agent);
    HttpClientBuilder& enableMetrics(bool enable = true);
    
    HttpRequest build();
    HttpResponse send();
    std::future<HttpResponse> sendAsync();

private:
    HttpRequest request_;
    HttpClient client_;
};

namespace auth {
    Auth basic(const std::string& username, const std::string& password);
    Auth bearer(const std::string& token);
    Auth apiKey(const std::string& key, const std::string& header = "X-API-Key");
}

namespace utils {
    std::string urlEncode(const std::string& str);
    std::string urlDecode(const std::string& str);
    std::string base64Encode(const std::string& str);
    std::string base64Decode(const std::string& str);
    std::map<std::string, std::string> parseQueryString(const std::string& query);
    std::string buildQueryString(const std::map<std::string, std::string>& params);
    std::string generateRequestId();
    bool isValidUrl(const std::string& url);
}

} // namespace blaze
