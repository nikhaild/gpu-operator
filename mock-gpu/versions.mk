# versions.mk — SINGLE SOURCE OF TRUTH for all external versions, image refs, and
# mock-GPU identity values. Every Makefile, Dockerfile, C component, and test must
# derive its values from here (via include, --build-arg, -D defines, or `include`).
#
# Override any value on the command line, e.g.:
#   make image MOCK_IMAGE_TAG=v0.1.0
#   make deploy-operator GPU_OPERATOR_CHART_SOURCE=remote GPU_OPERATOR_VERSION=v24.9.0
#
# Resolve every placeholder (values containing X / TODO) before running E2E.
#
# NOTE: assignment lines deliberately carry NO trailing inline comments — GNU make
# preserves the whitespace before a '#', which corrupts values used in paths/tags.

# Absolute path to this mock-gpu directory and the gpu-operator repo root.
MOCK_GPU_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT    := $(abspath $(MOCK_GPU_DIR)/..)

# ---------------------------------------------------------------------------
# Host / build assumptions (PLAN §2, §7). Ubuntu 24.04 host kernel.
# ---------------------------------------------------------------------------
HOST_OS            ?= ubuntu
HOST_OS_VERSION    ?= 24.04
BUILD_BASE_IMAGE   ?= ubuntu:24.04
# Host kernel; detected, overridable. Used in KDIR path + builder image tag.
KERNEL_VERSION     ?= $(shell uname -r)
ARCH               ?= $(shell uname -m)
# Optional apt archive override (blank = distro default).
APT_MIRROR         ?=

# ---------------------------------------------------------------------------
# Kubernetes / kind  (kind is assumed already on PATH — installed by the user).
# Match KIND_NODE_IMAGE to your installed kind version.
# ---------------------------------------------------------------------------
KIND_CLUSTER_NAME  ?= mock-gpu
KIND_NODE_IMAGE    ?= kindest/node:v1.31.4

# ---------------------------------------------------------------------------
# GPU Operator — chart source is configurable.
#   GPU_OPERATOR_CHART_SOURCE = local  -> install the chart in THIS repo (default)
#   GPU_OPERATOR_CHART_SOURCE = remote -> install GPU_OPERATOR_CHART from the helm repo
# ---------------------------------------------------------------------------
GPU_OPERATOR_CHART_SOURCE ?= local
GPU_OPERATOR_LOCAL_CHART  ?= $(REPO_ROOT)/deployments/gpu-operator
GPU_OPERATOR_REPO_NAME    ?= nvidia
GPU_OPERATOR_REPO_URL     ?= https://helm.ngc.nvidia.com/nvidia
GPU_OPERATOR_CHART        ?= gpu-operator
# Required only when SOURCE=remote (e.g. v24.9.0).
GPU_OPERATOR_VERSION      ?=
GPU_OPERATOR_NS           ?= gpu-operator
GPU_OPERATOR_RELEASE      ?= gpu-operator

# Informational: components shipped by the in-repo chart (deployments/gpu-operator).
# Used to derive the go-nvml linkage below; not consumed directly by deploy.
NFD_CHART_VERSION         ?= 0.18.3
# k8s-device-plugin (== GFD) version referenced by the in-repo chart.
GFD_DEVICE_PLUGIN_VERSION ?= v0.19.3
CONTAINER_TOOLKIT_VERSION ?= v1.19.1

# ---------------------------------------------------------------------------
# Mock GPU identity — Tesla T4 (Turing, no MIG => simplest). (PLAN §5)
# These feed BOTH the kernel module (fake PCI config space) and the mock NVML,
# so NFD's PCI view and GFD's NVML view stay consistent.
# PCI class for a datacenter GPU = 3D controller (0x0302xx), NOT VGA (0x0300xx);
# using the 3D-controller class avoids kernel VGA-arbiter involvement entirely.
# ---------------------------------------------------------------------------
NV_VENDOR_ID       ?= 0x10de
NV_DEVICE_ID       ?= 0x1eb8
NV_SUBSYS_VENDOR   ?= 0x10de
NV_SUBSYS_DEVICE   ?= 0x12a2
NV_REVISION        ?= 0xa1
NV_CLASS_BASE      ?= 0x03
NV_CLASS_SUB       ?= 0x02
NV_CLASS_PROG      ?= 0x00
NV_PRODUCT_NAME    ?= Tesla T4
NV_MEMORY_MIB      ?= 16384
NV_COMPUTE_MAJOR   ?= 7
NV_COMPUTE_MINOR   ?= 5
NV_ARCH            ?= turing
# NVML enum values that must agree with NV_ARCH/brand above:
#   nvmlDeviceArchitecture_t: TURING=6 ; nvmlBrandType_t: TESLA=2
NV_NVML_ARCH       ?= 6
NV_NVML_BRAND      ?= 2
MOCK_GPU_COUNT     ?= 1

# Reported software versions — kept self-consistent across /proc, NVML, nvidia-smi.
NV_DRIVER_VERSION  ?= 535.104.05
NV_CUDA_VERSION    ?= 12.2
NV_CUDA_VERSION_INT?= 12020
NVML_VERSION       ?= 12.535.104.05

# ---------------------------------------------------------------------------
# Fake-PCI tunables (kernel module params). enable_pci=0 falls back to the
# NodeFeatureRule gate (PLAN §15.1) — useful while validating on a new kernel.
# ---------------------------------------------------------------------------
MOCK_ENABLE_PCI    ?= 1
MOCK_PCI_DOMAIN    ?= 0x1000
MOCK_PCI_BUSNR     ?= 0

# ---------------------------------------------------------------------------
# Container images.
# ---------------------------------------------------------------------------
REGISTRY           ?= localhost
MOCK_IMAGE_NAME    ?= mock-gpu-driver
MOCK_IMAGE_TAG     ?= dev
MOCK_IMAGE         ?= $(REGISTRY)/$(MOCK_IMAGE_NAME):$(MOCK_IMAGE_TAG)
BUILDER_IMAGE      ?= $(REGISTRY)/mock-gpu-builder:$(KERNEL_VERSION)
VALIDATOR_SHIM_REPO       ?= $(REGISTRY)
VALIDATOR_SHIM_IMAGE_NAME ?= cuda-validator-noop
VALIDATOR_SHIM_TAG        ?= $(MOCK_IMAGE_TAG)
VALIDATOR_SHIM            ?= $(VALIDATOR_SHIM_REPO)/$(VALIDATOR_SHIM_IMAGE_NAME):$(VALIDATOR_SHIM_TAG)
# Namespace for our ungated bootstrap DaemonSet (PLAN §6).
MOCK_NAMESPACE     ?= mock-gpu

# Operator driver-image wiring (verify tag scheme against the pinned operator version).
DRIVER_REPOSITORY      ?= $(REGISTRY)
DRIVER_IMAGE_NAME      ?= $(MOCK_IMAGE_NAME)
DRIVER_VERSION         ?= $(MOCK_IMAGE_TAG)
DRIVER_USE_PRECOMPILED ?= false

# ---------------------------------------------------------------------------
# Test-only dependencies (docs/TESTING.md). PLAN §9 CRITICAL linkage:
# NVIDIA_GO_NVML_VERSION MUST equal the go-nvml version vendored by the GFD /
# device-plugin image of GFD_DEVICE_PLUGIN_VERSION above.
# Derive it from that release's go.mod:
#   https://github.com/NVIDIA/k8s-device-plugin/blob/<GFD_DEVICE_PLUGIN_VERSION>/go.mod
# Re-derive and re-run NVML tests whenever GFD_DEVICE_PLUGIN_VERSION changes.
# Confirmed against k8s-device-plugin v0.19.3 go.mod (2026-06-27).
# ---------------------------------------------------------------------------
NVIDIA_GO_NVML_VERSION ?= v0.13.0-1.0.20260212130905-92cf8c963449
BATS_VERSION           ?= 1.11.0
TEST_TIMEOUT_SECS      ?= 600

# ---------------------------------------------------------------------------
# Compiler -D defines for the mock NVML library AND its unit test (one source of
# truth so the .so and the test assert identical values). Used in plain gcc
# recipes (shell-processed), hence the '"..."' quoting around string values.
# ---------------------------------------------------------------------------
NVML_DEFS := \
	-DNV_PRODUCT_NAME='"$(NV_PRODUCT_NAME)"' \
	-DNV_DRIVER_VERSION='"$(NV_DRIVER_VERSION)"' \
	-DNVML_VERSION='"$(NVML_VERSION)"' \
	-DNV_CUDA_VERSION_INT=$(NV_CUDA_VERSION_INT) \
	-DNV_MEMORY_MIB=$(NV_MEMORY_MIB) \
	-DNV_COMPUTE_MAJOR=$(NV_COMPUTE_MAJOR) \
	-DNV_COMPUTE_MINOR=$(NV_COMPUTE_MINOR) \
	-DNV_NVML_ARCH=$(NV_NVML_ARCH) \
	-DNV_NVML_BRAND=$(NV_NVML_BRAND) \
	-DNV_VENDOR_ID=$(NV_VENDOR_ID) \
	-DNV_DEVICE_ID=$(NV_DEVICE_ID) \
	-DNV_SUBSYS_VENDOR=$(NV_SUBSYS_VENDOR) \
	-DNV_SUBSYS_DEVICE=$(NV_SUBSYS_DEVICE) \
	-DMOCK_PCI_DOMAIN=$(MOCK_PCI_DOMAIN) \
	-DMOCK_GPU_COUNT=$(MOCK_GPU_COUNT)
