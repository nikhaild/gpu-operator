#!/usr/bin/env bash
# Load selected versions.mk values into the environment for shell tests
# (single source of truth — PLAN §9). Source after assert.sh.
_vsh_repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
eval "$(make -s -C "$_vsh_repo" -f - p <<'EOF' 2>/dev/null
include versions.mk
p:
	@echo "export GPU_OPERATOR_NS='$(GPU_OPERATOR_NS)'"
	@echo "export GPU_OPERATOR_RELEASE='$(GPU_OPERATOR_RELEASE)'"
	@echo "export MOCK_NAMESPACE='$(MOCK_NAMESPACE)'"
	@echo "export MOCK_GPU_COUNT='$(MOCK_GPU_COUNT)'"
	@echo "export NV_PRODUCT_NAME='$(NV_PRODUCT_NAME)'"
	@echo "export KIND_CLUSTER_NAME='$(KIND_CLUSTER_NAME)'"
	@echo "export TEST_TIMEOUT_SECS='$(TEST_TIMEOUT_SECS)'"
EOF
)"
