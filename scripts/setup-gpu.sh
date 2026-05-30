#!/usr/bin/env bash
#
# setup-gpu.sh — enable AMD ROCm GPU acceleration for Ollama.
#
# This host has an AMD Radeon RX 6600 (Navi 23, gfx1032). By default Ollama
# runs the model on the CPU (size_vram=0), which is very slow. To use the GPU
# it needs three things, which this script sets up:
#
#   1. Your user must be in the 'render' and 'video' groups (to access
#      /dev/kfd and /dev/dri/renderD*).
#   2. Ollama's ROCm runtime libraries must be installed.
#   3. The server must run with HSA_OVERRIDE_GFX_VERSION=10.3.0 so ROCm treats
#      gfx1032 as the supported gfx1030 (handled by ollama-service.sh).
#
# Run it from a real terminal (it needs sudo):
#
#     ./scripts/setup-gpu.sh
#
# It is safe to re-run. After it finishes you MUST log out and back in (or
# reboot) for the new group membership to take effect, then start the server
# with:  ./scripts/ollama-service.sh start

set -euo pipefail

say()  { printf '\033[1m>>> %s\033[0m\n' "$*"; }
warn() { printf '\033[1;33m!!! %s\033[0m\n' "$*"; }
err()  { printf '\033[1;31m!!! %s\033[0m\n' "$*" >&2; }

NEED_RELOGIN=0

# --- 0. Sanity: is this actually an AMD GPU box? ------------------------------
say "Detecting GPU..."
if lspci 2>/dev/null | grep -Eiq 'vga|3d|display'; then
    lspci | grep -Ei 'vga|3d|display' | sed 's/^/    /'
fi
if ! lspci 2>/dev/null | grep -Eiq 'AMD|ATI|Radeon'; then
    warn "No AMD/Radeon GPU detected. This script is written for AMD ROCm."
    warn "If you have an NVIDIA card, install CUDA instead — Ollama uses it automatically."
    read -r -p "Continue anyway? [y/N] " ans
    [[ "${ans,,}" == "y" ]] || exit 1
fi

# Is Ollama managed by systemd? If so it runs as the 'ollama' service user, and
# THAT user (not you) needs GPU group access and the gfx override.
SVC_USER=""
if command -v systemctl >/dev/null 2>&1 && systemctl list-unit-files 2>/dev/null | grep '^ollama\.service' >/dev/null; then
    SVC_USER="$(systemctl show -p User --value ollama 2>/dev/null || true)"
    [[ -z "${SVC_USER}" ]] && SVC_USER="ollama"
    say "Detected systemd service 'ollama' running as user '${SVC_USER}'."
fi

# --- 1. Group membership for compute device access ---------------------------
# Add both the interactive user and (if present) the service user, so whichever
# actually runs `ollama serve` can open /dev/kfd and /dev/dri/renderD*.
GPU_USERS=("${USER}")
[[ -n "${SVC_USER}" && "${SVC_USER}" != "${USER}" ]] && GPU_USERS+=("${SVC_USER}")

say "Checking group membership (render, video) for: ${GPU_USERS[*]}"
for u in "${GPU_USERS[@]}"; do
    id "${u}" >/dev/null 2>&1 || { warn "  user '${u}' does not exist; skipping."; continue; }
    for grp in render video; do
        if getent group "${grp}" >/dev/null 2>&1; then
            if id -nG "${u}" | tr ' ' '\n' | grep -qx "${grp}"; then
                say "  ${u}: already in '${grp}'."
            else
                say "  ${u}: adding to '${grp}'..."
                sudo usermod -aG "${grp}" "${u}"
                # A re-login only matters for the interactive user; the service
                # user picks up groups on the next service (re)start.
                [[ "${u}" == "${USER}" ]] && NEED_RELOGIN=1
            fi
        else
            warn "  group '${grp}' does not exist on this system; skipping."
        fi
    done
done

# --- 2. Compute device nodes present? ----------------------------------------
say "Checking compute device nodes..."
if [[ -e /dev/kfd ]]; then
    say "  /dev/kfd present."
else
    err "  /dev/kfd missing — the amdgpu kernel driver may not expose compute."
    err "  Ensure the 'amdgpu' module is loaded (lsmod | grep amdgpu) and reboot."
fi
ls /dev/dri/renderD* >/dev/null 2>&1 && say "  /dev/dri/renderD* present." \
    || err "  no /dev/dri/renderD* node found."

# --- 3. Install / refresh Ollama's ROCm runtime ------------------------------
if ! command -v ollama >/dev/null 2>&1; then
    err "ollama is not installed. Run scripts/install-ollama.sh first."
    exit 1
fi

say "Installing/refreshing Ollama's ROCm runtime via the official installer..."
say "(This re-runs ollama.com/install.sh, which fetches ROCm libraries for AMD.)"
curl -fsSL https://ollama.com/install.sh | sh

# Heuristic check that ROCm libs landed somewhere Ollama looks.
if ls /usr/local/lib/ollama/rocm/* >/dev/null 2>&1 \
   || ls /usr/lib/ollama/rocm/* >/dev/null 2>&1 \
   || ls /usr/local/lib/ollama/libggml-hip* >/dev/null 2>&1; then
    say "ROCm runtime libraries detected under Ollama's lib dir."
else
    warn "Could not confirm ROCm libraries were installed."
    warn "If the GPU still isn't used, download the ROCm bundle manually:"
    warn "  https://github.com/ollama/ollama/releases (ollama-linux-amd64-rocm.tgz)"
fi

# --- 4. Reconcile the model store --------------------------------------------
# When Ollama runs as a service user, it looks for models under that user's
# home (e.g. /usr/share/ollama/.ollama), NOT under the models you pulled as
# yourself (~/.ollama). Rather than re-downloading several GB, point the service
# at your existing store via OLLAMA_MODELS — but the service user must be able
# to traverse your home dir to reach it.
USER_MODELS="${HOME}/.ollama/models"
MODELS_ENV=""
if [[ -n "${SVC_USER}" && -d "${USER_MODELS}" ]]; then
    say "Pointing the service at your existing models: ${USER_MODELS}"

    # Grant the service user execute-only (traverse) access to each parent dir
    # via ACLs. This lets it pass THROUGH /home/miro to reach ~/.ollama without
    # exposing the rest of your home (unlike adding it to your login group).
    if command -v setfacl >/dev/null 2>&1; then
        p="${HOME}"
        # Walk up from $HOME to / granting --x so the path is traversable.
        while [[ "${p}" != "/" && -n "${p}" ]]; do
            sudo setfacl -m "u:${SVC_USER}:--x" "${p}" 2>/dev/null || \
                warn "  could not set traverse ACL on ${p}"
            p="$(dirname "${p}")"
        done
        # The models tree itself is already world-readable (dirs r-x, blobs r--),
        # but set a read ACL on the top of it to be safe.
        sudo setfacl -R -m "u:${SVC_USER}:rX" "${HOME}/.ollama" 2>/dev/null || true
        MODELS_ENV="${USER_MODELS}"
        say "  granted ${SVC_USER} traverse access via ACLs."
    else
        warn "  setfacl not available; cannot grant access without loosening"
        warn "  home-dir permissions. Falling back to a fresh 'ollama pull'."
    fi
fi

# --- 5. Apply the gfx override (and model path) ------------------------------
# gfx1032 (RX 6600) isn't an official ROCm target; 10.3.0 (gfx1030) is the
# well-known working stand-in.
GFX="${HSA_OVERRIDE_GFX_VERSION:-10.3.0}"

if [[ -n "${SVC_USER}" ]]; then
    # systemd-managed: write a drop-in so the override survives Ollama upgrades
    # (which rewrite the main unit file) and applies every boot.
    DROPIN_DIR="/etc/systemd/system/ollama.service.d"
    DROPIN="${DROPIN_DIR}/10-aisim-gpu.conf"
    say "Writing systemd drop-in: ${DROPIN}"
    sudo install -d "${DROPIN_DIR}"
    {
        echo "# Managed by aisim/scripts/setup-gpu.sh — AMD RX 6600 (gfx1032) GPU override."
        echo "[Service]"
        echo "Environment=\"HSA_OVERRIDE_GFX_VERSION=${GFX}\""
        [[ -n "${MODELS_ENV}" ]] && echo "Environment=\"OLLAMA_MODELS=${MODELS_ENV}\""
    } | sudo tee "${DROPIN}" >/dev/null
    say "Reloading systemd and restarting the service..."
    sudo systemctl daemon-reload
    sudo systemctl restart ollama
    SYSTEMD_MANAGED=1
else
    say "No systemd service — the override is applied by ollama-service.sh at"
    say "launch (HSA_OVERRIDE_GFX_VERSION=${GFX}). Nothing to install."
    SYSTEMD_MANAGED=0
fi

# --- 5. Wrap up ---------------------------------------------------------------
echo
say "Setup complete."
if [[ "${NEED_RELOGIN}" -eq 1 ]]; then
    warn "You were added to new groups — LOG OUT AND BACK IN (or reboot) now,"
    warn "otherwise interactive 'ollama' runs still can't open /dev/kfd."
fi

if [[ "${SYSTEMD_MANAGED}" -eq 1 ]]; then
cat <<'EOF'

Next steps (systemd-managed):
  1. The service was restarted with the GPU override applied.
  2. Send a prompt, then confirm the GPU is engaged:
         ./build/aisim "hello"
         ./scripts/ollama-service.sh status     # look for size_vram > 0
  Manage the server with systemctl (start/stop/restart/status), or via
  ./scripts/ollama-service.sh which now delegates to systemctl automatically.
EOF
else
cat <<'EOF'

Next steps (manual server):
  1. (If prompted above) log out and back in, or reboot.
  2. Restart the server with the GPU override:
         ./scripts/ollama-service.sh restart
  3. Send a prompt, then confirm the GPU is engaged:
         ./build/aisim "hello"
         ./scripts/ollama-service.sh status     # look for size_vram > 0
EOF
fi
