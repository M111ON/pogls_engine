#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include "gpu/pogls_geomatrix.h"

void print_result(const char *test_name, bool result, int score) {
    printf("[%s] Score: %d | Status: %s\n", 
           test_name, score, result ? "✅ STABLE" : "❌ REJECTED");
}

int main() {
    srand(time(NULL));
    GeomatrixStats stats = {0, 0, 0};
    GeomatrixPath batch[GEOMATRIX_PATHS];

    printf("=== POGLS Geomatrix 18-Way Gate Logic Test ===\n");
    printf("Window: %d - %d | Target: 136 (17x8)\n\n", 
           GEOMATRIX_WINDOW_MIN, GEOMATRIX_WINDOW_MAX);

    /* Test 1: Perfect Geometry (17 correct, 1 incorrect) */
    for (int i = 0; i < 17; i++) {
        batch[i].packed = i;    /* hil = i */
        batch[i].addr = i;      /* addr = i (match!) */
    }
    batch[17].packed = 100;     /* hil = 100 */
    batch[17].addr = 0;         /* addr = 0 (mismatch) */
    
    bool res = geomatrix_batch_validate(batch, &stats);
    print_result("Perfect (17/18)", res, 136);

    /* Test 2: Minimum Stable (16 correct, 2 incorrect) */
    for (int i = 0; i < 16; i++) {
        batch[i].packed = i; batch[i].addr = i;
    }
    for (int i = 16; i < 18; i++) {
        batch[i].packed = 100; batch[i].addr = 0;
    }
    res = geomatrix_batch_validate(batch, &stats);
    print_result("Min Stable (16/18)", res, 128);

    /* Test 3: Maximum Stable (18 correct - All match) */
    for (int i = 0; i < 18; i++) {
        batch[i].packed = i; batch[i].addr = i;
    }
    res = geomatrix_batch_validate(batch, &stats);
    print_result("Max Stable (18/18)", res, 144);

    /* Test 4: Unstable (15 correct - Too many errors) */
    for (int i = 0; i < 15; i++) {
        batch[i].packed = i; batch[i].addr = i;
    }
    for (int i = 15; i < 18; i++) {
        batch[i].packed = 100; batch[i].addr = 0;
    }
    res = geomatrix_batch_validate(batch, &stats);
    print_result("Unstable (15/18)", res, 120);

    /* Test 5: Random Noise Simulation */
    printf("\n--- Simulating 10,000 Random Batches ---\n");
    for (int i = 0; i < 10000; i++) {
        for (int p = 0; p < GEOMATRIX_PATHS; p++) {
            batch[p].packed = rand();
            batch[p].addr = rand();
        }
        geomatrix_batch_validate(batch, &stats);
    }

    printf("\nFinal Geomatrix Report:\n");
    printf("Total Batches: %lu\n", stats.total_batches);
    printf("Stable Hits:   %lu\n", stats.stable_hits);
    printf("Rejects:       %lu\n", stats.unstable_rejects);
    printf("Noise Filtering Rate: %.2f%%\n", 
           (double)stats.unstable_rejects / stats.total_batches * 100.0);

    return 0;
}
