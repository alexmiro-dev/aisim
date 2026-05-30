#!/usr/bin/env bash
#
# ollama-service.sh — start/stop the Ollama server independently of any terminal.
#
# If Ollama is installed as a systemd service, this script delegates
# start/stop/restart to systemctl (systemd owns the lifecycle, and the GPU
# override lives in a drop-in written by setup-gpu.sh).
#
# Otherwise — a bare `ollama serve` dies the moment its terminal closes — this
# script launches the server in its own session (setsid) so it keeps running
# after you log out or close the shell. Either way you get one set of
# start/stop/restart/status/logs controls.
#
# Usage:
#     ./scripts/ollama-service.sh start     # start detached (no-op if already up)
#     ./scripts/ollama-service.sh stop      # stop the server we started
#     ./scripts/ollama-service.sh restart
#     ./scripts/ollama-service.sh status    # is the API answering?
#     ./scripts/ollama-service.sh logs      # tail the server log
#
# Environment:
#     OLLAMA_HOST                API endpoint to probe (default http://localhost:11434)
#     HSA_OVERRIDE_GFX_VERSION   AMD GPU gfx override. Defaults to 10.3.0, which
#                                makes ROCm treat an RX 6600 (gfx1032) as the
#                                officially-supported gfx1030 so it can run on
#                                the GPU. Set to empty to disable the override.
#     AISIM_NO_GPU=1             Skip the GPU override entirely (force whatever
#                                Ollama auto-detects, typically CPU here).

set -euo pipefail

HOST="${OLLAMA_HOST:-http://localhost:11434}"
RUN_DIR="${XDG_RUNTIME_DIR:-/tmp}"
PID_FILE="${RUN_DIR}/aisim-ollama.pid"
LOG_FILE="${TMPDIR:-/tmp}/aisim-ollama.log"

# AMD ROCm gfx override. The RX 6600 is gfx1032, which ROCm doesn't list as
# supported; 10.3.0 (gfx1030) is the well-known working stand-in. Override or
# clear via the environment.
GFX_OVERRIDE="${HSA_OVERRIDE_GFX_VERSION:-10.3.0}"

say()  { printf '\033[1m>>> %s\033[0m\n' "$*"; }
err()  { printf '\033[1;31m!!! %s\033[0m\n' "$*" >&2; }

# True if Ollama is installed as a systemd system service. When it is, systemd
# owns the lifecycle and we must NOT also launch our own copy (they'd fight over
# port 11434). In that case start/stop/restart delegate to systemctl, and the
# GPU override lives in a drop-in (see setup-gpu.sh), not in our launch env.
has_systemd_unit() {
    command -v systemctl >/dev/null 2>&1 || return 1
    # NB: no `grep -q` here — it exits on first match, which SIGPIPEs systemctl,
    # and `set -o pipefail` would then report this function as false. Letting
    # grep drain all input avoids that.
    systemctl list-unit-files 2>/dev/null | grep '^ollama\.service' >/dev/null
}

# True if the REST API answers — the only reliable "is it up?" signal.
api_up() {
    curl -fsS --max-time 2 "${HOST}/api/tags" >/dev/null 2>&1
}

# PID of a server we started (via the pid file), if it's still alive.
running_pid() {
    [[ -f "${PID_FILE}" ]] || return 1
    local pid
    pid="$(cat "${PID_FILE}" 2>/dev/null || true)"
    [[ -n "${pid}" ]] && kill -0 "${pid}" 2>/dev/null || return 1
    printf '%s' "${pid}"
}

cmd_start() {
    command -v ollama >/dev/null 2>&1 || { err "ollama not found — run scripts/install-ollama.sh first."; exit 1; }

    if has_systemd_unit; then
        say "Ollama is a systemd service — starting via systemctl."
        sudo systemctl start ollama
        say "Started. (GPU override, if any, comes from the systemd drop-in — see setup-gpu.sh.)"
        return 0
    fi

    if api_up; then
        say "Ollama already running and answering at ${HOST}."
        return 0
    fi

    # Decide whether to apply the AMD GPU override.
    local -a gfx_env=()
    if [[ -z "${AISIM_NO_GPU:-}" && -n "${GFX_OVERRIDE}" ]]; then
        gfx_env=(env "HSA_OVERRIDE_GFX_VERSION=${GFX_OVERRIDE}")
        say "Using AMD GPU override HSA_OVERRIDE_GFX_VERSION=${GFX_OVERRIDE}."
    else
        say "GPU override disabled — Ollama will auto-detect (likely CPU)."
    fi

    say "Starting 'ollama serve' detached (log: ${LOG_FILE})..."
    # setsid + the trailing & put the server in its own session with no
    # controlling terminal, so closing this shell won't send it SIGHUP.
    setsid "${gfx_env[@]}" ollama serve >"${LOG_FILE}" 2>&1 &
    echo $! >"${PID_FILE}"

    say "Waiting for the API at ${HOST} ..."
    for _ in $(seq 1 30); do
        if api_up; then
            say "Ollama is up (pid $(cat "${PID_FILE}")). It will survive this terminal closing."
            return 0
        fi
        sleep 1
    done

    err "API did not come up within 30s. Check the log: ${LOG_FILE}"
    exit 1
}

cmd_stop() {
    if has_systemd_unit; then
        say "Ollama is a systemd service — stopping via systemctl."
        sudo systemctl stop ollama
        say "Stopped. (It is still enabled and will start again on boot; "
        say " 'sudo systemctl disable ollama' to prevent that.)"
        return 0
    fi

    local pid
    if pid="$(running_pid)"; then
        say "Stopping Ollama (pid ${pid})..."
        kill "${pid}" 2>/dev/null || true
        for _ in $(seq 1 10); do
            kill -0 "${pid}" 2>/dev/null || break
            sleep 1
        done
        kill -0 "${pid}" 2>/dev/null && { err "Did not exit; sending SIGKILL."; kill -9 "${pid}" 2>/dev/null || true; }
        rm -f "${PID_FILE}"
        say "Stopped."
    elif api_up; then
        err "Ollama is running but was not started by this script (no valid pid file)."
        err "It may be a systemd service — stop it with: sudo systemctl stop ollama"
        exit 1
    else
        say "Ollama is not running."
    fi
}

cmd_status() {
    if api_up; then
        say "Ollama is UP at ${HOST}."
        if has_systemd_unit; then
            printf '    managed by systemd (%s)\n' "$(systemctl is-active ollama 2>/dev/null || echo unknown)"
        elif pid="$(running_pid)"; then
            printf '    managed by this script (pid %s)\n' "${pid}"
        else
            printf '    started elsewhere (another shell)\n'
        fi

        # Report whether a loaded model is on the GPU. size_vram > 0 means at
        # least part of the model is offloaded to VRAM (GPU is in use).
        local ps_json vram
        ps_json="$(curl -fsS --max-time 2 "${HOST}/api/ps" 2>/dev/null || true)"
        if [[ "${ps_json}" == *'"size_vram"'* ]]; then
            vram="$(printf '%s' "${ps_json}" | grep -o '"size_vram":[0-9]*' | head -1 | grep -o '[0-9]*')"
            if [[ -n "${vram}" && "${vram}" -gt 0 ]]; then
                printf '    GPU in use: %s bytes in VRAM (size_vram=%s)\n' "${vram}" "${vram}"
            else
                printf '    Running on CPU (size_vram=0) — GPU not engaged.\n'
            fi
        else
            printf '    No model loaded yet — run a prompt, then check status again.\n'
        fi
    else
        err "Ollama is DOWN — no response at ${HOST}."
        exit 1
    fi
}

cmd_logs() {
    [[ -f "${LOG_FILE}" ]] || { err "No log file at ${LOG_FILE} (server not started by this script?)."; exit 1; }
    tail -n 50 -f "${LOG_FILE}"
}

case "${1:-}" in
    start)        cmd_start ;;
    stop)         cmd_stop ;;
    restart)
        if has_systemd_unit; then
            say "Ollama is a systemd service — restarting via systemctl."
            sudo systemctl restart ollama
        else
            cmd_stop; cmd_start
        fi
        ;;
    status)       cmd_status ;;
    logs)         cmd_logs ;;
    *)
        printf 'usage: %s {start|stop|restart|status|logs}\n' "$(basename "$0")" >&2
        exit 2
        ;;
esac
