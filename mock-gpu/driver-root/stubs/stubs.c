/* SPDX-License-Identifier: MIT */
/*
 * stubs.c - Minimal stub for libcuda.so.1.
 *
 * libnvidia-container expects a small set of NVIDIA driver libraries in the
 * driver root to mount into operand containers. We never run real CUDA, so this
 * provides just enough of a valid ELF shared object (correct soname + a couple
 * of no-op entry points) to satisfy the injection/mount list. No GPU work is
 * possible. Built with -Wl,-soname,libcuda.so.1.
 */

typedef int CUresult;           /* real CUDA: 0 == CUDA_SUCCESS */
#define CUDA_SUCCESS            0
#define CUDA_ERROR_NO_DEVICE    100

CUresult cuInit(unsigned int flags)
{
	(void)flags;
	/* No real device; report no device rather than pretending success. */
	return CUDA_ERROR_NO_DEVICE;
}

CUresult cuDriverGetVersion(int *version)
{
	if (version)
		*version = NV_CUDA_VERSION_INT;
	return CUDA_SUCCESS;
}
