# Builder image (PLAN §7): Ubuntu 24.04 + the host kernel's headers + toolchain,
# fetched from the Ubuntu package archive. The module is then compiled INSIDE a
# container of this image against a bind-mounted source tree, so no compiler or
# headers are required on the node at runtime.
#
# Build (from mock-gpu/):
#   docker build -f image/Dockerfile.builder \
#     --build-arg BUILD_BASE_IMAGE=ubuntu:24.04 \
#     --build-arg KERNEL_VERSION=$(uname -r) \
#     -t localhost/mock-gpu-builder:$(uname -r) .
ARG BUILD_BASE_IMAGE=ubuntu:24.04
FROM ${BUILD_BASE_IMAGE}

ARG KERNEL_VERSION
ARG DEBIAN_FRONTEND=noninteractive

# linux-headers-${KERNEL_VERSION} must exist in the configured apt archive. This
# is true for stock Ubuntu 24.04 -generic/-hwe kernels; a custom/mainline kernel
# will not have a matching package (build fails clearly here).
RUN test -n "${KERNEL_VERSION}" || (echo "ERROR: KERNEL_VERSION build-arg is required" >&2; exit 1) \
 && apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential \
      bc \
      flex \
      bison \
      libelf-dev \
      kmod \
      linux-headers-${KERNEL_VERSION} \
 && rm -rf /var/lib/apt/lists/* \
 && test -e "/lib/modules/${KERNEL_VERSION}/build" \
      || (echo "ERROR: /lib/modules/${KERNEL_VERSION}/build missing after header install" >&2; exit 1)

WORKDIR /src
