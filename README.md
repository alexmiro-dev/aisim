# aisim

A small C++26 simulation harness that drives the local **`qwen2.5-coder:7b`**
model through [Ollama](https://ollama.com)'s REST API, dispatching prompts
concurrently.

It exercises modern C++ on purpose: **concepts** (`Backend`), **ranges**,
`std::expected` error handling, and **concurrency** (`std::jthread` +
`std::future`/`std::promise`).

## How aisim, Ollama, and the model fit together

If you're new to running local LLMs, it helps to keep three distinct things in
mind. They form a chain, where each layer only talks to the one next to it:

```
┌─────────┐   HTTP request     ┌──────────┐   loads &   ┌──────────────────┐
│  aisim  │ ────────────────▶ │  Ollama  │ ──────────▶│ qwen2.5-coder:7b │
│ (this   │ ◀──────────────── │ (server) │   runs      │     (model)      │
│  app)   │   HTTP response    └──────────┘             └──────────────────┘
└─────────┘
```

- **aisim** — the C++ program in this repo. It knows *what* to ask (your
  prompts) but contains **no AI itself**. It simply builds HTTP requests and
  sends them, then prints the answers it gets back. Think of it as a *client*.

- **Ollama** — a separate background server (installed once, runs as a
  service on `http://localhost:11434`). It's the *engine* that actually loads a
  model into memory, runs it on your CPU/GPU, and exposes a simple REST API.
  aisim never touches the model directly — it always goes through Ollama.

- **qwen2.5-coder:7b** — the *model*: a multi-gigabyte file of trained weights
  that Ollama downloads (`ollama pull`) and runs. This is the part that does the
  actual "thinking". You can swap it for any other model Ollama supports.

**In short:** aisim sends a prompt → Ollama feeds it to the model → the model
generates a reply → Ollama returns it → aisim prints it. Because the model lives
behind Ollama's API, **Ollama must be installed and running, and the model must
be pulled, before aisim can do anything** — see the next sections.

## Requirements

- clang-22 + libc++ (Ninja)
- CMake ≥ 3.28
- libcurl development headers
- [Ollama](https://ollama.com) running locally

## Installing Ollama (Ubuntu / Pop!_OS 24.04)

`qwen2.5-coder:7b` is a runtime dependency served by Ollama, not a C++ library.
You need Ollama installed and the model pulled before `aisim` can talk to it.

### Option A — run the helper script

From a real terminal (it needs your sudo password):

```sh
./scripts/install-ollama.sh
```

It installs Ollama, ensures the server is running, waits for the API, and pulls
`qwen2.5-coder:7b`. Override the model with
`AISIM_OLLAMA_MODEL=other-model ./scripts/install-ollama.sh`.

### Option B — run the commands manually

```sh
# 1. Install Ollama (installs to /usr/local, sets up a systemd service).
curl -fsSL https://ollama.com/install.sh | sh

# 2. Start / enable the server.
sudo systemctl enable --now ollama
#    (no systemd? run it in the background instead:  ollama serve &)

# 3. Confirm the API is up.
curl http://localhost:11434/api/tags

# 4. Pull the model aisim uses (several GB).
ollama pull qwen2.5-coder:7b

# 5. Smoke-test it.
ollama run qwen2.5-coder:7b "hello"
```

### Pulling the model via CMake

Once Ollama is installed, CMake discovers the `ollama` CLI and exposes an
opt-in target as an alternative to step 4 above:

```sh
cmake --preset clang
cmake --build build --target pull-model
```

The model name is configurable via `-DAISIM_OLLAMA_MODEL=...`.

## Build & run

Building takes two steps — **configure** once, then **build** (and re-build) as
often as you like. You run both from the repository root; there's **no need to
`cd` into `build/`** — the commands below place everything there for you.

```sh
# 1. Configure: generate the build files under ./build.
#    "clang" is a preset defined in CMakePresets.json. It picks the compiler
#    (clang-22 + libc++), the build type (RelWithDebInfo), and the generator
#    (Ninja), so you don't have to pass all those flags by hand every time.
cmake --preset clang

# 2. Build: compile the project using the files generated in step 1.
cmake --build --preset clang
```

You only need step 1 again if you change `CMakeLists.txt` or want a clean
reconfigure; day to day, just re-run step 2.

Then make sure Ollama is up (`ollama serve`) and run the binary from `./build`:

```sh
./build/aisim                              # runs a built-in batch of prompts
./build/aisim "Write a quicksort in C++."  # or pass your own prompts as args
```

Set `OLLAMA_HOST` to point at a non-default server
(default `http://localhost:11434`).

### Building with Ninja directly

The `clang` preset already uses **Ninja** as its generator, so `cmake --build
--preset clang` simply drives `ninja` for you. If you'd rather call `ninja`
yourself (e.g. for `ninja -v`, or to build a specific target), run it against
the generated build directory after the configure step:

```sh
cmake --preset clang   # configure once (as above)
ninja -C build         # equivalent to `cmake --build --preset clang`
```

## Running the Ollama server

`scripts/ollama-service.sh` gives you one set of controls regardless of how
Ollama is installed:

```sh
./scripts/ollama-service.sh start     # start the server
./scripts/ollama-service.sh stop
./scripts/ollama-service.sh restart
./scripts/ollama-service.sh status    # is the API up? CPU or GPU? (size_vram)
./scripts/ollama-service.sh logs      # (manual mode only)
```

- **If Ollama is a systemd service** (the official installer sets one up), the
  script delegates start/stop/restart to `systemctl` (these need sudo). systemd
  keeps the server running across reboots and terminal closes.
- **If it isn't**, the script launches `ollama serve` detached via `setsid`, so
  it survives closing the terminal, and tracks it with a pid file.

## GPU acceleration (AMD ROCm)

By default Ollama may run the model on the **CPU**, which is very slow (a single
7B prompt can take minutes). On an AMD Radeon GPU you can offload to the GPU via
ROCm. `scripts/setup-gpu.sh` automates the setup — adding you to the `render`/
`video` groups and installing Ollama's ROCm runtime:

```sh
./scripts/setup-gpu.sh                 # needs sudo; log out/in afterwards
./scripts/ollama-service.sh restart    # restarts with the GPU override
./scripts/ollama-service.sh status     # confirm: size_vram > 0 means GPU is in use
```

The RX 6600 (gfx1032) isn't an officially-supported ROCm target, so the server
runs with `HSA_OVERRIDE_GFX_VERSION=10.3.0` (it pretends to be the supported
gfx1030). `setup-gpu.sh` applies this for you:

- **systemd service:** it writes a drop-in at
  `/etc/systemd/system/ollama.service.d/10-aisim-gpu.conf` (survives Ollama
  upgrades and every boot) and adds the `ollama` service user to the
  `render`/`video` groups.
- **manual server:** `ollama-service.sh` sets the override at launch. Change it
  with `HSA_OVERRIDE_GFX_VERSION=...` or disable with `AISIM_NO_GPU=1`.

## Debug / sanitizers

```sh
cmake --preset clang-debug      # Debug + ASan/UBSan
cmake --build --preset clang-debug
```
