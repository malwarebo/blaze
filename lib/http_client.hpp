#pragma once

#include "types.hpp"
#include "task.hpp"
#include <memory>

namespace blaze {

class HttpClientBuilder;

/// Stops the async engine and joins its threads. Optional: the engine is otherwise
/// intentionally leaked to avoid static-destruction ordering hazards. Call only when
/// no transfers are outstanding.
void shutdown();

class HttpClient {
public:
    HttpClient();
    explicit HttpClient(const HttpConfig& config);
    ~HttpClient();

    HttpClient(HttpClient&&) noexcept;
    HttpClient& operator=(HttpClient&&) noexcept;

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpResponse get(const std::string& url, const Headers& headers = {});
    HttpResponse post(const std::string& url,
                      const std::string& body,
                      const Headers& headers = {});
    HttpResponse put(const std::string& url,
                     const std::string& body,
                     const Headers& headers = {});
    HttpResponse patch(const std::string& url,
                       const std::string& body,
                       const Headers& headers = {});
    HttpResponse del(const std::string& url, const Headers& headers = {});
    HttpResponse head(const std::string& url, const Headers& headers = {});
    HttpResponse options(const std::string& url, const Headers& headers = {});
    HttpResponse send(const HttpRequest& request);

    Task<HttpResponse> async_get(const std::string& url, const Headers& headers = {});
    Task<HttpResponse> async_post(const std::string& url,
                                  const std::string& body,
                                  const Headers& headers = {});
    Task<HttpResponse> async_put(const std::string& url,
                                 const std::string& body,
                                 const Headers& headers = {});
    Task<HttpResponse> async_patch(const std::string& url,
                                   const std::string& body,
                                   const Headers& headers = {});
    Task<HttpResponse> async_del(const std::string& url, const Headers& headers = {});
    Task<HttpResponse> async_send(HttpRequest request);
    Task<std::pair<size_t, HttpResponse>> async_race(std::vector<HttpRequest> requests);

    HttpResponse send_with_progress(const HttpRequest& request, ProgressCallback callback);
    HttpResponse stream_response(const HttpRequest& request, StreamCallback callback);
    HttpResponse upload_file(const std::string& url,
                             const std::string& file_path,
                             const std::string& field_name = "file",
                             const Headers& headers = {});
    HttpResponse download_file(const std::string& url,
                               const std::string& file_path,
                               const Headers& headers = {});

    void set_config(const HttpConfig& config);
    HttpConfig config() const;

    void set_default_header(const std::string& name, const std::string& value);
    void remove_default_header(const std::string& name);
    void clear_default_headers();

    void set_timeout(int timeout_ms);
    void set_connect_timeout(int timeout_ms);
    void set_follow_redirects(bool follow);
    void set_max_redirects(int max_redirects);
    void set_user_agent(const std::string& user_agent);
    void set_max_response_size(size_t max_size);

    void set_auth(const Auth& auth);
    void set_basic_auth(const std::string& username, const std::string& password);
    void set_bearer_token(const std::string& token);
    void set_api_key(const std::string& key, const std::string& header = "X-API-Key");
    void clear_auth();

    void set_proxy(const ProxyConfig& proxy);
    void clear_proxy();

    void set_ssl_config(const SSLConfig& ssl);
    void set_ssl_verification(bool verify_peer, bool verify_host = true);
    void set_ssl_ca_cert(const std::string& ca_cert_path);
    void set_ssl_client_cert(const std::string& cert_path, const std::string& key_path);

    void set_http_version(HttpVersion version);

    void set_retry_config(const RetryConfig& retry);
    void enable_retry(int max_attempts = 3);
    void disable_retry();

    void add_request_interceptor(RequestInterceptor interceptor);
    void add_response_interceptor(ResponseInterceptor interceptor);
    void clear_interceptors();

    void set_log_level(LogLevel level);
    void set_log_callback(LogCallback callback);

    void enable_connection_pooling(int max_connections = 10);
    void disable_connection_pooling();

    void clear_cookies();
    void set_cookie(const std::string& name,
                    const std::string& value,
                    const std::string& domain = "");

    HttpMetrics connection_metrics() const;
    void reset_metrics();

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
    HttpClientBuilder& headers(const Headers& headers);
    HttpClientBuilder& body(const std::string& body);
    HttpClientBuilder& json_body(const std::string& json);
    HttpClientBuilder& form_body(const Headers& form);
    HttpClientBuilder& timeout(int timeout_ms);
    HttpClientBuilder& auth(const Auth& auth);
    HttpClientBuilder& basic_auth(const std::string& username, const std::string& password);
    HttpClientBuilder& bearer_token(const std::string& token);
    HttpClientBuilder& api_key(const std::string& key,
                               const std::string& header = "X-API-Key");
    HttpClientBuilder& follow_redirects(bool follow = true);
    HttpClientBuilder& max_redirects(int max_redirects);
    HttpClientBuilder& user_agent(const std::string& user_agent);
    HttpClientBuilder& enable_metrics(bool enable = true);

    HttpRequest build();
    HttpResponse send();

private:
    HttpRequest request_;
    HttpClient client_;
};

namespace auth {
Auth basic(const std::string& username, const std::string& password);
Auth bearer(const std::string& token);
Auth api_key(const std::string& key, const std::string& header = "X-API-Key");
}  // namespace auth

namespace utils {
std::string url_encode(const std::string& str);
std::string url_decode(const std::string& str);
std::string base64_encode(const std::string& str);
std::string base64_decode(const std::string& str);
std::map<std::string, std::string> parse_query_string(const std::string& query);
std::string build_query_string(const std::map<std::string, std::string>& params);
std::string generate_request_id();
bool is_valid_url(const std::string& url);
}  // namespace utils

}  // namespace blaze
