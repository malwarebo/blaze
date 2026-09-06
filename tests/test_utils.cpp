#include <gtest/gtest.h>

#include "blaze.hpp"
#include "test_server.hpp"

namespace {

TEST(Utils, UrlEncoding) {
    EXPECT_EQ("Hello%20World", blaze::utils::url_encode("Hello World"));
    EXPECT_EQ("Hello World", blaze::utils::url_decode("Hello%20World"));
}

TEST(Utils, Base64) {
    EXPECT_EQ("SGVsbG8gV29ybGQ=", blaze::utils::base64_encode("Hello World"));
    EXPECT_EQ("Hello World", blaze::utils::base64_decode("SGVsbG8gV29ybGQ="));
    EXPECT_EQ("", blaze::utils::base64_encode(""));
}

TEST(Utils, QueryStrings) {
    auto params = blaze::utils::parse_query_string("key1=value1&key2=value%202");
    EXPECT_EQ("value1", params["key1"]);
    EXPECT_EQ("value 2", params["key2"]);

    auto query =
        blaze::utils::build_query_string({{"key1", "value1"}, {"key2", "value 2"}});
    EXPECT_NE(query.find("key1=value1"), std::string::npos);
    EXPECT_NE(query.find("key2=value%202"), std::string::npos);
}

TEST(Utils, UrlValidation) {
    EXPECT_TRUE(blaze::utils::is_valid_url("https://example.com"));
    EXPECT_TRUE(blaze::utils::is_valid_url("http://example.com"));
    EXPECT_FALSE(blaze::utils::is_valid_url("not-a-url"));
    EXPECT_FALSE(blaze::utils::is_valid_url(""));
    EXPECT_FALSE(blaze::utils::is_valid_url("http://exa mple.com"));
    EXPECT_FALSE(blaze::utils::is_valid_url("ftp://example.com"));
}

TEST(Utils, RequestIdsAreUniqueAndWellFormed) {
    auto first = blaze::utils::generate_request_id();
    auto second = blaze::utils::generate_request_id();

    EXPECT_EQ(36u, first.length());
    EXPECT_NE(first, second);
    EXPECT_EQ('-', first[8]);
    EXPECT_EQ('-', first[13]);
}

TEST(AuthHelpers, Basic) {
    auto auth = blaze::auth::basic("user", "pass");
    EXPECT_EQ(blaze::AuthType::Basic, auth.type);
    EXPECT_EQ("user", auth.username);
    EXPECT_EQ("pass", auth.password);
}

TEST(AuthHelpers, Bearer) {
    auto auth = blaze::auth::bearer("token123");
    EXPECT_EQ(blaze::AuthType::Bearer, auth.type);
    EXPECT_EQ("token123", auth.token);
}

TEST(AuthHelpers, ApiKey) {
    auto auth = blaze::auth::api_key("key123", "X-Key");
    EXPECT_EQ(blaze::AuthType::ApiKey, auth.type);
    EXPECT_EQ("key123", auth.token);
    EXPECT_EQ("X-Key", auth.api_key_header);
}

TEST(HeadersMap, LookupIsCaseInsensitive) {
    blaze::Headers headers;
    headers["Content-Type"] = "application/json";

    EXPECT_EQ("application/json", headers["content-type"]);
    EXPECT_EQ("application/json", headers["CONTENT-TYPE"]);
    EXPECT_EQ(1u, headers.size());
    EXPECT_EQ(1u, headers.count("cOnTeNt-TyPe"));
}

TEST(HeadersMap, AssignmentOverwritesRegardlessOfCase) {
    blaze::Headers headers;
    headers["X-Token"] = "first";
    headers["x-token"] = "second";

    EXPECT_EQ(1u, headers.size());
    EXPECT_EQ("second", headers["X-TOKEN"]);
}

TEST(HeadersMap, DistinctNamesRemainDistinct) {
    blaze::Headers headers;
    headers["X-A"] = "1";
    headers["X-B"] = "2";
    EXPECT_EQ(2u, headers.size());
}

TEST(HeadersMap, SupportsTransparentLookup) {
    blaze::Headers headers;
    headers["Accept"] = "*/*";
    EXPECT_NE(headers.find(std::string_view{"accept"}), headers.end());
}

TEST(HttpResponseModel, DefaultIsOkWithNoError) {
    blaze::HttpResponse response;
    EXPECT_TRUE(response.ok());
    EXPECT_EQ(blaze::ErrorType::None, response.error_type());
    EXPECT_TRUE(response.error_message().empty());
}

TEST(HttpResponseModel, TransportErrorIsNotSuccess) {
    blaze::HttpResponse response;
    response.status_code = 200;
    response.error = blaze::HttpError{blaze::ErrorType::TimeoutError, "timed out"};

    EXPECT_FALSE(response.ok());
    EXPECT_FALSE(response.is_success());
    EXPECT_EQ(blaze::ErrorType::TimeoutError, response.error_type());
    EXPECT_EQ("timed out", response.error_message());
}

TEST(HttpResponseModel, StatusClassification) {
    blaze::HttpResponse response;

    response.status_code = 204;
    EXPECT_TRUE(response.is_success());

    response.status_code = 301;
    EXPECT_TRUE(response.is_redirect());

    response.status_code = 404;
    EXPECT_TRUE(response.is_client_error());
    EXPECT_TRUE(response.is_http_error());

    response.status_code = 503;
    EXPECT_TRUE(response.is_server_error());
    EXPECT_TRUE(response.is_http_error());
}

TEST(ResponseHeaders, ServerCasingDoesNotAffectLookup) {
    blaze_test::TestServer server;
    blaze::HttpClient client;

    auto response = client.get(server.url("/lowercase-headers"));

    ASSERT_TRUE(response.ok()) << response.error_message();
    EXPECT_EQ("present", response.headers["X-Custom-Reply"]);
    EXPECT_EQ("en", response.headers["Content-Language"]);
}

}  // namespace
