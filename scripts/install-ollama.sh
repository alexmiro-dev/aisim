#!/usr/bin/env bash
#
# install-ollama.sh — install Ollama and pull the model aisim depends on.
#
# Tested on Ubuntu / Pop!_OS 24.04 (x86_64). Run from a real terminal:
#
#     ./scripts/install-ollama.sh
#
# It will ask for your sudo password (Ollama installs to /usr/local and sets up
# a systemd service). Override the model with AISIM_OLLAMA_MODEL=... if needed.

set -euo pipefail

MODEL="${AISIM_OLLAMA_MODEL:-qwen2.5-coder:7b}"

say() { printf '\033[1m>>> %s\033[0m\n' "$*"; }

# 1. Install Ollama (skips download if the binary already exists).
if command -v ollama >/dev/null 2>&1; then
    say "ollama already installed: $(ollama --version 2>/dev/null || echo present)"
else
    say "Installing Ollama via the official script..."
    curl -fsSL https://ollama.com/install.sh | sh
fi

# 2. Make sure the server is running.
#    On systemd hosts the installer enables a service; otherwise start it
#    ourselves in the background.
if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files 2>/dev/null | grep -q '^ollama\.service'; then
    say "Ensuring the ollama systemd service is running..."
    sudo systemctl enable --now ollama
else
    if ! curl -fsS http://localhost:11434/api/tags >/dev/null 2>&1; then
        say "Starting 'ollama serve' in the background (log: /tmp/ollama.log)..."
        nohup ollama serve >/tmp/ollama.log 2>&1 &
    fi
fi

# 3. Wait for the API to answer before pulling.
say "Waiting for the Ollama API at http://localhost:11434 ..."
for _ in $(seq 1 30); do
    if curl -fsS http://localhost:11434/api/tags >/dev/null 2>&1; then
        break
    fi
    sleep 1
done

# 4. Pull the model aisim uses.
say "Pulling model: ${MODEL} (this can be several GB)..."
ollama pull "${MODEL}"

say "Done. Try it:  ollama run ${MODEL} 'hello'"
