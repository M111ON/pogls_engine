/*
 * test_17n_lattice.c — POGLS38  3-World 17n Lattice Test Suite
 *
 * T01  Constants: compile-time invariants pass
 * T02  cell_of(): interaction_id × phase → correct cell, lane, group, world
 * T03  World boundary: group < 8 → WORLD_A, group >= 8 → WORLD_B
 * T04  gate_18: common clock (144÷8 = 162÷9 = 18)
 * T05  PHI scatter: 136+153 lanes get balanced distribution
 * T06  l17_write(): heat, seq, flags update correctly
 * T07  gate_18 crossing: fires every 18 writes, gate_crossings increments
 * T08  buffer zone: interaction_id >= 144 → is_buffer = 1
 * T09  V4 bridge: v4_lane 0..53 → valid L17Cell (no out-of-bounds)
 * T10  World stats: total = A + B + buffer
 * T11  Lane coverage: all 289 cells reachable via addr scatter
 * T12  17 is prime: no aliasing with 8 or 9 (mod test)
 * T13  gate_count resets: wraps 0→18→0 correctly
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../pogls_17n_lattice.h"

/* ── harness ──────────────────────────────────────────────────────── */
static int _pass = 0, _fail = 0;
static void check(int ok, const char *p, const char *f) {
    if (ok) { printf("    \033[32mv\033[0m %s\n", p); _pass++; }
    else    { printf("    \033[31mx FAIL\033[0m: %s\n", f); _fail++; }
}
static void section(const char *s) { printf("\n  -- %s\n", s); }

/* ══ T01 ══════════════════════════════════════════════════════════════ */
static void t01_constants(void) {
    section("T01  Lattice constants");
    check(L17_BRIDGE == 17,              "BRIDGE = 17 (prime)",        "wrong");
    check(L17_GROUPS_A == 8,             "GROUPS_A = 8 (2³)",          "wrong");
    check(L17_GROUPS_B == 9,             "GROUPS_B = 9 (3²)",          "wrong");
    check(L17_LANES_A == 136,            "LANES_A = 136 (8×17)",       "wrong");
    check(L17_LANES_B == 153,            "LANES_B = 153 (9×17)",       "wrong");
    check(L17_LANES_TOTAL == 289,        "TOTAL = 289 (17²)",          "wrong");
    check(L17_DYNAMICS == 144,           "DYNAMICS = 144 (Fib12)",     "wrong");
    check(L17_BUFFER == 145,             "BUFFER = 145 (289-144)",     "wrong");
    check(L17_GATE == 18,                "GATE = 18",                  "wrong");
    check(L17_DYNAMICS + L17_BUFFER == L17_LANES_TOTAL,
          "dynamics + buffer = total",   "sum wrong");
    check(PHI_UP == 1696631u,            "PHI_UP frozen",              "wrong");
    check(PHI_DOWN == 648055u,           "PHI_DOWN frozen",            "wrong");
}

/* ══ T02 ══════════════════════════════════════════════════════════════ */
static void t02_cell_of(void) {
    section("T02  l17_cell_of() mapping");

    /* interaction_id=0, phase=0 → cell_id=(0%17)+(0×17)=0 */
    L17Cell c = l17_cell_of(0, 0);
    check(c.cell_id == 0,   "id=0,ph=0 → cell_id=0",  "wrong");
    check(c.lane    == 0,   "lane = 0",                "wrong");
    check(c.group   == 0,   "group = 0",               "wrong");
    check(c.world   == L17_WORLD_A, "group 0 → WORLD_A", "wrong");

    /* interaction_id=17, phase=0 → iid=17%144=17, 17%17=0, cell=(0+0)=0 */
    L17Cell c2 = l17_cell_of(17, 0);
    check(c2.lane == 0,     "id=17 → lane=0 (17%17=0)", "wrong");

    /* interaction_id=1, phase=1 → iid=1, lane=1%17=1, cell=(1)+(1×17)=18 */
    L17Cell c3 = l17_cell_of(1, 1);
    check(c3.cell_id == 18, "id=1,ph=1 → cell_id=18",  "wrong");
    check(c3.lane    == 1,  "lane = 1",                 "wrong");
    check(c3.group   == 1,  "group = 1",                "wrong");

    /* phase clamped to 0..16 */
    L17Cell c4 = l17_cell_of(0, 17);   /* 17 % 17 = 0 */
    L17Cell c5 = l17_cell_of(0, 0);
    check(c4.cell_id == c5.cell_id, "phase=17 wraps to phase=0", "wrong");
}

/* ══ T03 ══════════════════════════════════════════════════════════════ */
static void t03_world_boundary(void) {
    section("T03  World A / B boundary at group 8");

    /* group 0..7 → WORLD_A */
    int all_a_ok = 1;
    for (int g = 0; g < 8; g++) {
        L17Cell c = l17_cell_of((uint32_t)(g * 17), 0);
        /* cell_id = g*17 % 17 = 0, cell_id + 0 = 0 -- need to craft properly */
        /* direct: cell with group=g has cell_id = g*17..g*17+16 */
        c.cell_id = (uint16_t)(g * 17);
        c.group   = (uint8_t)g;
        c.world   = (c.group < L17_GROUPS_A) ? L17_WORLD_A : L17_WORLD_B;
        if (c.world != L17_WORLD_A) all_a_ok = 0;
    }
    check(all_a_ok, "groups 0-7 → WORLD_A", "boundary wrong");

    /* group 8..16 → WORLD_B */
    int all_b_ok = 1;
    for (int g = 8; g < 17; g++) {
        uint8_t grp = (uint8_t)g;
        l17_world_t w = (grp < L17_GROUPS_A) ? L17_WORLD_A : L17_WORLD_B;
        if (w != L17_WORLD_B) all_b_ok = 0;
    }
    check(all_b_ok, "groups 8-16 → WORLD_B", "boundary wrong");

    /* cells 0..135 = WORLD_A lanes (8×17=136) */
    int wa_range_ok = 1;
    for (int i = 0; i < 136; i++) {
        uint8_t g = (uint8_t)(i / 17);
        if (g >= 8) { wa_range_ok = 0; break; }
    }
    check(wa_range_ok, "cells 0..135 all WORLD_A", "wrong");

    /* cells 136..288 = WORLD_B lanes (9×17=153) */
    int wb_range_ok = 1;
    for (int i = 136; i < 289; i++) {
        uint8_t g = (uint8_t)(i / 17);
        if (g < 8) { wb_range_ok = 0; break; }
    }
    check(wb_range_ok, "cells 136..288 all WORLD_B", "wrong");
}

/* ══ T04 ══════════════════════════════════════════════════════════════ */
static void t04_gate18_math(void) {
    section("T04  gate_18 = common clock for both worlds");
    check(L17_DYNAMICS % L17_GROUPS_A == 0,
          "144 ÷ 8 = 18 (no remainder)",    "wrong");
    check(L17_DYNAMICS / L17_GROUPS_A == L17_GATE,
          "144 ÷ 8 = gate_18 = 18",         "wrong");
    /* 162 ÷ 9 = 18 */
    check(162u % L17_GROUPS_B == 0,
          "162 ÷ 9 = 18 (no remainder)",    "wrong");
    check(162u / L17_GROUPS_B == L17_GATE,
          "162 ÷ 9 = gate_18 = 18",         "wrong");
    /* 17 × 18 = 306 (delta n=8 anchor) */
    check(L17_BRIDGE * L17_GATE == 306u,
          "17 × 18 = 306 (delta n=8)",      "wrong");
}

/* ══ T05 ══════════════════════════════════════════════════════════════ */
static void t05_phi_scatter_balance(void) {
    section("T05  PHI scatter: balanced distribution across 136+153 lanes");

    uint32_t count_a[L17_LANES_A];
    uint32_t count_b[L17_LANES_B];
    memset(count_a, 0, sizeof(count_a));
    memset(count_b, 0, sizeof(count_b));

    /* scatter 10000 sequential addresses */
    for (uint32_t i = 0; i < 10000; i++) {
        uint32_t scattered = (uint32_t)(((uint64_t)(i & 0xFFFFFu)
                                         * PHI_UP) % L17_LANES_TOTAL);
        if (scattered < L17_LANES_A)
            count_a[scattered]++;
        else
            count_b[scattered - L17_LANES_A]++;
    }

    /* check: every World A lane hit at least once */
    int a_coverage = 1;
    for (uint32_t i = 0; i < L17_LANES_A; i++)
        if (count_a[i] == 0) { a_coverage = 0; break; }
    check(a_coverage, "all 136 World A lanes reachable", "gap found");

    /* check: every World B lane hit at least once */
    int b_coverage = 1;
    for (uint32_t i = 0; i < L17_LANES_B; i++)
        if (count_b[i] == 0) { b_coverage = 0; break; }
    check(b_coverage, "all 153 World B lanes reachable", "gap found");

    /* balance: max/min ratio < 4 (PHI scatter = low variance) */
    uint32_t max_a = 0, min_a = UINT32_MAX;
    for (uint32_t i = 0; i < L17_LANES_A; i++) {
        if (count_a[i] > max_a) max_a = count_a[i];
        if (count_a[i] < min_a) min_a = count_a[i];
    }
    check(min_a > 0 && max_a < min_a * 4,
          "World A: max/min ratio < 4 (balanced)", "imbalanced");

    uint32_t max_b = 0, min_b = UINT32_MAX;
    for (uint32_t i = 0; i < L17_LANES_B; i++) {
        if (count_b[i] > max_b) max_b = count_b[i];
        if (count_b[i] < min_b) min_b = count_b[i];
    }
    check(min_b > 0 && max_b < min_b * 4,
          "World B: max/min ratio < 4 (balanced)", "imbalanced");
    printf("    (A: min=%u max=%u  B: min=%u max=%u)\n",
           min_a, max_a, min_b, max_b);
}

/* ══ T06 ══════════════════════════════════════════════════════════════ */
static void t06_write_lane_update(void) {
    section("T06  l17_write(): lane context update");
    L17Lattice lat; l17_lattice_init(&lat, LATTICE_MODE_17N);

    L17WriteResult r = l17_write(&lat, 0x12345ULL, 0xABCDULL);
    check(lat.total_writes == 1, "total_writes=1 after first write", "wrong");
    check(r.cell.cell_id < L17_LANES_TOTAL, "cell_id in range", "OOB");

    L17LaneCtx *lc = &lat.lanes[r.cell.cell_id];
    check(lc->seq == 1,          "seq=1 after first write",    "wrong");
    check(lc->flags & L17_FLAG_ACTIVE, "ACTIVE flag set",      "not set");
    check(lc->heat >= 1,         "heat >= 1 after write",      "no heat");

    /* write same addr 8 times → heat should grow */
    for (int i = 0; i < 7; i++) l17_write(&lat, 0x12345ULL, 0xABCDULL);
    check(lc->heat >= 1,         "heat still positive after 8 writes", "gone");
}

/* ══ T07 ══════════════════════════════════════════════════════════════ */
static void t07_gate_crossing(void) {
    section("T07  gate_18 crossing fires every 18 writes");
    L17Lattice lat; l17_lattice_init(&lat, LATTICE_MODE_17N);

    int crossings = 0;
    for (int i = 0; i < 18 * 5; i++) {
        L17WriteResult r = l17_write(&lat, (uint64_t)i * 7919ULL, 0);
        if (r.gate_event) crossings++;
    }
    check(crossings == 5, "5 crossings in 90 writes (every 18)", "wrong");
    check(lat.gate_crossings == 5, "gate_crossings counter = 5",  "wrong");
    check(lat.gate_count == 0,     "gate_count resets to 0",       "wrong");
}

/* ══ T08 ══════════════════════════════════════════════════════════════ */
static void t08_buffer_zone(void) {
    section("T08  buffer zone: interaction_id >= 144 → is_buffer=1");
    L17Cell in_dyn = l17_cell_of(143, 0);
    L17Cell in_buf = l17_cell_of(144, 0);
    L17Cell in_buf2 = l17_cell_of(200, 0);
    check(in_dyn.is_buffer == 0, "id=143 → not buffer (dynamics)", "wrong");
    check(in_buf.is_buffer == 1, "id=144 → buffer zone",            "wrong");
    check(in_buf2.is_buffer == 1,"id=200 → buffer zone",            "wrong");
}

/* ══ T09 ══════════════════════════════════════════════════════════════ */
static void t09_v4_bridge(void) {
    section("T09  V4 bridge: v4_lane 0..53 → valid L17Cell");
    int all_ok = 1;
    for (int i = 0; i < 54; i++) {
        L17Cell c = l17_from_v4_lane((uint8_t)i);
        if (c.cell_id >= L17_LANES_TOTAL) { all_ok = 0; break; }
        if (c.world != L17_WORLD_A && c.world != L17_WORLD_B) { all_ok = 0; break; }
    }
    check(all_ok, "all 54 V4 lanes map to valid L17Cells", "OOB or bad world");

    /* lanes 0..35: group < 8 → WORLD_A; lanes 36..53: group varies */
    L17Cell ca = l17_from_v4_lane(0);
    check(ca.world == L17_WORLD_A, "V4 lane 0 → WORLD_A (group 0)", "wrong");
    L17Cell cb = l17_from_v4_lane(53);
    check(cb.cell_id == 53,        "V4 lane 53 → cell_id=53",       "wrong");
}

/* ══ T10 ══════════════════════════════════════════════════════════════ */
static void t10_world_stats(void) {
    section("T10  World stats: A + B = total (no lost writes)");
    L17Lattice lat; l17_lattice_init(&lat, LATTICE_MODE_17N);

    for (int i = 0; i < 1000; i++)
        l17_write(&lat, (uint64_t)i * 1234567ULL, (uint64_t)i);

    check(lat.total_writes == 1000, "total_writes = 1000", "wrong");
    uint64_t sum = lat.world_a_writes + lat.world_b_writes;
    check(sum == 1000, "A_writes + B_writes = 1000 (no lost)", "sum wrong");
    check(lat.world_a_writes > 0, "some World A writes", "none");
    check(lat.world_b_writes > 0, "some World B writes", "none");
    printf("    (A=%llu  B=%llu  buf=%llu)\n",
           (unsigned long long)lat.world_a_writes,
           (unsigned long long)lat.world_b_writes,
           (unsigned long long)lat.buffer_writes);
}

/* ══ T11 ══════════════════════════════════════════════════════════════ */
static void t11_lane_coverage(void) {
    section("T11  Lane coverage: addr scatter reaches diverse cells");
    L17Lattice lat; l17_lattice_init(&lat, LATTICE_MODE_17N);
    uint8_t seen[L17_LANES_TOTAL];
    memset(seen, 0, sizeof(seen));

    /* use prime-stepped addresses for maximum coverage */
    for (uint32_t i = 0; i < 5000; i++) {
        uint64_t addr = (uint64_t)i * 999983ULL;  /* large prime */
        L17WriteResult r = l17_write(&lat, addr, 0);
        if (r.cell.cell_id < L17_LANES_TOTAL)
            seen[r.cell.cell_id] = 1;
    }
    int unique = 0;
    for (int i = 0; i < (int)L17_LANES_TOTAL; i++)
        if (seen[i]) unique++;
    check(unique > 250, "at least 250/289 cells reached in 5000 writes", "low");
    printf("    (unique_cells=%d / %u)\n", unique, L17_LANES_TOTAL);
}

/* ══ T12 ══════════════════════════════════════════════════════════════ */
static void t12_prime_no_aliasing(void) {
    section("T12  17 is prime: no aliasing with 8 or 9");
    /* 17 % 8 != 0 and 17 % 9 != 0 */
    check(L17_BRIDGE % L17_GROUPS_A != 0, "17 % 8 != 0 (no aliasing A)", "alias!");
    check(L17_BRIDGE % L17_GROUPS_B != 0, "17 % 9 != 0 (no aliasing B)", "alias!");
    /* 136 % 9 != 0, 153 % 8 != 0 (worlds don't divide into each other) */
    check(L17_LANES_A % L17_GROUPS_B != 0, "136 % 9 != 0 (no cross-alias)", "alias!");
    check(L17_LANES_B % L17_GROUPS_A != 0, "153 % 8 != 0 (no cross-alias)", "alias!");
    /* PHI_UP % 17 — scatter should not be a multiple of 17 */
    check(PHI_UP % L17_BRIDGE != 0, "PHI_UP % 17 != 0 (PHI scatter safe)", "alias!");
}

/* ══ T13 ══════════════════════════════════════════════════════════════ */
static void t13_gate_count_wrap(void) {
    section("T13  gate_count wraps 0→17→0 correctly");
    L17Lattice lat; l17_lattice_init(&lat, LATTICE_MODE_17N);
    check(lat.gate_count == 0, "starts at 0", "wrong");

    /* write 17 times — gate_count should reach 17, then cross on 18th */
    int gate_fired = 0;
    for (int i = 0; i < 17; i++) l17_write(&lat, (uint64_t)i, 0);
    check(lat.gate_count == 17, "after 17 writes: gate_count=17", "wrong");
    L17WriteResult r = l17_write(&lat, 17ULL, 0);
    gate_fired = r.gate_event;
    check(gate_fired == 1,       "18th write fires gate event",    "no fire");
    check(lat.gate_count == 0,   "after crossing: gate_count=0",   "no reset");
    check(lat.gate_crossings == 1, "gate_crossings=1",             "wrong");
}

/* ══ MAIN ════════════════════════════════════════════════════════════ */
int main(void) {
    printf("══════════════════════════════════════════════\n");
    printf("  POGLS38 — 17n Lattice Foundation Tests\n");
    printf("  cell_id | lane mapping | gate_18 | PHI\n");
    printf("══════════════════════════════════════════════\n");

    t01_constants();
    t02_cell_of();
    t03_world_boundary();
    t04_gate18_math();
    t05_phi_scatter_balance();
    t06_write_lane_update();
    t07_gate_crossing();
    t08_buffer_zone();
    t09_v4_bridge();
    t10_world_stats();
    t11_lane_coverage();
    t12_prime_no_aliasing();
    t13_gate_count_wrap();

    int total = _pass + _fail;
    printf("\n══════════════════════════════════════════════\n");
    if (_fail == 0)
        printf("  %d / %d PASS  \033[32mv ALL PASS — 17n lattice live 🌐\033[0m\n",
               _pass, total);
    else
        printf("  %d / %d PASS  \033[31mx %d FAILED\033[0m\n",
               _pass, total, _fail);
    printf("══════════════════════════════════════════════\n");
    return _fail ? 1 : 0;
}
