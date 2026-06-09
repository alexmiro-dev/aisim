// aisim — entry point.
//
// Per docs/architecture-v2.md §5.5, main() is tiny: read config, construct the
// single CompositionRoot (the application object), run it, and shut it down on
// signal. All concrete-type decisions live inside the composition root.
//
// A worked, self-contained Ollama client (C++ modules + Catch2) lives in
// examples/ollama-client/ — a separate project, not built from here.

import aisim.infrastructure;

int main() {
    aisim::infrastructure::CompositionRoot app;
    return app.run();
}
