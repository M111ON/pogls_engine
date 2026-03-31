/*
 * test_llm_adapter.c — Survey test for pogls_llm_adapter.h
 *
 * Build: gcc test_llm_adapter.c -o test_llm -lcurl -lpthread
 * Usage: ./test_llm <tunnel_url> <model_name>
 *
 * Example:
 *   ./test_llm https://xxx.trycloudflare.com qwen2.5-0.5b-instruct
 */

#include <stdio.h>
#include <stdlib.h>
#include "pogls_llm_adapter.h"

int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("Usage: %s <url> <model_name>\n", argv[0]);
        return 1;
    }

    LLMAdapterCtx ctx;
    llm_adapter_init(&ctx);
    llm_adapter_add(&ctx, argv[1], argv[2]);

    char out[LLM_MAX_RESPONSE_LEN];

    /* Test 1: single lane dispatch */
    printf("=== Test 1: lane dispatch ===\n");
    int ok = llm_dispatch_lane(&ctx, 0, "what is 2+2?", 30, out, sizeof(out));
    printf("ok=%d response: %s\n\n", ok, out);

    /* Test 2: cache hit */
    printf("=== Test 2: cache hit ===\n");
    ok = llm_dispatch_lane(&ctx, 0, "what is 2+2?", 30, out, sizeof(out));
    printf("ok=%d (should be cache hit)\n\n", ok);

    /* Test 3: parallel dispatch (same endpoint, 2 workers) */
    printf("=== Test 3: parallel dispatch ===\n");
    uint32_t ids[2] = {0, 0};
    const char *prompts[2] = {"hello", "what is pi?"};
    char results[2][LLM_MAX_RESPONSE_LEN];
    uint32_t mask = llm_dispatch_parallel(&ctx, ids, prompts, 2, 30, results);
    printf("success_mask: 0x%x\n\n", mask);

    llm_adapter_stats(&ctx);
    return 0;
}
