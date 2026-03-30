#ifndef FED_FAULT_INJECTION_H
#define FED_FAULT_INJECTION_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Simulate bit-rot in a buffer */
static inline void inject_bit_rot(void *buffer, size_t size, double probability) {
    uint8_t *ptr = (uint8_t *)buffer;
    for (size_t i = 0; i < size; i++) {
        if (((double)rand() / RAND_MAX) < probability) {
            ptr[i] ^= (1 << (rand() % 8));
        }
    }
}

/* Simulate dropped packets by clearing segments of a buffer */
static inline void inject_dropped_packets(void *buffer, size_t size, size_t segment_size, double probability) {
    uint8_t *ptr = (uint8_t *)buffer;
    for (size_t i = 0; i < size; i += segment_size) {
        if (((double)rand() / RAND_MAX) < probability) {
            size_t actual_size = (i + segment_size > size) ? (size - i) : segment_size;
            memset(ptr + i, 0, actual_size);
        }
    }
}

#endif
