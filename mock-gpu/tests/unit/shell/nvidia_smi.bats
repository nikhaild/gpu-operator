#!/usr/bin/env bats
# Unit tests for driver-root/nvidia-smi.sh shim (TESTING.md §1.3).

setup() {
  SMI="$BATS_TEST_DIRNAME/../../../driver-root/nvidia-smi.sh"
  export MOCK_NV_PRODUCT_NAME="Tesla T4"
  export MOCK_NV_DRIVER_VERSION="535.104.05"
  export MOCK_NV_CUDA_VERSION="12.2"
}

@test "default invocation exits 0 and shows driver version" {
  run "$SMI"
  [ "$status" -eq 0 ]
  [[ "$output" == *"Driver Version: 535.104.05"* ]]
  [[ "$output" == *"Tesla T4"* ]]
}

@test "-L lists the requested number of GPUs" {
  NVML_MOCK_GPU_COUNT=3 run "$SMI" -L
  [ "$status" -eq 0 ]
  [ "$(echo "$output" | grep -c '^GPU ')" -eq 3 ]
  [[ "$output" == *"UUID: GPU-"* ]]
}

@test "--query-gpu csv noheader returns one row per GPU" {
  NVML_MOCK_GPU_COUNT=2 run "$SMI" --query-gpu=index,name --format=csv,noheader
  [ "$status" -eq 0 ]
  [ "$(echo "$output" | wc -l)" -eq 2 ]
  [[ "$output" == *"Tesla T4"* ]]
}
