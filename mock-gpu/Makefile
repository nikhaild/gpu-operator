# Mock NVIDIA GPU — top-level build/test/deploy orchestration (PLAN §10).
# All versions/IDs/images come from versions.mk (single source of truth).
#
# Quick start (build + safe tests; no kernel load, no cluster):
#   make artifacts           # build .ko + mock NVML + stubs into build/
#   make test-unit           # C + bats unit tests
#   make image validator-shim
#
# Full E2E (requires kind on PATH + root + a snapshotted host):
#   make up                  # kind-up -> build -> load -> deploy -> verify
#
SHELL := /bin/bash
include versions.mk

# Exported for envsubst in deploy templates.
export MOCK_NAMESPACE MOCK_IMAGE MOCK_GPU_COUNT MOCK_ENABLE_PCI MOCK_PCI_DOMAIN MOCK_PCI_BUSNR
export DRIVER_REPOSITORY DRIVER_IMAGE_NAME DRIVER_VERSION
export VALIDATOR_SHIM_REPO VALIDATOR_SHIM_IMAGE_NAME VALIDATOR_SHIM_TAG

BUILD_DIR := build
DOCKER_USER := --user $(shell id -u):$(shell id -g)

.DEFAULT_GOAL := help

.PHONY: help
help: ## show this help
	@awk 'BEGIN{FS=":.*##"} /^[a-zA-Z0-9_-]+:.*##/{printf "  \033[36m%-18s\033[0m %s\n",$$1,$$2}' $(MAKEFILE_LIST)

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
.PHONY: bootstrap
bootstrap: ## build the Ubuntu 24.04 builder image (toolchain + host-kernel headers)
	docker build -f image/Dockerfile.builder \
	  --build-arg BUILD_BASE_IMAGE=$(BUILD_BASE_IMAGE) \
	  --build-arg KERNEL_VERSION=$(KERNEL_VERSION) \
	  -t $(BUILDER_IMAGE) .

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

.PHONY: kmod
kmod: bootstrap | $(BUILD_DIR) ## compile mock_gpu.ko inside the builder -> build/
	docker run --rm $(DOCKER_USER) -v "$(MOCK_GPU_DIR)":/src $(BUILDER_IMAGE) \
	  make -C /src/kmod modules KERNEL_VERSION=$(KERNEL_VERSION)
	cp kmod/mock_gpu.ko $(BUILD_DIR)/mock_gpu.ko
	docker run --rm $(DOCKER_USER) -v "$(MOCK_GPU_DIR)":/src $(BUILDER_IMAGE) \
	  make -C /src/kmod clean >/dev/null 2>&1 || true
	@echo ">> build/mock_gpu.ko ($(shell modinfo -F vermagic $(BUILD_DIR)/mock_gpu.ko 2>/dev/null))"

.PHONY: nvml
nvml: | $(BUILD_DIR) ## compile mock libnvidia-ml.so.1 -> build/
	$(MAKE) -C nvml all

.PHONY: stubs
stubs: | $(BUILD_DIR) ## compile stub driver libs (libcuda.so.1) -> build/
	$(MAKE) -C driver-root all

.PHONY: artifacts
artifacts: kmod nvml stubs ## build all compiled artifacts into build/

.PHONY: image
image: artifacts ## build the single mock-gpu-driver runtime image
	docker build -f image/Dockerfile \
	  --build-arg NV_PRODUCT_NAME="$(NV_PRODUCT_NAME)" \
	  --build-arg NV_DRIVER_VERSION="$(NV_DRIVER_VERSION)" \
	  --build-arg NV_CUDA_VERSION="$(NV_CUDA_VERSION)" \
	  --build-arg NV_MEMORY_MIB="$(NV_MEMORY_MIB)" \
	  --build-arg MOCK_GPU_COUNT="$(MOCK_GPU_COUNT)" \
	  --build-arg MOCK_ENABLE_PCI="$(MOCK_ENABLE_PCI)" \
	  --build-arg MOCK_PCI_DOMAIN="$(MOCK_PCI_DOMAIN)" \
	  --build-arg MOCK_PCI_BUSNR="$(MOCK_PCI_BUSNR)" \
	  -t $(MOCK_IMAGE) .

.PHONY: validator-shim
validator-shim: ## build the no-op validator image
	docker build -t $(VALIDATOR_SHIM) deploy/cuda-validator-shim

# ---------------------------------------------------------------------------
# Tests (TESTING.md). Unit = safe anywhere; functional/E2E = root + host.
# ---------------------------------------------------------------------------
.PHONY: test-unit
test-unit: ## run unit tests (C + bats); no kernel, no cluster
	$(MAKE) -C tests/unit config-space
	$(MAKE) -C tests/unit nvml
	$(MAKE) -C tests/unit shell

.PHONY: test-functional
test-functional: ## run functional tests (REQUIRES root; loads module into host kernel)
	tests/functional/kmod_load.sh
	tests/functional/nvml_runtime/run.sh
	tests/functional/image_modes.sh
	tests/functional/validator_shim.sh

.PHONY: test-e2e
test-e2e: ## run full E2E against the GPU Operator (REQUIRES kind + root)
	tests/e2e/e2e_test.sh

# ---------------------------------------------------------------------------
# kind cluster (kind is assumed already on PATH — installed by the user)
# ---------------------------------------------------------------------------
.PHONY: kind-up
kind-up: ## create the kind cluster
	kind create cluster --name $(KIND_CLUSTER_NAME) --image $(KIND_NODE_IMAGE) --config kind/kind-config.yaml

.PHONY: kind-down
kind-down: ## delete the kind cluster
	kind delete cluster --name $(KIND_CLUSTER_NAME)

.PHONY: kind-load
kind-load: ## load locally-built images into the kind cluster
	kind load docker-image $(MOCK_IMAGE) --name $(KIND_CLUSTER_NAME)
	kind load docker-image $(VALIDATOR_SHIM) --name $(KIND_CLUSTER_NAME)

# ---------------------------------------------------------------------------
# Deploy
# ---------------------------------------------------------------------------
.PHONY: deploy-mock
deploy-mock: | $(BUILD_DIR) ## deploy the ungated bootstrap DaemonSet + NFD gate fallback
	envsubst < deploy/bootstrap-daemonset.yaml > $(BUILD_DIR)/bootstrap-daemonset.rendered.yaml
	kubectl apply -f $(BUILD_DIR)/bootstrap-daemonset.rendered.yaml
	kubectl apply -f deploy/nodefeaturerule.yaml || \
	  echo "WARN: NodeFeatureRule CRD not present yet (apply after operator/NFD installs)"

.PHONY: deploy-operator
deploy-operator: | $(BUILD_DIR) ## helm install the GPU Operator with mock overrides
	envsubst < deploy/clusterpolicy-values.yaml > $(BUILD_DIR)/clusterpolicy-values.rendered.yaml
ifeq ($(GPU_OPERATOR_CHART_SOURCE),local)
	@echo ">> installing in-repo chart: $(GPU_OPERATOR_LOCAL_CHART)"
	-helm dependency build $(GPU_OPERATOR_LOCAL_CHART)
	helm upgrade --install $(GPU_OPERATOR_RELEASE) $(GPU_OPERATOR_LOCAL_CHART) \
	  -n $(GPU_OPERATOR_NS) --create-namespace \
	  -f $(BUILD_DIR)/clusterpolicy-values.rendered.yaml --wait --timeout $(TEST_TIMEOUT_SECS)s
else
	@test -n "$(GPU_OPERATOR_VERSION)" || { echo "ERROR: set GPU_OPERATOR_VERSION for remote source"; exit 1; }
	@echo ">> installing remote chart: $(GPU_OPERATOR_CHART) $(GPU_OPERATOR_VERSION)"
	helm repo add $(GPU_OPERATOR_REPO_NAME) $(GPU_OPERATOR_REPO_URL) || true
	helm repo update
	helm upgrade --install $(GPU_OPERATOR_RELEASE) $(GPU_OPERATOR_REPO_NAME)/$(GPU_OPERATOR_CHART) \
	  --version $(GPU_OPERATOR_VERSION) \
	  -n $(GPU_OPERATOR_NS) --create-namespace \
	  -f $(BUILD_DIR)/clusterpolicy-values.rendered.yaml --wait --timeout $(TEST_TIMEOUT_SECS)s
endif

.PHONY: undeploy
undeploy: ## remove operator + bootstrap (bootstrap SIGTERM -> rmmod cleans the host)
	-helm uninstall $(GPU_OPERATOR_RELEASE) -n $(GPU_OPERATOR_NS)
	-kubectl delete -f deploy/nodefeaturerule.yaml --ignore-not-found
	-kubectl delete -f $(BUILD_DIR)/bootstrap-daemonset.rendered.yaml --ignore-not-found

.PHONY: verify
verify: ## quick E2E assertions (labels, allocatable, pods Ready)
	tests/e2e/verify.sh

.PHONY: test-pod
test-pod: ## schedule a pod requesting nvidia.com/gpu and confirm it runs
	kubectl apply -f tests/e2e/manifests/test-pod.yaml
	kubectl wait --for=condition=Ready pod/mock-gpu-test --timeout=120s || \
	  kubectl wait --for=jsonpath='{.status.phase}'=Succeeded pod/mock-gpu-test --timeout=120s

# ---------------------------------------------------------------------------
# Lifecycle
# ---------------------------------------------------------------------------
.PHONY: up
up: kind-up image validator-shim kind-load deploy-mock deploy-operator verify ## full bring-up

.PHONY: down
down: kind-down ## tear everything down (deletes the cluster)

.PHONY: clean
clean: ## remove build artifacts
	rm -rf $(BUILD_DIR)
	$(MAKE) -C tests/unit clean
	-$(MAKE) -C nvml clean
	-$(MAKE) -C driver-root clean
