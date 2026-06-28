# Build the mock NVML shared library: libnvidia-ml.so.1
# Output goes to ../build/ so the runtime image can package it (PLAN §5.2).
# Compiled inside the builder container in the real pipeline, but it also builds
# with a plain host compiler (no kernel headers needed).

MOCK_NVML_DIR := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
include $(MOCK_NVML_DIR)../versions.mk

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Werror -std=gnu11 -fPIC
SONAME  := libnvidia-ml.so.1
OUTDIR  ?= $(MOCK_NVML_DIR)../build
OUT     := $(OUTDIR)/$(SONAME)

.PHONY: all clean
all: $(OUT)

$(OUT): nvml_mock.c nvml_mock.h | $(OUTDIR)
	$(CC) $(CFLAGS) $(NVML_DEFS) -shared -Wl,-soname,$(SONAME) -o $@ nvml_mock.c
	@echo ">> built $@ (soname $(SONAME))"

$(OUTDIR):
	mkdir -p $(OUTDIR)

clean:
	rm -f $(OUT)
