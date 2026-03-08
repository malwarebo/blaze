#pragma once

#include <coroutine>
#include <variant>
#include <exception>
#include <atomic>
#include <tuple>
#include <utility>
#include <future>
#include <optional>
#include <cstdint>

namespace blaze {

namespace detail {
    inline void* completed_tag() noexcept {
        return reinterpret_cast<void*>(std::uintptr_t(1));
    }

    struct SyncWaitCoro {
        struct promise_type {
            SyncWaitCoro get_return_object() {
                return SyncWaitCoro{
                    std::coroutine_handle<promise_type>::from_promise(*this)};
            }
            std::suspend_never initial_suspend() noexcept { return {}; }
            std::suspend_always final_suspend() noexcept { return {}; }
            void return_void() {}
            void unhandled_exception() { std::terminate(); }
        };
        std::coroutine_handle<promise_type> handle;
    };
} // namespace detail

template<typename T = void>
class Task;

template<typename T>
class Task {
public:
    struct promise_type {
        std::variant<std::monostate, T, std::exception_ptr> result;
        std::atomic<void*> waiter{nullptr};
        bool started{false};

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        auto final_suspend() noexcept {
            struct Awaiter {
                bool await_ready() noexcept { return false; }
                std::coroutine_handle<> await_suspend(
                    std::coroutine_handle<promise_type> h) noexcept {
                    void* expected = nullptr;
                    if (h.promise().waiter.compare_exchange_strong(
                            expected, detail::completed_tag(),
                            std::memory_order_acq_rel, std::memory_order_acquire)) {
                        return std::noop_coroutine();
                    }
                    return std::coroutine_handle<>::from_address(expected);
                }
                void await_resume() noexcept {}
            };
            return Awaiter{};
        }

        void return_value(T value) {
            result.template emplace<1>(std::move(value));
        }

        void unhandled_exception() {
            result.template emplace<2>(std::current_exception());
        }
    };

    using value_type = T;
    using handle_type = std::coroutine_handle<promise_type>;

    explicit Task(handle_type h) : handle_(h) {}

    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~Task() {
        if (handle_) handle_.destroy();
    }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    bool await_ready() const noexcept {
        return handle_.done();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        void* expected = nullptr;
        if (handle_.promise().waiter.compare_exchange_strong(
                expected, awaiting.address(),
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            if (!handle_.promise().started) {
                handle_.promise().started = true;
                return handle_;
            }
            return std::noop_coroutine();
        }
        return awaiting;
    }

    T await_resume() {
        auto& r = handle_.promise().result;
        if (r.index() == 2)
            std::rethrow_exception(std::get<2>(r));
        return std::move(std::get<1>(r));
    }

    void start() {
        if (!handle_.promise().started) {
            handle_.promise().started = true;
            handle_.resume();
        }
    }

    handle_type handle() const { return handle_; }

private:
    handle_type handle_;
};

template<>
class Task<void> {
public:
    struct promise_type {
        std::exception_ptr exception;
        std::atomic<void*> waiter{nullptr};
        bool started{false};

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        auto final_suspend() noexcept {
            struct Awaiter {
                bool await_ready() noexcept { return false; }
                std::coroutine_handle<> await_suspend(
                    std::coroutine_handle<promise_type> h) noexcept {
                    void* expected = nullptr;
                    if (h.promise().waiter.compare_exchange_strong(
                            expected, detail::completed_tag(),
                            std::memory_order_acq_rel, std::memory_order_acquire)) {
                        return std::noop_coroutine();
                    }
                    return std::coroutine_handle<>::from_address(expected);
                }
                void await_resume() noexcept {}
            };
            return Awaiter{};
        }

        void return_void() {}

        void unhandled_exception() {
            exception = std::current_exception();
        }
    };

    using value_type = void;
    using handle_type = std::coroutine_handle<promise_type>;

    explicit Task(handle_type h) : handle_(h) {}
    Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (handle_) handle_.destroy();
            handle_ = std::exchange(other.handle_, nullptr);
        }
        return *this;
    }

    ~Task() { if (handle_) handle_.destroy(); }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    bool await_ready() const noexcept { return handle_.done(); }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        void* expected = nullptr;
        if (handle_.promise().waiter.compare_exchange_strong(
                expected, awaiting.address(),
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            if (!handle_.promise().started) {
                handle_.promise().started = true;
                return handle_;
            }
            return std::noop_coroutine();
        }
        return awaiting;
    }

    void await_resume() {
        if (handle_.promise().exception)
            std::rethrow_exception(handle_.promise().exception);
    }

    void start() {
        if (!handle_.promise().started) {
            handle_.promise().started = true;
            handle_.resume();
        }
    }

    handle_type handle() const { return handle_; }

private:
    handle_type handle_;
};

template<typename... Ts>
Task<std::tuple<Ts...>> when_all(Task<Ts>... tasks) {
    (tasks.start(), ...);
    co_return std::tuple<Ts...>{co_await std::move(tasks)...};
}

template<typename T>
T sync_wait(Task<T>&& task) {
    std::promise<T> p;
    auto future = p.get_future();

    auto coro = [&]() -> detail::SyncWaitCoro {
        try {
            p.set_value(co_await task);
        } catch (...) {
            p.set_exception(std::current_exception());
        }
    }();

    auto result = future.get();
    coro.handle.destroy();
    return result;
}

inline void sync_wait(Task<void>&& task) {
    std::promise<void> p;
    auto future = p.get_future();

    auto coro = [&]() -> detail::SyncWaitCoro {
        try {
            co_await task;
            p.set_value();
        } catch (...) {
            p.set_exception(std::current_exception());
        }
    }();

    future.get();
    coro.handle.destroy();
}

} // namespace blaze
