/**
 * @file http_client.hpp
 * @brief HTTPClient library for making HTTP requests.
 */

#ifndef HTTP_CLIENT_HPP
#define HTTP_CLIENT_HPP

#include <string>
#include <unordered_map>

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

  std::string sendRequest(const std::string &url,
                          const std::string &requestType,
                          const std::string &data,
                          std::unordered_map<std::string, std::string> &headers);

  std::string buildRequest(const std::string &url,
                           const std::string &requestType,
                           const std::string &data);

  /**
   * @brief Sends a GET request to the specified URL.
   * @param url The URL to send the GET request to.
   * @return The response from the server.
   */
  std::string get(const std::string &url, std::unordered_map<std::string, std::string> &headers);

  /**
   * @brief Sends a PUT request to the specified URL with the given data.
   * @param url The URL to send the PUT request to.
   * @param data The data to include in the request body.
   * @return The response from the server.
   */
  std::string put(const std::string &url, const std::string &data, std::unordered_map<std::string, std::string> &headers);

  /**
   * @brief Sends an UPDATE request to the specified URL with the given data.
   * @param url The URL to send the UPDATE request to.
   * @param data The data to include in the request body.
   * @return The response from the server.
   */
  std::string update(const std::string &url, const std::string &data, std::unordered_map<std::string, std::string> &headers);

  /**
   * @brief Sends a DELETE request to the specified URL.
   * @param url The URL to send the DELETE request to.
   * @return The response from the server.
   */
  std::string del(const std::string &url, std::unordered_map<std::string, std::string> &headers);

  /**
   * @brief Sends a POST request to the specified URL with the given data.
   * @param url The URL to send the POST request to.
   * @param data The data to include in the request body.
   * @return The response from the server.
   */
  std::string post(const std::string &url, const std::string &data, std::unordered_map<std::string, std::string> &headers);
};

#endif // HTTP_CLIENT_HPP
