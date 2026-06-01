// adapter:: — the OUTSIDE of the hexagon. Technology lives here.
//
// Dependency rule: depends on `application::` (driving ports) and `domain::`
// (driven ports). This is the only place (besides app::) allowed to name real
// technology — Ollama, SQLite, WebSockets, REST, CLI.
// See docs/architecture-v2.md §5.1, §5.2, §5.6.
//
// Scaffold: real adapters land as partitions of `aisim.adapter`, grouped by
// technology in subdirectories —
//   ollama/   OllamaGateway        implements domain ModelGateway
//   sqlite/   *Repository          implement the repository driven ports
//   ws/       WebSocketHub, LiveResultPublisher (driving + ResultPublisher)
//   rest/     RestEndpoint         driving adapter over HTTP
//   cli/      CliDriver            driving adapter over the terminal
//
// A worked driven-adapter reference (Ollama over libcurl, with the Backend
// concept seam) already exists in examples/ollama-client/.

export module aisim.adapter;

import aisim.application;
import aisim.domain;

export namespace aisim::adapter {

// Placeholder so the layer compiles and links before real adapters exist.
constexpr const char* layer_name() noexcept { return "adapter"; }

}  // namespace aisim::adapter
