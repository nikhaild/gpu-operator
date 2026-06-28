#!/usr/bin/env bash
# Functional test: the no-op validator shim exits 0 for a component validation
# (TESTING.md §2.4). SAFE: no kernel, no cluster.
set -uo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
. "$HERE/../lib/assert.sh"
REPO="$(cd "$HERE/../.." && pwd)"

require_cmd docker
IMG="${VALIDATOR_SHIM:-localhost/cuda-validator-noop:dev}"
docker image inspect "$IMG" >/dev/null 2>&1 || { info "building shim"; make -C "$REPO" validator-shim >/dev/null; }

assert_cmd_ok "shim exits 0 for --component=cuda" -- docker run --rm "$IMG" --component=cuda
assert_cmd_ok "shim exits 0 for --component=driver" -- docker run --rm "$IMG" --component=driver
assert_summary
