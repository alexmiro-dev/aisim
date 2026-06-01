// domain:: — the INSIDE of the hexagon.
//
// Dependency rule: this layer depends on NOTHING. No technology (no HTTP, SQL,
// Ollama, JSON), and no other aisim layer. Everything else points inward to
// here. See docs/architecture-v2.md §2 and §5.6.
//
// Scaffold: real content lands here as separate module units —
//   entities.cppm    (User, Conversation, Turn, Skill, Mastery, PracticeTask…)
//   services.cppm    (SkillAssessor, MasteryPolicy, ContextAssembler)
//   ports/*.cppm     (driven ports: ModelGateway, ResultPublisher, repositories)
// all contributing to `export module aisim.domain;` via partitions.

export module aisim.domain;

export namespace aisim::domain {

// Placeholder so the layer compiles and links before real entities exist.
constexpr const char* layer_name() noexcept { return "domain"; }

}  // namespace aisim::domain
