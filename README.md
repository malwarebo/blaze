<p align="center">
  <img src="assets/logo.svg" alt="Blaze" width="200">
</p>

# Blaze

C++20 HTTP client. Built on libcurl. Coroutines for async, `curl_multi` for the event loop, no threads-per-request nonsense.

## Why

Every C++ HTTP library is either a thin curl wrapper with no async story, or a Boost.Beast-level commitment. Blaze sits in between: real non-blocking I/O through `curl_multi`, exposed as C++20 coroutines (`co_await`), with a sync API when you don't need that. One header, one library, link against curl.

## How it works

Sync requests go through `curl_easy_perform` with connection pooling. Async requests submit to a process-global `curl_multi` event loop on a background thread. Each `async_*` call returns a `Task<HttpResponse>` you `co_await`. `when_all` runs multiple requests concurrently through the same multi handle. `async_race` returns the first to complete and cancels the rest.

## Build

```bash
cmake -B build
cmake --build build
cd build && ctest --output-on-failure
cmake --install build --prefix /usr/local
```

Needs C++20 (GCC 11+, Clang 14+, MSVC 19.29+), CMake 3.14+, libcurl.

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

## License

Apache 2.0
