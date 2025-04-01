#include "http_client.hpp"
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace blaze {

// Callback function for writing received data
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    size_t total_size = size * nmemb;
    userp->append(static_cast<char*>(contents), total_size);
    return total_size;
}

// Callback function for headers
static size_t HeaderCallback(char* buffer, size_t size, size_t nitems, std::map<std::string, std::string>* userdata) {
    size_t total_size = size * nitems;
    std::string header(buffer, total_size);
    
    // Remove trailing \r\n
    if (header.size() > 2) {
        header = header.substr(0, header.size() - 2);
        
        // Parse header
        size_t separator = header.find(':');
        if (separator != std::string::npos) {
            std::string name = header.substr(0, separator);
            std::string value = header.substr(separator + 1);
            
            // Trim leading spaces in value
            size_t value_start = value.find_first_not_of(" ");
            if (value_start != std::string::npos) {
                value = value.substr(value_start);
            }
            
            // Convert header name to lowercase for case-insensitive lookup
            std::transform(name.begin(), name.end(), name.begin(), 
                           [](unsigned char c){ return std::tolower(c); });
            
            (*userdata)[name] = value;
        }
    }
    
    return total_size;
}

// Private implementation class
class HttpClient::Impl {
public:
    Impl() {
        curl_global_init(CURL_GLOBAL_ALL);
        timeout_ms = 30000;
        follow_redirects = true;
        max_redirects = 5;
    }
    
    ~Impl() {
        curl_global_cleanup();
    }
    
    HttpResponse performRequest(const HttpRequest& request) {
        CURL* curl = curl_easy_init();
        HttpResponse response;
        response.success = false;
        
        if (!curl) {
            response.error_message = "Failed to initialize curl";
            return response;
        }
        
        std::string responseBody;
        
        // Set URL
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        
        // Set request method
        if (request.method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            if (!request.body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.body.size());
            }
        } else if (request.method == "PUT") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            if (!request.body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.body.size());
            }
        } else if (request.method == "DELETE") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        } else if (request.method != "GET") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
        }
        
        // Set writing callback function
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
        
        // Set header callback function
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
        
        // Set timeout
        int timeout = (request.timeout_ms > 0) ? request.timeout_ms : timeout_ms;
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout);
        
        // Set redirect behavior
        bool follow = request.follow_redirects ? request.follow_redirects : follow_redirects;
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, follow ? 1L : 0L);
        
        int max_redir = (request.max_redirects > 0) ? request.max_redirects : max_redirects;
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, max_redir);
        
        // Set headers
        struct curl_slist* chunk = nullptr;
        
        // Add default headers
        for (const auto& header : defaultHeaders) {
            std::string headerStr = header.first + ": " + header.second;
            chunk = curl_slist_append(chunk, headerStr.c_str());
        }
        
        // Add request-specific headers
        for (const auto& header : request.headers) {
            std::string headerStr = header.first + ": " + header.second;
            chunk = curl_slist_append(chunk, headerStr.c_str());
        }
        
        if (chunk) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
        }
        
        // Set SSL verification (enabled by default)
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
        
        // Perform the request
        CURLcode res = curl_easy_perform(curl);
        
        if (res == CURLE_OK) {
            response.success = true;
            response.body = responseBody;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
        } else {
            response.error_message = curl_easy_strerror(res);
        }
        
        // Clean up
        if (chunk) {
            curl_slist_free_all(chunk);
        }
        curl_easy_cleanup(curl);
        
        return response;
    }
    
    std::map<std::string, std::string> defaultHeaders;
    int timeout_ms;
    bool follow_redirects;
    int max_redirects;
};

// HttpClient implementation
HttpClient::HttpClient() : pimpl(std::make_unique<Impl>()) {
    // Set some sensible default headers
    setDefaultHeader("User-Agent", "Blaze/1.0");
    setDefaultHeader("Accept", "*/*");
}

HttpClient::~HttpClient() = default;

HttpResponse HttpClient::get(const std::string& url, const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "GET";
    request.headers = headers;
    return send(request);
}

HttpResponse HttpClient::post(const std::string& url, 
                            const std::string& body,
                            const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "POST";
    request.body = body;
    request.headers = headers;
    
    // Set Content-Type header if not specified
    if (headers.find("Content-Type") == headers.end() && !body.empty()) {
        request.headers["Content-Type"] = "application/x-www-form-urlencoded";
    }
    
    return send(request);
}

HttpResponse HttpClient::put(const std::string& url, 
                           const std::string& body,
                           const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "PUT";
    request.body = body;
    request.headers = headers;
    
    // Set Content-Type header if not specified
    if (headers.find("Content-Type") == headers.end() && !body.empty()) {
        request.headers["Content-Type"] = "application/x-www-form-urlencoded";
    }
    
    return send(request);
}

HttpResponse HttpClient::del(const std::string& url, const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "DELETE";
    request.headers = headers;
    return send(request);
}

HttpResponse HttpClient::send(const HttpRequest& request) {
    return pimpl->performRequest(request);
}

std::future<HttpResponse> HttpClient::sendAsync(const HttpRequest& request) {
    return std::async(std::launch::async, [this, request]() {
        return this->send(request);
    });
}

void HttpClient::setDefaultHeader(const std::string& name, const std::string& value) {
    pimpl->defaultHeaders[name] = value;
}

void HttpClient::setTimeout(int timeoutMs) {
    pimpl->timeout_ms = timeoutMs;
}

void HttpClient::setFollowRedirects(bool follow) {
    pimpl->follow_redirects = follow;
}

void HttpClient::setMaxRedirects(int max) {
    pimpl->max_redirects = max;
}

} // namespace blaze
