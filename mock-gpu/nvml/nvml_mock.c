/* SPDX-License-Identifier: MIT */
/*
 * nvml_mock.c - Mock implementation of a subset of NVML.
 *
 * Built into libnvidia-ml.so.1 and injected into GFD / k8s-device-plugin /
 * libnvidia-container by the operator's toolkit (PLAN §5.2). Returns canned
 * values consistent with the kernel module's fake PCI device and versions.mk.
 *
 * Design rules:
 *  - Never crash on bad input: validate pointers/handles, return NVML errors.
 *  - Values are deterministic and derived from compile-time defines (versions.mk).
 *  - Anything we don't truly support returns NVML_ERROR_NOT_SUPPORTED so callers
 *    degrade gracefully instead of hitting a missing symbol.
 *  - Device count may be overridden at runtime via env NVML_MOCK_GPU_COUNT so it
 *    can match the kernel module's `count` param without a rebuild.
 */
#include "nvml_mock.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- build-time identity (from versions.mk NVML_DEFS) -------------------- */
#ifndef NV_PRODUCT_NAME
#define NV_PRODUCT_NAME    "Tesla T4"
#endif
#ifndef NV_DRIVER_VERSION
#define NV_DRIVER_VERSION  "535.104.05"
#endif
#ifndef NVML_VERSION
#define NVML_VERSION       "12.535.104.05"
#endif
#ifndef NV_CUDA_VERSION_INT
#define NV_CUDA_VERSION_INT 12020
#endif
#ifndef NV_MEMORY_MIB
#define NV_MEMORY_MIB      16384
#endif
#ifndef NV_COMPUTE_MAJOR
#define NV_COMPUTE_MAJOR   7
#endif
#ifndef NV_COMPUTE_MINOR
#define NV_COMPUTE_MINOR   5
#endif
#ifndef NV_NVML_ARCH
#define NV_NVML_ARCH       6  /* TURING */
#endif
#ifndef NV_NVML_BRAND
#define NV_NVML_BRAND      2  /* TESLA */
#endif
#ifndef NV_VENDOR_ID
#define NV_VENDOR_ID       0x10de
#endif
#ifndef NV_DEVICE_ID
#define NV_DEVICE_ID       0x1eb8
#endif
#ifndef NV_SUBSYS_VENDOR
#define NV_SUBSYS_VENDOR   0x10de
#endif
#ifndef NV_SUBSYS_DEVICE
#define NV_SUBSYS_DEVICE   0x12a2
#endif
#ifndef MOCK_PCI_DOMAIN
#define MOCK_PCI_DOMAIN    0x1000
#endif
#ifndef MOCK_GPU_COUNT
#define MOCK_GPU_COUNT     1
#endif

#define MOCK_MAX_GPUS      16
#define MEM_TOTAL_BYTES    ((unsigned long long)NV_MEMORY_MIB * 1024ULL * 1024ULL)

/* Opaque handle backing store: one entry per device; handle == &g_dev[i]. */
struct nvmlDevice_st {
	unsigned int index;
	int          valid;
};

static int          g_initialized;
static unsigned int g_count;
static struct nvmlDevice_st g_dev[MOCK_MAX_GPUS];

/* ---- helpers ------------------------------------------------------------- */
static unsigned int resolve_count(void)
{
	const char *env = getenv("NVML_MOCK_GPU_COUNT");
	long n;

	if (env && *env) {
		char *end = 0;
		n = strtol(env, &end, 10);
		if (end != env && n >= 1 && n <= MOCK_MAX_GPUS)
			return (unsigned int)n;
	}
	n = MOCK_GPU_COUNT;
	if (n < 1)
		n = 1;
	if (n > MOCK_MAX_GPUS)
		n = MOCK_MAX_GPUS;
	return (unsigned int)n;
}

/* Validate a handle and return its device index, or -1. */
static int dev_index(nvmlDevice_t device)
{
	struct nvmlDevice_st *d = (struct nvmlDevice_st *)device;

	if (!g_initialized || !d)
		return -1;
	if (d < &g_dev[0] || d >= &g_dev[MOCK_MAX_GPUS])
		return -1;
	if (!d->valid || d->index >= g_count)
		return -1;
	return (int)d->index;
}

/* Safe bounded string copy returning an NVML status. */
static nvmlReturn_t copy_str(char *dst, unsigned int len, const char *src)
{
	size_t need;

	if (!dst)
		return NVML_ERROR_INVALID_ARGUMENT;
	need = strlen(src) + 1;
	if (len < need)
		return NVML_ERROR_INSUFFICIENT_SIZE;
	memcpy(dst, src, need);
	return NVML_SUCCESS;
}

/* ---- init / shutdown ----------------------------------------------------- */
nvmlReturn_t nvmlInit_v2(void)
{
	unsigned int i;

	g_count = resolve_count();
	for (i = 0; i < MOCK_MAX_GPUS; i++) {
		g_dev[i].index = i;
		g_dev[i].valid = (i < g_count) ? 1 : 0;
	}
	g_initialized = 1;
	return NVML_SUCCESS;
}

nvmlReturn_t nvmlInit(void)             { return nvmlInit_v2(); }
nvmlReturn_t nvmlInitWithFlags(unsigned int flags) { (void)flags; return nvmlInit_v2(); }

nvmlReturn_t nvmlShutdown(void)
{
	g_initialized = 0;
	return NVML_SUCCESS;
}

const char *nvmlErrorString(nvmlReturn_t result)
{
	switch (result) {
	case NVML_SUCCESS:                   return "The operation was successful";
	case NVML_ERROR_UNINITIALIZED:       return "NVML was not first initialized with nvmlInit()";
	case NVML_ERROR_INVALID_ARGUMENT:    return "A supplied argument is invalid";
	case NVML_ERROR_NOT_SUPPORTED:       return "The requested operation is not available on this device";
	case NVML_ERROR_NOT_FOUND:           return "A query to find an object was unsuccessful";
	case NVML_ERROR_INSUFFICIENT_SIZE:   return "An input argument is not large enough";
	case NVML_ERROR_DRIVER_NOT_LOADED:   return "The driver is not loaded";
	case NVML_ERROR_FUNCTION_NOT_FOUND:  return "A function was not found in this version of NVML";
	default:                             return "An internal driver error occurred (mock)";
	}
}

/* ---- system info --------------------------------------------------------- */
nvmlReturn_t nvmlSystemGetDriverVersion(char *version, unsigned int length)
{
	return copy_str(version, length, NV_DRIVER_VERSION);
}

nvmlReturn_t nvmlSystemGetNVMLVersion(char *version, unsigned int length)
{
	return copy_str(version, length, NVML_VERSION);
}

nvmlReturn_t nvmlSystemGetCudaDriverVersion(int *cudaDriverVersion)
{
	if (!cudaDriverVersion)
		return NVML_ERROR_INVALID_ARGUMENT;
	*cudaDriverVersion = NV_CUDA_VERSION_INT;
	return NVML_SUCCESS;
}

nvmlReturn_t nvmlSystemGetCudaDriverVersion_v2(int *cudaDriverVersion)
{
	return nvmlSystemGetCudaDriverVersion(cudaDriverVersion);
}

/* ---- device enumeration -------------------------------------------------- */
nvmlReturn_t nvmlDeviceGetCount_v2(unsigned int *deviceCount)
{
	if (!g_initialized)
		return NVML_ERROR_UNINITIALIZED;
	if (!deviceCount)
		return NVML_ERROR_INVALID_ARGUMENT;
	*deviceCount = g_count;
	return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetCount(unsigned int *deviceCount)
{
	return nvmlDeviceGetCount_v2(deviceCount);
}

nvmlReturn_t nvmlDeviceGetHandleByIndex_v2(unsigned int index, nvmlDevice_t *device)
{
	if (!g_initialized)
		return NVML_ERROR_UNINITIALIZED;
	if (!device)
		return NVML_ERROR_INVALID_ARGUMENT;
	if (index >= g_count)
		return NVML_ERROR_INVALID_ARGUMENT;
	*device = (nvmlDevice_t)&g_dev[index];
	return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetHandleByIndex(unsigned int index, nvmlDevice_t *device)
{
	return nvmlDeviceGetHandleByIndex_v2(index, device);
}

nvmlReturn_t nvmlDeviceGetHandleByUUID(const char *uuid, nvmlDevice_t *device)
{
	unsigned int i;
	char buf[NVML_DEVICE_UUID_BUFFER_SIZE];

	if (!g_initialized)
		return NVML_ERROR_UNINITIALIZED;
	if (!uuid || !device)
		return NVML_ERROR_INVALID_ARGUMENT;
	for (i = 0; i < g_count; i++) {
		if (nvmlDeviceGetUUID((nvmlDevice_t)&g_dev[i], buf, sizeof(buf)) == NVML_SUCCESS &&
		    strcmp(buf, uuid) == 0) {
			*device = (nvmlDevice_t)&g_dev[i];
			return NVML_SUCCESS;
		}
	}
	return NVML_ERROR_NOT_FOUND;
}

/* ---- per-device attributes ----------------------------------------------- */
nvmlReturn_t nvmlDeviceGetIndex(nvmlDevice_t device, unsigned int *index)
{
	int i = dev_index(device);

	if (i < 0)
		return NVML_ERROR_INVALID_ARGUMENT;
	if (!index)
		return NVML_ERROR_INVALID_ARGUMENT;
	*index = (unsigned int)i;
	return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetName(nvmlDevice_t device, char *name, unsigned int length)
{
	if (dev_index(device) < 0)
		return NVML_ERROR_INVALID_ARGUMENT;
	return copy_str(name, length, NV_PRODUCT_NAME);
}

nvmlReturn_t nvmlDeviceGetUUID(nvmlDevice_t device, char *uuid, unsigned int length)
{
	int i = dev_index(device);
	char tmp[NVML_DEVICE_UUID_BUFFER_SIZE];

	if (i < 0)
		return NVML_ERROR_INVALID_ARGUMENT;
	/* Deterministic, unique-per-index, well-formed "GPU-<uuid>". */
	snprintf(tmp, sizeof(tmp),
		 "GPU-%08x-0000-4000-8000-%012x", 0x10de0000u + (unsigned)i, (unsigned)i);
	return copy_str(uuid, length, tmp);
}

nvmlReturn_t nvmlDeviceGetMinorNumber(nvmlDevice_t device, unsigned int *minorNumber)
{
	int i = dev_index(device);

	if (i < 0)
		return NVML_ERROR_INVALID_ARGUMENT;
	if (!minorNumber)
		return NVML_ERROR_INVALID_ARGUMENT;
	*minorNumber = (unsigned int)i;  /* matches /dev/nvidia<i> */
	return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetMemoryInfo(nvmlDevice_t device, nvmlMemory_t *memory)
{
	if (dev_index(device) < 0)
		return NVML_ERROR_INVALID_ARGUMENT;
	if (!memory)
		return NVML_ERROR_INVALID_ARGUMENT;
	memory->total = MEM_TOTAL_BYTES;
	memory->free  = MEM_TOTAL_BYTES;
	memory->used  = 0;
	return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetMemoryInfo_v2(nvmlDevice_t device, nvmlMemory_v2_t *memory)
{
	if (dev_index(device) < 0)
		return NVML_ERROR_INVALID_ARGUMENT;
	if (!memory)
		return NVML_ERROR_INVALID_ARGUMENT;
	/* Preserve caller-provided version; fill the rest. */
	memory->total    = MEM_TOTAL_BYTES;
	memory->reserved = 0;
	memory->free     = MEM_TOTAL_BYTES;
	memory->used     = 0;
	return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetPciInfo_v3(nvmlDevice_t device, nvmlPciInfo_t *pci)
{
	int i = dev_index(device);

	if (i < 0)
		return NVML_ERROR_INVALID_ARGUMENT;
	if (!pci)
		return NVML_ERROR_INVALID_ARGUMENT;
	memset(pci, 0, sizeof(*pci));
	pci->domain         = (unsigned int)MOCK_PCI_DOMAIN;
	pci->bus            = 0;
	pci->device         = (unsigned int)i;
	pci->pciDeviceId    = ((unsigned int)NV_DEVICE_ID << 16) | (unsigned int)NV_VENDOR_ID;
	pci->pciSubSystemId = ((unsigned int)NV_SUBSYS_DEVICE << 16) | (unsigned int)NV_SUBSYS_VENDOR;
	snprintf(pci->busId, sizeof(pci->busId), "%08X:%02X:%02X.0",
		 (unsigned int)MOCK_PCI_DOMAIN, 0, (unsigned int)i);
	snprintf(pci->busIdLegacy, sizeof(pci->busIdLegacy), "%04X:%02X:%02X.0",
		 (unsigned int)MOCK_PCI_DOMAIN & 0xffff, 0, (unsigned int)i);
	return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetPciInfo_v2(nvmlDevice_t device, nvmlPciInfo_t *pci)
{
	return nvmlDeviceGetPciInfo_v3(device, pci);
}

nvmlReturn_t nvmlDeviceGetCudaComputeCapability(nvmlDevice_t device, int *major, int *minor)
{
	if (dev_index(device) < 0)
		return NVML_ERROR_INVALID_ARGUMENT;
	if (!major || !minor)
		return NVML_ERROR_INVALID_ARGUMENT;
	*major = NV_COMPUTE_MAJOR;
	*minor = NV_COMPUTE_MINOR;
	return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetArchitecture(nvmlDevice_t device, nvmlDeviceArchitecture_t *arch)
{
	if (dev_index(device) < 0)
		return NVML_ERROR_INVALID_ARGUMENT;
	if (!arch)
		return NVML_ERROR_INVALID_ARGUMENT;
	*arch = (nvmlDeviceArchitecture_t)NV_NVML_ARCH;
	return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetBrand(nvmlDevice_t device, nvmlBrandType_t *type)
{
	if (dev_index(device) < 0)
		return NVML_ERROR_INVALID_ARGUMENT;
	if (!type)
		return NVML_ERROR_INVALID_ARGUMENT;
	*type = (nvmlBrandType_t)NV_NVML_BRAND;
	return NVML_SUCCESS;
}

nvmlReturn_t nvmlDeviceGetMigMode(nvmlDevice_t device, unsigned int *currentMode,
				  unsigned int *pendingMode)
{
	if (dev_index(device) < 0)
		return NVML_ERROR_INVALID_ARGUMENT;
	/* Tesla T4 has no MIG; report disabled (callers also accept NOT_SUPPORTED). */
	if (currentMode)
		*currentMode = NVML_DEVICE_MIG_DISABLE;
	if (pendingMode)
		*pendingMode = NVML_DEVICE_MIG_DISABLE;
	return NVML_SUCCESS;
}

/* ---- health events: deliberately unsupported ----------------------------- */
nvmlReturn_t nvmlEventSetCreate(nvmlEventSet_t *set)
{
	(void)set;
	return NVML_ERROR_NOT_SUPPORTED;
}

nvmlReturn_t nvmlDeviceRegisterEvents(nvmlDevice_t device, unsigned long long eventTypes,
				      nvmlEventSet_t set)
{
	(void)device; (void)eventTypes; (void)set;
	return NVML_ERROR_NOT_SUPPORTED;
}

nvmlReturn_t nvmlEventSetFree(nvmlEventSet_t set)
{
	(void)set;
	return NVML_ERROR_NOT_SUPPORTED;
}

/* ---- deliberately-stubbed metrics (graceful NOT_SUPPORTED) --------------- */
nvmlReturn_t nvmlDeviceGetTemperature(nvmlDevice_t device, int sensorType, unsigned int *temp)
{
	(void)device; (void)sensorType; (void)temp;
	return NVML_ERROR_NOT_SUPPORTED;
}

nvmlReturn_t nvmlDeviceGetPowerUsage(nvmlDevice_t device, unsigned int *milliwatts)
{
	(void)device; (void)milliwatts;
	return NVML_ERROR_NOT_SUPPORTED;
}

nvmlReturn_t nvmlDeviceGetUtilizationRates(nvmlDevice_t device, void *utilization)
{
	(void)device; (void)utilization;
	return NVML_ERROR_NOT_SUPPORTED;
}

nvmlReturn_t nvmlDeviceGetComputeMode(nvmlDevice_t device, int *mode)
{
	(void)device; (void)mode;
	return NVML_ERROR_NOT_SUPPORTED;
}

nvmlReturn_t nvmlDeviceGetMaxMigDeviceCount(nvmlDevice_t device, unsigned int *count)
{
	if (dev_index(device) < 0)
		return NVML_ERROR_INVALID_ARGUMENT;
	if (count)
		*count = 0;
	return NVML_SUCCESS;
}
