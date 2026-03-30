#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "../core/geo_config.h"
#include "../core/pogls_pipeline_wire.h"

int main() {
    printf("=== POGLS Master Pipeline Wire Test ===\n");

    GeoSeed seed = {0xAAAAAAAAAAAAAAAAULL, 0x5555555555555555ULL};
    uint64_t bundle[GEO_BUNDLE_WORDS];
    
    /* Simulate CPU derive bundle as in geomatrix_gpu_wire.cu */
    uint64_t mix = seed.gen2 ^ seed.gen3;
    bundle[0] = seed.gen2;
    bundle[1] = seed.gen3;
    bundle[2] = seed.gen2 ^ seed.gen3;
    bundle[3] = ~seed.gen2;
    bundle[4] = ~seed.gen3;
    bundle[5] = ~(seed.gen2 ^ seed.gen3);
    bundle[6] = (mix << 12) | (mix >> (64-12));
    bundle[7] = (mix << 18) | (mix >> (64-18));
    bundle[8] = (mix << 24) | (mix >> (64-24));

    PipelineWire pw;
    pipeline_wire_init(&pw, seed, bundle);

    printf("Pipeline initialized: %s\n", geo_net_state_name(&pw.geo.gn));

    uint64_t addr = 12345;
    uint64_t value = 0xDEADBEEF;
    uint8_t slot_hot = 0;
    PipelineResult res;

    printf("Processing single op (addr=%llu)...\n", (unsigned long long)addr);
    uint8_t fail = pipeline_wire_process(&pw, addr, value, slot_hot, &res);

    printf("Result:\n");
    printf("  spoke: %u\n", res.addr.spoke);
    printf("  slot:  %u\n", res.addr.slot);
    printf("  phase: %u\n", res.pkt.phase);
    printf("  sig32: 0x%08X\n", res.pkt.sig32);
    printf("  sig_ok: %u\n", res.sig_ok);
    printf("  audit_fail: %u\n", res.audit_fail);
    printf("  fail status: %u\n", fail);

    pipeline_wire_status(&pw);

    if (fail == 0 && res.sig_ok == 1) {
        printf("\n✅ Pipeline Wire test: PASSED\n");
    } else {
        printf("\n❌ Pipeline Wire test: FAILED\n");
        return 1;
    }

    return 0;
}
