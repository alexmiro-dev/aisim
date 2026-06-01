// aisim — a small C++26 client that drives a local qwen2.5-coder:7b model
// through Ollama's REST API, dispatching several prompts concurrently.
//
// Requires a running Ollama server (https://ollama.com) with the model pulled:
//     ollama pull qwen2.5-coder:7b
// or, from the build tree:
//     cmake --build build --target pull-model
//
// This translation unit is a thin consumer of the `aisim` module: it parses
// argv, hands the prompts to aisim::run_simulation, and renders the replies.

#include <curl/curl.h>

#include <cstdlib>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

import aisim;

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
