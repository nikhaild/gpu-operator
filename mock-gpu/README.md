# Mock NVIDIA GPU for testing the GPU Operator

A mock NVIDIA GPU — implemented as a Linux kernel module plus a mock NVML library
— that makes the NVIDIA **GPU Operator** believe a supported GPU is present on a
host with **no real GPU and no functional CUDA**. Intended for testing the
operator's discovery/scheduling path (NFD → GFD → device-plugin) in a `kind`
cluster.

> Design and rationale: [docs/PLAN.md](docs/PLAN.md).
> Test strategy: [docs/TESTING.md](docs/TESTING.md).
> All versions/IDs/images live in **[versions.mk](versions.mk)** (single source of truth).

## ⚠️ Safety

The kernel module fabricates a PCI device on the live PCI subsystem — the
highest-risk part of this project. It is built to be inert (no BARs, no IRQ,
`driver_override` so nothing binds) and degrades gracefully, but **always
validate module loading on a snapshotted / disposable host first.** Loading
happens only in the functional/E2E tests and at deploy time, never during the
build or unit tests.

## What this is

| Component | Role |
|---|---|
| `kmod/` | Kernel module: fake PCI device (vendor `0x10de`, 3D-controller class) for NFD, `/dev/nvidia*` char devices, `/proc/driver/nvidia/version`. |
| `nvml/` | Mock `libnvidia-ml.so.1` returning canned values; injected into GFD/device-plugin via the operator's toolkit (we ship the operator's **driver image**). |
| `driver-root/` | `nvidia-smi` shim, stub `libcuda.so.1`, and `install.sh` to populate the driver root. |
| `image/` | Builder image (Ubuntu 24.04 + headers) and the single runtime image with two modes (`bootstrap`, `driver`). |
| `deploy/` | Ungated bootstrap DaemonSet (NFD-gate bootstrap, PLAN §6), operator Helm values, NodeFeatureRule gate, Kyverno fallback, validator no-op shim. |
| `tests/` | `unit/` (C + bats), `functional/` (run components), `e2e/` (full operator). |

## Prerequisites (assumed already installed)

`docker`, `make`, `kubectl`, `helm`, a C toolchain, and — for E2E — `kind` and
`jq`. Host is assumed **Ubuntu 24.04** (`HOST_OS_VERSION` in `versions.mk`).
Kernel headers/toolchain are installed *inside* the builder container, not on the
host.

## Quick start — build + safe tests (no kernel load, no cluster)

```bash
make artifacts        # build mock_gpu.ko (in container) + libnvidia-ml.so.1 + libcuda.so.1
make test-unit        # C unit tests + bats shell tests
make image            # single mock-gpu-driver runtime image
make validator-shim   # no-op validator image
```

The mock NVML can be verified against the **real** `NVIDIA/go-nvml` (the binding
GFD uses) — also safe:

```bash
bash tests/functional/nvml_runtime/run.sh
```

## Functional tests (REQUIRE root; load the module into the host kernel)

```bash
sudo make test-functional      # kmod load/inspect/unload, image modes, etc.
```
Each script SKIPs cleanly (exit 0) when not root or not Ubuntu 24.04.

## Full E2E with the GPU Operator (REQUIRES kind + root; use a snapshot host)

```bash
sudo make up           # kind-up -> build -> load -> deploy-mock -> deploy-operator -> verify
sudo make test-pod     # schedule a pod requesting nvidia.com/gpu
sudo make down         # delete the cluster
```

Operator chart source is configurable (defaults to the in-repo chart):

```bash
# in-repo chart (default)
sudo make up
# remote chart
sudo make up GPU_OPERATOR_CHART_SOURCE=remote GPU_OPERATOR_VERSION=v24.9.0
```

Multi-GPU:

```bash
sudo make up MOCK_GPU_COUNT=2
```

## Status of this checkout

Validated on Ubuntu 24.04 / kernel `6.8.0-111-generic` (build + safe tests only):

- ✅ `mock_gpu.ko` compiles (host + containerized builder); vermagic matches; all
  kernel symbols resolve.
- ✅ Unit tests pass: config-space (26), mock NVML (37), bats shell (15).
- ✅ Mock `libnvidia-ml.so.1` works with the **real go-nvml** (`v0.13.0-1.0.…`,
  matching k8s-device-plugin v0.19.3): name/UUID/memory/CC all correct.
- ✅ Builder + runtime + validator-shim images build; image vermagic gate works.
- ⏳ Functional (module load) and E2E (operator) tests are written but **not run
  here** — run them on a snapshotted host.

## Known E2E-time items (see PLAN §15)

1. Fake-PCI on a live kernel — validate on a snapshot; `MOCK_ENABLE_PCI=0` falls
   back to the NodeFeatureRule gate (which keys on the loaded `mock_gpu` module).
2. The operator's **driver-image contract** + image-tag scheme vary by operator
   version — verify the computed driver ref matches `MOCK_IMAGE`.
3. The **validator** neutralization (validator no-op shim) — confirm the
   init-vs-long-running behavior and readiness files against the pinned operator.
4. `kind` cross-boundary `/dev/nvidia*` and driver-root propagation.
5. `NVIDIA_GO_NVML_VERSION` must track the GFD version (PLAN §9 linkage).
```
