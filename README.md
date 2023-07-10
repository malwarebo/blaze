# blaze
Simple C++ http library


To use the HTTP client library, follow these steps:

1. Include the http_client.hpp header file in your source code:

```cpp
#include "http_client.hpp"
```

2. Create an instance of the HTTPClient class:

```cpp
HTTPClient client;
```

3. Use the instance to make HTTP requests by calling the appropriate methods:

4. To make a GET request:

```cpp
std::string response = client.get("https://example.com");
```
    
5. To make a PUT request:

```cpp
std::string response = client.put("https://example.com/resource", "Data to PUT");
```

6. To make an UPDATE request:

```cpp
std::string response = client.update("https://example.com/resource", "Data to UPDATE");
```

7. To make a DELETE request:

```cpp
std::string response = client.del("https://example.com/resource");
```

8. To make a POST request:

```cpp
std::string response = client.post("https://example.com/resource", "Data to POST
```
