# Ollama client — a C++ modules example

A small, self-contained C++26 program that drives a local
[Ollama](https://ollama.com) server (the `qwen2.5-coder:7b` model by default),
dispatching several prompts **concurrently** and printing each reply.

It exists to demonstrate two things in isolation from the main `aisim`
application:

1. Structuring a C++ project with **named modules** (one module, several
   partitions) instead of header/source pairs.
2. Wiring a **Catch2 v3** test suite that exercises that module via CTest.

> This is a **standalone project**. It is intentionally *not* added to the root
> `CMakeLists.txt` — configure and build it from this directory.

---

## Layout

```
ollama-client/
├── CMakeLists.txt          # standalone build: module library + exe + tests
├── CMakePresets.json       # clang-22 + libc++ + Ninja presets
├── src/
│   ├── aisim.cppm          # primary module interface  (export module aisim;)
│   ├── aisim-core.cppm     # partition  aisim:core      (transport-agnostic)
│   ├── aisim-ollama.cppm   # partition  aisim:ollama    (HTTP/JSON adapter)
│   └── main.cpp            # thin consumer: `import aisim;`
└── tests/
    ├── json_helpers_test.cpp   # pure JSON helpers — no server needed
    └── simulation_test.cpp     # run_simulation against a fake Backend
```

## The module

Everything is one module named **`aisim`**, split into partitions so each piece
has a single responsibility and a clear dependency direction.

| Unit | Module declaration | Role | Depends on |
|---|---|---|---|
| `aisim-core.cppm` | `export module aisim:core;` | The transport-agnostic vocabulary: `Error`, `Reply`, `Outcome`, the `Backend` concept, and the concurrent `run_simulation` driver. Knows nothing about HTTP or Ollama. | — |
| `aisim-ollama.cppm` | `export module aisim:ollama;` | The Ollama adapter: `OllamaBackend` (speaks Ollama's REST protocol over libcurl, satisfies `Backend`) plus the pure text helpers `json_escape` / `extract_response` / `extract_error`, exported so they are unit-testable without a live server. | `aisim:core` |
| `aisim.cppm` | `export module aisim;` | The **primary interface**. Re-exports the partitions (`export import :core; export import :ollama;`) so a consumer writes a single `import aisim;` and sees the whole public surface. | both partitions |
| `main.cpp` | — (consumer) | Parses argv, calls `aisim::run_simulation`, renders the replies. | `import aisim;` |

Why partitions: the `Backend` concept lives in `:core` with no knowledge of
transport, so the simulation driver can be tested against a fake backend (see
`tests/simulation_test.cpp`) — and a real adapter like `OllamaBackend` is just
one implementation that happens to use libcurl.

### Gotcha: partitions don't share includes

Each module interface unit has its own *global module fragment* (the `module;`
block before the `export module` line). Includes are **not** inherited across
partitions — every `.cppm` must `#include` what it uses itself. For example,
`aisim-ollama.cppm` includes `<expected>` even though `aisim-core.cppm` already
does, because both use `std::unexpected`.

## Requirements

- **CMake ≥ 3.28** — the version where `FILE_SET TYPE CXX_MODULES` and module
  dependency scanning became stable.
- **Ninja** — module scanning is supported on the Ninja and MSVC generators.
- **A modules-capable compiler.** Tested with **clang-22 + libc++** (see the
  presets). The build sets `CMAKE_CXX_SCAN_FOR_MODULES ON`.
- **libcurl** development headers (`find_package(CURL REQUIRED)`).
- An internet connection on first configure: **Catch2 v3.7.1** is fetched via
  `FetchContent`.

## Build & run

From this directory:

```bash
# Configure (downloads Catch2 on first run)
cmake --preset clang            # or: clang-debug  (adds ASan/UBSan)

# Build the module library, the executable, and the tests
cmake --build build

# Run the example (needs a running Ollama server with the model pulled)
./build/aisim                                   # built-in demo prompts
./build/aisim "Explain RAII in one sentence."   # your own prompt(s)
```

The model host defaults to `http://localhost:11434` and can be overridden with
the `OLLAMA_HOST` environment variable. Pull the model with either:

```bash
ollama pull qwen2.5-coder:7b
# or, from the build tree:
cmake --build build --target pull-model
```

## Test

Tests run under CTest and need **no** Ollama server — they exercise the pure
JSON helpers and the `run_simulation` driver with an in-memory fake backend:

```bash
ctest --test-dir build --output-on-failure
```

`catch_discover_tests` registers each `TEST_CASE` as an individual CTest test,
so you can also filter:

```bash
ctest --test-dir build -R json        # only the JSON-helper cases
```
