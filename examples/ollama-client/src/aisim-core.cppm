// Module partition: aisim:core — the transport-agnostic vocabulary.
//
// Holds the result types, the Backend concept, and the concurrent simulation
// driver. Nothing here knows about HTTP, JSON, or Ollama; that lives in
// aisim:ollama. Keeping the concept and driver free of transport is what lets a
// test (or a future adapter) supply its own Backend.

module;

#include <concepts>
#include <expected>
#include <future>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

export module aisim:core;

export namespace aisim {

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

// Runs every prompt concurrently against the backend and returns the replies
// in the same order. Each request gets its own jthread; futures collect them.
template <Backend B>
std::vector<Outcome> run_simulation(const B& backend,
                                    std::span<const std::string_view> prompts) {
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
