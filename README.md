<p align="center">
  <img src="assets/logo.svg" alt="Blaze" width="200">
</p>

# Blaze

C++20 HTTP client. Built on libcurl. Coroutines for async, `curl_multi` for the event loop, no threads-per-request nonsense.

## Why

Every C++ HTTP library is either a thin curl wrapper with no async story, or a Boost.Beast-level commitment. Blaze sits in between: real non-blocking I/O through `curl_multi`, exposed as C++20 coroutines (`co_await`), with a sync API when you don't need that.

## How it works

Sync requests go through `curl_easy_perform` with connection pooling. Async requests submit to a process-global `curl_multi` event loop on a background thread. Each `async_*` call returns a `Task<HttpResponse>` you `co_await`.

Completed transfers are handed to a small dispatch pool rather than resumed on the event loop, so a slow continuation stalls only itself and never blocks other in-flight requests.

`when_all` runs multiple requests concurrently through the same multi handle and waits for all of them even if one fails. `async_race` returns the first to complete and cancels the rest.

## Build

```bash
cmake -B build
cmake --build build
cd build && ctest --output-on-failure
cmake --install build --prefix /usr/local
```

Needs C++20 (GCC 11+, Clang 14+, MSVC 19.29+), CMake 3.14+, libcurl.

| Option | Default | Meaning |
| --- | --- | --- |
| `BLAZE_BUILD_TESTS` | on when top-level | Build the test suite |
| `BLAZE_BUILD_EXAMPLES` | on when top-level | Build the examples |
| `BLAZE_SANITIZER` | empty | `address`, `thread`, or `undefined` |

Consuming an installed copy:

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

A response carries an `error` only when the transfer itself failed. A 4xx or 5xx is a completed transfer, so `ok()` is still true:

```cpp
auto r = client.get(url);

if (!r.ok())            // DNS failure, timeout, TLS problem, cancellation
    std::cerr << r.error_message();
else if (r.is_http_error())   // the server answered, with 4xx/5xx
    std::cerr << r.status_code;
```

Header lookup is case-insensitive, so `r.headers["Content-Type"]` works regardless of how the server or HTTP/2 cased it.

## Lifetimes

Destroying a `Task` that is still in flight is safe: it detaches, requests cancellation, and the coroutine frame reclaims itself when the transfer finishes.

`~HttpClient` cancels any async request it still owns and blocks until none can reach it again, so a client can be destroyed without first joining its outstanding work. Don't destroy a client from inside one of its own callbacks.

The async engine is process-global and intentionally never destroyed at exit, which avoids static-destruction ordering hazards. Call `blaze::shutdown()` if you want its threads joined explicitly, for instance under leak checking.

## License

Apache 2.0
