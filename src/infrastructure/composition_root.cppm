// infrastructure:: composition root — the application object `main` builds.
//
// This is THE only place that names concrete types: it chooses which adapter
// satisfies each port, injects them into the use cases, mounts the driving
// adapters, and owns startup/shutdown. It holds no business logic — once wired,
// requests flow through the use cases without it on the call path.
// See docs/architecture-v2.md §5.4, §5.5, §5.6.
//
// The name "composition root" is the dependency-injection term for the single
// place an application's object graph is composed; `AisimApplication` is a fair
// intuition for what it is.
//
// Scaffold: `run()` will construct storage → engine → bus → use cases → servers
// (inward-out), serve until stopped, then tear down in reverse.

module;

#include <print>

export module aisim.infrastructure;

import aisim.adapter;
import aisim.application;
import aisim.domain;

export namespace aisim::infrastructure {

// The composition root / application object. Construct, run(), shutdown().
class CompositionRoot {
public:
    // A real ctor will take infrastructure::Config and build the object graph.
    CompositionRoot() = default;

    // Build the graph, mount the front-ends, and serve until shutdown.
    int run() {
        std::println("aisim — composition root wired (scaffold)");
        return 0;
    }

    // Stop accepting, drain live streams, flush writes, close the store.
    void shutdown() noexcept {}
};

}  // namespace aisim::infrastructure
