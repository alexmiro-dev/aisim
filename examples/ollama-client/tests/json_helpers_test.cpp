// Unit tests for the pure text/JSON helpers in the aisim:ollama partition.
// These need no running Ollama server — they exercise the parsing in isolation,
// which is exactly why the helpers are exported from the module.

#include <catch2/catch_test_macros.hpp>

import aisim;

TEST_CASE("json_escape escapes JSON string metacharacters", "[json]") {
    using aisim::detail::json_escape;

    CHECK(json_escape("plain") == "plain");
    CHECK(json_escape("a\"b") == "a\\\"b");
    CHECK(json_escape("a\\b") == "a\\\\b");
    CHECK(json_escape("line1\nline2") == "line1\\nline2");
    CHECK(json_escape("\t\r") == "\\t\\r");
}

TEST_CASE("extract_response pulls the response field", "[json]") {
    using aisim::detail::extract_response;

    CHECK(extract_response(R"({"response":"hello"})") == "hello");
    CHECK(extract_response(R"({"model":"x","response":"hi there"})") == "hi there");
}

TEST_CASE("extract_response unescapes common sequences", "[json]") {
    using aisim::detail::extract_response;

    CHECK(extract_response(R"({"response":"a\nb"})") == "a\nb");
    CHECK(extract_response(R"({"response":"a\tb"})") == "a\tb");
}

TEST_CASE("extract_response returns the raw body when the field is absent", "[json]") {
    using aisim::detail::extract_response;

    const std::string body = R"({"unrelated":true})";
    CHECK(extract_response(body) == body);
}

TEST_CASE("extract_error finds an error field, or reports none", "[json]") {
    using aisim::detail::extract_error;

    const auto err = extract_error(R"({"error":"model not found"})");
    REQUIRE(err.has_value());
    CHECK(*err == "model not found");

    CHECK_FALSE(extract_error(R"({"response":"ok"})").has_value());
}
