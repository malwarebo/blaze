#pragma once

#include "types.hpp"
#include "task.hpp"
#include <memory>
#include <future>

namespace blaze {

class HttpClientBuilder;

class HttpClient {
public:
    HttpClient();
    explicit HttpClient(const HttpConfig& config);
    ~HttpClient();

    HttpClient(HttpClient&&) noexcept;
    HttpClient& operator=(HttpClient&&) noexcept;

    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    HttpResponse get(const std::string& url,
                     const std::map<std::string, std::string>& headers = {});
    HttpResponse post(const std::string& url, const std::string& body,
                      const std::map<std::string, std::string>& headers = {});
    HttpResponse put(const std::string& url, const std::string& body,
                     const std::map<std::string, std::string>& headers = {});
    HttpResponse patch(const std::string& url, const std::string& body,
                       const std::map<std::string, std::string>& headers = {});
    HttpResponse del(const std::string& url,
                     const std::map<std::string, std::string>& headers = {});
    HttpResponse head(const std::string& url,
                      const std::map<std::string, std::string>& headers = {});
    HttpResponse options(const std::string& url,
                         const std::map<std::string, std::string>& headers = {});
    HttpResponse send(const HttpRequest& request);

    Task<HttpResponse> async_get(const std::string& url,
                                 const std::map<std::string, std::string>& headers = {});
    Task<HttpResponse> async_post(const std::string& url, const std::string& body,
                                  const std::map<std::string, std::string>& headers = {});
    Task<HttpResponse> async_put(const std::string& url, const std::string& body,
                                 const std::map<std::string, std::string>& headers = {});
    Task<HttpResponse> async_patch(const std::string& url, const std::string& body,
                                   const std::map<std::string, std::string>& headers = {});
    Task<HttpResponse> async_del(const std::string& url,
                                 const std::map<std::string, std::string>& headers = {});
    Task<HttpResponse> async_send(HttpRequest request);
    Task<std::pair<size_t, HttpResponse>> async_race(
        std::vector<HttpRequest> requests);

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

    void setHttpVersion(HttpVersion version);

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
    void setCookie(const std::string& name, const std::string& value,
                   const std::string& domain = "");

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
