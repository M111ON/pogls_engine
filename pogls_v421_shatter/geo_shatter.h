/*
 * geo_shatter.h — POGLS GEOMATRIX  ORB Shatter & Reform
 * ══════════════════════════════════════════════════════════════════════
 *
 * Session 7 — new file
 *
 * Design: 6-stage escalation (warm before shatter, anchor before reform)
 *
 *   NORMAL → DRIFT → STRESSED → PRE_SHATTER → SHATTER → REFORM → NORMAL
 *
 *   DRIFT       : soft signal only (144-cycle +1 drift)
 *   STRESSED    : ThirdEye primary escalation
 *   PRE_SHATTER : warm phase — freeze writes, snapshot anchor
 *   SHATTER     : scatter → isolate (lane 53) → reconstruct via symmetry
 *   REFORM      : gradual re-enable, bleed ghost counters down
 *
 * Trigger (3-condition AND — all must be true):
 *   1. te_is_drift()                    (S7 secondary signal)
 *   2. anomaly_cycles ≥ SHAT_K (2)     (sustained, not spike)
 *   3. detach_drift_count ≥ SHAT_T     (ghost pressure)
 *
 * Key invariant:
 *   PRE_SHATTER duration ≤ 1 full cycle (144 ticks)
 *   Reconstruct uses ThirdEye snapshot + invert symmetry (spoke+3)%6
 *   No geometry constants change — architecture stays frozen
 *
 * Include order (after geo_thirdeye.h):
 *   geo_config.h → geo_thirdeye.h → geo_net.h → geo_shatter.h
 *
 * ══════════════════════════════════════════════════════════════════════
 */

#ifndef GEO_SHATTER_H
#define GEO_SHATTER_H

#include <stdint.h>
#include <string.h>
#include "geo_config.h"
#include "geo_thirdeye.h"   /* ThirdEye, te_is_drift(), QRPN_ANOMALY    */

/* ── Stage enum ─────────────────────────────────────────────────────── */

typedef enum {
    SHAT_NORMAL      = 0,   /* baseline — pass-through                  */
    SHAT_DRIFT       = 1,   /* soft signal: te_is_drift() active        */
    SHAT_STRESSED    = 2,   /* ThirdEye ANOMALY sustained               */
    SHAT_PRE_SHATTER = 3,   /* warm: freeze writes, capture anchor      */
    SHAT_SHATTER     = 4,   /* scatter → isolate → reconstruct          */
    SHAT_REFORM      = 5,   /* gradual re-enable, bleed counters        */
} ShatStage;

/* ── Trigger thresholds ─────────────────────────────────────────────── *
 * SHAT_K : anomaly cycles sustained before PRE_SHATTER                  *
 * SHAT_T : detach drift events (ghost pressure) before PRE_SHATTER      *
 * SHAT_WARM_MAX : max ticks in PRE_SHATTER before forced SHATTER        *
 * SHAT_REFORM_CYCLES : cycles in REFORM before return to NORMAL         */
#define SHAT_K               2u     /* ≥2 sustained anomaly cycles      */
#define SHAT_T               3u     /* ≥3 ghost drift events            */
#define SHAT_WARM_MAX        144u   /* ≤1 cycle (= GEO_TE_CYCLE)        */
#define SHAT_REFORM_CYCLES   2u     /* 2 cycles gradual re-enable       */

/* ── Anchor snapshot — frozen state from last good ThirdEye cycle ───── */

typedef struct {
    uint16_t spoke_count[6];    /* spoke distribution at anchor time    */
    uint16_t hot_slots;         /* hot slot count at anchor             */
    uint8_t  qrpn_state;        /* ThirdEye state at anchor             */
    uint8_t  dominant_spoke;    /* spoke with most ops (invert anchor)  */
    uint32_t op_count;          /* op_count at anchor capture           */
    uint8_t  valid;             /* 1 = anchor captured, 0 = empty      */
    uint8_t  _pad[3];
} ShatAnchor;

/* ── Shard record — data written to lane 53 during SHATTER ──────────── */

typedef struct {
    uint64_t shard_id;          /* monotonic                            */
    uint8_t  spoke;             /* origin spoke [0-5]                   */
    uint8_t  invert_spoke;      /* (spoke+3)%6                          */
    uint8_t  stage_at_shatter;  /* always SHAT_SHATTER                  */
    uint8_t  _pad;
    uint32_t anomaly_cycles;    /* how many cycles triggered shatter    */
    uint64_t anchor_op_count;   /* anchor's op_count reference          */
} ShatShard;                    /* fits in data[5..7] (3×8B = 24B)     */

/* ── Main context ───────────────────────────────────────────────────── */

typedef struct {
    ShatStage stage;            /* current escalation stage             */

    /* Trigger accumulators */
    uint32_t  anomaly_cycles;   /* sustained ANOMALY cycle count        */
    uint32_t  drift_events;     /* ghost drift events since last reset  */
    uint32_t  warm_ticks;       /* ticks spent in PRE_SHATTER           */
    uint32_t  reform_cycles;    /* cycles spent in REFORM               */

    /* Anchor */
    ShatAnchor anchor;          /* frozen state for reconstruct         */

    /* Stats */
    uint32_t  shatter_count;    /* total shatter events                 */
    uint32_t  reform_count;     /* total successful reforms             */
    uint64_t  shard_id_next;    /* monotonic shard counter              */

    /* Reconstruct result */
    uint8_t   recon_spoke;      /* majority spoke after reconstruct     */
    uint8_t   recon_valid;      /* 1 = reconstruct produced valid state */
    uint8_t   writes_frozen;    /* 1 = PRE_SHATTER: no new writes       */
    uint8_t   _pad;
} ShatCtx;

/* ── Init ───────────────────────────────────────────────────────────── */

static inline void shat_init(ShatCtx *sc) {
    memset(sc, 0, sizeof(*sc));
    sc->stage = SHAT_NORMAL;
}

/* ── Capture anchor from ThirdEye snap ──────────────────────────────── */

static inline void _shat_capture_anchor(ShatCtx *sc, const ThirdEye *te) {
    const GeoSnap *s = &te->snap[te->head];
    uint8_t dom = 0;
    uint16_t max = 0;
    for (uint8_t i = 0; i < 6; i++) {
        sc->anchor.spoke_count[i] = s->spoke_count[i];
        if (s->spoke_count[i] > max) { max = s->spoke_count[i]; dom = i; }
    }
    sc->anchor.hot_slots      = s->hot_slots;
    sc->anchor.qrpn_state     = te->qrpn_state;
    sc->anchor.dominant_spoke = dom;
    sc->anchor.op_count       = te->op_count;
    sc->anchor.valid          = 1;
}

/* ── Reconstruct: majority(spoke, invert_spoke) ─────────────────────── *
 * Uses anchor symmetry: dominant_spoke vs (dominant+3)%6               *
 * Returns winning spoke as new routing hint                             */
static inline uint8_t _shat_reconstruct(ShatCtx *sc) {
    if (!sc->anchor.valid) return 0;
    uint8_t  d  = sc->anchor.dominant_spoke;
    uint8_t  iv = (d + 3u) % 6u;                    /* invert = +3      */
    uint16_t d_count  = sc->anchor.spoke_count[d];
    uint16_t iv_count = sc->anchor.spoke_count[iv];
    /* majority vote: higher count = more stable spoke                  */
    sc->recon_spoke = (d_count >= iv_count) ? d : iv;
    sc->recon_valid = 1;
    return sc->recon_spoke;
}

/* ── Main tick — call once per op alongside te_tick ────────────────── *
 *                                                                       *
 * te             : current ThirdEye (read-only)                         *
 * drift_events   : new ghost drift events this tick (from detach stats) *
 * at_cycle_end   : 1 if te->op_count % TE_CYCLE == 0 (cycle boundary)  *
 *                                                                       *
 * Returns current ShatStage for caller to act on.                      */
static inline ShatStage shat_tick(ShatCtx      *sc,
                                   const ThirdEye *te,
                                   uint32_t        new_drift_events,
                                   uint8_t         at_cycle_end)
{
    sc->drift_events += new_drift_events;

    switch (sc->stage) {

    /* ── NORMAL ── */
    case SHAT_NORMAL:
        if (te_is_drift(te))
            sc->stage = SHAT_DRIFT;
        break;

    /* ── DRIFT ── */
    case SHAT_DRIFT:
        if (!te_is_drift(te)) {
            sc->stage = SHAT_NORMAL;   /* drift gone — back down        */
            break;
        }
        if (te->qrpn_state == QRPN_ANOMALY)
            sc->stage = SHAT_STRESSED;
        break;

    /* ── STRESSED ── */
    case SHAT_STRESSED:
        if (at_cycle_end && te->qrpn_state == QRPN_ANOMALY)
            sc->anomaly_cycles++;

        /* de-escalate if anomaly clears */
        if (te->qrpn_state != QRPN_ANOMALY && sc->anomaly_cycles == 0) {
            sc->stage = SHAT_DRIFT;
            break;
        }

        /* 3-condition trigger → PRE_SHATTER */
        if (te_is_drift(te)
            && sc->anomaly_cycles >= SHAT_K
            && sc->drift_events   >= SHAT_T)
        {
            sc->stage       = SHAT_PRE_SHATTER;
            sc->warm_ticks  = 0;
            sc->writes_frozen = 1;                  /* freeze writes    */
            _shat_capture_anchor(sc, te);           /* snapshot anchor  */
        }
        break;

    /* ── PRE_SHATTER (warm) ── */
    case SHAT_PRE_SHATTER:
        sc->warm_ticks++;
        /* forced shatter after SHAT_WARM_MAX ticks (≤1 cycle)         */
        if (sc->warm_ticks >= SHAT_WARM_MAX) {
            sc->stage = SHAT_SHATTER;
            sc->shatter_count++;
            _shat_reconstruct(sc);                  /* pre-compute recon*/
        }
        break;

    /* ── SHATTER ── */
    case SHAT_SHATTER:
        /* Caller reads sc->recon_spoke for routing hint                */
        /* Move to REFORM immediately after 1 tick                      */
        sc->stage         = SHAT_REFORM;
        sc->reform_cycles = 0;
        sc->writes_frozen = 0;                      /* re-enable writes */
        sc->drift_events  = 0;                      /* reset counters   */
        sc->anomaly_cycles = 0;
        break;

    /* ── REFORM ── */
    case SHAT_REFORM:
        if (at_cycle_end)
            sc->reform_cycles++;
        if (sc->reform_cycles >= SHAT_REFORM_CYCLES
            && te->qrpn_state == QRPN_NORMAL)
        {
            sc->stage = SHAT_NORMAL;
            sc->reform_count++;
            sc->recon_valid = 0;                    /* clear recon hint */
        }
        break;
    }

    return sc->stage;
}

/* ── Build shard record for lane 53 (data[5..7]) ────────────────────── */

static inline ShatShard shat_make_shard(ShatCtx *sc, uint8_t spoke) {
    ShatShard sh;
    sh.shard_id        = sc->shard_id_next++;
    sh.spoke           = spoke % 6u;
    sh.invert_spoke    = (sh.spoke + 3u) % 6u;
    sh.stage_at_shatter = (uint8_t)SHAT_SHATTER;
    sh._pad            = 0;
    sh.anomaly_cycles  = sc->anomaly_cycles;
    sh.anchor_op_count = sc->anchor.op_count;
    return sh;
}

/* ── Query helpers ──────────────────────────────────────────────────── */

static inline uint8_t shat_writes_ok(const ShatCtx *sc) {
    return !sc->writes_frozen;
}

static inline uint8_t shat_in_shatter(const ShatCtx *sc) {
    return sc->stage == SHAT_SHATTER;
}

static inline uint8_t shat_in_reform(const ShatCtx *sc) {
    return sc->stage == SHAT_REFORM;
}

/* ── Stage name (debug) ─────────────────────────────────────────────── */

static inline const char *shat_stage_name(ShatStage s) {
    switch (s) {
    case SHAT_NORMAL:      return "NORMAL";
    case SHAT_DRIFT:       return "DRIFT";
    case SHAT_STRESSED:    return "STRESSED";
    case SHAT_PRE_SHATTER: return "PRE_SHATTER";
    case SHAT_SHATTER:     return "SHATTER";
    case SHAT_REFORM:      return "REFORM";
    default:               return "?";
    }
}

static inline void shat_status(const ShatCtx *sc) {
    printf("[Shatter] stage=%s  anomaly_cyc=%u  drift_ev=%u"
           "  warm=%u  shatters=%u  reforms=%u\n",
           shat_stage_name(sc->stage),
           sc->anomaly_cycles, sc->drift_events,
           sc->warm_ticks, sc->shatter_count, sc->reform_count);
    if (sc->recon_valid)
        printf("  recon_spoke=%u  invert=%u\n",
               sc->recon_spoke, (sc->recon_spoke + 3u) % 6u);
    if (sc->anchor.valid)
        printf("  anchor: op=%u  dom_spoke=%u  hot=%u\n",
               sc->anchor.op_count,
               sc->anchor.dominant_spoke,
               sc->anchor.hot_slots);
}

#endif /* GEO_SHATTER_H */
