#pragma once

// Minimal in-process HTTP/1.1 server so the suite does not depend on a public
// service being reachable. POSIX sockets only.

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace blaze_test {

#ifdef MSG_NOSIGNAL
inline constexpr int kSendFlags = MSG_NOSIGNAL;
#else
inline constexpr int kSendFlags = 0;
#endif

struct Request {
    std::string method;
    std::string path;
    std::string body;
    std::map<std::string, std::string> headers;

    std::string header(const std::string& name) const {
        auto it = headers.find(lower(name));
        return it == headers.end() ? std::string{} : it->second;
    }

    static std::string lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(::tolower(static_cast<unsigned char>(c)));
        return s;
    }
};

class TestServer {
public:
    TestServer() {
        // A client that hangs up mid-response must not take the process with it.
        static std::once_flag once;
        std::call_once(once, [] { ::signal(SIGPIPE, SIG_IGN); });

        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        ::listen(listen_fd_, 64);

        socklen_t len = sizeof(addr);
        ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
        port_ = ntohs(addr.sin_port);

        running_ = true;
        accept_thread_ = std::thread([this] { accept_loop(); });
    }

    ~TestServer() {
        running_ = false;
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        if (accept_thread_.joinable()) accept_thread_.join();
        for (auto& t : workers_)
            if (t.joinable()) t.join();
    }

    TestServer(const TestServer&) = delete;
    TestServer& operator=(const TestServer&) = delete;

    int port() const { return port_; }

    std::string url(const std::string& path) const {
        return "http://127.0.0.1:" + std::to_string(port_) + path;
    }

private:
    void accept_loop() {
        while (running_) {
            int fd = ::accept(listen_fd_, nullptr, nullptr);
            if (fd < 0) {
                if (!running_) return;
                continue;
            }
            workers_.emplace_back([this, fd] {
#ifdef SO_NOSIGPIPE
                int on = 1;
                ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, sizeof(on));
#endif
                handle(fd);
                ::close(fd);
            });
        }
    }

    static bool read_request(int fd, Request& req) {
        std::string buffer;
        char chunk[4096];

        while (buffer.find("\r\n\r\n") == std::string::npos) {
            ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
            if (n <= 0) return false;
            buffer.append(chunk, static_cast<size_t>(n));
        }

        size_t head_end = buffer.find("\r\n\r\n");
        std::istringstream head(buffer.substr(0, head_end));
        std::string line;

        std::getline(head, line);
        {
            std::istringstream rl(line);
            std::string version;
            rl >> req.method >> req.path >> version;
        }

        while (std::getline(head, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto colon = line.find(':');
            if (colon == std::string::npos) continue;
            std::string name = Request::lower(line.substr(0, colon));
            std::string value = line.substr(colon + 1);
            auto start = value.find_first_not_of(" \t");
            if (start != std::string::npos)
                value = value.substr(start);
            else
                value.clear();
            req.headers[name] = value;
        }

        size_t content_length = 0;
        auto cl = req.headers.find("content-length");
        if (cl != req.headers.end()) content_length = std::stoul(cl->second);

        req.body = buffer.substr(head_end + 4);
        while (req.body.size() < content_length) {
            ssize_t n = ::recv(fd, chunk, sizeof(chunk), 0);
            if (n <= 0) break;
            req.body.append(chunk, static_cast<size_t>(n));
        }
        return true;
    }

    static void send_all(int fd, const std::string& data) {
        size_t sent = 0;
        while (sent < data.size()) {
            ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, kSendFlags);
            if (n <= 0) return;
            sent += static_cast<size_t>(n);
        }
    }

    static void respond(int fd,
                        int status,
                        const std::string& body,
                        const std::vector<std::pair<std::string, std::string>>& extra = {},
                        bool include_body = true) {
        std::string out =
            "HTTP/1.1 " + std::to_string(status) + " " + reason(status) + "\r\n";
        out += "Content-Type: application/json\r\n";
        out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        out += "Connection: close\r\n";
        for (auto& [k, v] : extra) out += k + ": " + v + "\r\n";
        out += "\r\n";
        if (include_body) out += body;
        send_all(fd, out);
    }

    static std::string reason(int status) {
        switch (status) {
            case 200:
                return "OK";
            case 301:
                return "Moved Permanently";
            case 302:
                return "Found";
            case 401:
                return "Unauthorized";
            case 404:
                return "Not Found";
            case 429:
                return "Too Many Requests";
            case 500:
                return "Internal Server Error";
            case 503:
                return "Service Unavailable";
            default:
                return "Status";
        }
    }

    static std::string escape(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') out += '\\';
            if (c == '\n') {
                out += "\\n";
                continue;
            }
            out += c;
        }
        return out;
    }

    static std::string headers_json(const Request& req) {
        std::string out = "{";
        bool first = true;
        for (auto& [k, v] : req.headers) {
            if (!first) out += ", ";
            out += "\"" + escape(k) + "\": \"" + escape(v) + "\"";
            first = false;
        }
        return out + "}";
    }

    static std::string echo_json(const Request& req) {
        return "{\"method\": \"" + req.method + "\", \"path\": \"" + escape(req.path) +
               "\", \"body\": \"" + escape(req.body) +
               "\", \"headers\": " + headers_json(req) + "}";
    }

    static bool starts_with(const std::string& s, const std::string& prefix) {
        return s.rfind(prefix, 0) == 0;
    }

    static int trailing_number(const std::string& path, const std::string& prefix) {
        if (!starts_with(path, prefix)) return -1;
        try {
            return std::stoi(path.substr(prefix.size()));
        } catch (...) {
            return -1;
        }
    }

    void handle(int fd) {
        Request req;
        if (!read_request(fd, req)) return;

        const std::string& path = req.path;

        if (path == "/get" || path == "/" || path == "/post" || path == "/put" ||
            path == "/patch" || path == "/delete" || path == "/headers") {
            respond(fd, 200, echo_json(req), {}, req.method != "HEAD");
            return;
        }

        if (int code = trailing_number(path, "/status/"); code > 0) {
            respond(fd, code, "{\"status\": " + std::to_string(code) + "}");
            return;
        }

        if (int hops = trailing_number(path, "/redirect/"); hops > 0) {
            std::string next = hops > 1 ? "/redirect/" + std::to_string(hops - 1) : "/get";
            respond(fd, 302, "", {{"Location", next}});
            return;
        }

        if (int seconds = trailing_number(path, "/delay/"); seconds >= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(seconds * 300));
            respond(fd, 200, echo_json(req));
            return;
        }

        if (int count = trailing_number(path, "/bytes/"); count >= 0) {
            respond(fd, 200, std::string(static_cast<size_t>(count), 'a'));
            return;
        }

        if (int lines = trailing_number(path, "/stream/"); lines > 0) {
            std::string head = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n";
            head += "Transfer-Encoding: chunked\r\nConnection: close\r\n\r\n";
            send_all(fd, head);
            for (int i = 0; i < lines; ++i) {
                std::string payload = "{\"index\": " + std::to_string(i) + "}\n";
                std::ostringstream chunk;
                chunk << std::hex << payload.size() << "\r\n" << payload << "\r\n";
                send_all(fd, chunk.str());
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            send_all(fd, "0\r\n\r\n");
            return;
        }

        if (starts_with(path, "/basic-auth/")) {
            auto rest = path.substr(std::string("/basic-auth/").size());
            auto slash = rest.find('/');
            std::string user = rest.substr(0, slash);
            std::string pass = slash == std::string::npos ? "" : rest.substr(slash + 1);

            std::string expected = "Basic " + base64(user + ":" + pass);
            if (req.header("Authorization") == expected)
                respond(fd, 200, "{\"authenticated\": true, \"user\": \"" + user + "\"}");
            else
                respond(fd, 401, "{\"authenticated\": false}");
            return;
        }

        if (path == "/bearer") {
            std::string auth = req.header("Authorization");
            if (starts_with(auth, "Bearer ")) {
                respond(fd,
                        200,
                        "{\"authenticated\": true, \"token\": \"" + escape(auth.substr(7)) +
                            "\"}");
            } else {
                respond(fd, 401, "{\"authenticated\": false}");
            }
            return;
        }

        // Deliberately lowercased to exercise case-insensitive header lookup.
        if (path == "/lowercase-headers") {
            respond(
                fd, 200, "{}", {{"x-custom-reply", "present"}, {"content-language", "en"}});
            return;
        }

        respond(fd, 404, "{\"error\": \"not found\"}");
    }

    static std::string base64(const std::string& in) {
        static constexpr char table[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        int val = 0, valb = -6;
        for (unsigned char c : in) {
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

    int listen_fd_{-1};
    int port_{0};
    std::atomic<bool> running_{false};
    std::thread accept_thread_;
    std::vector<std::thread> workers_;
};

}  // namespace blaze_test
