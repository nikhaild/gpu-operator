#!/usr/bin/env bash
# Shared bash assertions + helpers for functional/E2E tests (docs/TESTING.md §0).
# Source this file: . "$(dirname "$0")/../lib/assert.sh"
set -uo pipefail

_ASSERT_FAILS=0
_ASSERT_OK=0

_red()   { printf '\033[31m%s\033[0m\n' "$*"; }
_green() { printf '\033[32m%s\033[0m\n' "$*"; }
_yellow(){ printf '\033[33m%s\033[0m\n' "$*"; }

pass() { _ASSERT_OK=$((_ASSERT_OK+1));      _green  "PASS: $*"; }
fail() { _ASSERT_FAILS=$((_ASSERT_FAILS+1)); _red    "FAIL: $*"; }
info() { _yellow "INFO: $*"; }

# assert_eq <expected> <actual> <message>
assert_eq() {
  if [ "$1" = "$2" ]; then pass "$3 (= '$1')"; else fail "$3 (expected '$1', got '$2')"; fi
}

# assert_contains <haystack> <needle> <message>
assert_contains() {
  case "$1" in
    *"$2"*) pass "$3" ;;
    *)      fail "$3 (string '$2' not found)" ;;
  esac
}

# assert_file <path> <message>
assert_file() { if [ -e "$1" ]; then pass "$2 ($1 exists)"; else fail "$2 ($1 missing)"; fi; }
assert_no_file() { if [ ! -e "$1" ]; then pass "$2 ($1 absent)"; else fail "$2 ($1 still present)"; fi; }
assert_chardev() { if [ -c "$1" ]; then pass "$2 ($1 is a char device)"; else fail "$2 ($1 not a char device)"; fi; }

# assert_cmd_ok <message> -- <command...>
assert_cmd_ok() {
  local msg="$1"; shift; [ "$1" = "--" ] && shift
  if "$@" >/dev/null 2>&1; then pass "$msg"; else fail "$msg (command failed: $*)"; fi
}

# wait_for <timeout_secs> <description> -- <command...>  (polls until command succeeds)
wait_for() {
  local timeout="$1" desc="$2"; shift 2; [ "$1" = "--" ] && shift
  local start now
  start=$(date +%s)
  while true; do
    if "$@" >/dev/null 2>&1; then pass "wait_for: $desc"; return 0; fi
    now=$(date +%s)
    if [ $((now - start)) -ge "$timeout" ]; then fail "wait_for: $desc (timed out after ${timeout}s)"; return 1; fi
    sleep 3
  done
}

# --- SKIP guards (functional/E2E only) -------------------------------------
# These echo SKIP and exit 0 so off-host CI stays green; the host run must show PASS.
require_root() {
  if [ "$(id -u)" -ne 0 ]; then info "SKIP: requires root (got uid $(id -u))"; exit 0; fi
}
require_ubuntu() {
  local want="$1" have
  have=$(. /etc/os-release 2>/dev/null; echo "${VERSION_ID:-}")
  if [ "$have" != "$want" ]; then info "SKIP: requires Ubuntu $want (got '$have')"; exit 0; fi
}
require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then info "SKIP: requires '$1' on PATH"; exit 0; fi
}
# require_vermagic_match <path-to.ko>
require_vermagic_match() {
  local ko="$1" running vm
  running=$(uname -r)
  vm=$(modinfo -F vermagic "$ko" 2>/dev/null | awk '{print $1}')
  if [ -n "$vm" ] && [ "$vm" != "$running" ]; then
    info "SKIP: .ko vermagic '$vm' != running kernel '$running'"; exit 0
  fi
}

# dump_diagnostics <namespace> <outdir>  (best-effort cluster/host dump on failure)
dump_diagnostics() {
  local ns="$1" out="$2"
  mkdir -p "$out" 2>/dev/null || true
  info "dumping diagnostics to $out"
  { kubectl get pods,ds -A -o wide;
    echo "--- describe non-ready ---";
    kubectl get pods -n "$ns" --no-headers 2>/dev/null | grep -vE 'Running|Completed' | awk '{print $1}' \
      | while read -r p; do kubectl describe pod -n "$ns" "$p"; done;
  } > "$out/pods.txt" 2>&1 || true
  kubectl get nodes -o yaml > "$out/nodes.yaml" 2>&1 || true
  for app in nvidia-driver-daemonset gpu-feature-discovery nvidia-device-plugin-daemonset \
             nvidia-container-toolkit-daemonset nvidia-operator-validator; do
    kubectl logs -n "$ns" -l app="$app" --all-containers --tail=200 \
      > "$out/log-$app.txt" 2>&1 || true
  done
  { echo "### dmesg tail"; dmesg 2>/dev/null | tail -40;
    echo "### lsmod mock"; lsmod 2>/dev/null | grep mock || true;
    echo "### /dev/nvidia*"; ls -l /dev/nvidia* 2>/dev/null || true;
  } > "$out/host.txt" 2>&1 || true
}

# assert_summary  (call at end; sets exit code)
assert_summary() {
  echo "-----------------------------------------------"
  if [ "$_ASSERT_FAILS" -eq 0 ]; then
    _green "ALL PASSED ($_ASSERT_OK assertions)"; exit 0
  else
    _red "FAILURES: $_ASSERT_FAILS (passed $_ASSERT_OK)"; exit 1
  fi
}
