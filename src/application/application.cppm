// application:: — use cases (orchestration) and the driving ports.
//
// Dependency rule: depends on `domain::` ONLY. Knows no technology. It offers
// the driving ports (the API the core exposes) and implements them with small,
// stateless use cases that coordinate domain services and driven ports.
// See docs/architecture-v2.md §3.1, §5.3, §5.6.
//
// Scaffold: real content lands here as partitions of `aisim.application` —
//   ports/*.cppm       (driving ports: ConversationPort, PracticePort, ProfilePort)
//   use_cases/*.cppm   (SubmitPrompt, IngestProfile, RequestPractice, SolveTask)

export module aisim.application;

import aisim.domain;

export namespace aisim::application {

// Placeholder so the layer compiles and links before real use cases exist.
constexpr const char* layer_name() noexcept { return "application"; }

}  // namespace aisim::application
