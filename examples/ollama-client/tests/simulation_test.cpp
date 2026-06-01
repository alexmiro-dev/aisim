// Tests for aisim::run_simulation using a fake in-memory Backend. This proves
// the core driver is decoupled from transport: no HTTP, no Ollama, just a type
// that satisfies the aisim::Backend concept.

#include <catch2/catch_test_macros.hpp>

#include <expected>
#include <string>
#include <string_view>
#include <vector>

import aisim;

namespace {

// Echoes the prompt back as the reply; fails for the sentinel "boom".
struct EchoBackend {
    aisim::Reply generate(std::string_view prompt) const {
        if (prompt == "boom") {
            return std::unexpected(aisim::Error{"deliberate failure"});
        }
        return std::string{"echo: "} + std::string{prompt};
    }
};
static_assert(aisim::Backend<EchoBackend>);

}  // namespace

TEST_CASE("run_simulation preserves order and maps each prompt", "[sim]") {
    const EchoBackend backend;
    const std::vector<std::string_view> prompts{"one", "two", "three"};

    const std::vector<aisim::Outcome> out = aisim::run_simulation(backend, prompts);

    REQUIRE(out.size() == 3);
    REQUIRE(out[0].reply.has_value());
    CHECK(*out[0].reply == "echo: one");
    CHECK(*out[1].reply == "echo: two");
    CHECK(*out[2].reply == "echo: three");
}

TEST_CASE("run_simulation surfaces backend errors per prompt", "[sim]") {
    const EchoBackend backend;
    const std::vector<std::string_view> prompts{"ok", "boom"};

    const std::vector<aisim::Outcome> out = aisim::run_simulation(backend, prompts);

    REQUIRE(out.size() == 2);
    CHECK(out[0].reply.has_value());
    REQUIRE_FALSE(out[1].reply.has_value());
    CHECK(out[1].reply.error().what == "deliberate failure");
}
