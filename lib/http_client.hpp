/**
 * @file http_client.hpp
 * @brief HTTPClient library for making HTTP requests.
 */

#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

#include <string>
#include <unordered_map>
#include <map>

/**
 * @class HTTPClient
 * @brief Class for making HTTP requests.
 */
class HTTPClient {
public:
  /**
   * @brief Default constructor.
   */
  HTTPClient();

  /**
   * @brief Destructor.
   */
  ~HTTPClient();

  // Disable copying
  HTTPClient(const HTTPClient&) = delete;
  HTTPClient& operator=(const HTTPClient&) = delete;

  // Configuration methods
  void setTimeout(long seconds);
  void setHeader(const std::string& key, const std::string& value);
  void setContentType(const std::string& content_type);

  // HTTP methods
  struct Response {
    std::string body;
    long status_code;
    std::string error_message;
    bool success;
  };

  Response get(const std::string& url);
  Response post(const std::string& url, const std::string& data);
  Response put(const std::string& url, const std::string& data);
  Response del(const std::string& url);

private:
  Response sendRequest(const std::string& url, const std::string& requestType, const std::string& data);

  std::map<std::string, std::string> headers_;
  long timeout_seconds_{30};
  bool curl_initialized_{false};
};

#endif // HTTP_CLIENT_HPP
