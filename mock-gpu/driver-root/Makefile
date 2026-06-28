# Build stub driver libraries (currently libcuda.so.1) into ../build/.
# These are mounted into operand containers by libnvidia-container; we never run
# real CUDA. (PLAN §5.3)

MOCK_DR_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
include $(MOCK_DR_DIR)../versions.mk

CC     ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=gnu11 -fPIC
OUTDIR ?= $(MOCK_DR_DIR)../build

STUBS := $(OUTDIR)/libcuda.so.1

.PHONY: all clean
all: $(STUBS)

$(OUTDIR)/libcuda.so.1: stubs/stubs.c | $(OUTDIR)
	$(CC) $(CFLAGS) -DNV_CUDA_VERSION_INT=$(NV_CUDA_VERSION_INT) \
		-shared -Wl,-soname,libcuda.so.1 -o $@ stubs/stubs.c
	@echo ">> built $@"

$(OUTDIR):
	mkdir -p $(OUTDIR)

clean:
	rm -f $(STUBS)
