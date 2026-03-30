# Makefile — libpogls_v4.so
# Session 8
#
# Targets:
#   make          → build libpogls_v4.so
#   make cpu      → CPU-only (G4400, no CUDA)
#   make gpu      → Colab T4 (with QRPN_GPU_ENABLED)
#   make clean

CC      = gcc
CFLAGS  = -std=c99 -O2 -fPIC -Wall -Wextra \
          -fvisibility=hidden \
          -I. -Icore -Igpu
LDFLAGS = -shared -lpthread

SRCS = pogls_v4_api.c \
       core/pogls_delta.c \
       core/pogls_delta_world_b.c \
       core/pogls_delta_ext.c \
       core/pogls_snapshot.c \
       core/pogls_audit.c \
       core/pogls_compute_lut.c \
       core/pogls_compute_stack.c \
       core/angular_mapper_v36.c

OUT = libpogls_v4.so

.PHONY: all cpu gpu clean

all: cpu

cpu:
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(OUT) $(SRCS)
	@echo "[OK] $(OUT) built (CPU-only)"

gpu:
	nvcc -Xcompiler "$(CFLAGS) -DQRPN_GPU_ENABLED" \
	     $(LDFLAGS) -o $(OUT) $(SRCS) \
	     gpu/pogls_qrpn_persistent.cu
	@echo "[OK] $(OUT) built (GPU enabled)"

clean:
	rm -f $(OUT) *.o core/*.o
