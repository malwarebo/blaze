#include "http_client.hpp"
#include <curl/curl.h>
#include <iostream>
#include <sstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <random>
#include <fstream>
#include <regex>
#include <algorithm>
#include <iomanip>
#include <queue>
#include <atomic>
#include <cstdio>

namespace blaze {

namespace {
    std::once_flag curl_init_flag;
    void initializeCurl() {
        curl_global_init(CURL_GLOBAL_ALL);
    }
    
    void ensureCurlInitialized() {
        std::call_once(curl_init_flag, initializeCurl);
    }
}

struct WriteCallbackData {
    std::string* response_body;
    StreamCallback stream_callback;
    size_t max_size;
    size_t current_size{0};
    bool use_stream{false};
    bool size_exceeded{false};
};

struct ProgressCallbackData {
    ProgressCallback callback;
    bool cancelled{false};
};

static size_t WriteCallback(void* contents, size_t size, size_t nmemb, WriteCallbackData* data) {
    size_t total_size = size * nmemb;
    
    if (data->current_size + total_size > data->max_size) {
        data->size_exceeded = true;
        return 0;
    }
    
    data->current_size += total_size;
    
    if (data->use_stream && data->stream_callback) {
        if (!data->stream_callback(static_cast<char*>(contents), total_size)) {
            return 0;
        }
    } else if (data->response_body) {
        data->response_body->append(static_cast<char*>(contents), total_size);
    }
    
    return total_size;
}

static size_t HeaderCallback(char* buffer, size_t size, size_t nitems, std::map<std::string, std::string>* userdata) {
    size_t total_size = size * nitems;
    std::string header(buffer, total_size);
    
    if (header.size() > 2) {
        header = header.substr(0, header.size() - 2);
        
        size_t separator = header.find(':');
        if (separator != std::string::npos) {
            std::string name = header.substr(0, separator);
            std::string value = header.substr(separator + 1);
            
            size_t value_start = value.find_first_not_of(" ");
            if (value_start != std::string::npos) {
                value = value.substr(value_start);
            }
            
            (*userdata)[name] = value;
        }
    }
    
    return total_size;
}

static int CurlProgressCallback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow) {
    auto* data = static_cast<ProgressCallbackData*>(clientp);
    if (data->callback) {
        if (!data->callback(static_cast<size_t>(dlnow), static_cast<size_t>(dltotal))) {
            data->cancelled = true;
            return 1;
        }
    }
    return 0;
}

class ConnectionPool {
public:
    explicit ConnectionPool(size_t max_connections) : max_connections_(max_connections) {}
    
    ~ConnectionPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!available_handles_.empty()) {
            curl_easy_cleanup(available_handles_.front());
            available_handles_.pop();
        }
    }
    
    CURL* acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!available_handles_.empty()) {
            CURL* handle = available_handles_.front();
            available_handles_.pop();
            curl_easy_reset(handle);
            return handle;
        }
        
        if (total_handles_ < max_connections_) {
            total_handles_++;
            return curl_easy_init();
        }
        
        return curl_easy_init();
    }
    
    void release(CURL* handle) {
        if (!handle) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        if (available_handles_.size() < max_connections_) {
            available_handles_.push(handle);
        } else {
            curl_easy_cleanup(handle);
            if (total_handles_ > 0) total_handles_--;
        }
    }

private:
    std::mutex mutex_;
    std::queue<CURL*> available_handles_;
    size_t max_connections_;
    std::atomic<size_t> total_handles_{0};
};

class Logger {
public:
    static void log(LogLevel level, const std::string& message, LogCallback callback = nullptr) {
        if (callback) {
            callback(level, message);
        } else {
            const char* level_str = "UNKNOWN";
            switch (level) {
                case LogLevel::Error: level_str = "ERROR"; break;
                case LogLevel::Warn: level_str = "WARN"; break;
                case LogLevel::Info: level_str = "INFO"; break;
                case LogLevel::Debug: level_str = "DEBUG"; break;
                default: break;
            }
            
            if (level <= LogLevel::Error) {
                std::cerr << "[" << level_str << "] " << message << std::endl;
            }
        }
    }
};

class HttpClient::Impl {
public:
    Impl() : connection_pool_(10) {
        ensureCurlInitialized();
        config_.default_headers["User-Agent"] = "Blaze/2.0";
        config_.default_headers["Accept"] = "*/*";
    }
    
    Impl(const HttpConfig& config) : config_(config), connection_pool_(config.max_connections) {
        ensureCurlInitialized();
    }
    
    ~Impl() = default;

    HttpResponse performRequest(const HttpRequest& request) {
        auto start_time = std::chrono::steady_clock::now();
        HttpResponse response;
        response.request_id = request.request_id.empty() ? utils::generateRequestId() : request.request_id;
        response.success = false;
        
        HttpRequest processed_request = request;
        for (auto& interceptor : request_interceptors_) {
            interceptor(processed_request);
        }
        
        if (!utils::isValidUrl(processed_request.url)) {
            response.error_message = "Invalid URL";
            response.error_type = ErrorType::InvalidUrl;
            log(LogLevel::Error, "Invalid URL: " + processed_request.url);
            return response;
        }
        
        int attempts = 0;
        int max_attempts = config_.retry.max_attempts;
        auto delay = config_.retry.initial_delay;
        
        while (attempts < max_attempts) {
            attempts++;
            
            auto result = performSingleRequest(processed_request, response);
            
            if (result.success || attempts >= max_attempts) {
                response = result;
                break;
            }
            
            bool should_retry = false;
            for (int code : config_.retry.retry_status_codes) {
                if (result.status_code == code) {
                    should_retry = true;
                    break;
                }
            }
            
            if (!should_retry && result.error_type != ErrorType::NetworkError && 
                result.error_type != ErrorType::TimeoutError) {
                response = result;
                break;
            }
            
            if (attempts < max_attempts) {
                log(LogLevel::Info, "Retrying request " + response.request_id + 
                    " (attempt " + std::to_string(attempts + 1) + "/" + std::to_string(max_attempts) + ")");
                std::this_thread::sleep_for(delay);
                delay = std::chrono::milliseconds(static_cast<long>(delay.count() * config_.retry.backoff_multiplier));
                if (delay > config_.retry.max_delay) {
                    delay = config_.retry.max_delay;
                }
            }
        }
        
        if (processed_request.enable_metrics) {
            auto end_time = std::chrono::steady_clock::now();
            response.metrics.total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        }
        
        for (auto& interceptor : response_interceptors_) {
            interceptor(response);
        }
        
        return response;
    }
    
    HttpResponse performSingleRequest(const HttpRequest& request, HttpResponse& response) {
        CURL* curl = connection_pool_.acquire();
        
        if (!curl) {
            response.error_message = "Failed to initialize curl";
            response.error_type = ErrorType::Unknown;
            return response;
        }
        
        WriteCallbackData write_data;
        write_data.response_body = &response.body;
        write_data.max_size = config_.max_response_size;
        write_data.use_stream = false;
        
        setupCurlOptions(curl, request, write_data, response);
        
        auto start_time = std::chrono::steady_clock::now();
        CURLcode res = curl_easy_perform(curl);
        auto end_time = std::chrono::steady_clock::now();
        
        if (request.enable_metrics) {
            extractMetrics(curl, response, start_time, end_time);
        }
        
        if (res == CURLE_OK) {
            response.success = true;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
            
            if (write_data.size_exceeded) {
                response.success = false;
                response.error_message = "Response size exceeded maximum limit";
                response.error_type = ErrorType::ResponseTooLarge;
            }
        } else {
            response.error_message = curl_easy_strerror(res);
            response.error_type = mapCurlErrorToErrorType(res);
            log(LogLevel::Error, "Request failed: " + response.error_message);
        }
        
        connection_pool_.release(curl);
        return response;
    }
    
    HttpResponse streamResponse(const HttpRequest& request, StreamCallback callback) {
        CURL* curl = connection_pool_.acquire();
        HttpResponse response;
        response.request_id = request.request_id.empty() ? utils::generateRequestId() : request.request_id;
        response.success = false;
        
        if (!curl) {
            response.error_message = "Failed to initialize curl";
            response.error_type = ErrorType::Unknown;
            return response;
        }
        
        WriteCallbackData write_data;
        write_data.stream_callback = callback;
        write_data.max_size = config_.max_response_size;
        write_data.use_stream = true;
        
        setupCurlOptions(curl, request, write_data, response);
        
        auto start_time = std::chrono::steady_clock::now();
        CURLcode res = curl_easy_perform(curl);
        auto end_time = std::chrono::steady_clock::now();
        
        if (request.enable_metrics) {
            extractMetrics(curl, response, start_time, end_time);
        }
        
        if (res == CURLE_OK) {
            response.success = true;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
        } else {
            response.error_message = curl_easy_strerror(res);
            response.error_type = mapCurlErrorToErrorType(res);
        }
        
        connection_pool_.release(curl);
        return response;
    }
    
    HttpResponse sendWithProgress(const HttpRequest& request, ProgressCallback callback) {
        CURL* curl = connection_pool_.acquire();
        HttpResponse response;
        response.request_id = request.request_id.empty() ? utils::generateRequestId() : request.request_id;
        response.success = false;
        
        if (!curl) {
            response.error_message = "Failed to initialize curl";
            response.error_type = ErrorType::Unknown;
            return response;
        }
        
        WriteCallbackData write_data;
        write_data.response_body = &response.body;
        write_data.max_size = config_.max_response_size;
        write_data.use_stream = false;
        
        ProgressCallbackData progress_data;
        progress_data.callback = callback;
        
        setupCurlOptions(curl, request, write_data, response);
        
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_data);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
        
        auto start_time = std::chrono::steady_clock::now();
        CURLcode res = curl_easy_perform(curl);
        auto end_time = std::chrono::steady_clock::now();
        
        if (request.enable_metrics) {
            extractMetrics(curl, response, start_time, end_time);
        }
        
        if (res == CURLE_OK && !progress_data.cancelled) {
            response.success = true;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
        } else if (progress_data.cancelled) {
            response.error_message = "Request cancelled by progress callback";
            response.error_type = ErrorType::Unknown;
        } else {
            response.error_message = curl_easy_strerror(res);
            response.error_type = mapCurlErrorToErrorType(res);
        }
        
        connection_pool_.release(curl);
        return response;
    }

private:
    void setupCurlOptions(CURL* curl, const HttpRequest& request, WriteCallbackData& write_data, HttpResponse& response) {
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        
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
        } else if (request.method == "PATCH") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            if (!request.body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, request.body.size());
            }
        } else if (request.method == "DELETE") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        } else if (request.method == "HEAD") {
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        } else if (request.method == "OPTIONS") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "OPTIONS");
        } else if (request.method != "GET") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, request.method.c_str());
        }
        
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_data);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);
        
        int timeout = request.timeout_ms.value_or(config_.timeout_ms);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, config_.connect_timeout_ms);
        
        bool follow = request.follow_redirects.value_or(config_.follow_redirects);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, follow ? 1L : 0L);
        
        int max_redir = request.max_redirects.value_or(config_.max_redirects);
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, max_redir);
        
        if (config_.enable_compression) {
            curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        }
        
        if (config_.keep_alive) {
            curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);
        }
        
        setupHeaders(curl, request);
        configureAuth(curl, request.auth.value_or(config_.auth));
        configureSSL(curl, config_.ssl);
        configureProxy(curl, config_.proxy);
    }
    
    void setupHeaders(CURL* curl, const HttpRequest& request) {
        struct curl_slist* chunk = nullptr;
        
        for (const auto& header : config_.default_headers) {
            std::string headerStr = header.first + ": " + header.second;
            chunk = curl_slist_append(chunk, headerStr.c_str());
        }
        
        for (const auto& header : request.headers) {
            std::string headerStr = header.first + ": " + header.second;
            chunk = curl_slist_append(chunk, headerStr.c_str());
        }
        
        if (chunk) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);
            header_lists_.push_back(chunk);
        }
    }
    
    void configureAuth(CURL* curl, const Auth& auth) {
        switch (auth.type) {
            case AuthType::Basic:
                curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
                curl_easy_setopt(curl, CURLOPT_USERNAME, auth.username.c_str());
                curl_easy_setopt(curl, CURLOPT_PASSWORD, auth.password.c_str());
                break;
            case AuthType::Bearer:
                {
                    std::string auth_header = "Authorization: Bearer " + auth.token;
                    struct curl_slist* auth_list = curl_slist_append(nullptr, auth_header.c_str());
                    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, auth_list);
                    header_lists_.push_back(auth_list);
                }
                break;
            case AuthType::ApiKey:
                {
                    std::string auth_header = auth.api_key_header + ": " + auth.token;
                    struct curl_slist* auth_list = curl_slist_append(nullptr, auth_header.c_str());
                    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, auth_list);
                    header_lists_.push_back(auth_list);
                }
                break;
            case AuthType::None:
            default:
                break;
        }
    }
    
    void configureSSL(CURL* curl, const SSLConfig& ssl) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, ssl.verify_peer ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, ssl.verify_host ? 2L : 0L);
        
        if (!ssl.ca_cert_path.empty()) {
            curl_easy_setopt(curl, CURLOPT_CAINFO, ssl.ca_cert_path.c_str());
        }
        
        if (!ssl.client_cert_path.empty()) {
            curl_easy_setopt(curl, CURLOPT_SSLCERT, ssl.client_cert_path.c_str());
        }
        
        if (!ssl.client_key_path.empty()) {
            curl_easy_setopt(curl, CURLOPT_SSLKEY, ssl.client_key_path.c_str());
        }
        
        if (!ssl.ciphers.empty()) {
            curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, ssl.ciphers.c_str());
        }
        
        if (ssl.ssl_version > 0) {
            curl_easy_setopt(curl, CURLOPT_SSLVERSION, ssl.ssl_version);
        }
    }
    
    void configureProxy(CURL* curl, const ProxyConfig& proxy) {
        if (proxy.enabled && !proxy.url.empty()) {
            curl_easy_setopt(curl, CURLOPT_PROXY, proxy.url.c_str());
            
            if (!proxy.username.empty() && !proxy.password.empty()) {
                curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME, proxy.username.c_str());
                curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD, proxy.password.c_str());
            }
        }
    }
    
    void extractMetrics(CURL* curl, HttpResponse& response,
                       const std::chrono::steady_clock::time_point& start_time,
                       const std::chrono::steady_clock::time_point& end_time) {
        response.metrics.total_time = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        double connect_time, dns_time, upload_size, download_size, upload_speed, download_speed;
        
        curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect_time);
        curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &dns_time);
        curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD, &upload_size);
        curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &download_size);
        curl_easy_getinfo(curl, CURLINFO_SPEED_UPLOAD, &upload_speed);
        curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD, &download_speed);
        
        response.metrics.connect_time = std::chrono::milliseconds(static_cast<long>(connect_time * 1000));
        response.metrics.dns_time = std::chrono::milliseconds(static_cast<long>(dns_time * 1000));
        response.metrics.upload_size = static_cast<size_t>(upload_size);
        response.metrics.download_size = static_cast<size_t>(download_size);
        response.metrics.upload_speed = upload_speed;
        response.metrics.download_speed = download_speed;
    }
    
    ErrorType mapCurlErrorToErrorType(CURLcode code) {
        switch (code) {
            case CURLE_OPERATION_TIMEDOUT:
                return ErrorType::TimeoutError;
            case CURLE_SSL_CONNECT_ERROR:
            case CURLE_SSL_CERTPROBLEM:
            case CURLE_SSL_CIPHER:
            case CURLE_SSL_CACERT:
                return ErrorType::SSLError;
            case CURLE_URL_MALFORMAT:
                return ErrorType::InvalidUrl;
            case CURLE_COULDNT_RESOLVE_HOST:
            case CURLE_COULDNT_CONNECT:
            case CURLE_RECV_ERROR:
            case CURLE_SEND_ERROR:
                return ErrorType::NetworkError;
            default:
                return ErrorType::Unknown;
        }
    }
    
    void log(LogLevel level, const std::string& message) {
        if (level <= config_.log_level) {
            Logger::log(level, message, log_callback_);
        }
    }

public:
    HttpConfig config_;
    ConnectionPool connection_pool_;
    std::vector<RequestInterceptor> request_interceptors_;
    std::vector<ResponseInterceptor> response_interceptors_;
    LogCallback log_callback_;
    std::vector<struct curl_slist*> header_lists_;
    HttpMetrics total_metrics_;
    std::mutex metrics_mutex_;
};

HttpClient::HttpClient() : pimpl(std::make_unique<Impl>()) {}

HttpClient::HttpClient(const HttpConfig& config) : pimpl(std::make_unique<Impl>(config)) {}

HttpClient::~HttpClient() = default;

HttpResponse HttpClient::get(const std::string& url, const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "GET";
    request.headers = headers;
    return send(request);
}

HttpResponse HttpClient::post(const std::string& url, const std::string& body,
                             const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "POST";
    request.body = body;
    request.headers = headers;
    
    if (headers.find("Content-Type") == headers.end() && !body.empty()) {
        if (body.front() == '{' || body.front() == '[') {
            request.headers["Content-Type"] = "application/json";
        } else {
            request.headers["Content-Type"] = "application/x-www-form-urlencoded";
        }
    }
    
    return send(request);
}

HttpResponse HttpClient::put(const std::string& url, const std::string& body,
                            const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "PUT";
    request.body = body;
    request.headers = headers;
    
    if (headers.find("Content-Type") == headers.end() && !body.empty()) {
        if (body.front() == '{' || body.front() == '[') {
            request.headers["Content-Type"] = "application/json";
        } else {
            request.headers["Content-Type"] = "application/x-www-form-urlencoded";
        }
    }
    
    return send(request);
}

HttpResponse HttpClient::patch(const std::string& url, const std::string& body,
                              const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "PATCH";
    request.body = body;
    request.headers = headers;
    
    if (headers.find("Content-Type") == headers.end() && !body.empty()) {
        if (body.front() == '{' || body.front() == '[') {
            request.headers["Content-Type"] = "application/json";
        } else {
            request.headers["Content-Type"] = "application/x-www-form-urlencoded";
        }
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

HttpResponse HttpClient::head(const std::string& url, const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "HEAD";
    request.headers = headers;
    return send(request);
}

HttpResponse HttpClient::options(const std::string& url, const std::map<std::string, std::string>& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "OPTIONS";
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

HttpResponse HttpClient::sendWithProgress(const HttpRequest& request, ProgressCallback callback) {
    return pimpl->sendWithProgress(request, callback);
}

HttpResponse HttpClient::streamResponse(const HttpRequest& request, StreamCallback callback) {
    return pimpl->streamResponse(request, callback);
}

HttpResponse HttpClient::uploadFile(const std::string& url, const std::string& file_path,
                                   const std::string& field_name,
                                   const std::map<std::string, std::string>& headers) {
    return HttpResponse{};
}

HttpResponse HttpClient::downloadFile(const std::string& url, const std::string& file_path,
                                     const std::map<std::string, std::string>& headers) {
    return HttpResponse{};
}

void HttpClient::setConfig(const HttpConfig& config) {
    pimpl->config_ = config;
}

HttpConfig HttpClient::getConfig() const {
    return pimpl->config_;
}

void HttpClient::setDefaultHeader(const std::string& name, const std::string& value) {
    pimpl->config_.default_headers[name] = value;
}

void HttpClient::removeDefaultHeader(const std::string& name) {
    pimpl->config_.default_headers.erase(name);
}

void HttpClient::clearDefaultHeaders() {
    pimpl->config_.default_headers.clear();
}

void HttpClient::setTimeout(int timeout_ms) {
    pimpl->config_.timeout_ms = timeout_ms;
}

void HttpClient::setConnectTimeout(int timeout_ms) {
    pimpl->config_.connect_timeout_ms = timeout_ms;
}

void HttpClient::setFollowRedirects(bool follow) {
    pimpl->config_.follow_redirects = follow;
}

void HttpClient::setMaxRedirects(int max_redirects) {
    pimpl->config_.max_redirects = max_redirects;
}

void HttpClient::setUserAgent(const std::string& user_agent) {
    pimpl->config_.user_agent = user_agent;
    pimpl->config_.default_headers["User-Agent"] = user_agent;
}

void HttpClient::setMaxResponseSize(size_t max_size) {
    pimpl->config_.max_response_size = max_size;
}

void HttpClient::setAuth(const Auth& auth) {
    pimpl->config_.auth = auth;
}

void HttpClient::setBasicAuth(const std::string& username, const std::string& password) {
    pimpl->config_.auth = auth::basic(username, password);
}

void HttpClient::setBearerToken(const std::string& token) {
    pimpl->config_.auth = auth::bearer(token);
}

void HttpClient::setApiKey(const std::string& key, const std::string& header) {
    pimpl->config_.auth = auth::apiKey(key, header);
}

void HttpClient::clearAuth() {
    pimpl->config_.auth = Auth{};
}

void HttpClient::setProxy(const ProxyConfig& proxy) {
    pimpl->config_.proxy = proxy;
}

void HttpClient::clearProxy() {
    pimpl->config_.proxy = ProxyConfig{};
}

void HttpClient::setSSLConfig(const SSLConfig& ssl) {
    pimpl->config_.ssl = ssl;
}

void HttpClient::setSSLVerification(bool verify_peer, bool verify_host) {
    pimpl->config_.ssl.verify_peer = verify_peer;
    pimpl->config_.ssl.verify_host = verify_host;
}

void HttpClient::setSSLCACert(const std::string& ca_cert_path) {
    pimpl->config_.ssl.ca_cert_path = ca_cert_path;
}

void HttpClient::setSSLClientCert(const std::string& cert_path, const std::string& key_path) {
    pimpl->config_.ssl.client_cert_path = cert_path;
    pimpl->config_.ssl.client_key_path = key_path;
}

void HttpClient::setRetryConfig(const RetryConfig& retry) {
    pimpl->config_.retry = retry;
}

void HttpClient::enableRetry(int max_attempts) {
    pimpl->config_.retry.max_attempts = max_attempts;
}

void HttpClient::disableRetry() {
    pimpl->config_.retry.max_attempts = 1;
}

void HttpClient::addRequestInterceptor(RequestInterceptor interceptor) {
    pimpl->request_interceptors_.push_back(interceptor);
}

void HttpClient::addResponseInterceptor(ResponseInterceptor interceptor) {
    pimpl->response_interceptors_.push_back(interceptor);
}

void HttpClient::clearInterceptors() {
    pimpl->request_interceptors_.clear();
    pimpl->response_interceptors_.clear();
}

void HttpClient::setLogLevel(LogLevel level) {
    pimpl->config_.log_level = level;
}

void HttpClient::setLogCallback(LogCallback callback) {
    pimpl->log_callback_ = callback;
}

void HttpClient::enableConnectionPooling(int max_connections) {
    pimpl->config_.max_connections = max_connections;
}

void HttpClient::disableConnectionPooling() {
    pimpl->config_.max_connections = 1;
}

void HttpClient::clearCookies() {
}

void HttpClient::setCookie(const std::string& name, const std::string& value, const std::string& domain) {
}

HttpMetrics HttpClient::getConnectionMetrics() const {
    std::lock_guard<std::mutex> lock(pimpl->metrics_mutex_);
    return pimpl->total_metrics_;
}

void HttpClient::resetMetrics() {
    std::lock_guard<std::mutex> lock(pimpl->metrics_mutex_);
    pimpl->total_metrics_ = HttpMetrics{};
}

HttpClientBuilder HttpClient::builder() {
    return HttpClientBuilder{};
}

HttpClientBuilder& HttpClientBuilder::url(const std::string& url) {
    request_.url = url;
    return *this;
}

HttpClientBuilder& HttpClientBuilder::method(const std::string& method) {
    request_.method = method;
    return *this;
}

HttpClientBuilder& HttpClientBuilder::header(const std::string& name, const std::string& value) {
    request_.headers[name] = value;
    return *this;
}

HttpClientBuilder& HttpClientBuilder::headers(const std::map<std::string, std::string>& headers) {
    for (const auto& header : headers) {
        request_.headers[header.first] = header.second;
    }
    return *this;
}

HttpClientBuilder& HttpClientBuilder::body(const std::string& body) {
    request_.body = body;
    return *this;
}

HttpClientBuilder& HttpClientBuilder::jsonBody(const std::string& json) {
    request_.body = json;
    request_.headers["Content-Type"] = "application/json";
    return *this;
}

HttpClientBuilder& HttpClientBuilder::formBody(const std::map<std::string, std::string>& form) {
    request_.body = utils::buildQueryString(form);
    request_.headers["Content-Type"] = "application/x-www-form-urlencoded";
    return *this;
}

HttpClientBuilder& HttpClientBuilder::timeout(int timeout_ms) {
    request_.timeout_ms = timeout_ms;
    return *this;
}

HttpClientBuilder& HttpClientBuilder::auth(const Auth& auth) {
    request_.auth = auth;
    return *this;
}

HttpClientBuilder& HttpClientBuilder::basicAuth(const std::string& username, const std::string& password) {
    request_.auth = auth::basic(username, password);
    return *this;
}

HttpClientBuilder& HttpClientBuilder::bearerToken(const std::string& token) {
    request_.auth = auth::bearer(token);
    return *this;
}

HttpClientBuilder& HttpClientBuilder::apiKey(const std::string& key, const std::string& header) {
    request_.auth = auth::apiKey(key, header);
    return *this;
}

HttpClientBuilder& HttpClientBuilder::followRedirects(bool follow) {
    request_.follow_redirects = follow;
    return *this;
}

HttpClientBuilder& HttpClientBuilder::maxRedirects(int max_redirects) {
    request_.max_redirects = max_redirects;
    return *this;
}

HttpClientBuilder& HttpClientBuilder::userAgent(const std::string& user_agent) {
    request_.headers["User-Agent"] = user_agent;
    return *this;
}

HttpClientBuilder& HttpClientBuilder::enableMetrics(bool enable) {
    request_.enable_metrics = enable;
    return *this;
}

HttpRequest HttpClientBuilder::build() {
    if (request_.request_id.empty()) {
        request_.request_id = utils::generateRequestId();
    }
    return request_;
}

HttpResponse HttpClientBuilder::send() {
    return client_.send(build());
}

std::future<HttpResponse> HttpClientBuilder::sendAsync() {
    return client_.sendAsync(build());
}

namespace auth {
    Auth basic(const std::string& username, const std::string& password) {
        Auth auth;
        auth.type = AuthType::Basic;
        auth.username = username;
        auth.password = password;
        return auth;
    }
    
    Auth bearer(const std::string& token) {
        Auth auth;
        auth.type = AuthType::Bearer;
        auth.token = token;
        return auth;
    }
    
    Auth apiKey(const std::string& key, const std::string& header) {
        Auth auth;
        auth.type = AuthType::ApiKey;
        auth.token = key;
        auth.api_key_header = header;
        return auth;
    }
}

namespace utils {
    std::string urlEncode(const std::string& str) {
        std::string encoded;
        for (char c : str) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded += c;
            } else {
                char hex[4];
                snprintf(hex, sizeof(hex), "%%%02X", static_cast<unsigned char>(c));
                encoded += hex;
            }
        }
        return encoded;
    }
    
    std::string urlDecode(const std::string& str) {
        std::string decoded;
        for (size_t i = 0; i < str.length(); ++i) {
            if (str[i] == '%' && i + 2 < str.length()) {
                std::string hex = str.substr(i + 1, 2);
                char c = static_cast<char>(std::stoi(hex, nullptr, 16));
                decoded += c;
                i += 2;
            } else if (str[i] == '+') {
                decoded += ' ';
            } else {
                decoded += str[i];
            }
        }
        return decoded;
    }
    
    std::string base64Encode(const std::string& str) {
        const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        int pad = str.length() % 3;
        
        for (size_t i = 0; i < str.length(); i += 3) {
            uint32_t temp = static_cast<unsigned char>(str[i]) << 16;
            if (i + 1 < str.length()) temp |= static_cast<unsigned char>(str[i + 1]) << 8;
            if (i + 2 < str.length()) temp |= static_cast<unsigned char>(str[i + 2]);
            
            encoded += chars[(temp >> 18) & 0x3F];
            encoded += chars[(temp >> 12) & 0x3F];
            encoded += (i + 1 < str.length()) ? chars[(temp >> 6) & 0x3F] : '=';
            encoded += (i + 2 < str.length()) ? chars[temp & 0x3F] : '=';
        }
        
        return encoded;
    }
    
    std::string base64Decode(const std::string& str) {
        const std::string chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string decoded;
        
        for (size_t i = 0; i < str.length(); i += 4) {
            uint32_t temp = 0;
            for (int j = 0; j < 4; ++j) {
                if (i + j < str.length() && str[i + j] != '=') {
                    size_t pos = chars.find(str[i + j]);
                    if (pos != std::string::npos) {
                        temp |= pos << (18 - j * 6);
                    }
                }
            }
            
            decoded += static_cast<char>((temp >> 16) & 0xFF);
            if (str[i + 2] != '=') decoded += static_cast<char>((temp >> 8) & 0xFF);
            if (str[i + 3] != '=') decoded += static_cast<char>(temp & 0xFF);
        }
        
        return decoded;
    }
    
    std::map<std::string, std::string> parseQueryString(const std::string& query) {
        std::map<std::string, std::string> params;
        std::istringstream iss(query);
        std::string pair;
        
        while (std::getline(iss, pair, '&')) {
            size_t pos = pair.find('=');
            if (pos != std::string::npos) {
                std::string key = urlDecode(pair.substr(0, pos));
                std::string value = urlDecode(pair.substr(pos + 1));
                params[key] = value;
            }
        }
        
        return params;
    }
    
    std::string buildQueryString(const std::map<std::string, std::string>& params) {
        std::string query;
        for (const auto& param : params) {
            if (!query.empty()) query += "&";
            query += urlEncode(param.first) + "=" + urlEncode(param.second);
        }
        return query;
    }
    
    std::string generateRequestId() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        
        const char* hex_chars = "0123456789abcdef";
        std::string id;
        for (int i = 0; i < 8; ++i) {
            id += hex_chars[dis(gen)];
        }
        id += "-";
        for (int i = 0; i < 4; ++i) {
            id += hex_chars[dis(gen)];
        }
        id += "-4";
        for (int i = 0; i < 3; ++i) {
            id += hex_chars[dis(gen)];
        }
        id += "-";
        id += hex_chars[8 + dis(gen) % 4];
        for (int i = 0; i < 3; ++i) {
            id += hex_chars[dis(gen)];
        }
        id += "-";
        for (int i = 0; i < 12; ++i) {
            id += hex_chars[dis(gen)];
        }
        return id;
    }
    
    bool isValidUrl(const std::string& url) {
        std::regex url_regex(R"(^https?://[^\s/$.?#].[^\s]*$)", std::regex_constants::icase);
        return std::regex_match(url, url_regex);
    }
}

} // namespace blaze
