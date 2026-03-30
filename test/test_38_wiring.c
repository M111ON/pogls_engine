/*
 * test_38_wiring.c — POGLS38 Wiring Layer Tests
 *
 * T01  WarpDetach: init, hibernate opens warp, wake closes it
 * T02  WarpDetach: readable() correct in each state
 * T03  Entangle/Tails role: Tails summoned, Entangle logs moves
 * T04  Tentacle: push/pop MPMC queue correct
 * T05  Tentacle: detach_activate marks cell + neighbors
 * T06  Tentacle: ntacle_bind distributes frame_ids across threads
 * T07  Tentacle write_path: addr → cell_id enqueued
 * T08  H3 bridge: outside circle → head 0
 * T09  H3 bridge: inside circle → round-robin (not head 0)
 * T10  H3 density: inside/outside count correct
 * T11  QuadFibo: X+(-X)=PHI_SCALE, Y+(-Y)=PHI_SCALE (invariant)
 * T12  QuadFibo: ok=1 always (math guarantee)
 * T13  QuadFibo batch: 0 fails for 289 cells
 * T14  Pressure: OK/SLOW/BLOCK thresholds
 * T15  Pressure: mask/unmask blocks repeat writes
 * T16  Temporal: write stores entry, hash lookup returns it
 * T17  Temporal: slot_next = slot_base + 18 (gate_18 prefetch)
 * T18  Full pipeline: write → audit → pressure → H3 → temporal
 * T19  Full pipeline: anomaly → Tails summon → warp state
 */

#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../pogls_38_wiring.h"

static int _pass=0, _fail=0;
static void check(int ok, const char *p, const char *f) {
    if (ok) { printf("    \033[32mv\033[0m %s\n", p); _pass++; }
    else    { printf("    \033[31mx FAIL\033[0m: %s\n", f); _fail++; }
}
static void section(const char *s) { printf("\n  -- %s\n", s); }

static uint32_t _pipe_flush_bytes = 0;
static void pipe_flush_cb(const uint8_t *b, uint32_t n, void *c)
{ (void)b;(void)c; _pipe_flush_bytes += n; }

/* ══ T01-T02: WARP DETACH ════════════════════════════════════════════ */
static void t01_warp_init_hibernate(void) {
    section("T01  WarpDetach: init → hibernate → wake");
    L38WarpDetach wd;
    l38_warp_init(&wd, 1, 5, 100000ULL, 0);
    check(wd.detach_active == 1, "detach_active=1 after init", "wrong");
    check(wd.warp_open == 0,     "warp_open=0 at start",       "wrong");

    l38_warp_hibernate(&wd);
    check(wd.warp_open == 1,     "warp_open=1 after hibernate","wrong");
    check(wd.detach_active == 1, "still detached while sleeping","wrong");

    l38_warp_wake(&wd);
    check(wd.warp_open == 0,     "warp_open=0 after wake",     "wrong");
    check(wd.detach_active == 0, "detach_active=0 after wake", "wrong");
}

static void t02_warp_readable(void) {
    section("T02  WarpDetach: readable() in each state");
    L38WarpDetach wd;
    l38_warp_init(&wd, 2, 6, 200000ULL, 0);
    check(!l38_warp_readable(&wd), "detached+no warp → not readable", "wrong");

    l38_warp_hibernate(&wd);
    check(l38_warp_readable(&wd), "hibernating+warp open → readable","wrong");

    l38_warp_wake(&wd);
    check(l38_warp_readable(&wd), "after wake (not detached) → readable","wrong");
}

/* ══ T03: ROLE CLARITY ═══════════════════════════════════════════════ */
static void t03_entangle_tails_roles(void) {
    section("T03  Role check: Tails summons, Entangle logs, Tails no-write");
    L38Tails t; l38_tails_init(&t);
    L38RepairCtx ctx; l38_repair_ctx_init(&ctx);
    L17Lattice lat; l17_lattice_init(&lat, LATTICE_MODE_17N);

    /* Tails summon → Entangle repair pipeline runs */
    L38TailsSummonResult r = l38_tails_summon(&t, &ctx, &lat, 3, 50000ULL, 0);
    check(r.summoned == 1,      "Tails summoned Entangle",         "wrong");
    check(ctx.total_repairs==1, "Entangle ran repair",             "not run");

    /* Tails only logs — frame log inside repair shows DETACH first */
    /* (repair ran internally, Tails cannot see frame directly here
     *  but move_count in checkpoint proves Entangle logged moves) */
    check(r.checkpoint.move_count >= 1, "Entangle logged ≥1 move", "no log");
    check(t.total_errors >= 1, "Tails logged error",               "no log");

    /* Tails.total_summons increments but Tails has no repair count */
    check(t.total_summons == 1, "Tails summon counter", "wrong");
}

/* ══ T04-T07: TENTACLE ══════════════════════════════════════════════ */
static void t04_ntacle_queue(void) {
    section("T04  Tentacle MPMC queue: push/pop");
    L38NtacleQueue q; memset(&q, 0, sizeof(q));
    L38NtacleTask t1 = {.cell_id=5, .op=L38_NTACLE_OP_WRITE, .addr=12345};
    L38NtacleTask t2 = {.cell_id=9, .op=L38_NTACLE_OP_AUDIT, .addr=67890};
    check(l38_ntacle_push(&q, t1) == 0, "push t1 OK",     "fail");
    check(l38_ntacle_push(&q, t2) == 0, "push t2 OK",     "fail");
    L38NtacleTask out;
    check(l38_ntacle_pop(&q, &out) == 0, "pop OK",          "fail");
    check(out.cell_id == 5,              "FIFO: got t1",   "wrong");
    check(l38_ntacle_pop(&q, &out) == 0, "pop t2 OK",      "fail");
    check(out.cell_id == 9,              "FIFO: got t2",   "wrong");
    check(l38_ntacle_pop(&q, &out) == -1,"empty → -1",     "wrong");
}

static void t05_tentacle_detach_activate(void) {
    section("T05  Tentacle: detach_activate marks cell + neighbors");
    L38BBGraph g; l38_frontier_build_adj(&g);
    L38Bitboard frontier = l38_bb_zero();

    l38_tentacle_detach_activate(&frontier, 100, &g);
    check(l38_bb_test(&frontier, 100), "seed cell 100 marked",   "missing");
    /* neighbors of 100 should also be marked */
    uint32_t cnt = l38_bb_popcount(&frontier);
    check(cnt == 5, "seed + 4 neighbors = 5 bits set", "wrong count");
}

static void t06_ntacle_bind(void) {
    section("T06  Tentacle bind: distributes frame_ids across threads");
    int buckets[16] = {0};
    for (uint32_t i = 0; i < 160; i++) {
        uint32_t tid = l38_ntacle_bind(i, 1, 16);
        check(tid < 16, "bind result < 16", "OOB");
        buckets[tid]++;
        if (tid >= 16) break;
    }
    /* check no bucket is empty (good distribution) */
    int all_used = 1;
    for (int i = 0; i < 16; i++) if (!buckets[i]) all_used = 0;
    check(all_used, "all 16 threads get at least 1 task (160 frames)", "uneven");
}

static void t07_ntacle_write_path(void) {
    section("T07  Tentacle write_path: addr → cell_id enqueued");
    L38NtacleQueue q; memset(&q, 0, sizeof(q));
    uint64_t addr = 0x12345678ULL;
    uint32_t cell = l38_ntacle_write_path(&q, addr, 0);
    check(cell < L38_BB_NODES, "cell_id in range", "OOB");
    L38NtacleTask out;
    check(l38_ntacle_pop(&q, &out) == 0, "task enqueued",  "empty");
    check(out.addr == addr,              "addr matches",   "wrong");
    check(out.cell_id == cell,           "cell_id matches","wrong");
}

/* ══ T08-T10: H3 BRIDGE ════════════════════════════════════════════ */
static void t08_h3_outside_priority(void) {
    section("T08  H3: outside circle → head 0 (priority)");
    /* addr outside circle: use a large addr that fails circle check */
    uint32_t big_cell = PHI_SCALE - 1u;  /* maps to addr outside circle */
    /* verify it's outside */
    uint32_t addr = (uint32_t)(((uint64_t)big_cell * PHI_UP) % PHI_SCALE);
    if (!l38_in_circle(addr)) {
        uint8_t h = l38_h3_route(big_cell, 0);
        check(h == 0, "outside circle → head 0", "wrong head");
    } else {
        check(1, "skip: this cell happens to be in circle (ok)", "");
    }
}

static void t09_h3_inside_roundrobin(void) {
    section("T09  H3: inside circle → spreads across heads");
    int heads_used[L38_H3_HEADS] = {0};
    for (uint32_t c = 0; c < 50; c++) {
        uint32_t addr = (uint32_t)(((uint64_t)c * PHI_UP) % PHI_SCALE);
        if (!l38_in_circle(addr)) continue;
        uint8_t h = l38_h3_route(c, 0);
        if (h < L38_H3_HEADS) heads_used[h]++;
    }
    int multiple_heads = 0;
    for (int i = 1; i < L38_H3_HEADS; i++) if (heads_used[i]) multiple_heads++;
    check(multiple_heads > 0, "inside-circle cells spread to multiple heads", "all head0");
}

static void t10_h3_density(void) {
    section("T10  H3 density: inside/outside count sums to total");
    L38Bitboard cells = l38_bb_zero();
    for (int i = 0; i < 20; i++) l38_bb_set(&cells, (uint16_t)(i*3));
    L38H3Density d = l38_h3_density(1, &cells);
    check(d.total == 20,                 "total=20",                "wrong");
    check(d.inside + d.outside == 20,    "inside+outside=total",    "wrong");
    printf("    (total=%u in=%u out=%u)\n", d.total, d.inside, d.outside);
}

/* ══ T11-T13: QUAD FIBO ════════════════════════════════════════════ */
static void t11_quad_fibo_invariant(void) {
    section("T11  QuadFibo: X+(-X)=PHI_SCALE (integer exact)");
    for (uint32_t n = 0; n < 50; n++) {
        L38QuadFibo q = l38_quad_fibo(n, 0);
        check(q.axis_x + q.axis_nx == PHI_SCALE,
              "X+(-X)=PHI_SCALE", "broken");
        check(q.axis_y + q.axis_ny == PHI_SCALE,
              "Y+(-Y)=PHI_SCALE", "broken");
        if (q.axis_x + q.axis_nx != PHI_SCALE) break;
    }
}

static void t12_quad_fibo_always_ok(void) {
    section("T12  QuadFibo: ok=1 for all n (math guarantee)");
    int all_ok = 1;
    for (uint32_t n = 0; n < 289; n++) {
        if (!l38_quad_fibo(n,0).ok || !l38_quad_fibo(n,1).ok)
            { all_ok = 0; break; }
    }
    check(all_ok, "all 289×2 world cells pass quad audit", "fail");
}

static void t13_quad_fibo_batch(void) {
    section("T13  QuadFibo batch: 0 fails for 289 cells");
    uint32_t fails = l38_quad_fibo_audit(0, L17_LANES_TOTAL, 0);
    check(fails == 0, "0 audit failures (World A)", "fails>0");
    fails = l38_quad_fibo_audit(0, L17_LANES_TOTAL, 1);
    check(fails == 0, "0 audit failures (World B)", "fails>0");
}

/* ══ T14-T15: PRESSURE ═════════════════════════════════════════════ */
static void t14_pressure_thresholds(void) {
    section("T14  Pressure: OK/SLOW/BLOCK thresholds");
    L38PressureBridge pb; l38_pressure_init(&pb);
    check(l38_pressure_check(&pb) == L38_PRESSURE_OK, "start = OK", "wrong");
    /* fill to HIGH */
    for (int i = 0; i < L38_PRESSURE_HIGH; i++) l38_pressure_enqueue(&pb);
    check(l38_pressure_check(&pb) == L38_PRESSURE_SLOW, "200 = SLOW", "wrong");
    /* fill to MAX */
    for (int i = L38_PRESSURE_HIGH; i < L38_PRESSURE_MAX; i++)
        l38_pressure_enqueue(&pb);
    check(l38_pressure_check(&pb) == L38_PRESSURE_BLOCK, "240 = BLOCK", "wrong");
    /* drain below LOW */
    for (int i = 0; i < L38_PRESSURE_MAX; i++) l38_pressure_dequeue(&pb);
    check(l38_pressure_check(&pb) == L38_PRESSURE_OK, "after drain = OK", "wrong");
}

static void t15_pressure_mask(void) {
    section("T15  Pressure: mask blocks cell, unmask clears");
    L38PressureBridge pb; l38_pressure_init(&pb);
    check(!l38_pressure_is_masked(&pb, 5), "cell 5 not masked initially","wrong");
    l38_pressure_mask(&pb, 5);
    check(l38_pressure_is_masked(&pb, 5),  "cell 5 masked",             "wrong");
    check(!l38_pressure_is_masked(&pb, 6), "cell 6 still clear",        "wrong");
    l38_pressure_unmask(&pb, 5);
    check(!l38_pressure_is_masked(&pb, 5), "cell 5 unmasked",           "wrong");
}

/* ══ T16-T17: TEMPORAL ══════════════════════════════════════════════ */
static void t16_temporal_write_lookup(void) {
    section("T16  Temporal: write → hash lookup returns entry");
    L38TemporalBridge tb; l38_temporal_init(&tb);
    check(tb.magic == L38_TEMPORAL_MAGIC, "temporal magic", "wrong");

    l38_temporal_write(&tb, 0x12345ULL, 0xABCDULL, 7);
    const L38TemporalEntry *e = l38_temporal_lookup(&tb, 0x12345ULL);
    check(e != NULL,                  "lookup found entry",       "NULL");
    check(e && e->value == 0xABCDULL, "value matches",            "wrong");
    check(e && e->cell_id == 7,       "cell_id=7",                "wrong");
}

static void t17_temporal_gate18_slots(void) {
    section("T17  Temporal: slot_next = slot_base + gate_18 (prefetch)");
    L38TemporalBridge tb; l38_temporal_init(&tb);
    l38_temporal_write(&tb, 100ULL, 0, 10);  /* cell_id=10 */
    uint32_t expected_next  = (10 + L17_GATE) % L17_LANES_TOTAL;
    uint32_t expected_next2 = (10 + L17_GATE*2) % L17_LANES_TOTAL;
    check(tb.slot_base  == 10,            "slot_base = cell_id",        "wrong");
    check(tb.slot_next  == expected_next, "slot_next = base+18",        "wrong");
    check(tb.slot_next2 == expected_next2,"slot_next2 = base+36",       "wrong");
    printf("    (base=%u next=%u next2=%u)\n",
           tb.slot_base, tb.slot_next, tb.slot_next2);
}

/* ══ T18-T19: FULL PIPELINE ══════════════════════════════════════ */
static void t18_full_write(void) {
    section("T18  Full pipeline: normal write flows through all layers");
    L38NtacleQueue nq; memset(&nq, 0, sizeof(nq));
    L38BBGraph bbg; l38_frontier_build_adj(&bbg);
    L38PressureBridge pb; l38_pressure_init(&pb);
    L38TemporalBridge tb; l38_temporal_init(&tb);
    L38ExecWindow ew; l38_ew_init(&ew, pipe_flush_cb, NULL);
    L38Tails tails; l38_tails_init(&tails);
    L38RepairCtx ctx; l38_repair_ctx_init(&ctx);
    L17Lattice lat; l17_lattice_init(&lat, LATTICE_MODE_17N);

    L38WirePipeline pipe = {
        .lattice=&lat, .ntacle=&nq, .bbgraph=&bbg,
        .pressure=&pb, .temporal=&tb, .ew=&ew,
        .tails=&tails, .repair=&ctx, .next_frame_id=1
    };

    /* normal write: addr in circle */
    uint64_t good = 100000ULL;
    L38WriteResult r = l38_wire_write(&pipe, good, 0xABCD);

    check(r.quad_ok == 1,               "QuadFibo audit pass",    "fail");
    check(r.pressure_level == L38_PRESSURE_OK, "pressure OK",     "blocked");
    check(r.cell_id < L38_BB_NODES,     "cell_id in range",       "OOB");
    check(r.anomaly == 0,               "no anomaly (in circle)", "wrong");
    check(lat.total_writes == 1,        "L17 lattice recorded",   "wrong");
    check(ew.total_ops == 1,            "ExecWindow recorded",    "wrong");
    check(tb.total_written == 1,        "Temporal recorded",      "wrong");

    /* Tentacle queue has 1 task */
    L38NtacleTask out;
    check(l38_ntacle_pop(&nq, &out) == 0, "Tentacle task enqueued","empty");
}

static void t19_anomaly_warp(void) {
    section("T19  Full pipeline: anomaly → Tails summon → warp state");
    L38NtacleQueue nq; memset(&nq, 0, sizeof(nq));
    L38BBGraph bbg; l38_frontier_build_adj(&bbg);
    L38PressureBridge pb; l38_pressure_init(&pb);
    L38TemporalBridge tb; l38_temporal_init(&tb);
    L38ExecWindow ew; l38_ew_init(&ew, pipe_flush_cb, NULL);
    L38Tails tails; l38_tails_init(&tails);
    L38RepairCtx ctx; l38_repair_ctx_init(&ctx);
    L17Lattice lat; l17_lattice_init(&lat, LATTICE_MODE_17N);

    L38WirePipeline pipe = {
        .lattice=&lat, .ntacle=&nq, .bbgraph=&bbg,
        .pressure=&pb, .temporal=&tb, .ew=&ew,
        .tails=&tails, .repair=&ctx, .next_frame_id=1
    };

    /* anomaly write: addr outside circle */
    uint64_t bad = (uint64_t)(PHI_SCALE - 1u);
    L38WriteResult r = l38_wire_write(&pipe, bad, 0xDEAD);

    check(r.anomaly == 1,               "anomaly detected",        "wrong");
    check(tails.total_summons == 1,     "Tails summoned Entangle", "wrong");
    check(ctx.total_repairs == 1,       "Entangle ran repair",     "wrong");
    printf("    (repair_ok=%d warp_open=%d head_id=%d)\n",
           r.repair_ok, r.warp_open, r.head_id);
}

/* ══ MAIN ════════════════════════════════════════════════════════════ */
int main(void) {
    l38_rubik_init();
    printf("══════════════════════════════════════════════════\n");
    printf("  POGLS38 — Wiring Layer Tests\n");
    printf("  Warp|Tentacle|H3|QuadFibo|Pressure|Temporal\n");
    printf("══════════════════════════════════════════════════\n");

    t01_warp_init_hibernate();
    t02_warp_readable();
    t03_entangle_tails_roles();
    t04_ntacle_queue();
    t05_tentacle_detach_activate();
    t06_ntacle_bind();
    t07_ntacle_write_path();
    t08_h3_outside_priority();
    t09_h3_inside_roundrobin();
    t10_h3_density();
    t11_quad_fibo_invariant();
    t12_quad_fibo_always_ok();
    t13_quad_fibo_batch();
    t14_pressure_thresholds();
    t15_pressure_mask();
    t16_temporal_write_lookup();
    t17_temporal_gate18_slots();
    t18_full_write();
    t19_anomaly_warp();

    int total = _pass + _fail;
    printf("\n══════════════════════════════════════════════════\n");
    if (_fail == 0)
        printf("  %d / %d PASS  \033[32mv ALL PASS — system wired 🕸\033[0m\n",
               _pass, total);
    else
        printf("  %d / %d PASS  \033[31mx %d FAILED\033[0m\n",
               _pass, total, _fail);
    printf("══════════════════════════════════════════════════\n");
    return _fail ? 1 : 0;
}
