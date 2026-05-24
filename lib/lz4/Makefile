ccflags-y += -O2 \
     -DLZ4_FREESTANDING=0 \
     -DLZ4_FAST_DEC_LOOP=1

obj-y += lz4.o
CFLAGS_lz4.o  += -DLZ4_HEAPMODE=1

obj-$(CONFIG_ARM64) += $(addprefix lz4armv8/, lz4accel.o lz4armv8.o)
