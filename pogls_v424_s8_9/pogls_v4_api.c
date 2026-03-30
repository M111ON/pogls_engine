/*
 * pogls_v4_api.c — POGLS V4  Public C API implementation
 * ══════════════════════════════════════════════════════════════════════
 * Session 8
 *
 * Build (.so):
 *   gcc -shared -fPIC -O2 -o libpogls_v4.so pogls_v4_api.c \
 *       pogls_delta.c pogls_delta_world_b.c pogls_delta_ext.c \
 *       pogls_snapshot.c pogls_audit.c pogls_compute_lut.c \
 *       -I. -lpthread
 *
 * Python ctypes usage:
 *   lib = ctypes.CDLL("./libpogls_v4.so")
 *   ctx = ctypes.c_void_p()
 *   lib.pogls_open(b"myfile.dat", 0, ctypes.byref(ctx))
 * ══════════════════════════════════════════════════════════════════════
 */

#include "pogls_v4_api.h"

/* internal includes */
#include "pogls_platform.h"
#include "geo_config.h"
#include "geomatrix_shared.h"
#include "geo_thirdeye.h"
#include "geo_shatter.h"
#include "geo_net.h"
#include "geo_radial_hilbert.h"
#include "geo_pipeline_wire.h"
#include "pogls_geomatrix.h"
#include "pogls_qrpn_phaseE.h"
#include "pogls_pipeline_wire.h"
#include "pogls_delta.h"
#include "pogls_delta_world_b.h"
#include "pogls_delta_ext.h"
#include "pogls_snapshot.h"
#include "pogls_audit.h"
#include "pogls_v3.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ══════════════════════════════════════════════════════════════════════
 * Internal context
 * ══════════════════════════════════════════════════════════════════════ */

struct pogls_ctx_s {
    /* pipeline */
    PipelineWire       pw;

    /* delta storage */
    Delta_Context      delta_a;    /* World A lanes 0-3   */
    Delta_ContextB     delta_b;    /* World B lanes 4-255 */
    Delta_ContextAB    delta_ab;   /* combined view       */

    /* audit */
    POGLS_AuditContext audit;

    /* snapshot */
    uint64_t           next_snapshot_id;

    /* static seed derived from source_path */
    GeoSeed            seed;

    /* bundle (sig reference) — 8 words, derived from seed */
    uint64_t           bundle[8];

    char               source_path[512];
    uint8_t            is_open;
};

/* ── seed derivation from path ──────────────────────────────────────── */
static GeoSeed _seed_from_path(const char *path) {
    uint64_t h = 0xcbf29ce484222325ULL;   /* FNV-1a offset basis */
    for (const char *p = path; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 0x100000001b3ULL;
    }
    GeoSeed s;
    s.gen2 = h ^ (h >> 33);
    s.gen3 = h * POGLS_PHI_UP;
    return s;
}

static void _bundle_from_seed(const GeoSeed *s, uint64_t bundle[8]) {
    uint64_t v = s->gen2;
    for (int i = 0; i < 8; i++) {
        v ^= v >> 17;
        v *= POGLS_PHI_DOWN;
        v ^= v >> 31;
        bundle[i] = v;
    }
}

/* ══════════════════════════════════════════════════════════════════════
 * LIFECYCLE
 * ══════════════════════════════════════════════════════════════════════ */

POGLS_API uint32_t pogls_api_version(void) {
    return POGLS_V4_API_VERSION;
}

POGLS_API int pogls_open(const char   *source_path,
                          uint32_t      flags,
                          pogls_ctx_t **ctx_out)
{
    (void)flags;
    if (!source_path || !ctx_out) return POGLS_ERR_ARG;

    pogls_ctx_t *ctx = (pogls_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return POGLS_ERR_NOMEM;

    strncpy(ctx->source_path, source_path, sizeof(ctx->source_path) - 1);

    /* seed + bundle */
    ctx->seed = _seed_from_path(source_path);
    _bundle_from_seed(&ctx->seed, ctx->bundle);

    /* pipeline init */
    pipeline_wire_init(&ctx->pw, ctx->seed, ctx->bundle);

    /* delta World A */
    int r = delta_open(&ctx->delta_a, source_path);
    if (r != 0) { free(ctx); return POGLS_ERR_IO; }

    /* delta World B */
    r = delta_b_open(&ctx->delta_b, source_path);
    if (r != 0) {
        delta_close(&ctx->delta_a);
        free(ctx);
        return POGLS_ERR_IO;
    }

    ctx->delta_ab.a = ctx->delta_a;
    ctx->delta_ab.b = ctx->delta_b;

    ctx->next_snapshot_id = 1;
    ctx->is_open = 1;
    *ctx_out = ctx;
    return POGLS_OK;
}

POGLS_API int pogls_close(pogls_ctx_t *ctx) {
    if (!ctx || !ctx->is_open) return POGLS_ERR_ARG;

    /* drain GPU queue before close */
    pipeline_wire_drain_gpu(&ctx->pw);
    pipeline_wire_destroy(&ctx->pw);

    delta_close(&ctx->delta_a);
    delta_b_close(&ctx->delta_b);

    ctx->is_open = 0;
    free(ctx);
    return POGLS_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 * WRITE PATH
 * ══════════════════════════════════════════════════════════════════════ */

POGLS_API int pogls_write(pogls_ctx_t *ctx,
                           uint8_t      lane_id,
                           const void  *data,
                           uint32_t     size)
{
    if (!ctx || !ctx->is_open || !data || size == 0) return POGLS_ERR_ARG;

    /* check shatter write-freeze */
    if (!shat_writes_ok(&ctx->pw.shat)) return POGLS_ERR_FROZEN;

    int r;
    if (lane_id < LANE_B_START) {
        /* World A */
        r = delta_append(&ctx->delta_a, lane_id, data, size);
    } else {
        /* World B */
        r = delta_b_append(&ctx->delta_b, lane_id, data, size);
    }
    return (r == 0) ? POGLS_OK : POGLS_ERR_IO;
}

POGLS_API int pogls_commit(pogls_ctx_t *ctx) {
    if (!ctx || !ctx->is_open) return POGLS_ERR_ARG;

    /* drain GPU QRPN queue first */
    pipeline_wire_drain_gpu(&ctx->pw);

    /* audit both worlds */
    int ra = delta_audit(&ctx->delta_a);
    int rb = delta_b_audit(&ctx->delta_b);
    if (ra != 0 || rb != 0) return POGLS_ERR_AUDIT;

    /* dual Merkle commit */
    Delta_DualMerkleRecord rec;
    int r = delta_dual_merkle_compute(&ctx->delta_a, &ctx->delta_b, &rec);
    if (r != 0) return POGLS_ERR_IO;

    /* World A commit */
    r = delta_commit(&ctx->delta_a);
    if (r != 0) return POGLS_ERR_IO;

    /* World B commit */
    r = delta_b_commit(&ctx->delta_b);
    if (r != 0) return POGLS_ERR_IO;

    /* write merkle_AB */
    char pogls_dir[600];
    snprintf(pogls_dir, sizeof(pogls_dir), "%s.pogls", ctx->source_path);
    r = delta_dual_merkle_write(pogls_dir, &rec);
    return (r == 0) ? POGLS_OK : POGLS_ERR_IO;
}

/* ══════════════════════════════════════════════════════════════════════
 * READ PATH
 * ══════════════════════════════════════════════════════════════════════ */

POGLS_API int pogls_read_merkle(pogls_ctx_t *ctx, uint8_t out_root[64]) {
    if (!ctx || !ctx->is_open || !out_root) return POGLS_ERR_ARG;

    char pogls_dir[600];
    snprintf(pogls_dir, sizeof(pogls_dir), "%s.pogls", ctx->source_path);

    Delta_DualMerkleRecord rec;
    int r = delta_dual_merkle_read(pogls_dir, &rec);
    if (r != 0) return (r == -1) ? POGLS_ERR_IO : POGLS_ERR_CORRUPT;

    memcpy(out_root,      rec.root_a, 32);
    memcpy(out_root + 32, rec.root_b, 32);
    return POGLS_OK;
}

POGLS_API int pogls_address(double theta, uint32_t n_bits, uint64_t *out_addr) {
    if (!out_addr || n_bits == 0 || n_bits > 32) return POGLS_ERR_ARG;
    *out_addr = pogls_compute_address(theta, n_bits);
    return POGLS_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 * AUDIT & STATUS
 * ══════════════════════════════════════════════════════════════════════ */

POGLS_API int pogls_audit(pogls_ctx_t *ctx) {
    if (!ctx || !ctx->is_open) return POGLS_ERR_ARG;
    int ra = delta_audit(&ctx->delta_a);
    int rb = delta_b_audit(&ctx->delta_b);
    return (ra == 0 && rb == 0) ? POGLS_OK : POGLS_ERR_AUDIT;
}

POGLS_API int pogls_status(pogls_ctx_t *ctx, pogls_status_t *out) {
    if (!ctx || !out) return POGLS_ERR_ARG;
    memset(out, 0, sizeof(*out));

    out->total_ops      = ctx->pw.total_ops;
    out->qrpn_fails     = ctx->pw.qrpn_fails;
    out->qrpn_state     = ctx->pw.geo.gn.eye.qrpn_state;
    out->shat_stage     = (uint8_t)ctx->pw.shat.stage;
    out->drift_active   = te_is_drift(&ctx->pw.geo.gn.eye);
    out->writes_frozen  = ctx->pw.shat.writes_frozen;
    out->shatter_count  = ctx->pw.shat.shatter_count;
    out->epoch          = ctx->delta_ab.a.epoch;

#ifdef QRPN_GPU_ENABLED
    out->gpu_fail_count = ctx->pw.gpu_fail_count;
#endif

    /* count active World B lanes (lazy: check lane_fd >= 0) */
    uint32_t active = 0;
    for (int i = 0; i < (int)LANE_B_COUNT; i++)
        if (ctx->delta_b.lane_fd[i] >= 0) active++;
    out->lane_b_active = active;

    return POGLS_OK;
}

/* ══════════════════════════════════════════════════════════════════════
 * SNAPSHOT
 * ══════════════════════════════════════════════════════════════════════ */

POGLS_API int64_t pogls_snapshot_create(pogls_ctx_t *ctx,
                                         uint8_t      is_checkpoint)
{
    if (!ctx || !ctx->is_open) return (int64_t)POGLS_ERR_ARG;

    /* must commit first */
    int r = pogls_commit(ctx);
    if (r != POGLS_OK) return (int64_t)r;

    int64_t sid = (int64_t)ctx->next_snapshot_id++;

    POGLS_SnapshotHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.snapshot_id    = (uint64_t)sid;
    hdr.state          = 0;   /* SNAP_PENDING */
    hdr.is_checkpoint  = is_checkpoint;
    hdr.created_at_ms  = 0;   /* caller fills if needed */

    r = pogls_snap_certify(&hdr, NULL, NULL);
    return (r == 0) ? sid : (int64_t)POGLS_ERR_IO;
}

POGLS_API int pogls_snapshot_verify(pogls_ctx_t *ctx, int64_t snapshot_id) {
    if (!ctx || !ctx->is_open || snapshot_id <= 0) return POGLS_ERR_ARG;
    /* verify merkle integrity as proxy */
    uint8_t root[64];
    int r = pogls_read_merkle(ctx, root);
    return (r == POGLS_OK) ? POGLS_OK : POGLS_ERR_CORRUPT;
}

/* ══════════════════════════════════════════════════════════════════════
 * UTILITY
 * ══════════════════════════════════════════════════════════════════════ */

POGLS_API const char *pogls_strerror(int err) {
    switch (err) {
    case POGLS_OK:           return "OK";
    case POGLS_ERR_ARG:      return "bad argument";
    case POGLS_ERR_IO:       return "I/O error";
    case POGLS_ERR_AUDIT:    return "audit/merkle fail";
    case POGLS_ERR_FROZEN:   return "write frozen (PRE_SHATTER)";
    case POGLS_ERR_CORRUPT:  return "data integrity fail";
    case POGLS_ERR_NOMEM:    return "out of memory";
    default:                 return "unknown error";
    }
}

POGLS_API const char *pogls_shat_stage_name(uint8_t stage) {
    return shat_stage_name((ShatStage)stage);
}
