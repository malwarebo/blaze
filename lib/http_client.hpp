#pragma once

#include <string>
#include <map>
#include <memory>
#include <functional>
#include <vector>
#include <future>

namespace blaze {

struct HttpResponse {
    int status_code;
    std::map<std::string, std::string> headers;
    std::string body;
    bool success;
    std::string error_message;
};

struct HttpRequest {
    std::string url;
    std::string method = "GET";
    std::map<std::string, std::string> headers;
    std::string body;
    int timeout_ms = 30000; // 30 seconds default timeout
    bool follow_redirects = true;
    int max_redirects = 5;
};

using ResponseCallback = std::function<void(const HttpResponse&)>;

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    // Synchronous requests
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
    
    // Asynchronous requests
    std::future<HttpResponse> sendAsync(const HttpRequest& request);
    
    // Set global default headers for all requests
    void setDefaultHeader(const std::string& name, const std::string& value);
    
    // Set timeout for all requests in milliseconds
    void setTimeout(int timeout_ms);
    
    // Configure redirect behavior
    void setFollowRedirects(bool follow);
    void setMaxRedirects(int max_redirects);

private:
    class Impl;
    std::unique_ptr<Impl> pimpl;
};

} // namespace blaze
