#include "http_client.hpp"

#include <curl/curl.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstdint>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <coroutine>
#include <memory>

namespace blaze {

static void set_error(HttpResponse& response, ErrorType type, std::string message) {
    response.error = HttpError{type, std::move(message)};
}

static void ensureCurlInitialized() {
    static std::once_flag flag;
    std::call_once(flag, [] {
        curl_global_init(CURL_GLOBAL_ALL);
        std::atexit(curl_global_cleanup);
    });
}

struct WriteCallbackData {
    std::string* response_body{nullptr};
    StreamCallback stream_callback;
    bool use_stream{false};
    size_t max_size{0};
    size_t current_size{0};
    bool size_exceeded{false};
};

struct ProgressCallbackData {
    ProgressCallback callback;
    bool cancelled{false};
};

static size_t WriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    size_t total = size * nmemb;
    auto* data = static_cast<WriteCallbackData*>(userdata);

    if (data->use_stream && data->stream_callback) {
        return data->stream_callback(ptr, total) ? total : 0;
    }

    if (data->max_size > 0 && data->current_size + total > data->max_size) {
        data->size_exceeded = true;
        return 0;
    }

    if (data->response_body) {
        data->response_body->append(ptr, total);
        data->current_size += total;
    }
    return total;
}

static size_t HeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    size_t total = size * nitems;
    auto* headers = static_cast<Headers*>(userdata);
    std::string line(buffer, total);

    auto colon = line.find(':');
    if (colon != std::string::npos) {
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        auto start = value.find_first_not_of(" \t");
        auto end = value.find_last_not_of(" \t\r\n");
        if (start == std::string::npos || end == std::string::npos || end < start) {
            value.clear();
        } else {
            value = value.substr(start, end - start + 1);
        }
        (*headers)[name] = value;
    }
    return total;
}

static int CurlProgressCallback(void* clientp,
                                curl_off_t dltotal,
                                curl_off_t dlnow,
                                curl_off_t /*ultotal*/,
                                curl_off_t /*ulnow*/) {
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
    explicit ConnectionPool(int max) : max_connections_(max) {}

    ~ConnectionPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto* h : handles_) curl_easy_cleanup(h);
    }

    CURL* acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!handles_.empty()) {
            CURL* h = handles_.back();
            handles_.pop_back();
            curl_easy_reset(h);
            return h;
        }
        return curl_easy_init();
    }

    void release(CURL* h) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (static_cast<int>(handles_.size()) < max_connections_) {
            handles_.push_back(h);
        } else {
            curl_easy_cleanup(h);
        }
    }

    void resize(int max) {
        std::lock_guard<std::mutex> lock(mutex_);
        max_connections_ = max;
        while (static_cast<int>(handles_.size()) > max_connections_) {
            curl_easy_cleanup(handles_.back());
            handles_.pop_back();
        }
    }

private:
    std::vector<CURL*> handles_;
    int max_connections_;
    std::mutex mutex_;
};

class Logger {
public:
    void setLevel(LogLevel level) { level_ = level; }
    void setCallback(LogCallback cb) { callback_ = std::move(cb); }

    void log(LogLevel level, const std::string& msg) const {
        if (level > level_) return;
        if (callback_) {
            callback_(level, msg);
        }
    }

private:
    LogLevel level_{LogLevel::Error};
    LogCallback callback_;
};

using TransferId = std::uint64_t;

class AsyncEngine;

struct AsyncTransfer {
    TransferId id{0};
    CURL* easy{nullptr};
    struct curl_slist* header_list{nullptr};
    HttpRequest stored_request;
    std::string response_body;
    Headers response_headers;
    WriteCallbackData write_data{};
    std::atomic<void*> state{nullptr};
    HttpResponse result;
    std::function<void(HttpResponse)> race_callback;

    ~AsyncTransfer() {
        if (header_list) curl_slist_free_all(header_list);
        if (easy) curl_easy_cleanup(easy);
    }

    void complete(HttpResponse response) {
        if (race_callback) {
            race_callback(std::move(response));
            return;
        }
        result = std::move(response);
        void* expected = nullptr;
        if (state.compare_exchange_strong(expected,
                                          detail::completed_tag(),
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
            return;
        }
        std::coroutine_handle<>::from_address(expected).resume();
    }

    bool try_suspend(std::coroutine_handle<> h) {
        void* expected = nullptr;
        return state.compare_exchange_strong(
            expected, h.address(), std::memory_order_acq_rel, std::memory_order_acquire);
    }
};

static void cancel_transfer(TransferId id);

struct AsyncAwaiter {
    std::shared_ptr<AsyncTransfer> transfer;

    bool await_ready() const noexcept {
        return transfer->state.load(std::memory_order_acquire) == detail::completed_tag();
    }

    template<typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
        h.promise().cancel->set([weak = std::weak_ptr<AsyncTransfer>(transfer)] {
            if (auto t = weak.lock()) cancel_transfer(t->id);
        });
        return transfer->try_suspend(h);
    }

    HttpResponse await_resume() { return std::move(transfer->result); }
};

struct RaceState {
    std::atomic<bool> has_winner{false};
    std::atomic<void*> waiter{nullptr};
    size_t winner{0};
    HttpResponse result;
    std::vector<TransferId> ids;
};

struct RaceAwaiter {
    std::shared_ptr<RaceState> state;

    bool await_ready() const noexcept {
        return state->has_winner.load(std::memory_order_acquire);
    }

    template<typename Promise>
    bool await_suspend(std::coroutine_handle<Promise> h) noexcept {
        h.promise().cancel->set([state = state] {
            for (auto id : state->ids) cancel_transfer(id);
        });

        void* expected = nullptr;
        return state->waiter.compare_exchange_strong(
            expected, h.address(), std::memory_order_acq_rel, std::memory_order_acquire);
    }

    void await_resume() noexcept {}
};

static ErrorType mapCurlError(CURLcode code) {
    switch (code) {
        case CURLE_COULDNT_RESOLVE_HOST:
        case CURLE_COULDNT_CONNECT:
            return ErrorType::NetworkError;
        case CURLE_OPERATION_TIMEDOUT:
            return ErrorType::TimeoutError;
        case CURLE_SSL_CONNECT_ERROR:
        case CURLE_PEER_FAILED_VERIFICATION:
        case CURLE_SSL_CERTPROBLEM:
        case CURLE_SSL_CIPHER:
            return ErrorType::SSLError;
        default:
            return ErrorType::Unknown;
    }
}

class CompletionQueue {
public:
    explicit CompletionQueue(unsigned workers) {
        for (unsigned i = 0; i < workers; ++i) workers_.emplace_back([this] { run(); });
    }

    ~CompletionQueue() { stop(); }

    void post(std::shared_ptr<AsyncTransfer> transfer, HttpResponse response) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.emplace_back(std::move(transfer), std::move(response));
        }
        cv_.notify_one();
    }

    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        cv_.notify_all();
        for (auto& worker : workers_)
            if (worker.joinable()) worker.join();
        workers_.clear();
    }

private:
    void run() {
        for (;;) {
            std::pair<std::shared_ptr<AsyncTransfer>, HttpResponse> item;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                if (queue_.empty()) return;
                item = std::move(queue_.front());
                queue_.pop_front();
            }
            item.first->complete(std::move(item.second));
        }
    }

    std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<std::pair<std::shared_ptr<AsyncTransfer>, HttpResponse>> queue_;
    std::vector<std::thread> workers_;
    bool stopping_{false};
};

class AsyncEngine {
public:
    static AsyncEngine& instance();

    ~AsyncEngine() {
        running_ = false;
        curl_multi_wakeup(multi_);
        if (thread_.joinable()) thread_.join();

        completions_.stop();

        for (auto& [easy, transfer] : active_) {
            curl_multi_remove_handle(multi_, easy);
        }
        active_.clear();
        by_id_.clear();
        curl_multi_cleanup(multi_);
    }

    AsyncEngine(const AsyncEngine&) = delete;
    AsyncEngine& operator=(const AsyncEngine&) = delete;

    TransferId next_id() { return next_id_.fetch_add(1, std::memory_order_relaxed) + 1; }

    void submit(std::shared_ptr<AsyncTransfer> transfer) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            pending_.push_back(std::move(transfer));
        }
        curl_multi_wakeup(multi_);
    }

    void cancel(TransferId id) {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            to_cancel_.push_back(id);
        }
        curl_multi_wakeup(multi_);
    }

private:
    AsyncEngine() : completions_(2) {
        ensureCurlInitialized();
        multi_ = curl_multi_init();
        running_ = true;
        thread_ = std::thread([this] { run(); });
    }

    void run() {
        while (running_) {
            processPending();
            processCancellations();

            int still_running;
            curl_multi_perform(multi_, &still_running);

            CURLMsg* msg;
            int msgs_left;
            while ((msg = curl_multi_info_read(multi_, &msgs_left))) {
                if (msg->msg == CURLMSG_DONE) {
                    handleCompletion(msg->easy_handle, msg->data.result);
                }
            }

            int numfds;
            curl_multi_poll(multi_, nullptr, 0, 1000, &numfds);
        }
    }

    void processPending() {
        std::vector<std::shared_ptr<AsyncTransfer>> batch;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            batch.swap(pending_);
        }
        for (auto& t : batch) {
            CURL* easy = t->easy;
            TransferId id = t->id;
            curl_multi_add_handle(multi_, easy);
            by_id_[id] = easy;
            active_[easy] = std::move(t);
        }
    }

    void processCancellations() {
        std::vector<TransferId> batch;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            batch.swap(to_cancel_);
        }
        for (auto id : batch) {
            auto id_it = by_id_.find(id);
            if (id_it == by_id_.end()) continue;

            CURL* easy = id_it->second;
            by_id_.erase(id_it);

            auto it = active_.find(easy);
            if (it == active_.end()) continue;

            auto transfer = std::move(it->second);
            active_.erase(it);
            curl_multi_remove_handle(multi_, easy);

            HttpResponse response;
            response.request_id = transfer->stored_request.request_id;
            set_error(response, ErrorType::Cancelled, "Request cancelled");
            completions_.post(std::move(transfer), std::move(response));
        }
    }

    void handleCompletion(CURL* easy, CURLcode code) {
        auto it = active_.find(easy);
        if (it == active_.end()) return;

        auto transfer = std::move(it->second);
        active_.erase(it);
        by_id_.erase(transfer->id);
        curl_multi_remove_handle(multi_, easy);

        HttpResponse response;
        response.body = std::move(transfer->response_body);
        response.headers = std::move(transfer->response_headers);
        response.request_id = transfer->stored_request.request_id;

        if (code == CURLE_OK) {
            long http_code;
            curl_easy_getinfo(easy, CURLINFO_RESPONSE_CODE, &http_code);
            response.status_code = static_cast<int>(http_code);

            double total_time, connect_time, dns_time;
            double upload_size, download_size;
            double upload_speed, download_speed;
            curl_easy_getinfo(easy, CURLINFO_TOTAL_TIME, &total_time);
            curl_easy_getinfo(easy, CURLINFO_CONNECT_TIME, &connect_time);
            curl_easy_getinfo(easy, CURLINFO_NAMELOOKUP_TIME, &dns_time);
            curl_easy_getinfo(easy, CURLINFO_SIZE_UPLOAD, &upload_size);
            curl_easy_getinfo(easy, CURLINFO_SIZE_DOWNLOAD, &download_size);
            curl_easy_getinfo(easy, CURLINFO_SPEED_UPLOAD, &upload_speed);
            curl_easy_getinfo(easy, CURLINFO_SPEED_DOWNLOAD, &download_speed);

            response.metrics.total_time =
                std::chrono::milliseconds(static_cast<long long>(total_time * 1000));
            response.metrics.connect_time =
                std::chrono::milliseconds(static_cast<long long>(connect_time * 1000));
            response.metrics.dns_time =
                std::chrono::milliseconds(static_cast<long long>(dns_time * 1000));
            response.metrics.upload_size = static_cast<size_t>(upload_size);
            response.metrics.download_size = static_cast<size_t>(download_size);
            response.metrics.upload_speed = upload_speed;
            response.metrics.download_speed = download_speed;
        } else if (transfer->write_data.size_exceeded) {
            set_error(response,
                      ErrorType::ResponseTooLarge,
                      "Response size exceeded maximum allowed");
        } else {
            set_error(response, mapCurlError(code), curl_easy_strerror(code));
        }

        completions_.post(std::move(transfer), std::move(response));
    }

    CURLM* multi_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<TransferId> next_id_{0};
    std::mutex mutex_;
    std::vector<std::shared_ptr<AsyncTransfer>> pending_;
    std::vector<TransferId> to_cancel_;
    std::unordered_map<CURL*, std::shared_ptr<AsyncTransfer>> active_;
    std::unordered_map<TransferId, CURL*> by_id_;
    CompletionQueue completions_;
};

static std::mutex g_engine_mutex;
static AsyncEngine* g_engine = nullptr;

AsyncEngine& AsyncEngine::instance() {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (!g_engine) g_engine = new AsyncEngine();
    return *g_engine;
}

static void cancel_transfer(TransferId id) {
    std::lock_guard<std::mutex> lock(g_engine_mutex);
    if (g_engine) g_engine->cancel(id);
}

void shutdown() {
    AsyncEngine* engine = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_engine_mutex);
        engine = std::exchange(g_engine, nullptr);
    }
    delete engine;
}

class HttpClient::Impl {
public:
    HttpConfig config_;
    ConnectionPool connection_pool_;
    Logger logger_;

    std::vector<RequestInterceptor> request_interceptors_;
    std::vector<ResponseInterceptor> response_interceptors_;

    std::map<std::string, std::string> cookies_;
    HttpMetrics total_metrics_;
    mutable std::mutex metrics_mutex_;

    Impl() : connection_pool_(10) { ensureCurlInitialized(); }

    explicit Impl(const HttpConfig& cfg)
        : config_(cfg), connection_pool_(cfg.max_connections) {
        ensureCurlInitialized();
    }

    class AsyncScope {
    public:
        explicit AsyncScope(Impl* impl) : impl_(impl) {
            std::lock_guard<std::mutex> lock(impl_->async_mutex_);
            ++impl_->outstanding_;
        }

        ~AsyncScope() { release(); }

        AsyncScope(const AsyncScope&) = delete;
        AsyncScope& operator=(const AsyncScope&) = delete;

        void track(TransferId id) {
            std::lock_guard<std::mutex> lock(impl_->async_mutex_);
            ids_.push_back(id);
            impl_->live_ids_.insert(id);
        }

        void release() {
            if (!impl_) return;
            Impl* impl = std::exchange(impl_, nullptr);
            {
                std::lock_guard<std::mutex> lock(impl->async_mutex_);
                for (auto id : ids_) impl->live_ids_.erase(id);
                --impl->outstanding_;
            }
            impl->async_cv_.notify_all();
        }

    private:
        Impl* impl_;
        std::vector<TransferId> ids_;
    };

    void drain() {
        std::unique_lock<std::mutex> lock(async_mutex_);
        while (outstanding_ > 0) {
            std::vector<TransferId> ids(live_ids_.begin(), live_ids_.end());
            lock.unlock();
            for (auto id : ids) cancel_transfer(id);
            lock.lock();
            async_cv_.wait_for(
                lock, std::chrono::milliseconds(50), [this] { return outstanding_ == 0; });
        }
    }

    std::mutex async_mutex_;
    std::condition_variable async_cv_;
    std::unordered_set<TransferId> live_ids_;
    int outstanding_{0};

    struct curl_slist* buildHeaderList(const Headers& request_headers,
                                       const Auth& effective_auth) {
        struct curl_slist* list = nullptr;

        for (auto& [k, v] : config_.default_headers) {
            if (request_headers.count(k)) continue;
            list = curl_slist_append(list, (k + ": " + v).c_str());
        }

        for (auto& [k, v] : request_headers) {
            list = curl_slist_append(list, (k + ": " + v).c_str());
        }

        if (!cookies_.empty()) {
            std::string cookie_header = "Cookie: ";
            bool first = true;
            for (auto& [k, v] : cookies_) {
                if (!first) cookie_header += "; ";
                cookie_header += k + "=" + v;
                first = false;
            }
            list = curl_slist_append(list, cookie_header.c_str());
        }

        if (effective_auth.type == AuthType::Bearer && !effective_auth.token.empty()) {
            list = curl_slist_append(
                list, ("Authorization: Bearer " + effective_auth.token).c_str());
        } else if (effective_auth.type == AuthType::ApiKey &&
                   !effective_auth.token.empty()) {
            list = curl_slist_append(
                list,
                (effective_auth.api_key_header + ": " + effective_auth.token).c_str());
        }

        return list;
    }

    void configureAuth(CURL* curl, const Auth& auth) {
        if (auth.type == AuthType::Basic) {
            curl_easy_setopt(curl, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
            curl_easy_setopt(curl, CURLOPT_USERNAME, auth.username.c_str());
            curl_easy_setopt(curl, CURLOPT_PASSWORD, auth.password.c_str());
        }
    }

    void configureSSL(CURL* curl, const SSLConfig& ssl) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, ssl.verify_peer ? 1L : 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, ssl.verify_host ? 2L : 0L);
        if (!ssl.ca_cert_path.empty())
            curl_easy_setopt(curl, CURLOPT_CAINFO, ssl.ca_cert_path.c_str());
        if (!ssl.client_cert_path.empty())
            curl_easy_setopt(curl, CURLOPT_SSLCERT, ssl.client_cert_path.c_str());
        if (!ssl.client_key_path.empty())
            curl_easy_setopt(curl, CURLOPT_SSLKEY, ssl.client_key_path.c_str());
        if (!ssl.ciphers.empty())
            curl_easy_setopt(curl, CURLOPT_SSL_CIPHER_LIST, ssl.ciphers.c_str());
        if (ssl.ssl_version > 0)
            curl_easy_setopt(curl, CURLOPT_SSLVERSION, ssl.ssl_version);
    }

    void configureProxy(CURL* curl, const ProxyConfig& proxy) {
        if (!proxy.enabled) return;
        curl_easy_setopt(curl, CURLOPT_PROXY, proxy.url.c_str());
        if (!proxy.username.empty()) {
            curl_easy_setopt(curl, CURLOPT_PROXYUSERNAME, proxy.username.c_str());
            curl_easy_setopt(curl, CURLOPT_PROXYPASSWORD, proxy.password.c_str());
        }
    }

    struct curl_slist* configureCurl(CURL* curl,
                                     const HttpRequest& request,
                                     WriteCallbackData* wd,
                                     Headers* resp_hdrs) {
        curl_easy_setopt(curl, CURLOPT_URL, request.url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, config_.user_agent.c_str());

        if (request.method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(
                curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
        } else if (request.method == "PUT") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(
                curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
        } else if (request.method == "PATCH") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request.body.c_str());
            curl_easy_setopt(
                curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(request.body.size()));
        } else if (request.method == "DELETE") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "DELETE");
        } else if (request.method == "HEAD") {
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        } else if (request.method == "OPTIONS") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "OPTIONS");
        }

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, wd);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, resp_hdrs);

        long timeout = static_cast<long>(request.timeout_ms.value_or(config_.timeout_ms));
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout);
        curl_easy_setopt(
            curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(config_.connect_timeout_ms));

        bool follow = request.follow_redirects.value_or(config_.follow_redirects);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, follow ? 1L : 0L);
        curl_easy_setopt(
            curl,
            CURLOPT_MAXREDIRS,
            static_cast<long>(request.max_redirects.value_or(config_.max_redirects)));

        if (config_.enable_compression) curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");
        if (config_.keep_alive) curl_easy_setopt(curl, CURLOPT_TCP_KEEPALIVE, 1L);

        Auth effective_auth = request.auth.value_or(config_.auth);
        struct curl_slist* hdr_list = buildHeaderList(request.headers, effective_auth);
        if (hdr_list) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr_list);
        configureAuth(curl, effective_auth);
        configureSSL(curl, config_.ssl);
        configureProxy(curl, config_.proxy);

        if (config_.http_version != HttpVersion::Default) {
            long ver = CURL_HTTP_VERSION_NONE;
            switch (config_.http_version) {
                case HttpVersion::Http1_1:
                    ver = CURL_HTTP_VERSION_1_1;
                    break;
                case HttpVersion::Http2:
                    ver = CURL_HTTP_VERSION_2;
                    break;
                case HttpVersion::Http2TLS:
                    ver = CURL_HTTP_VERSION_2TLS;
                    break;
                case HttpVersion::Http3:
                    ver = CURL_HTTP_VERSION_3;
                    break;
                default:
                    break;
            }
            curl_easy_setopt(curl, CURLOPT_HTTP_VERSION, ver);
        }

        return hdr_list;
    }

    void extractMetrics(CURL* curl, HttpResponse& response) {
        double total_time, connect_time, dns_time;
        double upload_size, download_size;
        double upload_speed, download_speed;

        curl_easy_getinfo(curl, CURLINFO_TOTAL_TIME, &total_time);
        curl_easy_getinfo(curl, CURLINFO_CONNECT_TIME, &connect_time);
        curl_easy_getinfo(curl, CURLINFO_NAMELOOKUP_TIME, &dns_time);
        curl_easy_getinfo(curl, CURLINFO_SIZE_UPLOAD, &upload_size);
        curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD, &download_size);
        curl_easy_getinfo(curl, CURLINFO_SPEED_UPLOAD, &upload_speed);
        curl_easy_getinfo(curl, CURLINFO_SPEED_DOWNLOAD, &download_speed);

        response.metrics.total_time =
            std::chrono::milliseconds(static_cast<long long>(total_time * 1000));
        response.metrics.connect_time =
            std::chrono::milliseconds(static_cast<long long>(connect_time * 1000));
        response.metrics.dns_time =
            std::chrono::milliseconds(static_cast<long long>(dns_time * 1000));
        response.metrics.upload_size = static_cast<size_t>(upload_size);
        response.metrics.download_size = static_cast<size_t>(download_size);
        response.metrics.upload_speed = upload_speed;
        response.metrics.download_speed = download_speed;

        std::lock_guard<std::mutex> lock(metrics_mutex_);
        total_metrics_.total_time += response.metrics.total_time;
        total_metrics_.upload_size += response.metrics.upload_size;
        total_metrics_.download_size += response.metrics.download_size;
    }

    HttpResponse performSingleRequest(const HttpRequest& request) {
        HttpResponse response;
        response.request_id = request.request_id;

        CURL* curl = connection_pool_.acquire();
        WriteCallbackData wd{};
        wd.response_body = &response.body;
        wd.max_size = config_.max_response_size;

        struct curl_slist* hdr_list = configureCurl(curl, request, &wd, &response.headers);
        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            long http_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            response.status_code = static_cast<int>(http_code);
            if (request.enable_metrics) extractMetrics(curl, response);
        } else if (wd.size_exceeded) {
            set_error(response,
                      ErrorType::ResponseTooLarge,
                      "Response size exceeded maximum allowed");
        } else {
            set_error(response, mapCurlError(res), curl_easy_strerror(res));
        }

        if (hdr_list) curl_slist_free_all(hdr_list);
        connection_pool_.release(curl);
        return response;
    }

    bool shouldRetry(const HttpResponse& response) const {
        if (!response.ok()) {
            return response.error_type() != ErrorType::Cancelled &&
                   response.error_type() != ErrorType::ResponseTooLarge &&
                   response.error_type() != ErrorType::InvalidUrl;
        }
        auto& codes = config_.retry.retry_status_codes;
        return std::find(codes.begin(), codes.end(), response.status_code) != codes.end();
    }

    HttpResponse performRequest(const HttpRequest& request) {
        auto& retry = config_.retry;
        HttpResponse response = performSingleRequest(request);

        if (retry.max_attempts <= 1 || !shouldRetry(response)) return response;

        auto delay = retry.initial_delay;
        for (int attempt = 1; attempt < retry.max_attempts; ++attempt) {
            logger_.log(LogLevel::Info,
                        "Retry attempt " + std::to_string(attempt) + " after " +
                            std::to_string(delay.count()) + "ms");
            std::this_thread::sleep_for(delay);

            response = performSingleRequest(request);
            if (!shouldRetry(response)) return response;

            delay = std::chrono::milliseconds(
                static_cast<long long>(delay.count() * retry.backoff_multiplier));
            if (delay > retry.max_delay) delay = retry.max_delay;
        }
        return response;
    }

    std::shared_ptr<AsyncTransfer> setupTransfer(HttpRequest& request) {
        auto transfer = std::make_shared<AsyncTransfer>();
        transfer->id = AsyncEngine::instance().next_id();
        transfer->stored_request = request;
        transfer->easy = curl_easy_init();
        transfer->write_data.response_body = &transfer->response_body;
        transfer->write_data.max_size = config_.max_response_size;

        transfer->header_list = configureCurl(transfer->easy,
                                              transfer->stored_request,
                                              &transfer->write_data,
                                              &transfer->response_headers);

        return transfer;
    }

    AsyncAwaiter asyncSetup(HttpRequest request, AsyncScope& scope) {
        if (request.request_id.empty()) request.request_id = utils::generate_request_id();

        if (!utils::is_valid_url(request.url)) {
            auto transfer = std::make_shared<AsyncTransfer>();
            HttpResponse err;
            err.request_id = request.request_id;
            set_error(err, ErrorType::InvalidUrl, "Invalid URL: " + request.url);
            transfer->complete(std::move(err));
            return AsyncAwaiter{transfer};
        }

        auto transfer = setupTransfer(request);
        scope.track(transfer->id);
        AsyncEngine::instance().submit(transfer);
        return AsyncAwaiter{transfer};
    }

    HttpResponse streamResponseImpl(const HttpRequest& request, StreamCallback callback) {
        HttpResponse response;
        response.request_id = request.request_id;

        CURL* curl = connection_pool_.acquire();
        WriteCallbackData wd{};
        wd.use_stream = true;
        wd.stream_callback = std::move(callback);

        struct curl_slist* hdr_list = configureCurl(curl, request, &wd, &response.headers);
        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            long http_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            response.status_code = static_cast<int>(http_code);
        } else {
            set_error(response, mapCurlError(res), curl_easy_strerror(res));
        }

        if (hdr_list) curl_slist_free_all(hdr_list);
        connection_pool_.release(curl);
        return response;
    }

    HttpResponse sendWithProgressImpl(const HttpRequest& request,
                                      ProgressCallback progress) {
        HttpResponse response;
        response.request_id = request.request_id;

        CURL* curl = connection_pool_.acquire();
        WriteCallbackData wd{};
        wd.response_body = &response.body;
        wd.max_size = config_.max_response_size;

        ProgressCallbackData pd{};
        pd.callback = std::move(progress);

        struct curl_slist* hdr_list = configureCurl(curl, request, &wd, &response.headers);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, CurlProgressCallback);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &pd);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

        CURLcode res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            long http_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            response.status_code = static_cast<int>(http_code);
        } else if (pd.cancelled) {
            set_error(response, ErrorType::Cancelled, "Cancelled by progress callback");
        } else {
            set_error(response, mapCurlError(res), curl_easy_strerror(res));
        }

        if (hdr_list) curl_slist_free_all(hdr_list);
        connection_pool_.release(curl);
        return response;
    }

    HttpResponse uploadFileImpl(const std::string& url,
                                const std::string& file_path,
                                const std::string& field_name,
                                const Headers& headers) {
        HttpResponse response;

        CURL* curl = connection_pool_.acquire();
        curl_mime* mime = curl_mime_init(curl);
        curl_mimepart* part = curl_mime_addpart(mime);
        curl_mime_name(part, field_name.c_str());
        curl_mime_filedata(part, file_path.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_MIMEPOST, mime);
        curl_easy_setopt(curl, CURLOPT_USERAGENT, config_.user_agent.c_str());

        WriteCallbackData wd{};
        wd.response_body = &response.body;
        wd.max_size = config_.max_response_size;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &wd);
        curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, HeaderCallback);
        curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response.headers);

        Auth effective_auth = config_.auth;
        struct curl_slist* hdr_list = buildHeaderList(headers, effective_auth);
        if (hdr_list) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, hdr_list);
        configureAuth(curl, effective_auth);
        configureSSL(curl, config_.ssl);

        CURLcode res = curl_easy_perform(curl);
        if (res == CURLE_OK) {
            long http_code;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            response.status_code = static_cast<int>(http_code);
        } else {
            set_error(response, mapCurlError(res), curl_easy_strerror(res));
        }

        curl_mime_free(mime);
        if (hdr_list) curl_slist_free_all(hdr_list);
        connection_pool_.release(curl);
        return response;
    }

    HttpResponse downloadFileImpl(const std::string& url,
                                  const std::string& file_path,
                                  const Headers& headers) {
        HttpRequest request;
        request.url = url;
        request.method = "GET";
        request.headers = headers;

        std::ofstream ofs(file_path, std::ios::binary);
        if (!ofs) {
            HttpResponse err;
            set_error(err, ErrorType::Unknown, "Cannot open file: " + file_path);
            return err;
        }

        auto response =
            streamResponseImpl(request, [&ofs](const char* data, size_t size) -> bool {
                ofs.write(data, static_cast<std::streamsize>(size));
                return ofs.good();
            });

        ofs.close();
        if (!response.is_success()) std::remove(file_path.c_str());
        return response;
    }

    void log(LogLevel level, const std::string& msg) { logger_.log(level, msg); }
};

HttpClient::HttpClient() : pimpl(std::make_unique<Impl>()) {}
HttpClient::HttpClient(const HttpConfig& config) : pimpl(std::make_unique<Impl>(config)) {}

HttpClient::~HttpClient() {
    if (pimpl) pimpl->drain();
}

HttpClient::HttpClient(HttpClient&&) noexcept = default;

HttpClient& HttpClient::operator=(HttpClient&& other) noexcept {
    if (this != &other) {
        if (pimpl) pimpl->drain();
        pimpl = std::move(other.pimpl);
    }
    return *this;
}

HttpResponse HttpClient::get(const std::string& url, const Headers& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "GET";
    request.headers = headers;
    return send(request);
}

HttpResponse HttpClient::post(const std::string& url,
                              const std::string& body,
                              const Headers& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "POST";
    request.body = body;
    request.headers = headers;
    if (request.headers.find("Content-Type") == request.headers.end() && !body.empty()) {
        if (body.front() == '{' || body.front() == '[')
            request.headers["Content-Type"] = "application/json";
        else
            request.headers["Content-Type"] = "application/x-www-form-urlencoded";
    }
    return send(request);
}

HttpResponse HttpClient::put(const std::string& url,
                             const std::string& body,
                             const Headers& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "PUT";
    request.body = body;
    request.headers = headers;
    return send(request);
}

HttpResponse HttpClient::patch(const std::string& url,
                               const std::string& body,
                               const Headers& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "PATCH";
    request.body = body;
    request.headers = headers;
    return send(request);
}

HttpResponse HttpClient::del(const std::string& url, const Headers& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "DELETE";
    request.headers = headers;
    return send(request);
}

HttpResponse HttpClient::head(const std::string& url, const Headers& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "HEAD";
    request.headers = headers;
    return send(request);
}

HttpResponse HttpClient::options(const std::string& url, const Headers& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "OPTIONS";
    request.headers = headers;
    return send(request);
}

HttpResponse HttpClient::send(const HttpRequest& request) {
    HttpRequest req = request;
    if (req.request_id.empty()) req.request_id = utils::generate_request_id();

    if (!utils::is_valid_url(req.url)) {
        HttpResponse err;
        err.request_id = req.request_id;
        set_error(err, ErrorType::InvalidUrl, "Invalid URL: " + req.url);
        return err;
    }

    for (auto& interceptor : pimpl->request_interceptors_) interceptor(req);

    pimpl->log(LogLevel::Debug, req.method + " " + req.url);
    HttpResponse response = pimpl->performRequest(req);

    for (auto& interceptor : pimpl->response_interceptors_) interceptor(response);

    return response;
}

Task<HttpResponse> HttpClient::async_get(const std::string& url, const Headers& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "GET";
    request.headers = headers;
    return async_send(std::move(request));
}

Task<HttpResponse> HttpClient::async_post(const std::string& url,
                                          const std::string& body,
                                          const Headers& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "POST";
    request.body = body;
    request.headers = headers;
    if (request.headers.find("Content-Type") == request.headers.end() && !body.empty()) {
        if (body.front() == '{' || body.front() == '[')
            request.headers["Content-Type"] = "application/json";
        else
            request.headers["Content-Type"] = "application/x-www-form-urlencoded";
    }
    return async_send(std::move(request));
}

Task<HttpResponse> HttpClient::async_put(const std::string& url,
                                         const std::string& body,
                                         const Headers& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "PUT";
    request.body = body;
    request.headers = headers;
    return async_send(std::move(request));
}

Task<HttpResponse> HttpClient::async_patch(const std::string& url,
                                           const std::string& body,
                                           const Headers& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "PATCH";
    request.body = body;
    request.headers = headers;
    return async_send(std::move(request));
}

Task<HttpResponse> HttpClient::async_del(const std::string& url, const Headers& headers) {
    HttpRequest request;
    request.url = url;
    request.method = "DELETE";
    request.headers = headers;
    return async_send(std::move(request));
}

Task<HttpResponse> HttpClient::async_send(HttpRequest request) {
    Impl::AsyncScope scope(pimpl.get());

    for (auto& interceptor : pimpl->request_interceptors_) interceptor(request);

    auto response = co_await pimpl->asyncSetup(std::move(request), scope);

    for (auto& interceptor : pimpl->response_interceptors_) interceptor(response);

    scope.release();
    co_return response;
}

Task<std::pair<size_t, HttpResponse>> HttpClient::async_race(
    std::vector<HttpRequest> requests) {
    if (requests.empty()) {
        HttpResponse err;
        set_error(err, ErrorType::Unknown, "async_race requires at least one request");
        co_return std::pair<size_t, HttpResponse>{0, std::move(err)};
    }

    auto state = std::make_shared<RaceState>();
    std::vector<std::shared_ptr<AsyncTransfer>> transfers;
    transfers.reserve(requests.size());
    state->ids.reserve(requests.size());

    Impl::AsyncScope scope(pimpl.get());

    for (size_t i = 0; i < requests.size(); ++i) {
        auto& req = requests[i];
        if (req.request_id.empty()) req.request_id = utils::generate_request_id();

        for (auto& interceptor : pimpl->request_interceptors_) interceptor(req);

        auto transfer = pimpl->setupTransfer(req);
        auto captured_state = state;
        transfer->race_callback = [captured_state, i](HttpResponse resp) {
            bool expected = false;
            if (captured_state->has_winner.compare_exchange_strong(
                    expected, true, std::memory_order_acq_rel)) {
                captured_state->winner = i;
                captured_state->result = std::move(resp);
                void* w = captured_state->waiter.exchange(detail::completed_tag(),
                                                          std::memory_order_acq_rel);
                if (w && w != detail::completed_tag())
                    std::coroutine_handle<>::from_address(w).resume();
            }
        };
        state->ids.push_back(transfer->id);
        scope.track(transfer->id);
        transfers.push_back(std::move(transfer));
    }

    for (auto& t : transfers) AsyncEngine::instance().submit(t);

    co_await RaceAwaiter{state};

    for (auto id : state->ids) AsyncEngine::instance().cancel(id);

    for (auto& interceptor : pimpl->response_interceptors_) interceptor(state->result);

    scope.release();
    co_return std::pair<size_t, HttpResponse>{state->winner, std::move(state->result)};
}

HttpResponse HttpClient::send_with_progress(const HttpRequest& request,
                                            ProgressCallback callback) {
    return pimpl->sendWithProgressImpl(request, std::move(callback));
}

HttpResponse HttpClient::stream_response(const HttpRequest& request,
                                         StreamCallback callback) {
    return pimpl->streamResponseImpl(request, std::move(callback));
}

HttpResponse HttpClient::upload_file(const std::string& url,
                                     const std::string& file_path,
                                     const std::string& field_name,
                                     const Headers& headers) {
    return pimpl->uploadFileImpl(url, file_path, field_name, headers);
}

HttpResponse HttpClient::download_file(const std::string& url,
                                       const std::string& file_path,
                                       const Headers& headers) {
    return pimpl->downloadFileImpl(url, file_path, headers);
}

void HttpClient::set_config(const HttpConfig& config) {
    pimpl->config_ = config;
}
HttpConfig HttpClient::config() const {
    return pimpl->config_;
}

void HttpClient::set_default_header(const std::string& name, const std::string& value) {
    pimpl->config_.default_headers[name] = value;
}
void HttpClient::remove_default_header(const std::string& name) {
    pimpl->config_.default_headers.erase(name);
}
void HttpClient::clear_default_headers() {
    pimpl->config_.default_headers.clear();
}

void HttpClient::set_timeout(int timeout_ms) {
    pimpl->config_.timeout_ms = timeout_ms;
}
void HttpClient::set_connect_timeout(int timeout_ms) {
    pimpl->config_.connect_timeout_ms = timeout_ms;
}
void HttpClient::set_follow_redirects(bool follow) {
    pimpl->config_.follow_redirects = follow;
}
void HttpClient::set_max_redirects(int max) {
    pimpl->config_.max_redirects = max;
}
void HttpClient::set_user_agent(const std::string& ua) {
    pimpl->config_.user_agent = ua;
}
void HttpClient::set_max_response_size(size_t max) {
    pimpl->config_.max_response_size = max;
}

void HttpClient::set_auth(const Auth& auth) {
    pimpl->config_.auth = auth;
}
void HttpClient::set_basic_auth(const std::string& user, const std::string& pass) {
    pimpl->config_.auth = {AuthType::Basic, user, pass, "", "X-API-Key"};
}
void HttpClient::set_bearer_token(const std::string& token) {
    pimpl->config_.auth = {AuthType::Bearer, "", "", token, "X-API-Key"};
}
void HttpClient::set_api_key(const std::string& key, const std::string& header) {
    pimpl->config_.auth = {AuthType::ApiKey, "", "", key, header};
}
void HttpClient::clear_auth() {
    pimpl->config_.auth = Auth{};
}

void HttpClient::set_proxy(const ProxyConfig& proxy) {
    pimpl->config_.proxy = proxy;
}
void HttpClient::clear_proxy() {
    pimpl->config_.proxy = ProxyConfig{};
}

void HttpClient::set_ssl_config(const SSLConfig& ssl) {
    pimpl->config_.ssl = ssl;
}
void HttpClient::set_ssl_verification(bool peer, bool host) {
    pimpl->config_.ssl.verify_peer = peer;
    pimpl->config_.ssl.verify_host = host;
}
void HttpClient::set_ssl_ca_cert(const std::string& path) {
    pimpl->config_.ssl.ca_cert_path = path;
}
void HttpClient::set_ssl_client_cert(const std::string& cert, const std::string& key) {
    pimpl->config_.ssl.client_cert_path = cert;
    pimpl->config_.ssl.client_key_path = key;
}

void HttpClient::set_http_version(HttpVersion v) {
    pimpl->config_.http_version = v;
}

void HttpClient::set_retry_config(const RetryConfig& retry) {
    pimpl->config_.retry = retry;
}
void HttpClient::enable_retry(int max_attempts) {
    pimpl->config_.retry.max_attempts = max_attempts;
}
void HttpClient::disable_retry() {
    pimpl->config_.retry.max_attempts = 1;
}

void HttpClient::add_request_interceptor(RequestInterceptor i) {
    pimpl->request_interceptors_.push_back(std::move(i));
}
void HttpClient::add_response_interceptor(ResponseInterceptor i) {
    pimpl->response_interceptors_.push_back(std::move(i));
}
void HttpClient::clear_interceptors() {
    pimpl->request_interceptors_.clear();
    pimpl->response_interceptors_.clear();
}

void HttpClient::set_log_level(LogLevel level) {
    pimpl->logger_.setLevel(level);
}
void HttpClient::set_log_callback(LogCallback cb) {
    pimpl->logger_.setCallback(std::move(cb));
}

void HttpClient::enable_connection_pooling(int max) {
    pimpl->config_.max_connections = max;
    pimpl->connection_pool_.resize(max);
}
void HttpClient::disable_connection_pooling() {
    pimpl->config_.max_connections = 0;
    pimpl->connection_pool_.resize(0);
}

void HttpClient::clear_cookies() {
    pimpl->cookies_.clear();
}
void HttpClient::set_cookie(const std::string& name,
                            const std::string& value,
                            const std::string& /*domain*/) {
    pimpl->cookies_[name] = value;
}

HttpMetrics HttpClient::connection_metrics() const {
    std::lock_guard<std::mutex> lock(pimpl->metrics_mutex_);
    return pimpl->total_metrics_;
}
void HttpClient::reset_metrics() {
    std::lock_guard<std::mutex> lock(pimpl->metrics_mutex_);
    pimpl->total_metrics_ = HttpMetrics{};
}

HttpClientBuilder HttpClient::builder() {
    return HttpClientBuilder{};
}

HttpClientBuilder& HttpClientBuilder::url(const std::string& u) {
    request_.url = u;
    return *this;
}
HttpClientBuilder& HttpClientBuilder::method(const std::string& m) {
    request_.method = m;
    return *this;
}
HttpClientBuilder& HttpClientBuilder::header(const std::string& n, const std::string& v) {
    request_.headers[n] = v;
    return *this;
}
HttpClientBuilder& HttpClientBuilder::headers(const Headers& h) {
    for (auto& [k, v] : h) request_.headers[k] = v;
    return *this;
}
HttpClientBuilder& HttpClientBuilder::body(const std::string& b) {
    request_.body = b;
    return *this;
}
HttpClientBuilder& HttpClientBuilder::json_body(const std::string& json) {
    request_.body = json;
    request_.headers["Content-Type"] = "application/json";
    return *this;
}
HttpClientBuilder& HttpClientBuilder::form_body(const Headers& form) {
    std::string encoded;
    for (auto& [k, v] : form) {
        if (!encoded.empty()) encoded += "&";
        encoded += utils::url_encode(k) + "=" + utils::url_encode(v);
    }
    request_.body = encoded;
    request_.headers["Content-Type"] = "application/x-www-form-urlencoded";
    return *this;
}
HttpClientBuilder& HttpClientBuilder::timeout(int ms) {
    request_.timeout_ms = ms;
    return *this;
}
HttpClientBuilder& HttpClientBuilder::auth(const Auth& a) {
    request_.auth = a;
    return *this;
}
HttpClientBuilder& HttpClientBuilder::basic_auth(const std::string& u,
                                                 const std::string& p) {
    request_.auth = Auth{AuthType::Basic, u, p, "", "X-API-Key"};
    return *this;
}
HttpClientBuilder& HttpClientBuilder::bearer_token(const std::string& t) {
    request_.auth = Auth{AuthType::Bearer, "", "", t, "X-API-Key"};
    return *this;
}
HttpClientBuilder& HttpClientBuilder::api_key(const std::string& k, const std::string& h) {
    request_.auth = Auth{AuthType::ApiKey, "", "", k, h};
    return *this;
}
HttpClientBuilder& HttpClientBuilder::follow_redirects(bool f) {
    request_.follow_redirects = f;
    return *this;
}
HttpClientBuilder& HttpClientBuilder::max_redirects(int m) {
    request_.max_redirects = m;
    return *this;
}
HttpClientBuilder& HttpClientBuilder::user_agent(const std::string& ua) {
    request_.headers["User-Agent"] = ua;
    return *this;
}
HttpClientBuilder& HttpClientBuilder::enable_metrics(bool e) {
    request_.enable_metrics = e;
    return *this;
}

HttpRequest HttpClientBuilder::build() {
    return request_;
}
HttpResponse HttpClientBuilder::send() {
    return client_.send(request_);
}

Auth auth::basic(const std::string& user, const std::string& pass) {
    return {AuthType::Basic, user, pass, "", "X-API-Key"};
}
Auth auth::bearer(const std::string& token) {
    return {AuthType::Bearer, "", "", token, "X-API-Key"};
}
Auth auth::api_key(const std::string& key, const std::string& header) {
    return {AuthType::ApiKey, "", "", key, header};
}

std::string utils::url_encode(const std::string& str) {
    CURL* curl = curl_easy_init();
    if (!curl) return str;
    char* encoded = curl_easy_escape(curl, str.c_str(), static_cast<int>(str.length()));
    std::string result(encoded ? encoded : str);
    if (encoded) curl_free(encoded);
    curl_easy_cleanup(curl);
    return result;
}

std::string utils::url_decode(const std::string& str) {
    CURL* curl = curl_easy_init();
    if (!curl) return str;
    int out_len;
    char* decoded =
        curl_easy_unescape(curl, str.c_str(), static_cast<int>(str.length()), &out_len);
    std::string result(decoded ? std::string(decoded, out_len) : str);
    if (decoded) curl_free(decoded);
    curl_easy_cleanup(curl);
    return result;
}

std::string utils::base64_encode(const std::string& str) {
    static constexpr char table[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : str) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(table[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(table[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

std::string utils::base64_decode(const std::string& str) {
    static constexpr int table[256] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1,
        -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, -1, 26, 27, 28,
        29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
        49, 50, 51, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    };
    std::string out;
    int val = 0, valb = -8;
    for (unsigned char c : str) {
        if (table[c] == -1) break;
        val = (val << 6) + table[c];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<char>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::map<std::string, std::string> utils::parse_query_string(const std::string& query) {
    std::map<std::string, std::string> params;
    std::istringstream stream(query);
    std::string pair;
    while (std::getline(stream, pair, '&')) {
        auto eq = pair.find('=');
        if (eq != std::string::npos) {
            params[url_decode(pair.substr(0, eq))] = url_decode(pair.substr(eq + 1));
        }
    }
    return params;
}

std::string utils::build_query_string(const std::map<std::string, std::string>& params) {
    std::string result;
    for (auto& [k, v] : params) {
        if (!result.empty()) result += "&";
        result += url_encode(k) + "=" + url_encode(v);
    }
    return result;
}

std::string utils::generate_request_id() {
    thread_local std::mt19937 gen{std::random_device{}()};
    static constexpr char hex[] = "0123456789abcdef";
    std::uniform_int_distribution<int> dist(0, 15);
    std::string id;
    id.reserve(36);
    for (int i = 0; i < 36; ++i) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            id += '-';
        } else {
            id += hex[dist(gen)];
        }
    }
    return id;
}

bool utils::is_valid_url(const std::string& url) {
    if (url.empty()) return false;
    for (char c : url) {
        if (std::isspace(static_cast<unsigned char>(c))) return false;
    }
    bool has_scheme =
        (url.compare(0, 7, "http://") == 0) || (url.compare(0, 8, "https://") == 0);
    return has_scheme;
}

}  // namespace blaze
