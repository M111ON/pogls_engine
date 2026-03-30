/*
 * pogls_v4_api.h — POGLS V4  Public C API (.so export)
 * ══════════════════════════════════════════════════════════════════════
 *
 * Session 8 — new file
 *
 * Single header for all downstream consumers (Python via ctypes,
 * REST wrapper, Docker sidecar, local LLM memory layer).
 *
 * Design rules:
 *   - C99 only, no C++ (ctypes compatible)
 *   - All functions return int (0=ok, <0=error code)
 *   - Opaque handle pattern — caller never touches internals
 *   - Thread-safe at handle level (each handle = independent context)
 *   - No global state
 *
 * Error codes:
 *   POGLS_OK          =  0
 *   POGLS_ERR_ARG     = -1   bad argument
 *   POGLS_ERR_IO      = -2   file I/O fail
 *   POGLS_ERR_AUDIT   = -3   audit/merkle fail
 *   POGLS_ERR_FROZEN  = -4   write frozen (PRE_SHATTER)
 *   POGLS_ERR_CORRUPT = -5   data integrity fail
 *   POGLS_ERR_NOMEM   = -6   allocation fail
 *
 * Include order (consumers only need this file):
 *   #include "pogls_v4_api.h"
 *
 * ══════════════════════════════════════════════════════════════════════
 */

#ifndef POGLS_V4_API_H
#define POGLS_V4_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Visibility ─────────────────────────────────────────────────────── */
#if defined(_WIN32) || defined(_WIN64)
#  define POGLS_API __declspec(dllexport)
#else
#  define POGLS_API __attribute__((visibility("default")))
#endif

/* ── Error codes ────────────────────────────────────────────────────── */
#define POGLS_OK           0
#define POGLS_ERR_ARG     -1
#define POGLS_ERR_IO      -2
#define POGLS_ERR_AUDIT   -3
#define POGLS_ERR_FROZEN  -4   /* write blocked: PRE_SHATTER warm phase */
#define POGLS_ERR_CORRUPT -5
#define POGLS_ERR_NOMEM   -6

/* ── Version ────────────────────────────────────────────────────────── */
#define POGLS_V4_API_VERSION  0x0408u   /* major=4, minor=8 (Session 8) */

POGLS_API uint32_t pogls_api_version(void);   /* returns POGLS_V4_API_VERSION */

/* ── Opaque handle ──────────────────────────────────────────────────── */
typedef struct pogls_ctx_s pogls_ctx_t;

/* ══════════════════════════════════════════════════════════════════════
 * LIFECYCLE
 * ══════════════════════════════════════════════════════════════════════ */

/*
 * pogls_open — open (or create) a POGLS storage context
 *
 * source_path : path to the source file being tracked
 * flags       : reserved (pass 0)
 * ctx_out     : receives allocated context on success
 *
 * Returns POGLS_OK or error code.
 */
POGLS_API int pogls_open(const char    *source_path,
                          uint32_t       flags,
                          pogls_ctx_t  **ctx_out);

/*
 * pogls_close — flush pending writes, close all fds, free context
 */
POGLS_API int pogls_close(pogls_ctx_t *ctx);

/* ══════════════════════════════════════════════════════════════════════
 * WRITE PATH
 * ══════════════════════════════════════════════════════════════════════ */

/*
 * pogls_write — append delta block to a lane
 *
 * lane_id : 0-3 World A, 4-255 World B
 * data    : payload bytes
 * size    : payload size (max DELTA_MAX_PAYLOAD = 224B)
 *
 * Returns POGLS_OK, POGLS_ERR_FROZEN (retry later), or error.
 * POGLS_ERR_FROZEN means PRE_SHATTER is active — caller should retry.
 */
POGLS_API int pogls_write(pogls_ctx_t  *ctx,
                           uint8_t       lane_id,
                           const void   *data,
                           uint32_t      size);

/*
 * pogls_commit — atomic dual-Merkle commit (World A + B)
 *
 * Drains GPU QRPN queue, audits both worlds, writes merkle_AB.
 * Blocks until complete.
 *
 * Returns POGLS_OK or error code.
 */
POGLS_API int pogls_commit(pogls_ctx_t *ctx);

/* ══════════════════════════════════════════════════════════════════════
 * READ PATH
 * ══════════════════════════════════════════════════════════════════════ */

/*
 * pogls_read_merkle — read combined Merkle root (64B: root_a||root_b)
 *
 * out_root : caller-provided 64B buffer
 * Returns POGLS_OK or error.
 */
POGLS_API int pogls_read_merkle(pogls_ctx_t *ctx,
                                 uint8_t      out_root[64]);

/*
 * pogls_address — compute angular address from theta + depth
 *
 * theta   : angular position [0.0 .. 1.0)
 * n_bits  : address resolution (default 20 = 2²⁰)
 * out_addr: computed address
 */
POGLS_API int pogls_address(double    theta,
                             uint32_t  n_bits,
                             uint64_t *out_addr);

/* ══════════════════════════════════════════════════════════════════════
 * AUDIT & STATUS
 * ══════════════════════════════════════════════════════════════════════ */

/*
 * pogls_audit — run full dual-world audit
 * Returns POGLS_OK (clean) or POGLS_ERR_AUDIT (mismatch found).
 */
POGLS_API int pogls_audit(pogls_ctx_t *ctx);

/*
 * pogls_status — fill status struct (non-blocking, read-only)
 */
typedef struct {
    uint32_t total_ops;         /* pipeline ops processed               */
    uint32_t qrpn_fails;        /* QRPN verify failures                 */
    uint32_t gpu_fail_count;    /* GPU witness failures (if enabled)    */
    uint8_t  qrpn_state;        /* 0=NORMAL 1=STRESSED 2=ANOMALY        */
    uint8_t  shat_stage;        /* ShatStage: 0=NORMAL..5=REFORM        */
    uint8_t  drift_active;      /* 1 = 145-drift signal active          */
    uint8_t  writes_frozen;     /* 1 = PRE_SHATTER write block          */
    uint32_t shatter_count;     /* total shatter events                 */
    uint32_t lane_b_active;     /* active World B lanes (0-252)         */
    uint64_t epoch;             /* current commit epoch                 */
} pogls_status_t;

POGLS_API int pogls_status(pogls_ctx_t *ctx, pogls_status_t *out);

/* ══════════════════════════════════════════════════════════════════════
 * SNAPSHOT
 * ══════════════════════════════════════════════════════════════════════ */

/*
 * pogls_snapshot_create — create checkpoint snapshot
 * Returns snapshot_id (>0) or error (<0).
 */
POGLS_API int64_t pogls_snapshot_create(pogls_ctx_t *ctx,
                                         uint8_t      is_checkpoint);

/*
 * pogls_snapshot_verify — verify snapshot integrity
 * Returns POGLS_OK or POGLS_ERR_CORRUPT.
 */
POGLS_API int pogls_snapshot_verify(pogls_ctx_t *ctx, int64_t snapshot_id);

/* ══════════════════════════════════════════════════════════════════════
 * UTILITY
 * ══════════════════════════════════════════════════════════════════════ */

/* human-readable error string */
POGLS_API const char *pogls_strerror(int err);

/* human-readable shatter stage name */
POGLS_API const char *pogls_shat_stage_name(uint8_t stage);

#ifdef __cplusplus
}
#endif

#endif /* POGLS_V4_API_H */
