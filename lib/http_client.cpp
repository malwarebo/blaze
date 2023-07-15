#include "http_client.hpp"
#include <curl/curl.h>
#include <iostream>

// Constructor
HTTPClient::HTTPClient() {}

// Destructor
HTTPClient::~HTTPClient() {}

// Callback function to write response data into a string
static size_t writeCallback(void *contents, size_t size, size_t nmemb,
                            std::string *response) {
  size_t totalSize = size * nmemb;
  response->append(static_cast<char *>(contents), totalSize);
  return totalSize;
}

// Helper function to send HTTP request
std::string HTTPClient::sendRequest(const std::string &url,
                                    const std::string &requestType,
                                    const std::string &data) {
  CURL *curl = curl_easy_init();
  std::string response;

  if (curl) {
    // Set the request URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // Set the request type
    if (requestType == "GET") {
      curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
    } else if (requestType == "PUT") {
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    } else if (requestType == "UPDATE") {
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "UPDATE");
    } else if (requestType == "DELETE") {
      curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
    } else if (requestType == "POST") {
      curl_easy_setopt(curl, CURLOPT_POST, 1L);
    }

    // Set the request data (if any)
    if (!data.empty()) {
      curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    }

    // Set the response callback function
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    // Perform the request
    CURLcode res = curl_easy_perform(curl);

    // Check for errors
    if (res != CURLE_OK) {
      std::cerr << "Failed to perform HTTP request: " << curl_easy_strerror(res)
                << std::endl;
    }

    // Cleanup
    curl_easy_cleanup(curl);
  }

  return response;
}

// Helper function to build the HTTP request string
std::string HTTPClient::buildRequest(const std::string &url,
                                     const std::string &requestType,
                                     const std::string &data) {
  std::string request = requestType + " " + url + " HTTP/1.1\r\n";
  request += "Host: " + url + "\r\n";
  request += "Content-Length: " + std::to_string(data.length()) + "\r\n";
  request += "\r\n";
  request += data;
  return request;
}

// Send GET request
std::string HTTPClient::get(const std::string &url) {
  return sendRequest(url, "GET", "");
}

// Send PUT request
std::string HTTPClient::put(const std::string &url, const std::string &data) {
  return sendRequest(url, "PUT", data);
}

// Send UPDATE request
std::string HTTPClient::update(const std::string &url,
                               const std::string &data) {
  return sendRequest(url, "UPDATE", data);
}

// Send DELETE request
std::string HTTPClient::del(const std::string &url) {
  return sendRequest(url, "DELETE", "");
}

// Send POST request
std::string HTTPClient::post(const std::string &url, const std::string &data) {
  return sendRequest(url, "POST", data);
}
