// Primary module interface for `aisim`.
//
// Re-exports the partitions so a consumer writes a single `import aisim;` and
// sees the whole public surface: the core vocabulary (Error, Reply, Outcome,
// Backend, run_simulation) and the Ollama adapter (OllamaBackend, JSON helpers).

export module aisim;

export import :core;
export import :ollama;
