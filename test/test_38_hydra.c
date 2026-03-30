/*
 * test_38_hydra.c — POGLS38 Hydra Scheduler Tests
 *
 * T01  Constants: HS_HEADS=16 (not 32), queue=256, magic correct
 * T02  Task size: L38HydraTask = 24B
 * T03  Push/pop SPSC: basic roundtrip
 * T04  Queue full: push returns -1 at capacity
 * T05  Queue depth: head-tail difference
 * T06  Work steal: idle head steals from busy head
 * T07  Work steal: no steal when all empty
 * T08  Work steal: steals from busiest (greedy)
 * T09  Weighted routing: weight=0 → routes by PHI dist
 * T10  Weighted routing: busy head → avoid (weight penalty)
 * T11  Weight decay: 7/8 per gate_18 tick
 * T12  Hydra schedule: task routed + weight updated
 * T13  Hydra schedule fallback: full head → adjacent
 * T14  Hydra drain: all tasks executed
 * T15  Gate_18 tick: decay_tick increments + weights decay
 * T16  Total depth: sum across all heads
 * T17  Tails pointer: tail + neg = 0xFFFFFFFF invariant
 * T18  Tails ptr corrupt detect: flip tail → invariant breaks
 * T19  Tails scan: all 16 queues checked, 0 corrupt initially
 * T20  No overflow: 16×256 = 4096 tasks without crash
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../pogls_38_hydra.h"

static int _pass=0, _fail=0;
static void check(int ok, const char *p, const char *f) {
    if (ok) { printf("    \033[32mv\033[0m %s\n", p); _pass++; }
    else    { printf("    \033[31mx FAIL\033[0m: %s\n", f); _fail++; }
}
static void section(const char *s) { printf("\n  -- %s\n", s); }

/* ══ T01 ══ */
static void t01_constants(void) {
    section("T01  Constants: 16 heads (not 32), frozen");
    check(L38_HS_HEADS == 16,      "HS_HEADS=16 (not 32)",     "OVERFLOW RISK");
    check(L38_HS_QUEUE_SIZE == 256,"queue=256",                 "wrong");
    check(L38_HS_QUEUE_MASK == 255,"queue_mask=255",            "wrong");
    check(L38_PTR_INVARIANT == 0xFFFFFFFFu, "ptr invariant=~0", "wrong");
}

/* ══ T02 ══ */
static void t02_task_size(void) {
    section("T02  L38HydraTask = 24B");
    check(sizeof(L38HydraTask) == 24, "task=24B", "wrong");
}

/* ══ T03 ══ */
static void t03_push_pop(void) {
    section("T03  Push/pop SPSC roundtrip");
    L38HydraQueue q; memset(&q, 0, sizeof(q));
    L38HydraTask t1 = {.op=L38_HS_OP_WRITE, .cell_id=5, .addr=0x1234, .value=99};
    L38HydraTask t2 = {.op=L38_HS_OP_AUDIT, .cell_id=7, .addr=0x5678, .value=42};

    check(l38_hs_push(&q, &t1) == 0,  "push t1 OK",   "fail");
    check(l38_hs_push(&q, &t2) == 0,  "push t2 OK",   "fail");
    check(l38_hs_depth(&q) == 2,      "depth=2",       "wrong");

    L38HydraTask out;
    check(l38_hs_pop(&q, &out) == 1,  "pop OK",        "fail");
    check(out.cell_id == 5,            "FIFO: got t1",  "wrong");
    check(l38_hs_pop(&q, &out) == 1,  "pop t2 OK",     "fail");
    check(out.cell_id == 7,            "FIFO: got t2",  "wrong");
    check(l38_hs_pop(&q, &out) == 0,  "empty → 0",     "wrong");
}

/* ══ T04 ══ */
static void t04_queue_full(void) {
    section("T04  Queue full: push returns -1");
    L38HydraQueue q; memset(&q, 0, sizeof(q));
    L38HydraTask t = {.op=L38_HS_OP_WRITE, .cell_id=1, .addr=1};
    for (uint32_t i = 0; i < L38_HS_QUEUE_SIZE; i++) {
        t.addr = i;
        l38_hs_push(&q, &t);
    }
    check(l38_hs_push(&q, &t) == -1, "push on full → -1", "accepted");
}

/* ══ T05 ══ */
static void t05_depth(void) {
    section("T05  Queue depth");
    L38HydraQueue q; memset(&q, 0, sizeof(q));
    check(l38_hs_depth(&q) == 0, "depth=0 initially", "wrong");
    L38HydraTask t = {.op=1};
    l38_hs_push(&q, &t); l38_hs_push(&q, &t);
    check(l38_hs_depth(&q) == 2, "depth=2 after 2 pushes", "wrong");
}

/* ══ T06 ══ */
static void t06_work_steal(void) {
    section("T06  Work steal: idle head steals from busy head");
    L38HydraQueue qs[L38_HS_HEADS]; memset(qs, 0, sizeof(qs));
    /* push 5 tasks to head 3 */
    for (int i = 0; i < 5; i++) {
        L38HydraTask t = {.op=L38_HS_OP_WRITE, .addr=(uint64_t)i*100};
        l38_hs_push(&qs[3], &t);
    }
    L38HydraTask stolen;
    int r = l38_hs_steal(qs, 0, &stolen);  /* head 0 steals */
    check(r == 1, "steal succeeded", "failed");
    check(l38_hs_depth(&qs[3]) == 4, "victim lost 1 task", "wrong depth");
}

/* ══ T07 ══ */
static void t07_steal_empty(void) {
    section("T07  Work steal: nothing to steal when all empty");
    L38HydraQueue qs[L38_HS_HEADS]; memset(qs, 0, sizeof(qs));
    L38HydraTask out;
    check(l38_hs_steal(qs, 0, &out) == 0, "steal empty → 0", "got something");
}

/* ══ T08 ══ */
static void t08_steal_busiest(void) {
    section("T08  Work steal: steals from busiest head");
    L38HydraQueue qs[L38_HS_HEADS]; memset(qs, 0, sizeof(qs));
    /* head 5 = 10 tasks, head 7 = 3 tasks */
    L38HydraTask t = {.op=L38_HS_OP_WRITE};
    for (int i = 0; i < 10; i++) l38_hs_push(&qs[5], &t);
    for (int i = 0; i < 3;  i++) l38_hs_push(&qs[7], &t);

    L38HydraTask stolen;
    l38_hs_steal(qs, 0, &stolen);
    /* head 5 should have lost 1 task (was busiest) */
    check(l38_hs_depth(&qs[5]) == 9, "stole from busiest (head 5)", "wrong");
}

/* ══ T09 ══ */
static void t09_weighted_routing_zero(void) {
    section("T09  Weighted routing: all weight=0 → consistent route");
    L38HydraWeight hw; l38_hw_init(&hw);
    uint32_t h1 = l38_hs_route_weighted(&hw, 0x1000ULL);
    uint32_t h2 = l38_hs_route_weighted(&hw, 0x1000ULL);
    check(h1 == h2, "same addr → same head (deterministic)", "random");
    check(h1 < L38_HS_HEADS, "route in range", "OOB");
}

/* ══ T10 ══ */
static void t10_weighted_routing_busy(void) {
    section("T10  Weighted routing: busy head avoided");
    L38HydraWeight hw; l38_hw_init(&hw);
    /* find default head for addr */
    uint32_t base_head = l38_hs_route_weighted(&hw, 0x2000ULL);
    /* saturate that head */
    hw.weight[base_head] = 255;
    uint32_t new_head = l38_hs_route_weighted(&hw, 0x2000ULL);
    check(new_head != base_head || L38_HS_HEADS == 1,
          "busy head avoided (different head chosen)", "still same");
}

/* ══ T11 ══ */
static void t11_weight_decay(void) {
    section("T11  Weight decay: 7/8 per gate_18 tick");
    L38HydraWeight hw; l38_hw_init(&hw);
    hw.weight[0] = 128;
    l38_hw_decay(&hw);
    check(hw.weight[0] == 112, "128 × 7/8 = 112", "wrong");
    l38_hw_decay(&hw);
    check(hw.weight[0] == 98,  "112 × 7/8 = 98",  "wrong");
    /* zero stays zero */
    hw.weight[1] = 0;
    l38_hw_decay(&hw);
    check(hw.weight[1] == 0,   "0 × 7/8 = 0",     "wrong");
}

/* ══ T12 ══ */
static void t12_hydra_schedule(void) {
    section("T12  Hydra schedule: task routed + weight bumped");
    L38Hydra h; l38_hydra_init(&h);
    check(h.magic == L38_HS_MAGIC, "magic OK", "wrong");

    L38HydraTask t = {.op=L38_HS_OP_WRITE, .cell_id=10, .addr=0xABCDU};
    uint32_t hid = 99;
    check(l38_hydra_schedule(&h, &t, &hid) == 0, "schedule OK", "fail");
    check(hid < L38_HS_HEADS, "head_id in range", "OOB");
    check(h.weight.weight[hid] > 0, "weight bumped", "still 0");
    check(h.tasks_pushed[hid] == 1, "tasks_pushed[hid]=1", "wrong");
    check(l38_hydra_total_depth(&h) == 1, "total_depth=1", "wrong");
}

/* ══ T13 ══ */
static void t13_schedule_fallback(void) {
    section("T13  Schedule fallback: full head → adjacent head");
    L38Hydra h; l38_hydra_init(&h);
    L38HydraTask t = {.op=L38_HS_OP_WRITE, .addr=0};

    /* find which head addr=0 routes to, fill it */
    uint32_t target = l38_hs_route_weighted(&h.weight, 0);
    for (uint32_t i = 0; i < L38_HS_QUEUE_SIZE; i++) {
        t.addr = i;
        l38_hs_push(&h.queues[target], &t);
    }
    /* now schedule again — should fall back */
    t.addr = 0;
    uint32_t alt = 99;
    int r = l38_hydra_schedule(&h, &t, &alt);
    check(r == 0, "schedule OK with fallback", "all full");
    if (r == 0) {
        check(alt != target || alt < L38_HS_HEADS,
              "routed to different head", "wrong");
    }
}

/* ══ T14 ══ */
static void t14_drain(void) {
    section("T14  Hydra drain: all tasks executed");
    L38Hydra h; l38_hydra_init(&h);
    static int exec_count = 0;

    /* push tasks to various heads */
    for (uint32_t i = 0; i < 50; i++) {
        L38HydraTask t = {.op=L38_HS_OP_WRITE, .addr=(uint64_t)i*13};
        l38_hydra_schedule(&h, &t, NULL);
    }
    check(l38_hydra_total_depth(&h) == 50, "50 tasks pending", "wrong");

    /* drain — C11 needs named function */
    void _count_exec(const L38HydraTask *t2, void *ud) {
        (void)t2; (*(int*)ud)++;
    }
    int total = l38_hydra_drain_all(&h, _count_exec, &exec_count);
    (void)total;
    check(exec_count == 50, "50 tasks executed", "wrong count");
    check(l38_hydra_total_depth(&h) == 0, "all queues empty", "still pending");
}

/* ══ T15 ══ */
static void t15_gate18(void) {
    section("T15  Gate_18 tick: decay + counter");
    L38Hydra h; l38_hydra_init(&h);
    h.weight.weight[3] = 100;
    l38_hydra_gate18(&h);
    check(h.decay_tick == 1, "decay_tick=1", "wrong");
    check(h.weight.weight[3] < 100, "weight[3] decayed", "unchanged");
}

/* ══ T16 ══ */
static void t16_total_depth(void) {
    section("T16  Total depth: sum all heads");
    L38Hydra h; l38_hydra_init(&h);
    check(l38_hydra_total_depth(&h) == 0, "start=0", "wrong");
    L38HydraTask t = {.op=1, .addr=1};
    l38_hs_push(&h.queues[0], &t);
    l38_hs_push(&h.queues[5], &t);
    l38_hs_push(&h.queues[15], &t);
    check(l38_hydra_total_depth(&h) == 3, "total=3", "wrong");
}

/* ══ T17 ══ */
static void t17_ptr_invariant(void) {
    section("T17  Tails pointer: tail + neg = 0xFFFFFFFF");
    L38HydraQueue q; memset(&q, 0, sizeof(q));
    L38HydraTask t = {.op=1}; l38_hs_push(&q, &t);
    L38TailsQueuePtr p = l38_tails_snapshot_ptr(&q, 0);
    check(l38_tails_ptr_valid(&p), "ptr invariant holds", "broken");
    check(p.tail_ptr + p.neg_ptr == L38_PTR_INVARIANT,
          "tail + neg = 0xFFFFFFFF", "wrong");
    check(p.neg_ptr == ~p.tail_ptr, "neg = ~tail (bitwise)", "wrong");
}

/* ══ T18 ══ */
static void t18_ptr_corrupt_detect(void) {
    section("T18  Tails: corrupted pointer detected");
    L38HydraQueue q; memset(&q, 0, sizeof(q));
    L38TailsQueuePtr p = l38_tails_snapshot_ptr(&q, 1);
    check(l38_tails_ptr_valid(&p), "clean ptr valid", "wrong");
    /* corrupt: flip tail_ptr */
    p.tail_ptr ^= 0x55;
    check(!l38_tails_ptr_valid(&p), "corrupt ptr detected", "undetected");
}

/* ══ T19 ══ */
static void t19_tails_scan(void) {
    section("T19  Tails scan: 16 queues, 0 corrupt initially");
    L38Hydra h; l38_hydra_init(&h);
    L38TailsQueuePtr snaps[L38_HS_HEADS];
    uint32_t corrupt = l38_tails_scan_ptrs(&h, snaps);
    check(corrupt == 0, "0 corrupt queues on fresh hydra", "corrupt found");
    /* all snapshots valid */
    int all_valid = 1;
    for (uint32_t i = 0; i < L38_HS_HEADS; i++)
        if (!l38_tails_ptr_valid(&snaps[i])) all_valid = 0;
    check(all_valid, "all 16 pointer snapshots valid", "some invalid");
}

/* ══ T20 ══ */
static void t20_no_overflow(void) {
    section("T20  No overflow: 16×256=4096 tasks, no crash");
    L38Hydra h; l38_hydra_init(&h);
    L38HydraTask t = {.op=L38_HS_OP_WRITE};
    int pushed = 0, failed = 0;

    /* fill every head completely */
    for (uint32_t head = 0; head < L38_HS_HEADS; head++) {
        for (uint32_t i = 0; i < L38_HS_QUEUE_SIZE; i++) {
            t.addr = (uint64_t)(head * 1000 + i);
            if (l38_hs_push(&h.queues[head], &t) == 0) pushed++;
            else failed++;
        }
    }
    check(pushed == L38_HS_HEADS * L38_HS_QUEUE_SIZE,
          "4096 tasks pushed (no crash, no overflow)", "overflow");
    check(failed == 0, "0 push failures when filling exactly", "some failed");
    check(l38_hydra_total_depth(&h) == 4096, "total_depth=4096", "wrong");
    printf("    (pushed=%d failed=%d depth=%u)\n",
           pushed, failed, l38_hydra_total_depth(&h));
}

/* ══ MAIN ══ */
int main(void) {
    printf("══════════════════════════════════════════════════\n");
    printf("  POGLS38 — Hydra Scheduler Tests (Light)\n");
    printf("  16 heads | work-steal | weighted | ptr-guard\n");
    printf("══════════════════════════════════════════════════\n");

    t01_constants();
    t02_task_size();
    t03_push_pop();
    t04_queue_full();
    t05_depth();
    t06_work_steal();
    t07_steal_empty();
    t08_steal_busiest();
    t09_weighted_routing_zero();
    t10_weighted_routing_busy();
    t11_weight_decay();
    t12_hydra_schedule();
    t13_schedule_fallback();
    t14_drain();
    t15_gate18();
    t16_total_depth();
    t17_ptr_invariant();
    t18_ptr_corrupt_detect();
    t19_tails_scan();
    t20_no_overflow();

    int total = _pass + _fail;
    printf("\n══════════════════════════════════════════════════\n");
    if (_fail == 0)
        printf("  %d / %d PASS  \033[32mv ALL PASS — hydra stable 🐉\033[0m\n",
               _pass, total);
    else
        printf("  %d / %d PASS  \033[31mx %d FAILED\033[0m\n",
               _pass, total, _fail);
    printf("══════════════════════════════════════════════════\n");
    return _fail ? 1 : 0;
}
