#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include "gpu/pogls_federation.h"
#include "test/fed_fault_injection.h"

int main() {
    srand(time(NULL));
    GateStats gs = {0, 0, 0, FED_GATE_MAGIC};
    
    printf("--- POGLS Federation Healing Baseline Test ---\n");
    
    /* 1. Normal Case: iso=0, lane matches hilbert%54 */
    uint32_t hil = 108;
    uint32_t lane = 108 % 54;
    uint32_t iso = 0;
    uint32_t packed = hil | (lane << 20) | (iso << 26);
    
    GateResult res = fed_gate(packed, 10, &gs);
    printf("Normal check: res=%d, passed=%lu, dropped=%lu\n", res, gs.passed, gs.dropped);
    assert(res == GATE_PASS);
    
    /* 2. Simulated Bit-rot Case */
    printf("Injecting bit-rot into packed data...\n");
    int failures = 0;
    for(int i=0; i<1000; i++) {
        uint32_t corrupted = packed;
        inject_bit_rot(&corrupted, sizeof(corrupted), 0.05); // 5% bit-rot
        
        GateResult c_res = fed_gate(corrupted, 10, &gs);
        if (c_res == GATE_DROP) {
            failures++;
        }
    }
    
    printf("Bit-rot results: %d dropped out of 1000 trials\n", failures);
    if (failures > 0) {
        printf("BASELINE VERIFIED: Bit-rot causes dropped packets in current logic.\n");
    } else {
        printf("WARNING: Bit-rot did not cause drops. Check injection parameters.\n");
    }

    return 0;
}
