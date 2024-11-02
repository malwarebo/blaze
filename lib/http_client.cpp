#include "http_client.hpp"
#include <curl/curl.h>
#include <iostream>
#include <map>

// Constructor
HTTPClient::HTTPClient() {
    // Initialize CURL globally
    curl_global_init(CURL_GLOBAL_ALL);
    curl_initialized_ = true;
}

// Destructor
HTTPClient::~HTTPClient() {
    if (curl_initialized_) {
        curl_global_cleanup();
    }
}

// Callback function to write response data into a string
static size_t writeCallback(void *contents, size_t size, size_t nmemb,
                            std::string *response) {
  size_t totalSize = size * nmemb;
  response->append(static_cast<char *>(contents), totalSize);
  return totalSize;
}

// Helper function to send HTTP request
HTTPClient::Response HTTPClient::sendRequest(const std::string &url,
                                    const std::string &requestType,
                                    const std::string &data) {
    Response response{};
    CURL* curl = curl_easy_init();
    
    if (!curl) {
        response.success = false;
        response.error_message = "Failed to initialize CURL";
        return response;
    }

    // Use RAII for curl cleanup
    struct CURLGuard {
        CURL* curl;
        curl_slist* headers;
        CURLGuard(CURL* c) : curl(c), headers(nullptr) {}
        ~CURLGuard() {
            if (headers) curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
        }
    } guard(curl);

    // Setup headers
    curl_slist* headers = nullptr;
    for (const auto& [key, value] : headers_) {
        std::string header = key + ": " + value;
        headers = curl_slist_append(headers, header.c_str());
    }
    guard.headers = headers;

    // Basic CURL setup
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds_);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response.body);
    
    // SSL/TLS options
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);

    // Set the request type
    if (requestType == "GET") {
      curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (requestType == "PUT") {
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    } else if (requestType == "DELETE") {
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (requestType == "POST") {
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
    }

    // Set the request data (if any)
    if (!data.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    }

    // Perform the request
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        response.success = false;
        response.error_message = curl_easy_strerror(res);
        return response;
    }

    // Get status code
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
    response.success = (response.status_code >= 200 && response.status_code < 300);

    return response;
}

// Send GET request
HTTPClient::Response HTTPClient::get(const std::string &url) {
    return sendRequest(url, "GET", "");
}

// Send PUT request
HTTPClient::Response HTTPClient::put(const std::string &url, const std::string &data) {
    return sendRequest(url, "PUT", data);
}

// Send DELETE request
HTTPClient::Response HTTPClient::del(const std::string &url) {
    return sendRequest(url, "DELETE", "");
}

// Send POST request
HTTPClient::Response HTTPClient::post(const std::string &url, const std::string &data) {
    return sendRequest(url, "POST", data);
}

void HTTPClient::setTimeout(long seconds) {
    timeout_seconds_ = seconds;
}

void HTTPClient::setHeader(const std::string& key, const std::string& value) {
    headers_[key] = value;
}

void HTTPClient::setContentType(const std::string& content_type) {
    headers_["Content-Type"] = content_type;
}
