<p align="center">
  <img src="assets/logo.svg" alt="Blaze" width="200">
</p>

# Blaze

C++20 HTTP client. Built on libcurl. Coroutines for async, `curl_multi` for the event loop, no threads-per-request nonsense.

## Why

Every C++ HTTP library is either a thin curl wrapper with no async story, or a Boost.Beast-level commitment. Blaze sits in between: real non-blocking I/O through `curl_multi`, exposed as C++20 coroutines (`co_await`), with a sync API when you don't need that.

## How it works

Sync requests go through `curl_easy_perform` with connection pooling. Async requests submit to a process-global `curl_multi` event loop on a background thread. Each `async_*` call returns a `Task<HttpResponse>` you `co_await`. Completions resume on a dispatch pool, not the event loop, so a slow continuation can't stall other transfers. `when_all` runs multiple requests concurrently through the same multi handle and waits for all of them even if one fails. `async_race` returns the first to complete and cancels the rest.

## Build

```bash
cmake -B build
cmake --build build
cd build && ctest --output-on-failure
cmake --install build --prefix /usr/local
```

Needs C++20 (GCC 11+, Clang 14+, MSVC 19.29+), CMake 3.14+, libcurl.

Options: `BLAZE_BUILD_TESTS`, `BLAZE_BUILD_EXAMPLES` (both on when top-level), `BLAZE_SANITIZER` (`address`, `thread`, `undefined`).

```cmake
find_package(blaze REQUIRED)
target_link_libraries(app PRIVATE blaze::blaze)
```

## Usage

```cpp
#include <blaze/blaze.hpp>

blaze::HttpClient client;

auto r = client.get("https://api.example.com/data");

auto r = co_await client.async_get("https://api.example.com/data");

auto [r1, r2, r3] = co_await blaze::when_all(
    client.async_get("https://api.example.com/a"),
    client.async_get("https://api.example.com/b"),
    client.async_get("https://api.example.com/c")
);

auto [winner, response] = co_await client.async_race(std::move(requests));

blaze::sync_wait(someCoroutine());
```

Config, auth, SSL, retry, interceptors, HTTP/2/3, proxy, streaming, file upload/download — all there. Look at `lib/http_client.hpp` for the full API.

## Errors

`error` is set only when the transfer failed. A 4xx/5xx is a completed transfer, so `ok()` stays true.

```cpp
if (!r.ok())                 // DNS, timeout, TLS, cancellation
    std::cerr << r.error_message();
else if (r.is_http_error())  // server answered 4xx/5xx
    std::cerr << r.status_code;
```

Header lookup is case-insensitive.

## Lifetimes

Destroying an in-flight `Task` is safe: it detaches, cancels, and the frame reclaims itself. `~HttpClient` cancels and drains its outstanding async work, so don't destroy a client from inside its own callback.

The async engine is never destroyed at exit, avoiding static-destruction ordering hazards. Call `blaze::shutdown()` to join its threads explicitly.

## License

Apache 2.0
