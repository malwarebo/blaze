#pragma once

#include "types.hpp"
#include <concepts>

namespace blaze {

template<typename F>
concept StreamCallable = requires(F f, const char* data, size_t size) {
    { f(data, size) } -> std::convertible_to<bool>;
};

template<typename F>
concept ProgressCallable = requires(F f, size_t downloaded, size_t total) {
    { f(downloaded, total) } -> std::convertible_to<bool>;
};

template<typename F>
concept RequestInterceptable = requires(F f, HttpRequest& req) {
    { f(req) };
};

template<typename F>
concept ResponseInterceptable = requires(F f, HttpResponse& resp) {
    { f(resp) };
};

} // namespace blaze
