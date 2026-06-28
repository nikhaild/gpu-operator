/* SPDX-License-Identifier: MIT */
/*
 * nvml_mock.h - Minimal subset of the NVML ABI implemented by the mock library.
 *
 * Only the types, enums, constants and function prototypes actually used by
 * GFD / k8s-device-plugin / libnvidia-container are declared here. The struct
 * layouts and enum values intentionally MATCH the real NVML ABI so that
 * go-nvml (which marshals these structs) interprets them correctly. Do NOT
 * reorder or resize the published structs.
 *
 * This is an independent reimplementation of a small public ABI surface; it
 * vendors no NVIDIA source.
 */
#ifndef NVML_MOCK_H
#define NVML_MOCK_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- return codes (must match nvmlReturn_t) ------------------------------ */
typedef enum nvmlReturn_enum {
	NVML_SUCCESS                        = 0,
	NVML_ERROR_UNINITIALIZED           = 1,
	NVML_ERROR_INVALID_ARGUMENT        = 2,
	NVML_ERROR_NOT_SUPPORTED           = 3,
	NVML_ERROR_NO_PERMISSION           = 4,
	NVML_ERROR_ALREADY_INITIALIZED     = 5,
	NVML_ERROR_NOT_FOUND               = 6,
	NVML_ERROR_INSUFFICIENT_SIZE       = 7,
	NVML_ERROR_INSUFFICIENT_POWER      = 8,
	NVML_ERROR_DRIVER_NOT_LOADED       = 9,
	NVML_ERROR_TIMEOUT                 = 10,
	NVML_ERROR_IRQ_ISSUE               = 11,
	NVML_ERROR_LIBRARY_NOT_FOUND       = 12,
	NVML_ERROR_FUNCTION_NOT_FOUND      = 13,
	NVML_ERROR_CORRUPTED_INFOROM       = 14,
	NVML_ERROR_GPU_IS_LOST             = 15,
	NVML_ERROR_RESET_REQUIRED          = 16,
	NVML_ERROR_OPERATING_SYSTEM        = 17,
	NVML_ERROR_LIB_RM_VERSION_MISMATCH = 18,
	NVML_ERROR_IN_USE                  = 19,
	NVML_ERROR_MEMORY                  = 20,
	NVML_ERROR_NO_DATA                 = 21,
	NVML_ERROR_VGPU_ECC_NOT_SUPPORTED  = 22,
	NVML_ERROR_INSUFFICIENT_RESOURCES  = 23,
	NVML_ERROR_FREQ_NOT_SUPPORTED      = 24,
	NVML_ERROR_ARGUMENT_VERSION_MISMATCH = 25,
	NVML_ERROR_DEPRECATED              = 26,
	NVML_ERROR_NOT_READY               = 27,
	NVML_ERROR_UNKNOWN                 = 999
} nvmlReturn_t;

/* ---- opaque handles ------------------------------------------------------ */
typedef struct nvmlDevice_st   *nvmlDevice_t;
typedef struct nvmlEventSet_st *nvmlEventSet_t;

/* ---- buffer sizes (match real NVML) -------------------------------------- */
#define NVML_DEVICE_NAME_BUFFER_SIZE            64
#define NVML_DEVICE_NAME_V2_BUFFER_SIZE         96
#define NVML_DEVICE_UUID_BUFFER_SIZE            80
#define NVML_DEVICE_UUID_V2_BUFFER_SIZE         96
#define NVML_SYSTEM_DRIVER_VERSION_BUFFER_SIZE  80
#define NVML_SYSTEM_NVML_VERSION_BUFFER_SIZE    80
#define NVML_DEVICE_PCI_BUS_ID_BUFFER_SIZE      32
#define NVML_DEVICE_PCI_BUS_ID_BUFFER_V2_SIZE   16

/* ---- structs (layout MUST match real NVML) ------------------------------- */
typedef struct nvmlMemory_st {
	unsigned long long total;
	unsigned long long free;
	unsigned long long used;
} nvmlMemory_t;

typedef struct nvmlMemory_v2_st {
	unsigned int       version;
	unsigned long long total;
	unsigned long long reserved;
	unsigned long long free;
	unsigned long long used;
} nvmlMemory_v2_t;

typedef struct nvmlPciInfo_st {
	char         busIdLegacy[NVML_DEVICE_PCI_BUS_ID_BUFFER_V2_SIZE];
	unsigned int domain;
	unsigned int bus;
	unsigned int device;
	unsigned int pciDeviceId;
	unsigned int pciSubSystemId;
	char         busId[NVML_DEVICE_PCI_BUS_ID_BUFFER_SIZE];
} nvmlPciInfo_t;

/* ---- enums (values MUST match real NVML) --------------------------------- */
typedef enum nvmlBrandType_enum {
	NVML_BRAND_UNKNOWN = 0,
	NVML_BRAND_QUADRO  = 1,
	NVML_BRAND_TESLA   = 2,
	NVML_BRAND_NVS     = 3,
	NVML_BRAND_GRID    = 4,
	NVML_BRAND_GEFORCE = 5,
	NVML_BRAND_TITAN   = 6
} nvmlBrandType_t;

typedef unsigned int nvmlDeviceArchitecture_t;
#define NVML_DEVICE_ARCH_KEPLER  2
#define NVML_DEVICE_ARCH_MAXWELL 3
#define NVML_DEVICE_ARCH_PASCAL  4
#define NVML_DEVICE_ARCH_VOLTA   5
#define NVML_DEVICE_ARCH_TURING  6
#define NVML_DEVICE_ARCH_AMPERE  7
#define NVML_DEVICE_ARCH_ADA     8
#define NVML_DEVICE_ARCH_HOPPER  9
#define NVML_DEVICE_ARCH_UNKNOWN 0xffffffff

#define NVML_DEVICE_MIG_DISABLE  0
#define NVML_DEVICE_MIG_ENABLE   1

#define NVML_INIT_FLAG_NO_GPUS       1
#define NVML_INIT_FLAG_NO_ATTACH     2

/* ---- functions (subset) -------------------------------------------------- */
nvmlReturn_t nvmlInit_v2(void);
nvmlReturn_t nvmlInit(void);
nvmlReturn_t nvmlInitWithFlags(unsigned int flags);
nvmlReturn_t nvmlShutdown(void);
const char  *nvmlErrorString(nvmlReturn_t result);

nvmlReturn_t nvmlSystemGetDriverVersion(char *version, unsigned int length);
nvmlReturn_t nvmlSystemGetNVMLVersion(char *version, unsigned int length);
nvmlReturn_t nvmlSystemGetCudaDriverVersion(int *cudaDriverVersion);
nvmlReturn_t nvmlSystemGetCudaDriverVersion_v2(int *cudaDriverVersion);

nvmlReturn_t nvmlDeviceGetCount_v2(unsigned int *deviceCount);
nvmlReturn_t nvmlDeviceGetCount(unsigned int *deviceCount);
nvmlReturn_t nvmlDeviceGetHandleByIndex_v2(unsigned int index, nvmlDevice_t *device);
nvmlReturn_t nvmlDeviceGetHandleByIndex(unsigned int index, nvmlDevice_t *device);
nvmlReturn_t nvmlDeviceGetHandleByUUID(const char *uuid, nvmlDevice_t *device);

nvmlReturn_t nvmlDeviceGetName(nvmlDevice_t device, char *name, unsigned int length);
nvmlReturn_t nvmlDeviceGetUUID(nvmlDevice_t device, char *uuid, unsigned int length);
nvmlReturn_t nvmlDeviceGetMinorNumber(nvmlDevice_t device, unsigned int *minorNumber);
nvmlReturn_t nvmlDeviceGetIndex(nvmlDevice_t device, unsigned int *index);
nvmlReturn_t nvmlDeviceGetMemoryInfo(nvmlDevice_t device, nvmlMemory_t *memory);
nvmlReturn_t nvmlDeviceGetMemoryInfo_v2(nvmlDevice_t device, nvmlMemory_v2_t *memory);
nvmlReturn_t nvmlDeviceGetPciInfo_v3(nvmlDevice_t device, nvmlPciInfo_t *pci);
nvmlReturn_t nvmlDeviceGetPciInfo_v2(nvmlDevice_t device, nvmlPciInfo_t *pci);
nvmlReturn_t nvmlDeviceGetCudaComputeCapability(nvmlDevice_t device, int *major, int *minor);
nvmlReturn_t nvmlDeviceGetArchitecture(nvmlDevice_t device, nvmlDeviceArchitecture_t *arch);
nvmlReturn_t nvmlDeviceGetBrand(nvmlDevice_t device, nvmlBrandType_t *type);
nvmlReturn_t nvmlDeviceGetMigMode(nvmlDevice_t device, unsigned int *currentMode, unsigned int *pendingMode);

/* Health-monitoring event APIs: deliberately unsupported so the device-plugin
 * disables event-based health checks instead of failing. */
nvmlReturn_t nvmlEventSetCreate(nvmlEventSet_t *set);
nvmlReturn_t nvmlDeviceRegisterEvents(nvmlDevice_t device, unsigned long long eventTypes, nvmlEventSet_t set);
nvmlReturn_t nvmlEventSetFree(nvmlEventSet_t set);

/* Commonly-probed metrics we deliberately stub as NOT_SUPPORTED (graceful
 * degradation; present so callers get a clean error, not a missing symbol). */
nvmlReturn_t nvmlDeviceGetTemperature(nvmlDevice_t device, int sensorType, unsigned int *temp);
nvmlReturn_t nvmlDeviceGetPowerUsage(nvmlDevice_t device, unsigned int *milliwatts);
nvmlReturn_t nvmlDeviceGetUtilizationRates(nvmlDevice_t device, void *utilization);
nvmlReturn_t nvmlDeviceGetComputeMode(nvmlDevice_t device, int *mode);
nvmlReturn_t nvmlDeviceGetMaxMigDeviceCount(nvmlDevice_t device, unsigned int *count);

#ifdef __cplusplus
}
#endif

#endif /* NVML_MOCK_H */
