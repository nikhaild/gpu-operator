/* SPDX-License-Identifier: MIT */
/*
 * Unit tests for the mock NVML library (nvml/nvml_mock.c), TESTING.md §1.1.
 * Linked directly against nvml_mock.c; expected values injected from versions.mk
 * via -D (NVML_DEFS) so test and library share one source of truth (PLAN §9).
 */
#include <stdio.h>
#include <string.h>
#include "nvml_mock.h"

#ifndef NV_PRODUCT_NAME
#error "NVML_DEFS (from versions.mk) must be provided via -D"
#endif

static int g_fail, g_ok;

#define OK(cond, msg) do { \
	if (cond) { g_ok++; } \
	else { g_fail++; printf("FAIL: %s\n", msg); } } while (0)

#define EQ_U(exp, act, msg) do { \
	unsigned long long _e=(unsigned long long)(exp), _a=(unsigned long long)(act); \
	if (_e==_a) { g_ok++; } \
	else { g_fail++; printf("FAIL: %s (expected %llu got %llu)\n", msg, _e, _a); } } while (0)

#define EQ_S(exp, act, msg) do { \
	if (strcmp((exp),(act))==0) { g_ok++; } \
	else { g_fail++; printf("FAIL: %s (expected '%s' got '%s')\n", msg, exp, act); } } while (0)

int main(void)
{
	unsigned int count = 0, i;
	char buf[128];

	OK(nvmlInit_v2() == NVML_SUCCESS, "nvmlInit_v2 returns SUCCESS");
	OK(nvmlInit_v2() == NVML_SUCCESS, "nvmlInit_v2 is re-entrant");

	/* System info */
	OK(nvmlSystemGetDriverVersion(buf, sizeof(buf)) == NVML_SUCCESS, "GetDriverVersion ok");
	EQ_S(NV_DRIVER_VERSION, buf, "driver version string");
	OK(nvmlSystemGetNVMLVersion(buf, sizeof(buf)) == NVML_SUCCESS, "GetNVMLVersion ok");
	EQ_S(NVML_VERSION, buf, "nvml version string");
	{
		int cuda = 0;
		OK(nvmlSystemGetCudaDriverVersion(&cuda) == NVML_SUCCESS, "GetCudaDriverVersion ok");
		EQ_U(NV_CUDA_VERSION_INT, cuda, "cuda driver version int");
	}

	/* Count */
	OK(nvmlDeviceGetCount_v2(&count) == NVML_SUCCESS, "GetCount_v2 ok");
	EQ_U(MOCK_GPU_COUNT, count, "device count");

	/* Out-of-range handle */
	{
		nvmlDevice_t bad;
		OK(nvmlDeviceGetHandleByIndex_v2(count, &bad) != NVML_SUCCESS,
		   "GetHandleByIndex out-of-range fails cleanly");
	}

	for (i = 0; i < count; i++) {
		nvmlDevice_t dev;
		nvmlMemory_t mem;
		nvmlPciInfo_t pci;
		nvmlDeviceArchitecture_t arch;
		nvmlBrandType_t brand;
		unsigned int minor = 999, cur = 9, pend = 9;
		int maj = 0, min = 0;
		char uuid[NVML_DEVICE_UUID_BUFFER_SIZE];

		OK(nvmlDeviceGetHandleByIndex_v2(i, &dev) == NVML_SUCCESS, "GetHandleByIndex ok");

		OK(nvmlDeviceGetName(dev, buf, sizeof(buf)) == NVML_SUCCESS, "GetName ok");
		EQ_S(NV_PRODUCT_NAME, buf, "product name");

		OK(nvmlDeviceGetUUID(dev, uuid, sizeof(uuid)) == NVML_SUCCESS, "GetUUID ok");
		OK(strncmp(uuid, "GPU-", 4) == 0, "UUID starts with GPU-");

		OK(nvmlDeviceGetMinorNumber(dev, &minor) == NVML_SUCCESS, "GetMinorNumber ok");
		EQ_U(i, minor, "minor number == index");

		OK(nvmlDeviceGetMemoryInfo(dev, &mem) == NVML_SUCCESS, "GetMemoryInfo ok");
		EQ_U((unsigned long long)NV_MEMORY_MIB * 1024ULL * 1024ULL, mem.total, "memory total");
		OK(mem.free <= mem.total, "memory free <= total");

		OK(nvmlDeviceGetCudaComputeCapability(dev, &maj, &min) == NVML_SUCCESS, "GetCC ok");
		EQ_U(NV_COMPUTE_MAJOR, maj, "compute major");
		EQ_U(NV_COMPUTE_MINOR, min, "compute minor");

		OK(nvmlDeviceGetArchitecture(dev, &arch) == NVML_SUCCESS, "GetArchitecture ok");
		EQ_U(NV_NVML_ARCH, arch, "architecture enum");

		OK(nvmlDeviceGetBrand(dev, &brand) == NVML_SUCCESS, "GetBrand ok");
		EQ_U(NV_NVML_BRAND, brand, "brand enum");

		OK(nvmlDeviceGetPciInfo_v3(dev, &pci) == NVML_SUCCESS, "GetPciInfo_v3 ok");
		EQ_U(((unsigned)NV_DEVICE_ID << 16) | (unsigned)NV_VENDOR_ID, pci.pciDeviceId, "pciDeviceId");
		EQ_U(MOCK_PCI_DOMAIN, pci.domain, "pci domain");
		OK(strlen(pci.busId) > 0, "pci busId non-empty");

		OK(nvmlDeviceGetMigMode(dev, &cur, &pend) == NVML_SUCCESS ||
		   nvmlDeviceGetMigMode(dev, &cur, &pend) == NVML_ERROR_NOT_SUPPORTED,
		   "GetMigMode ok-or-unsupported");
	}

	/* UUID uniqueness across devices */
	if (count >= 2) {
		nvmlDevice_t d0, d1;
		char u0[NVML_DEVICE_UUID_BUFFER_SIZE], u1[NVML_DEVICE_UUID_BUFFER_SIZE];
		nvmlDeviceGetHandleByIndex_v2(0, &d0);
		nvmlDeviceGetHandleByIndex_v2(1, &d1);
		nvmlDeviceGetUUID(d0, u0, sizeof(u0));
		nvmlDeviceGetUUID(d1, u1, sizeof(u1));
		OK(strcmp(u0, u1) != 0, "UUIDs unique across devices");
	}

	/* Event APIs degrade to NOT_SUPPORTED (device-plugin tolerates this) */
	{
		nvmlEventSet_t set;
		OK(nvmlEventSetCreate(&set) == NVML_ERROR_NOT_SUPPORTED, "EventSetCreate NOT_SUPPORTED");
	}

	/* Graceful stub: a defined-but-unsupported metric returns NOT_SUPPORTED */
	{
		nvmlDevice_t dev;
		unsigned int mw = 0;
		if (count > 0) {
			nvmlDeviceGetHandleByIndex_v2(0, &dev);
			OK(nvmlDeviceGetPowerUsage(dev, &mw) == NVML_ERROR_NOT_SUPPORTED,
			   "GetPowerUsage stub returns NOT_SUPPORTED");
		}
	}

	OK(nvmlErrorString(NVML_ERROR_NOT_SUPPORTED) != 0, "errorString non-null");
	OK(nvmlShutdown() == NVML_SUCCESS, "nvmlShutdown ok");

	printf("-----------------------------------------------\n");
	if (g_fail == 0) { printf("nvml: ALL PASSED (%d checks)\n", g_ok); return 0; }
	printf("nvml: FAILURES %d (passed %d)\n", g_fail, g_ok);
	return 1;
}
