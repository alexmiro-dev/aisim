// aisim — a small C++26 client that drives a local qwen2.5-coder:7b model
// through Ollama's REST API, dispatching several prompts concurrently.
//
// Requires a running Ollama server (https://ollama.com) with the model pulled:
//     ollama pull qwen2.5-coder:7b
// or, from the build tree:
//     cmake --build build --target pull-model
//
// Showcases: concepts, ranges, std::expected, std::jthread + std::future.

#include <curl/curl.h>

#include <algorithm>
#include <concepts>
#include <cstdlib>
#include <expected>
#include <future>
#include <optional>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace aisim {

// An error carries a human-readable reason; success carries the model's text.
struct Error {
    std::string what;
};
using Reply = std::expected<std::string, Error>;

// Wraps a Reply so that std::expected is not an associated class of the
// element/iterator type. Storing std::expected directly in a std::vector trips
// a libc++ bug where the vector's reverse-iterator comparison pulls
// std::expected's hidden-friend operator== into a self-referential constraint.
struct Outcome {
    Reply reply;
};

// A Backend is anything that can turn a prompt into a Reply. Constraining the
// call site with a concept keeps the simulation loop decoupled from transport.
template <typename T>
concept Backend = requires(T backend, std::string_view prompt) {
    { backend.generate(prompt) } -> std::same_as<Reply>;
};

namespace detail {

std::size_t write_cb(char* ptr, std::size_t size, std::size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

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

}  // namespace detail

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

// Runs every prompt concurrently against the backend and returns the replies
// in the same order. Each request gets its own jthread; futures collect them.
template <Backend B>
std::vector<Outcome> run_simulation(const B& backend, std::span<const std::string_view> prompts) {
    // Pre-size once: growing a std::vector<std::future<std::expected<…>>> trips
    // a libc++22 relocation bug, so we default-construct N futures up front and
    // assign by index instead of push_back.
    std::vector<std::future<Reply>> pending(prompts.size());

    for (std::size_t i = 0; i < prompts.size(); ++i) {
        const std::string_view prompt = prompts[i];
        std::promise<Reply> promise;
        pending[i] = promise.get_future();
        std::jthread{[&backend, prompt, p = std::move(promise)]() mutable {
            p.set_value(backend.generate(prompt));
        }}.detach();
    }

    std::vector<Outcome> results;
    results.reserve(pending.size());
    for (std::size_t i = 0; i < pending.size(); ++i) {
        results.push_back(Outcome{pending[i].get()});
    }
    return results;
}

}  // namespace aisim

int main(int argc, char** argv) {
    using namespace aisim;

    const char* host = std::getenv("OLLAMA_HOST");
    const std::string base_url = host ? host : "http://localhost:11434";

    // Prompts: CLI args if given, otherwise a small built-in simulation batch.
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        args = {
            "In one line, what is a deterministic simulation?",
            "Write a C++23 one-liner that prints the numbers 1..5.",
            "Name one advantage of running an LLM locally.",
        };
    }
    std::vector<std::string_view> prompts(args.begin(), args.end());

    std::println("aisim — model: {} @ {}", AISIM_MODEL, base_url);
    std::println("dispatching {} prompt(s) concurrently...\n", prompts.size());

    const OllamaBackend backend(base_url, AISIM_MODEL);

    curl_global_init(CURL_GLOBAL_DEFAULT);
    const std::vector<Outcome> replies = run_simulation(backend, prompts);
    curl_global_cleanup();

    int failures = 0;
    for (std::size_t i = 0; i < replies.size(); ++i) {
        const Reply& reply = replies[i].reply;
        std::println("── prompt #{}: {}", i, prompts[i]);
        if (reply) {
            std::println("{}\n", *reply);
        } else {
            ++failures;
            std::println(stderr, "error: {}\n", reply.error().what);
        }
    }

    if (failures > 0) {
        std::println(stderr,
                     "{} request(s) failed. Is Ollama running at {}?", failures, base_url);
        return 1;
    }
    return 0;
}
