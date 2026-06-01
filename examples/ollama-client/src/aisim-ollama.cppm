// Module partition: aisim:ollama — the HTTP/JSON adapter for an Ollama server.
//
// Encapsulates all engine/transport specifics: it speaks Ollama's REST protocol
// over libcurl and satisfies the aisim::Backend concept. The pure text helpers
// (json_escape / extract_response / extract_error) are exported so they can be
// unit-tested without a live server.

module;

#include <curl/curl.h>

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

export module aisim:ollama;

import :core;

export namespace aisim::detail {

// Escapes a string so it can be embedded inside a JSON string literal.
std::string json_escape(std::string_view in) {
    std::string out;
    out.reserve(in.size() + 16);
    for (char c : in) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;
        }
    }
    return out;
}

// Pulls the value of "response" out of Ollama's JSON body without a full JSON
// dependency. Good enough for a non-streaming reply; returns the raw body if
// the field is absent.
std::string extract_response(std::string_view body) {
    constexpr std::string_view key = "\"response\":\"";
    const auto start = body.find(key);
    if (start == std::string_view::npos) {
        return std::string{body};
    }
    std::string out;
    for (std::size_t i = start + key.size(); i < body.size(); ++i) {
        const char c = body[i];
        if (c == '\\' && i + 1 < body.size()) {
            switch (const char n = body[++i]) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                default:  out += n;    break;
            }
        } else if (c == '"') {
            break;
        } else {
            out += c;
        }
    }
    return out;
}

// Extracts the value of a top-level "error" string field from Ollama's JSON, if
// present. Ollama reports failures (unknown model, bad request, …) as
// {"error":"..."} with a non-2xx status, so detecting this stops us from
// printing an error body as if it were a successful reply. Returns std::nullopt
// when there is no "error" field.
std::optional<std::string> extract_error(std::string_view body) {
    constexpr std::string_view key = "\"error\":\"";
    const auto start = body.find(key);
    if (start == std::string_view::npos) {
        return std::nullopt;
    }
    std::string out;
    for (std::size_t i = start + key.size(); i < body.size(); ++i) {
        const char c = body[i];
        if (c == '\\' && i + 1 < body.size()) {
            out += body[++i];  // keep the escaped char verbatim; good enough for a message
        } else if (c == '"') {
            break;
        } else {
            out += c;
        }
    }
    return out;
}

}  // namespace aisim::detail

namespace aisim::detail {

// Not exported: libcurl write callback used only by OllamaBackend.
std::size_t write_cb(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

}  // namespace aisim::detail

export namespace aisim {

// Talks to an Ollama server over HTTP. Satisfies the Backend concept.
class OllamaBackend {
public:
    OllamaBackend(std::string base_url, std::string model)
        : base_url_(std::move(base_url)), model_(std::move(model)) {}

    Reply generate(std::string_view prompt) const {
        CURL* curl = curl_easy_init();
        if (!curl) {
            return std::unexpected(Error{"failed to init curl handle"});
        }

        const std::string url = base_url_ + "/api/generate";
        const std::string payload = std::string{R"({"model":")"} + model_ +
                                    R"(","prompt":")" + detail::json_escape(prompt) +
                                    R"(","stream":false})";

        std::string body;
        curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, detail::write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 120L);

        const CURLcode rc = curl_easy_perform(curl);
        long http_status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (rc != CURLE_OK) {
            return std::unexpected(Error{curl_easy_strerror(rc)});
        }

        // Ollama signals failures (unknown model, bad request, server error)
        // with a non-2xx status and an {"error":"..."} body. Surface that as a
        // real error instead of printing the error JSON as a "reply".
        if (auto err = detail::extract_error(body)) {
            return std::unexpected(Error{std::move(*err)});
        }
        if (http_status < 200 || http_status >= 300) {
            return std::unexpected(
                Error{"HTTP " + std::to_string(http_status) + ": " + body});
        }
        return detail::extract_response(body);
    }

private:
    std::string base_url_;
    std::string model_;
};
static_assert(Backend<OllamaBackend>);

}  // namespace aisim
