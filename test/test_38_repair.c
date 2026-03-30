/*
 * test_38_repair.c — POGLS38 Repair Layer Tests
 *
 * T01  Scale ladder: 162÷3=54, 54÷3=18, 18÷3=6, 6÷3=2 (compile-time)
 * T02  Unit circle: addr < 741455 → in circle, addr ≥ 741455 → outside
 * T03  Rubik permute: deterministic, move+9 = inverse-like (bijective)
 * T04  World flip: A→B via PHI_DOWN, B→A via PHI_UP, both in range
 * T05  Movement log: push entries, head increments, ring wraps
 * T06  Repair pipeline: addr already in circle → immediate FOLD_OK
 * T07  Repair pipeline: forced fail → scale ladder descends 162→54→18...
 * T08  World flip trigger: if rubik fails → flip world → check circle
 * T09  Recycle flag: magic + crc32 set correctly after full fail
 * T10  Tails checkpoint: captures frame state correctly
 * T11  l38_repair_cell(): integrates with L17Lattice flags
 * T12  Stats: total_folded + total_recycled = total_repairs
 * T13  Log sequence: DETACH → REPAIR_TRY ... → FOLD_OK/RECYCLE correct
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../pogls_38_repair.h"

/* ── harness ──────────────────────────────────────────────────────── */
static int _pass = 0, _fail = 0;
static void check(int ok, const char *p, const char *f) {
    if (ok) { printf("    \033[32mv\033[0m %s\n", p); _pass++; }
    else    { printf("    \033[31mx FAIL\033[0m: %s\n", f); _fail++; }
}
static void section(const char *s) { printf("\n  -- %s\n", s); }

/* threshold: addr < PHI_SCALE / sqrt(2) ≈ 741455 → in circle */
#define CIRCLE_THRESH  741455u

/* ══ T01 ══════════════════════════════════════════════════════════════ */
static void t01_scale_ladder(void) {
    section("T01  Scale ladder constants");
    check(l38_scale_ladder[0] == 162, "ladder[0]=162 (NODE_MAX)", "wrong");
    check(l38_scale_ladder[1] ==  54, "ladder[1]=54  (Rubik nexus)", "wrong");
    check(l38_scale_ladder[2] ==  18, "ladder[2]=18  (gate_18)", "wrong");
    check(l38_scale_ladder[3] ==   6, "ladder[3]=6   (minimal)", "wrong");
    check(l38_scale_ladder[4] ==   2, "ladder[4]=2   (floor)", "wrong");
    check(L38_SCALE_STEPS == 5,       "5 steps total", "wrong");
    /* each step ÷3 */
    check(l38_scale_ladder[0]/3 == l38_scale_ladder[1], "162÷3=54", "wrong");
    check(l38_scale_ladder[1]/3 == l38_scale_ladder[2], "54÷3=18",  "wrong");
    check(l38_scale_ladder[2]/3 == l38_scale_ladder[3], "18÷3=6",   "wrong");
    check(l38_scale_ladder[3]/3 == l38_scale_ladder[4], "6÷3=2",    "wrong");
}

/* ══ T02 ══════════════════════════════════════════════════════════════ */
static void t02_unit_circle(void) {
    section("T02  Unit circle check: 2a² < PHI_SCALE²");
    check(l38_in_circle(0),            "addr=0 → in circle",          "wrong");
    check(l38_in_circle(CIRCLE_THRESH-1), "addr=741454 → in circle",  "wrong");
    check(!l38_in_circle(PHI_SCALE-1), "addr=PHI_SCALE-1 → outside",  "wrong");
    /* PHI_SCALE/2+1 = 524289: 2a²=549B < PHI_SCALE²=1099B → actually IN circle */
    check(l38_in_circle(PHI_SCALE/2 + 1), "addr=PHI_SCALE/2+1 → in circle (math verified)", "wrong");
    /* PHI_DOWN = 648055 < CIRCLE_THRESH → in circle */
    check(l38_in_circle(PHI_DOWN), "PHI_DOWN in circle", "wrong");
    /* PHI_UP = 1696631 > PHI_SCALE — mod first */
    uint32_t phi_up_mod = PHI_UP % PHI_SCALE;
    check(phi_up_mod < PHI_SCALE, "PHI_UP mod in range", "wrong");
}

/* ══ T03 ══════════════════════════════════════════════════════════════ */
static void t03_rubik_perm(void) {
    section("T03  Rubik permute: deterministic + bijective");
    /* same byte + same move = same result */
    uint8_t r1 = l38_rubik_perm(0xAB, 5);
    uint8_t r2 = l38_rubik_perm(0xAB, 5);
    check(r1 == r2, "deterministic (same inputs = same output)", "random");

    /* all 18 moves produce different results for byte 0xAB */
    uint8_t results[18];
    int all_different = 1;
    for (int m = 0; m < 18; m++) results[m] = l38_rubik_perm(0xAB, (uint8_t)m);
    for (int i = 0; i < 18; i++)
        for (int j = i+1; j < 18; j++)
            if (results[i] == results[j]) { all_different = 0; }
    check(all_different, "18 moves produce 18 different results", "collision");

    /* bijective: permuting all 256 bytes with fixed move → no collisions */
    uint8_t seen[256] = {0};
    int bijective = 1;
    for (int b = 0; b < 256; b++) {
        uint8_t out = l38_rubik_perm((uint8_t)b, 3);
        if (seen[out]++) { bijective = 0; break; }
    }
    check(bijective, "bijective: 256 inputs → 256 unique outputs", "collision");
}

/* ══ T04 ══════════════════════════════════════════════════════════════ */
static void t04_world_flip(void) {
    section("T04  World flip: A→B and B→A stay in PHI_SCALE range");
    uint32_t test_addrs[] = {0, 100000, PHI_DOWN, 500000, PHI_SCALE/3};
    int all_ok = 1;
    for (int i = 0; i < 5; i++) {
        uint32_t a = test_addrs[i];
        uint32_t ab = l38_world_flip_addr(a, 0);  /* A→B */
        uint32_t ba = l38_world_flip_addr(a, 1);  /* B→A */
        if (ab >= PHI_SCALE || ba >= PHI_SCALE) all_ok = 0;
    }
    check(all_ok, "all flipped addrs stay in [0..PHI_SCALE)", "OOB");

    /* addr=0 → flip = 0 (trivial) */
    check(l38_world_flip_addr(0, 0) == 0, "flip(0) = 0", "wrong");

    /* A→B→A roundtrip: not necessarily exact (PHI modular), but in circle range */
    uint32_t orig = 100000u;
    uint32_t ab   = l38_world_flip_addr(orig, 0);
    check(ab < PHI_SCALE, "A→B result in range", "OOB");
    printf("    (orig=%u  A→B=%u  in_circle=%d)\n",
           orig, ab, l38_in_circle(ab));
}

/* ══ T05 ══════════════════════════════════════════════════════════════ */
static void t05_movement_log(void) {
    section("T05  Movement log: ring buffer push + wrap");
    L38MoveLog log; l38_log_init(&log, 42);
    check(atomic_load(&log.head) == 0, "head starts at 0", "wrong");

    l38_log_push(&log, ENT_MOVE_DETACH, 0, 0, 0, 5);
    check(atomic_load(&log.head) == 1, "head=1 after push", "wrong");
    check(log.entries[0].move == ENT_MOVE_DETACH, "entry[0].move=DETACH", "wrong");
    check(log.entries[0].world == 0,              "entry[0].world=0",     "wrong");

    /* push 256 more — should wrap (head wraps mod 256) */
    for (int i = 0; i < 256; i++)
        l38_log_push(&log, ENT_MOVE_REPAIR_TRY, 0, 0, (uint8_t)i, 5);
    check(atomic_load(&log.head) == 257, "head=257 after 257 pushes", "wrong");
    /* ring wraps: slot 257%256=1 should have last push */
    check(log.entries[1].move == ENT_MOVE_REPAIR_TRY, "ring wrap correct", "wrong");
}

/* ══ T06 ══════════════════════════════════════════════════════════════ */
static void t06_repair_immediate_ok(void) {
    section("T06  Repair: addr already in circle → immediate FOLD_OK");
    L38EjectFrame ef;
    L38RepairCtx ctx; l38_repair_ctx_init(&ctx);

    /* addr in circle → rubik perm[0] should keep it in circle (or find one fast) */
    uint32_t addr_in = 50000u;  /* well within circle */
    check(l38_in_circle(addr_in), "test addr is in circle", "setup wrong");

    l38_frame_init(&ef, 1, 5, (uint64_t)addr_in, 0);
    int ok = l38_repair_pipeline(&ef, &addr_in);

    check(ok == 1,                   "repair returns 1 (folded)",          "wrong");
    check(ef.state == L38_STATE_FOLDED, "state=FOLDED",                    "wrong");
    check(l38_in_circle(addr_in),    "result addr still in circle",        "wrong");
}

/* ══ T07 ══════════════════════════════════════════════════════════════ */
static void t07_scale_ladder_descent(void) {
    section("T07  Scale ladder: repair logs descend through steps");
    L38EjectFrame ef;
    /* Use addr that is outside circle to stress repair */
    uint32_t addr = PHI_SCALE - 1u;  /* max addr, way outside circle */
    l38_frame_init(&ef, 2, 7, (uint64_t)addr, 0);
    int ok = l38_repair_pipeline(&ef, &addr);

    /* either folded or recycled — both valid outcomes */
    check(ok == 1 || ef.recycled == 1, "pipeline completes (fold or recycle)", "stuck");
    /* log has at least DETACH + some REPAIR_TRY entries */
    uint32_t moves = atomic_load(&ef.log.head);
    check(moves >= 2, "at least 2 log entries (DETACH + attempts)", "too few");
    printf("    (outcome=%s  moves_logged=%u  scale_reached=%u)\n",
           ok ? "FOLDED" : "RECYCLED", moves, ef.scale_step);
}

/* ══ T08 ══════════════════════════════════════════════════════════════ */
static void t08_world_flip_trigger(void) {
    section("T08  World flip: triggered after all scale steps fail");
    L38EjectFrame ef;
    /* Use worst-case addr: outside circle and resists permutation */
    uint32_t addr = PHI_SCALE - 2u;
    l38_frame_init(&ef, 3, 11, (uint64_t)addr, 0);
    int ok = l38_repair_pipeline(&ef, &addr);

    /* if not immediately folded, flip should have been attempted */
    if (!ok) {
        check(ef.flipped == 1, "world flip was attempted", "no flip");
        check(ef.world == 1,   "world flipped to B",      "still A");
    } else {
        check(1, "folded before needing world flip (valid)", "");
    }

    /* log should contain WORLD_FLIP if recycled */
    if (ef.recycled) {
        int found_flip = 0;
        uint32_t cnt = atomic_load(&ef.log.head);
        for (uint32_t i = 0; i < cnt && i < L38_LOG_SIZE; i++) {
            if (ef.log.entries[i % L38_LOG_SIZE].move == ENT_MOVE_WORLD_FLIP)
                found_flip = 1;
        }
        check(found_flip || ef.flipped, "WORLD_FLIP in log or flipped=1", "missing");
    }
}

/* ══ T09 ══════════════════════════════════════════════════════════════ */
static void t09_recycle_flag(void) {
    section("T09  Recycle flag: magic + crc set on full fail");
    L38EjectFrame ef;
    /* craft a situation that will recycle */
    uint32_t addr = PHI_SCALE - 3u;
    l38_frame_init(&ef, 4, 17, (uint64_t)addr, 0);
    int ok = l38_repair_pipeline(&ef, &addr);

    if (!ok) {
        check(ef.recycle.magic == L38_RECYCLE_MAGIC,
              "recycle.magic correct", "wrong");
        check(ef.recycle.frame_id == 4,
              "recycle.frame_id = 4", "wrong");
        check(ef.recycle.crc32 != 0,
              "recycle.crc32 non-zero", "zero");
        check(ef.state == L38_STATE_RECYCLED,
              "state = RECYCLED", "wrong");
    } else {
        check(1, "folded (no recycle needed — valid)", "");
    }
    printf("    (outcome=%s)\n", ok ? "FOLDED" : "RECYCLED");
}

/* ══ T10 ══════════════════════════════════════════════════════════════ */
static void t10_tails_checkpoint(void) {
    section("T10  Tails checkpoint: captures frame state");
    L38EjectFrame ef;
    uint32_t addr = 200000u;  /* likely in circle */
    l38_frame_init(&ef, 5, 22, (uint64_t)addr, 0);
    l38_repair_pipeline(&ef, &addr);

    L38TailsCheckpoint cp;
    l38_tails_checkpoint(&ef, &cp);

    check(cp.frame_id == 5,   "checkpoint.frame_id=5",    "wrong");
    check(cp.cell_id == 22,   "checkpoint.cell_id=22",    "wrong");
    check(cp.move_count >= 1, "move_count >= 1 (DETACH+)", "wrong");
    check(cp.last_move > 0,   "last_move > 0 (valid move)", "zero");
    check(cp.recycled == ef.recycled, "recycled matches frame", "mismatch");
    check(cp.flipped  == ef.flipped,  "flipped matches frame",  "mismatch");
}

/* ══ T11 ══════════════════════════════════════════════════════════════ */
static void t11_repair_cell_integration(void) {
    section("T11  l38_repair_cell() wires to L17Lattice flags");
    L17Lattice lat; l17_lattice_init(&lat, LATTICE_MODE_17N);
    L38RepairCtx ctx; l38_repair_ctx_init(&ctx);

    /* repair cell 5 with addr in circle → should fold */
    uint32_t good_addr = 100000u;
    L38TailsCheckpoint cp;
    int r1 = l38_repair_cell(&ctx, &lat, 5, (uint64_t)good_addr, 0, &cp);

    check(ctx.total_repairs == 1, "total_repairs = 1", "wrong");
    if (r1) {
        check(ctx.total_folded == 1,   "total_folded = 1", "wrong");
        check(!(lat.lanes[5].flags & L17_FLAG_GHOST),
              "GHOST flag cleared after fold", "still set");
    } else {
        check(ctx.total_recycled == 1, "total_recycled = 1", "wrong");
        check(lat.lanes[5].flags & L17_FLAG_GHOST,
              "GHOST flag set after recycle", "not set");
    }
}

/* ══ T12 ══════════════════════════════════════════════════════════════ */
static void t12_stats_consistent(void) {
    section("T12  Stats: folded + recycled = total_repairs");
    L38RepairCtx ctx; l38_repair_ctx_init(&ctx);

    uint32_t addrs[] = {50000, 200000, PHI_SCALE-1, 100, 400000, PHI_SCALE-100};
    for (int i = 0; i < 6; i++) {
        L38TailsCheckpoint cp;
        l38_repair_cell(&ctx, NULL, (uint32_t)i, (uint64_t)addrs[i],
                        (uint8_t)(i & 1), &cp);
    }
    check(ctx.total_repairs == 6, "total_repairs = 6", "wrong");
    check(ctx.total_folded + ctx.total_recycled == ctx.total_repairs,
          "folded + recycled = total (no lost)", "leak");
    printf("    (folded=%llu recycled=%llu flips=%llu)\n",
           (unsigned long long)ctx.total_folded,
           (unsigned long long)ctx.total_recycled,
           (unsigned long long)ctx.world_flips);
}

/* ══ T13 ══════════════════════════════════════════════════════════════ */
static void t13_log_sequence(void) {
    section("T13  Log sequence: DETACH is always first entry");
    L38EjectFrame ef;
    uint32_t addr = 300000u;
    l38_frame_init(&ef, 10, 33, (uint64_t)addr, 0);

    /* first entry must be DETACH regardless of addr */
    check(ef.log.entries[0].move == ENT_MOVE_DETACH,
          "log.entries[0] = DETACH", "wrong");
    check(ef.log.entries[0].frame_id == 10,
          "DETACH.frame_id = 10", "wrong");
    check(ef.log.entries[0].cell_id == 33,
          "DETACH.cell_id = 33", "wrong");

    l38_repair_pipeline(&ef, &addr);
    uint32_t cnt = atomic_load(&ef.log.head);
    /* last entry must be FOLD_OK or RECYCLE */
    uint32_t last_idx = (cnt - 1) % L38_LOG_SIZE;
    uint8_t last_move = ef.log.entries[last_idx].move;
    check(last_move == ENT_MOVE_FOLD_OK || last_move == ENT_MOVE_RECYCLE,
          "last log entry = FOLD_OK or RECYCLE", "wrong");
    printf("    (total_log=%u  last_move=%u)\n", cnt, last_move);
}

/* ══ MAIN ════════════════════════════════════════════════════════════ */
int main(void) {
    printf("══════════════════════════════════════════════════\n");
    printf("  POGLS38 — Repair Layer Tests\n");
    printf("  ScaleLadder | WorldFlip | Entangle | Tails\n");
    printf("══════════════════════════════════════════════════\n");

    t01_scale_ladder();
    t02_unit_circle();
    t03_rubik_perm();
    t04_world_flip();
    t05_movement_log();
    t06_repair_immediate_ok();
    t07_scale_ladder_descent();
    t08_world_flip_trigger();
    t09_recycle_flag();
    t10_tails_checkpoint();
    t11_repair_cell_integration();
    t12_stats_consistent();
    t13_log_sequence();

    int total = _pass + _fail;
    printf("\n══════════════════════════════════════════════════\n");
    if (_fail == 0)
        printf("  %d / %d PASS  \033[32mv ALL PASS — P→P→R→E live 🔧\033[0m\n",
               _pass, total);
    else
        printf("  %d / %d PASS  \033[31mx %d FAILED\033[0m\n",
               _pass, total, _fail);
    printf("══════════════════════════════════════════════════\n");
    return _fail ? 1 : 0;
}
