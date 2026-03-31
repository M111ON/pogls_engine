// geo_lite_wire.h
#ifndef GEO_LITE_WIRE_H
#define GEO_LITE_WIRE_H

#include <stdint.h>
#include <stdbool.h>

/* ============================================================
 * CONFIG
 * ============================================================ */
#define GEO_SPOKES        6
#define GEO_INV_OFFSET    3
#define GEO_MASK_512      0x1FF
#define GEO_DRIFT_MOD     144

/* ============================================================
 * CORE STRUCT
 * ============================================================ */
typedef struct {
    uint64_t core;      // base value
    uint64_t invert;    // ~core
    uint16_t mirror;    // 2B mirror encoding
    uint16_t slot;      // 0..511
    uint8_t  spoke;     // 0..5
} geo_block_t;

/* ============================================================
 * FAST PRIMITIVES
 * ============================================================ */

// 1. invert (1 instruction)
static inline uint64_t geo_invert(uint64_t core) {
    return ~core;
}

// 2. XOR check (free checksum)
static inline bool geo_valid(uint64_t core, uint64_t inv) {
    return (core ^ inv) == 0xFFFFFFFFFFFFFFFFULL;
}

// 3. spoke mapping
static inline uint8_t geo_spoke(uint32_t idx) {
    return (uint8_t)(idx % GEO_SPOKES);
}

// 4. invert spoke
static inline uint8_t geo_spoke_inv(uint8_t s) {
    return (uint8_t)((s + GEO_INV_OFFSET) % GEO_SPOKES);
}

// 5. slot mapping
static inline uint16_t geo_slot(uint32_t raw) {
    return (uint16_t)(raw & GEO_MASK_512);
}

/* ============================================================
 * MIRROR (2B encoding)
 * ============================================================ */
/*
 * bit layout:
 * [15:13] axis mask (3 bits)
 * [12:0 ] offset (0..8191)
 */
static inline uint16_t geo_mirror_encode(uint8_t axis, uint16_t offset) {
    return (uint16_t)(((axis & 0x7) << 13) | (offset & 0x1FFF));
}

static inline uint8_t geo_mirror_axis(uint16_t m) {
    return (uint8_t)(m >> 13);
}

static inline uint16_t geo_mirror_offset(uint16_t m) {
    return (uint16_t)(m & 0x1FFF);
}

/* ============================================================
 * MIX (entropy guard)
 * ============================================================ */
static inline uint64_t geo_mix64(uint64_t x) {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
}

/* ============================================================
 * BUILD BLOCK
 * ============================================================ */
static inline geo_block_t geo_build(uint64_t input, uint32_t idx) {
    geo_block_t b;

    uint64_t core = geo_mix64(input);

    b.core   = core;
    b.invert = geo_invert(core);

    b.spoke  = geo_spoke(idx);
    b.slot   = geo_slot((uint32_t)core);

    // simple mirror: axis from spoke, offset from core
    b.mirror = geo_mirror_encode(b.spoke & 0x3, (uint16_t)(core >> 16));

    return b;
}

/* ============================================================
 * DRIFT DETECTOR (ThirdEye lite)
 * ============================================================ */
static inline bool geo_is_drift(uint32_t seq) {
    return (seq % GEO_DRIFT_MOD) == 1;
}

/* ============================================================
 * SHATTER (lite)
 * ============================================================ */
typedef enum {
    GEO_OK = 0,
    GEO_DRIFT,
    GEO_ANOMALY,
    GEO_SHATTER
} geo_state_t;

static inline geo_state_t geo_state_eval(
    uint32_t seq,
    uint32_t anomaly_count,
    uint32_t ghost_streak)
{
    bool drift = geo_is_drift(seq);

    if (drift && anomaly_count >= 2 && ghost_streak >= 4)
        return GEO_SHATTER;

    if (anomaly_count > 0)
        return GEO_ANOMALY;

    if (drift)
        return GEO_DRIFT;

    return GEO_OK;
}

/* ============================================================
 * REPAIR (invert symmetry)
 * ============================================================ */
static inline void geo_repair(geo_block_t *b) {
    uint8_t inv_spoke = geo_spoke_inv(b->spoke);
    b->spoke = inv_spoke;
    // slot unchanged → symmetry repair
}

/* ============================================================
 * PROCESS (FAST PATH)
 * ============================================================ */
static inline bool geo_process(uint64_t input,
                               uint32_t idx,
                               geo_block_t *out)
{
    geo_block_t b = geo_build(input, idx);

    // fast integrity
    if (!geo_valid(b.core, b.invert))
        return false;

    *out = b;
    return true;
}

/* ============================================================
 * OPTIONAL: TAG (anti-pattern)
 * ============================================================ */
static inline uint64_t geo_tag(uint64_t core) {
    return core ^ (core >> 17);
}

#endif // GEO_LITE_WIRE_H