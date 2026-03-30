# GEOMATRIX — Handoff v3.3 → Session 9
Session 8 Complete · March 2026

---

## ⚠️ NAMING CLARIFICATION (อ่านก่อนทุกครั้ง)

| ชื่อใน code | คือ | หมายเหตุ |
|---|---|---|
| `QRPN_NORMAL/STRESSED/ANOMALY` | ThirdEye state enum | อยู่ใน `geo_thirdeye.h` |
| `ShatStage` | ORB Shatter 6-stage | อยู่ใน `geo_shatter.h` |
| `qrpn_ctx_t` | POGLS verify context | อยู่ใน `pogls_qrpn_phaseE.h` |
| `rh_audit_group(buf,spoke,0)` | **เลิกใช้แล้ว** | ใช้ `rh_audit_group_domain()` แทน |
| `LANE_B_COUNT` | **252** | ห้าม hardcode 4 |
| `DELTA_MERKLE_V37_SIZE` | `sizeof(Delta_DualMerkleRecord)` | ~2200B (S7) |
| `DELTA_MERKLE_CURRENT_SIZE` | alias → V37 | ใช้อันนี้เสมอ |
| `DiamondBlock.quad_mirror` | **ลบแล้ว S8** | ใช้ `mirror_offset + mirror_axes` แทน |
| `fold_build_quad_mirror()` | alias → `fold_mirror_init()` | backward compat |
| `pipeline_wire_process()` return 2 | write frozen | PRE_SHATTER active |

**กฎ include order** (ห้ามเปลี่ยน):
```
pogls_platform.h → geo_config.h → geomatrix_shared.h
    → geo_thirdeye.h → geo_cylinder.h → geo_net.h
    → geo_radial_hilbert.h → geo_pipeline_wire.h
    → geo_shatter.h
    → pogls_geomatrix.h → pogls_qrpn_phaseE.h
    → pogls_pipeline_wire.h
    → pogls_v4_api.h   ← consumer-facing only
```

---

## Architecture Stack (S8 final)

| Layer | File | Role | Status |
|---|---|---|---|
| Config | geo_config.h | SINGLE SOURCE | ✓ locked |
| Shared | geomatrix_shared.h | GeoPacket, sig helpers | ✓ locked |
| ThirdEye | geo_thirdeye.h | OBSERVE + drift signal | ✓ S7 |
| Shatter | geo_shatter.h | ESCALATE 6-stage | ✓ S7 |
| Cylinder | geo_cylinder.h | SPACE 3456 geometry | ✓ locked |
| GeoNet | geo_net.h | ROUTE spoke/slot | ✓ locked |
| Radial Hilbert | geo_radial_hilbert.h | HILBERT + domain XOR | ✓ S7 |
| Pipeline CPU | geo_pipeline_wire.h | GeoNet→RH→GeoPacket | ✓ S7 |
| Fold | pogls_fold.h | DiamondBlock smart mirror | ✓ **S8** |
| Detach | pogls_detach_lane.h | shock absorber + ShatShard | ✓ **S8** |
| Geomatrix | pogls_geomatrix.h | FILTER 18-path | ✓ locked |
| QRPN Verify | pogls_qrpn_phaseE.h | Pythagorean + GPU witness | ✓ locked |
| Master Pipeline | pogls_pipeline_wire.h | full stack + shatter wire | ✓ S7+S8 |
| World B | pogls_delta_world_b.h/.c | 252 lanes + Merkle | ✓ S7+S8 |
| Dual Merkle | pogls_delta_ext.h/.c | V37 format | ✓ **S8** |
| Public API | pogls_v4_api.h/.c | .so export | ✓ **NEW S8** |
| GPU Wire | geomatrix_gpu_wire.cu | 3072 space | ✓ locked |
| QRPN Persistent | pogls_qrpn_persistent.cu | GPU kernel | ✓ S7 wired |

---

## Session 8 Deliverables

| # | File | สิ่งที่ทำ | ผล |
|---|---|---|---|
| B1 | pogls_delta_world_b.c | dynamic filenames, _b_idx 0-251, spoke-pair audit | ✓ |
| B2 | pogls_delta_ext.h/.c | V37 size, MISSING=4 fix, static_assert, V36 compat read | ✓ |
| W1 | pogls_pipeline_wire.h | shat_tick() step 6, return 2=frozen, shat_status() | ✓ |
| W2 | pogls_detach_lane.h | ShatCtx* field, ShatShard → data[5..7] | ✓ |
| S8 | pogls_fold.h | Smart mirror: quad_mirror[32] → offset(1B)+axes(1B)+mount[30] | ✓ |
| S8 | pogls_v4_api.h/.c | NEW — public C API (.so), Makefile | ✓ |

---

## S8 Key: Smart Mirror DiamondBlock (LOCKED)

```
64B layout (unchanged total):
  [0-7]   core.raw          8B  primary
  [8-15]  invert            8B  NOT(core)
  [16]    mirror_offset     1B  rotation stride (default=1)
  [17]    mirror_axes       1B  axis flags bit0=X bit1=Y bit2=Z bit3=W
  [18-47] mount[30]        30B  Face/Shadow virtual attach + ShatShard
  [48-63] honeycomb[16]    16B  Tails reserved

fold_fibo_intersect = core & rot(core,off) & rot(core,off×2) & rot(core,off×3)
Cost: 3 rotations + 3 ANDs (register only, no memory)

mount[30] layout:
  [0-7]  data[5] = ShatShard.shard_id
  [8-15] data[6] = spoke|invert_spoke|stage|anomaly_cycles
  [16-23] data[7] = anchor_op_count
  [24-29] reserved (Rubik recovery future)
```

---

## S8 Key: V4 Public API (.so)

```c
// Build
make cpu    // G4400 CPU-only
make gpu    // Colab T4 (-DQRPN_GPU_ENABLED)

// Lifecycle
pogls_open(path, 0, &ctx)
pogls_close(ctx)

// Write/Commit
pogls_write(ctx, lane_id, data, size)  // returns POGLS_ERR_FROZEN if PRE_SHATTER
pogls_commit(ctx)                       // drain GPU + audit + dual Merkle

// Read
pogls_read_merkle(ctx, root64)
pogls_address(theta, n_bits, &addr)

// Ops
pogls_audit(ctx)
pogls_status(ctx, &st)    // qrpn_state, shat_stage, drift_active, ...
pogls_snapshot_create(ctx, is_checkpoint)

// Python ctypes
lib = ctypes.CDLL("./libpogls_v4.so")
ctx = ctypes.c_void_p()
lib.pogls_open(b"file.dat", 0, ctypes.byref(ctx))
```

---

## ShatShard → data[5..7] Wire (LOCKED)

```
Two worlds mount in DiamondBlock:
  Face world   → core.raw engine_id → EngineSlice (face-attached)
  Shadow world → GiantShadow lane 53 ring (shadow-attached)

detach_flush_pass() ใน PRE_SHATTER/SHATTER:
  if (dl->shat && shat_in_shatter(dl->shat)):
    sh = shat_make_shard(dl->shat, phase18 % 6)
    blk[i].data[5] = sh.shard_id
    blk[i].data[6] = spoke | invert_spoke<<8 | stage<<16 | anomaly_cyc<<32
    blk[i].data[7] = sh.anchor_op_count

Caller wires: dl->shat = &pw->shat  (after pogls_open)
```

---

## Constants (FROZEN — never change)

```
PHI_UP=1696631  PHI_DOWN=648055  PHI_SCALE=2^20
17, 18, 54, 144, 162, 289
CAN_ANCHOR=144  TC_CYCLE=720
sig32 = (sig64>>32) ^ (sig64&0xFFFFFFFF)

144² = 2⁸ × 3⁴ = 256 × 81    binary×ternary lock
6    = spoke axis (sync all worlds)
op%144==1 → DRIFT signal
DRIFT + ANOMALY(≥2) + GHOST(≥3) → PRE_SHATTER
DiamondBlock = 64B = 1 cache line (FROZEN)
LANE_B_COUNT = 252, LANE_TOTAL = 256
DELTA_MERKLE_V37_SIZE = sizeof(Delta_DualMerkleRecord) ~2200B
```

---

## Pattern Anchors

```
144 × 24 = 3456    full geometry
512 × 6  = 3072    gpu_wire
(spoke+3)%6        invert
hidx = (lane<<3) | rev3(group)
rh_domain_xor(face,group)   audit XOR
fold_fibo_intersect = AND(Z₄ orbit of core.raw)  ← S8
NORMAL→DRIFT→STRESSED→PRE_SHATTER→SHATTER→REFORM
```

---

## Session 9 Priorities

| # | Task | Detail |
|---|---|---|
| 1 | **dl->shat wire** | ใส่ `dl.shat = &pw.shat` ใน `detach_lane_init()` หรือ caller side |
| 2 | **Python bindings** | ctypes wrapper + pytest สำหรับ open/write/commit/audit |
| 3 | **Docker** | `Dockerfile` + `docker-compose.yml` สำหรับ REST wrapper |
| 4 | **REST API** | HTTP wrapper บน pogls_v4_api.so (Flask หรือ C httpd) |
| 5 | **LLM memory layer** | `pogls_memory.h` — map ThirdEye state → LLM context token |

---

## Open Questions → Session 9

- **dl->shat wire point** — init หรือ caller? (แนะนำ: `detach_lane_set_shat(dl, shat)` helper)
- **World B lazy open** — pre-open ทั้ง 252 หรือ on-demand ตาม lane_id?
- **REST API framework** — Flask (Python) หรือ embedded C httpd?
- **LLM memory schema** — format ของ angular address → embedding vector?

---

## Files Changed S8

| File | Change |
|---|---|
| `pogls_delta_world_b.c` | dynamic filenames + spoke-pair audit |
| `pogls_delta_ext.h` | V37 + enum fix + static_assert |
| `pogls_delta_ext.c` | V37 size refs + V36 compat |
| `pogls_pipeline_wire.h` | shat_tick wire + return 2 |
| `pogls_detach_lane.h` | ShatCtx* + ShatShard data[5..7] |
| `pogls_fold.h` | Smart mirror (quad→offset+axes+mount) |
| `pogls_v4_api.h` | **NEW** public C API header |
| `pogls_v4_api.c` | **NEW** API implementation |
| `Makefile` | **NEW** build cpu/gpu targets |

---

## All Sessions Summary

```
S6: GPU wire (3072), QRPN P2, domain XOR P3
S7: GPU persistent kernel, 256 lanes, DRIFT signal, ORB Shatter, rh_domain_xor merge
S8: Blocking fixes, Shatter wire, Smart mirror, ShatShard data[5..7], V4 API (.so)
S9: Python bindings, Docker, REST API, LLM memory layer
```

---

*Session 8 → 9 · All blocking resolved · API production-ready · Ready for Colab*
