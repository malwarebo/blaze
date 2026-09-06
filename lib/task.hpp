#pragma once

#include <coroutine>
#include <variant>
#include <exception>
#include <atomic>
#include <tuple>
#include <utility>
#include <optional>
#include <functional>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <type_traits>
#include <cstdint>

namespace blaze {

namespace detail {
// Values of promise::waiter. Anything else is the address of an awaiting coroutine.
inline void* completed_tag() noexcept {
    return reinterpret_cast<void*>(std::uintptr_t(1));
}
inline void* detached_tag() noexcept {
    return reinterpret_cast<void*>(std::uintptr_t(2));
}

/// Lives outside the coroutine frame so an abandoning Task can cancel without
/// racing the frame's self-destruction.
class CancelHook {
public:
    void set(std::function<void()> fn) {
        std::lock_guard<std::mutex> lock(mutex_);
        fn_ = std::move(fn);
    }

    void invoke() {
        std::function<void()> fn;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            fn = fn_;
        }
        if (fn) fn();
    }

private:
    std::mutex mutex_;
    std::function<void()> fn_;
};

struct PromiseBase {
    std::atomic<void*> waiter{nullptr};
    bool started{false};
    std::shared_ptr<CancelHook> cancel{std::make_shared<CancelHook>()};
};

template<typename Promise>
struct FinalAwaiter {
    bool await_ready() noexcept { return false; }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> h) noexcept {
        void* prev =
            h.promise().waiter.exchange(completed_tag(), std::memory_order_acq_rel);

        if (prev == detached_tag()) {
            h.destroy();
            return std::noop_coroutine();
        }
        if (prev == nullptr) {
            return std::noop_coroutine();
        }
        return std::coroutine_handle<>::from_address(prev);
    }

    void await_resume() noexcept {}
};

/// Self-destroying driver for sync_wait. Never suspends at the end, so the
/// waiting thread never has to touch (or destroy) the frame.
struct SyncWaitCoro {
    struct promise_type {
        SyncWaitCoro get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { std::terminate(); }
    };
};

struct SyncWaitEvent {
    void set() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            done_ = true;
        }
        cv_.notify_all();
    }

    void wait() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return done_; });
    }

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    bool done_{false};
};
}  // namespace detail

template<typename T = void>
class Task;

template<typename T>
class Task {
public:
    struct promise_type : detail::PromiseBase {
        std::variant<std::monostate, T, std::exception_ptr> result;

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        detail::FinalAwaiter<promise_type> final_suspend() noexcept { return {}; }

        void return_value(T value) { result.template emplace<1>(std::move(value)); }

        void unhandled_exception() { result.template emplace<2>(std::current_exception()); }
    };

    using value_type = T;
    using handle_type = std::coroutine_handle<promise_type>;

    explicit Task(handle_type h) : handle_(h), cancel_(h.promise().cancel) {}

    Task(Task&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)),
          cancel_(std::move(other.cancel_)) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            release();
            handle_ = std::exchange(other.handle_, nullptr);
            cancel_ = std::move(other.cancel_);
        }
        return *this;
    }

    ~Task() { release(); }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    bool await_ready() const noexcept {
        return handle_.promise().waiter.load(std::memory_order_acquire) ==
               detail::completed_tag();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        // Everything is read before the continuation is published: the instant the
        // exchange succeeds another thread may resume and destroy this frame.
        auto handle = handle_;
        bool needs_start = !handle.promise().started;
        if (needs_start) handle.promise().started = true;

        void* expected = nullptr;
        if (handle.promise().waiter.compare_exchange_strong(expected,
                                                            awaiting.address(),
                                                            std::memory_order_acq_rel,
                                                            std::memory_order_acquire)) {
            return needs_start ? std::coroutine_handle<>{handle} : std::noop_coroutine();
        }
        return awaiting;
    }

    T await_resume() {
        auto& r = handle_.promise().result;
        if (r.index() == 2) std::rethrow_exception(std::get<2>(r));
        return std::move(std::get<1>(r));
    }
    void start() {
        if (handle_ && !handle_.promise().started) {
            handle_.promise().started = true;
            handle_.resume();
        }
    }

    handle_type handle() const { return handle_; }

private:
    // A started-but-unfinished coroutine may still be owned by the I/O engine, so the
    // frame is never destroyed here; it reclaims itself in FinalAwaiter.
    void release() noexcept {
        if (!handle_) return;

        auto cancel = cancel_;
        auto handle = std::exchange(handle_, nullptr);
        cancel_.reset();

        if (!handle.promise().started) {
            handle.destroy();
            return;
        }

        void* prev = handle.promise().waiter.exchange(detail::detached_tag(),
                                                      std::memory_order_acq_rel);

        if (prev == detail::completed_tag()) {
            handle.destroy();
            return;
        }
        if (cancel) cancel->invoke();
    }

    handle_type handle_{nullptr};
    std::shared_ptr<detail::CancelHook> cancel_;
};

template<>
class Task<void> {
public:
    struct promise_type : detail::PromiseBase {
        std::exception_ptr exception;

        Task get_return_object() {
            return Task{std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept { return {}; }

        detail::FinalAwaiter<promise_type> final_suspend() noexcept { return {}; }

        void return_void() {}

        void unhandled_exception() { exception = std::current_exception(); }
    };

    using value_type = void;
    using handle_type = std::coroutine_handle<promise_type>;

    explicit Task(handle_type h) : handle_(h), cancel_(h.promise().cancel) {}

    Task(Task&& other) noexcept
        : handle_(std::exchange(other.handle_, nullptr)),
          cancel_(std::move(other.cancel_)) {}

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            release();
            handle_ = std::exchange(other.handle_, nullptr);
            cancel_ = std::move(other.cancel_);
        }
        return *this;
    }

    ~Task() { release(); }

    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    bool await_ready() const noexcept {
        return handle_.promise().waiter.load(std::memory_order_acquire) ==
               detail::completed_tag();
    }

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) noexcept {
        auto handle = handle_;
        bool needs_start = !handle.promise().started;
        if (needs_start) handle.promise().started = true;

        void* expected = nullptr;
        if (handle.promise().waiter.compare_exchange_strong(expected,
                                                            awaiting.address(),
                                                            std::memory_order_acq_rel,
                                                            std::memory_order_acquire)) {
            return needs_start ? std::coroutine_handle<>{handle} : std::noop_coroutine();
        }
        return awaiting;
    }

    void await_resume() {
        if (handle_.promise().exception)
            std::rethrow_exception(handle_.promise().exception);
    }

    void start() {
        if (handle_ && !handle_.promise().started) {
            handle_.promise().started = true;
            handle_.resume();
        }
    }

    handle_type handle() const { return handle_; }

private:
    void release() noexcept {
        if (!handle_) return;

        auto cancel = cancel_;
        auto handle = std::exchange(handle_, nullptr);
        cancel_.reset();

        if (!handle.promise().started) {
            handle.destroy();
            return;
        }

        void* prev = handle.promise().waiter.exchange(detail::detached_tag(),
                                                      std::memory_order_acq_rel);

        if (prev == detail::completed_tag()) {
            handle.destroy();
            return;
        }
        if (cancel) cancel->invoke();
    }

    handle_type handle_{nullptr};
    std::shared_ptr<detail::CancelHook> cancel_;
};

namespace detail {
template<typename T>
struct WhenAllSlot {
    std::optional<T> value;
    std::exception_ptr error;
};

template<typename T>
Task<WhenAllSlot<T>> capture(Task<T> task) {
    WhenAllSlot<T> slot;
    try {
        slot.value.emplace(co_await task);
    } catch (...) {
        slot.error = std::current_exception();
    }
    co_return slot;
}
}  // namespace detail

/// Runs every task concurrently and waits for all of them, even if some fail.
/// If several throw, the leftmost exception propagates.
template<typename... Ts>
Task<std::tuple<Ts...>> when_all(Task<Ts>... tasks) {
    static_assert(sizeof...(Ts) > 0, "when_all requires at least one task");
    static_assert((!std::is_void_v<Ts> && ...), "when_all does not support Task<void>");

    (tasks.start(), ...);

    // Wrapping first makes every co_await below noexcept, so an early failure can
    // never abandon a sibling that is still in flight.
    std::tuple<detail::WhenAllSlot<Ts>...> slots{
        co_await detail::capture(std::move(tasks))...};

    co_return std::apply(
        [](auto&... slot) {
            (..., (slot.error ? std::rethrow_exception(slot.error) : void()));
            return std::tuple<Ts...>{std::move(*slot.value)...};
        },
        slots);
}

namespace detail {
// Deliberately not lambdas: a capturing lambda coroutine refers back to a
// closure object that dies at the end of the full-expression, while these
// parameters live in the coroutine frame.
template<typename T>
SyncWaitCoro sync_wait_driver(Task<T>& task,
                              std::optional<T>& value,
                              std::exception_ptr& error,
                              SyncWaitEvent& event) {
    try {
        value.emplace(co_await task);
    } catch (...) {
        error = std::current_exception();
    }
    event.set();
}

inline SyncWaitCoro sync_wait_driver_void(Task<void>& task,
                                          std::exception_ptr& error,
                                          SyncWaitEvent& event) {
    try {
        co_await task;
    } catch (...) {
        error = std::current_exception();
    }
    event.set();
}
}  // namespace detail

template<typename T>
T sync_wait(Task<T>&& task) {
    detail::SyncWaitEvent event;
    std::optional<T> value;
    std::exception_ptr error;

    detail::sync_wait_driver(task, value, error, event);
    event.wait();

    if (error) std::rethrow_exception(error);
    return std::move(*value);
}

inline void sync_wait(Task<void>&& task) {
    detail::SyncWaitEvent event;
    std::exception_ptr error;

    detail::sync_wait_driver_void(task, error, event);
    event.wait();

    if (error) std::rethrow_exception(error);
}

}  // namespace blaze
