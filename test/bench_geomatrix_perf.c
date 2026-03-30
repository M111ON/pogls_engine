#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include "gpu/pogls_geomatrix.h"

#define BENCH_ITERATIONS 1000000 /* 10 Million Batches */

double get_time_sec() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main() {
    srand(time(NULL));
    GeomatrixStats stats = {0, 0, 0};
    GeomatrixPath batch[GEOMATRIX_PATHS];

    /* Pre-fill batch with random data to simulate real-world noise */
    for (int p = 0; p < GEOMATRIX_PATHS; p++) {
        batch[p].packed = rand();
        batch[p].addr = rand();
    }

    printf("=== POGLS Geomatrix Performance Benchmark (G4400 Target) ===\n");
    printf("Processing %d batches (%d million cells)...\n\n", 
           BENCH_ITERATIONS, (BENCH_ITERATIONS * GEOMATRIX_PATHS) / 1000000);

    double start_time = get_time_sec();

    /* Start Heavy Loop */
    for (int i = 0; i < BENCH_ITERATIONS; i++) {
        /* 
         * Simulate a "Geometric Signal":
         * 80% of the time, provide valid data that follows the invariant.
         * 20% of the time, provide random noise.
         */
        bool is_noise = (i % 5 == 0); 

        for (int p = 0; p < GEOMATRIX_PATHS; p++) {
            if (!is_noise) {
                /* Valid Signal: hil % 17 == addr % 17 */
                uint32_t base = rand() % 1000;
                batch[p].packed = base * 17; 
                batch[p].addr   = base * 17;
            } else {
                /* Random Noise */
                batch[p].packed = rand();
                batch[p].addr   = rand();
            }
        }
        
        geomatrix_batch_validate(batch, &stats);
    }

    double end_time = get_time_sec();
    double total_time = end_time - start_time;
    double batches_per_sec = BENCH_ITERATIONS / total_time;
    double cells_per_sec = (BENCH_ITERATIONS * GEOMATRIX_PATHS) / total_time;

    printf("--- Execution Results ---\n");
    printf("Total Time:         %.4f seconds\n", total_time);
    printf("Throughput (Batch): %.2f M-batches/sec\n", batches_per_sec / 1e6);
    printf("Throughput (Cell):  %.2f M-cells/sec\n", cells_per_sec / 1e6);
    printf("Avg Latency/Cell:   %.2f nanoseconds\n", (total_time / (BENCH_ITERATIONS * GEOMATRIX_PATHS)) * 1e9);

    printf("\n--- Filtering Results ---\n");
    printf("Stable Hits:   %lu\n", stats.stable_hits);
    printf("Rejects:       %lu\n", stats.unstable_rejects);
    printf("Stability Rate: %.6f%%\n", (double)stats.stable_hits / stats.total_batches * 100.0);

    return 0;
}
