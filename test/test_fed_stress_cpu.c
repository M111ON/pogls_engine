#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include "gpu/pogls_federation.h"
#include "test/fed_fault_injection.h"

#define TOTAL_OPS 10000
#define PHASE_OPS 2000

void run_phase(FederationCtx *f, const char *name, double bit_rot_prob, int force_lane_error) {
    printf("\n>>> STARTING PHASE: %s (Rot Prob: %.2f, Lane Err: %s)\n", 
           name, bit_rot_prob, force_lane_error ? "YES" : "NO");
    
    for (int i = 0; i < PHASE_OPS; i++) {
        uint32_t hil = rand() % 1000000;
        uint32_t lane = hil % 54;
        uint32_t iso = 0;
        
        if (force_lane_error && (rand() % 10 == 0)) {
            lane = (lane + 1) % 54; /* Deliberate error */
        }
        
        uint32_t packed = hil | (lane << 20) | (iso << 26);
        uint64_t addr = rand();
        uint64_t val = rand();
        
        if (bit_rot_prob > 0) {
            inject_bit_rot(&packed, sizeof(packed), bit_rot_prob);
            inject_bit_rot(&addr, sizeof(addr), bit_rot_prob);
            inject_bit_rot(&val, sizeof(val), bit_rot_prob);
        }
        
        fed_write(f, packed, addr, val);
        
        /* Simulate V4 drain to prevent hard-cap drop in stress test */
        if (i % 5 == 0) fed_drain(f, 1);
    }
    
    fed_stats(f);
}

int main() {
    srand(time(NULL));
    FederationCtx f;
    
    system("mkdir -p /tmp/fed_stress");
    if (fed_init(&f, "/tmp/fed_stress") != 0) {
        printf("Failed to init\n");
        return 1;
    }

    printf("=== POGLS Federation CPU Stress Test (Multi-Scenario) ===\n");

    /* Phase 1: Baseline Normal */
    run_phase(&f, "Baseline Normal", 0.0, 0);

    /* Phase 2: Healable Lane Flips (10% of ops have wrong lane) */
    run_phase(&f, "Healable Lane Flips", 0.0, 1);

    /* Phase 3: Light Random Bit-rot (1%) */
    run_phase(&f, "Light Bit-rot", 0.01, 0);

    /* Phase 4: Heavy Bit-rot (5%) - Testing Robustness/Drop */
    run_phase(&f, "Heavy Bit-rot", 0.05, 0);

    /* Phase 5: High-Speed Burst (Trigger Backpressure) */
    printf("\n>>> STARTING PHASE: High-Speed Burst (No Drain)\n");
    for (int i = 0; i < 5000; i++) {
        uint32_t hil = 108;
        uint32_t packed = hil | ((hil % 54) << 20);
        fed_write(&f, packed, i, i);
    }
    fed_stats(&f);

    printf("\n=== FINAL STRESS REPORT ===\n");
    printf("Total Ops: %lu\n", f.op_count);
    printf("Successfully Healed: %lu\n", f.healed_count);
    printf("Healing Success Rate: %.2f%%\n", 
           (double)f.healed_count / (f.gate.dropped + f.healed_count + 1) * 100.0);

    fed_close(&f);
    return 0;
}
