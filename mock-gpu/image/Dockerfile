# Runtime image (PLAN §5.5): the single mock-gpu-driver image, run by us in
# MODE=bootstrap (ungated DaemonSet) and by the operator in MODE=driver.
#
# Packages PREBUILT artifacts (no compiler/headers at runtime). Build context is
# the mock-gpu/ directory; build/ must already contain the compiled artifacts:
#   build/mock_gpu.ko  build/libnvidia-ml.so.1  build/libcuda.so.1
# Build via the top-level Makefile `image` target (which builds artifacts first).
ARG BASE_IMAGE=ubuntu:24.04
FROM ${BASE_IMAGE}

ARG DEBIAN_FRONTEND=noninteractive
# kmod -> insmod/rmmod/modinfo/lsmod ; libc-bin -> ldconfig ; bash present already.
RUN apt-get update \
 && apt-get install -y --no-install-recommends kmod libc-bin \
 && rm -rf /var/lib/apt/lists/*

WORKDIR /opt/mock-gpu

COPY build/mock_gpu.ko          /opt/mock-gpu/mock_gpu.ko
COPY build/libnvidia-ml.so.1    /opt/mock-gpu/lib/libnvidia-ml.so.1
COPY build/libcuda.so.1         /opt/mock-gpu/lib/libcuda.so.1
COPY driver-root/nvidia-smi.sh  /opt/mock-gpu/bin/nvidia-smi
COPY driver-root/install.sh     /opt/mock-gpu/install.sh
COPY image/entrypoint.sh        /opt/mock-gpu/entrypoint.sh
RUN chmod +x /opt/mock-gpu/bin/nvidia-smi /opt/mock-gpu/install.sh /opt/mock-gpu/entrypoint.sh

# Defaults (overridable by the DaemonSet env). Identity values feed the
# nvidia-smi shim; module params feed the entrypoint's insmod.
ARG NV_PRODUCT_NAME="Tesla T4"
ARG NV_DRIVER_VERSION="535.104.05"
ARG NV_CUDA_VERSION="12.2"
ARG NV_MEMORY_MIB="16384"
ARG MOCK_GPU_COUNT="1"
ARG MOCK_ENABLE_PCI="1"
ARG MOCK_PCI_DOMAIN="0x1000"
ARG MOCK_PCI_BUSNR="0"
ENV MOCK_NV_PRODUCT_NAME=${NV_PRODUCT_NAME} \
    MOCK_NV_DRIVER_VERSION=${NV_DRIVER_VERSION} \
    MOCK_NV_CUDA_VERSION=${NV_CUDA_VERSION} \
    MOCK_NV_MEMORY_MIB=${NV_MEMORY_MIB} \
    MOCK_GPU_COUNT=${MOCK_GPU_COUNT} \
    NVML_MOCK_GPU_COUNT=${MOCK_GPU_COUNT} \
    MOCK_ENABLE_PCI=${MOCK_ENABLE_PCI} \
    MOCK_PCI_DOMAIN=${MOCK_PCI_DOMAIN} \
    MOCK_PCI_BUSNR=${MOCK_PCI_BUSNR} \
    KO_PATH=/opt/mock-gpu/mock_gpu.ko \
    SRC_DIR=/opt/mock-gpu

ENTRYPOINT ["/opt/mock-gpu/entrypoint.sh"]
