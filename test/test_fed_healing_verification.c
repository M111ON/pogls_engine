#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include "gpu/pogls_federation.h"
#include "test/fed_fault_injection.h"

int main() {
    srand(time(NULL));
    FederationCtx f;
    
    /* Mock vault directory */
    system("mkdir -p /tmp/fed_vault");
    
    if (fed_init(&f, "/tmp/fed_vault") != 0) {
        printf("Failed to init federation\n");
        return 1;
    }
    
    printf("--- POGLS Federation Healing Verification Test ---\n");
    
    uint32_t hil = 108;
    uint32_t lane = 108 % 54;
    uint32_t iso = 0;
    uint32_t packed = hil | (lane << 20) | (iso << 26);
    uint64_t addr = 12345;
    uint64_t val = 0xDEADBEEF;

    /* 0. Warm-up (8 ops) */
    printf("Warming up ghost maturity (8 ops)...\n");
    for (int i = 0; i < 8; i++) {
        fed_write(&f, packed, addr, val);
    }
    
    /* 1. Normal Case */
    GateResult res = fed_write(&f, packed, addr, val);
    printf("Normal write: res=%d, passed=%lu, healed=%lu\n", res, f.gate.passed, f.healed_count);
    assert(res == GATE_PASS);
    
    /* 2. Corrupted Case (1-bit flip in lane) */
    printf("Injecting deliberate 1-bit flip in lane field...\n");
    uint32_t corrupted = packed ^ (1 << 20); /* Flip first bit of lane */
    
    res = fed_write(&f, corrupted, addr, val);
    printf("Corrupted write (healable): res=%d, passed=%lu, healed=%lu\n", res, f.gate.passed, f.healed_count);
    
    if (f.healed_count > 0 && res == GATE_PASS) {
        printf("SUCCESS: Corrupted packet was HEALED and PASSED.\n");
    } else {
        printf("FAILURE: Packet was not healed. res=%d, healed=%lu\n", res, f.healed_count);
    }

    fed_stats(&f);
    fed_close(&f);
    
    return 0;
}
