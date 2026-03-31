# GEOMATRIX — Handoff v4.2.4 → Session 10
Session 9 Complete · March 2026

---

## ⚠️ NAMING CLARIFICATION (อ่านก่อนทุกครั้ง)

| ชื่อใน code | คือ | หมายเหตุ |
|---|---|---|
| `QRPN_NORMAL/STRESSED/ANOMALY` | ThirdEye state enum | อยู่ใน `geo_thirdeye.h` — ไม่ใช่ POGLS verify |
| `qrpn_ctx_t` | POGLS verify context | อยู่ใน `pogls_qrpn_phaseE.h` — คนละตัว |
| `ShatStage` 0..5 | NORMAL→DRIFT→STRESSED→PRE_SHATTER→SHATTER→REFORM | อยู่ใน `geo_shatter.h` |
| `rh_audit_group(buf,spoke,0)` | **เลิกใช้** | ใช้ `rh_audit_group_domain(buf,spoke,face,group)` |
| `LANE_B_COUNT` | **252** ห้าม hardcode 4 | อยู่ใน `pogls_delta_world_b.h` |
| `DiamondBlock.quad_mirror` | **ลบแล้ว S8** | ใช้ `mirror_offset + mirror_axes + mount[30]` |
| `GEO_BUNDLE_WORDS` | **8** เสมอ | ห้ามเป็น 9 (center face bug) |
| `CYL_FULL_N` ใน gpu_wire | **3072** (512×6) | geo_config.h ยังเป็น 3456 (full geometry) |
| `geo_lite_wire.h` | **XOR drop layer ใหม่** | วางหน้า `geo_net.h` ใน pipeline — S10 |
| `GEO_OK/DRIFT/ANOMALY/SHATTER` | geo_lite_wire state enum | คนละ enum กับ `ShatStage` และ `QRPN_*` |
| `pipeline_wire_process()` return 2 | write frozen (PRE_SHATTER) | ห้ามแก้ return logic |
| `pogls_qrpn.h` | **เวอร์ชันเก่า ห้ามใช้** | ใช้ `pogls_qrpn_phaseE.h` เท่านั้น |

**กฎ include order** (ห้ามเปลี่ยน):
```
pogls_platform.h → geo_config.h → geomatrix_shared.h
    → geo_thirdeye.h → geo_cylinder.h
    → geo_lite_wire.h          ← S10 NEW (XOR drop, หน้า geo_net)
    → geo_net.h
    → geo_radial_hilbert.h → geo_pipeline_wire.h
    → geo_shatter.h
    → pogls_geomatrix.h → pogls_qrpn_phaseE.h
    → pogls_pipeline_wire.h
    → pogls_fold.h → pogls_detach_lane.h
    → pogls_delta_world_b.h → pogls_delta_ext.h
    → pogls_memory.h           ← S9 NEW
    → pogls_v4_api.h           ← consumer-facing only (ท้ายสุด)
```

---

## Architecture Stack (S9 final)

| Layer | File | Role | Status |
|---|---|---|---|
| Config | geo_config.h | SINGLE SOURCE | ✓ frozen |
| Shared | geomatrix_shared.h | GeoPacket, sig helpers | ✓ frozen |
| ThirdEye | geo_thirdeye.h | OBSERVE + drift signal | ✓ frozen |
| Shatter | geo_shatter.h | ESCALATE 6-stage | ✓ frozen |
| Cylinder | geo_cylinder.h | SPACE 3456 geometry | ✓ frozen |
| **XOR Drop** | **geo_lite_wire.h** | **PRE-FILTER ~80% load** | **○ S10 wire** |
| GeoNet | geo_net.h | ROUTE spoke/slot | ✓ frozen |
| Radial Hilbert | geo_radial_hilbert.h | HILBERT + domain XOR | ✓ frozen |
| Pipeline CPU | geo_pipeline_wire.h | GeoNet→RH→GeoPacket | ✓ frozen |
| Fold | pogls_fold.h | DiamondBlock smart mirror | ✓ frozen |
| Detach | pogls_detach_lane.h | shock absorber + ShatShard | ✓ frozen |
| Geomatrix | pogls_geomatrix.h | FILTER 18-path | ✓ frozen |
| QRPN Verify | pogls_qrpn_phaseE.h | Pythagorean + GPU witness | ✓ frozen |
| Master Pipeline | pogls_pipeline_wire.h | full stack + shatter wire | ✓ frozen |
| World B | pogls_delta_world_b.h/.c | 252 lanes + Merkle | ✓ frozen |
| Dual Merkle | pogls_delta_ext.h/.c | V37 format | ✓ frozen |
| LLM Memory | pogls_memory.h | ThirdEye→token 32B | ✓ **S9** |
| Public API | pogls_v4_api.h/.c | .so export | ✓ **S9** |
| Python | pogls_v4.py | ctypes bindings | ✓ **S9** |
| REST | rest_server.py | Flask HTTP wrapper | ✓ **S9** |
| Docker | Dockerfile + docker-compose.yml | deploy | ✓ **S9** |
| GPU Wire | geomatrix_gpu_wire_v33_patched.cu | 3072 space | ✓ frozen |

---

## Session 9 Deliverables (Complete)

| # | File | สิ่งที่ทำ | ผล |
|---|---|---|---|
| P1 | pogls_v4_api.c | `detach_lane_set_shat(&ctx->dl, &ctx->pw.shat)` | ✓ |
| P2 | pogls_v4.py | ctypes wrapper + PoglsCtx/PoglsStatus/PoglsError | ✓ 334 lines |
| P3 | Dockerfile + docker-compose.yml | CPU/GPU build targets | ✓ |
| P4 | rest_server.py | Flask REST 11 endpoints | ✓ 255 lines |
| P5 | pogls_memory.h | PoGLSMemToken 32B + ring buffer 144 slots | ✓ |
| P5 | test_pogls_v4.py | 40 pytest tests | ✓ |

---

## Wireless Hilbert — New Technology (Session 9 handoff)

**สิ่งที่ proven แล้ว** (จาก `wireless_hilbert_handoff.docx`):

| Claim | Status | สรุป |
|---|---|---|
| `invert = NOT(core)` | ✓ PROVEN | 1 CPU instruction, 0 lookup |
| `core ^ invert = 0xFFFF...` | ✓ PROVEN | checksum FREE |
| 3 mirrors derive from offset | ✓ PROVEN | 32B → 10B (68% saved, เข้า mount[30] แล้ว S8) |
| Tamper detection via XOR | ✓ PROVEN | 1 bit flip ตรวจเจอทันที |
| Wireless Hilbert Router concept | ✓ PROVEN | concept solid |
| Router benchmark vs L3 | ⚠️ BUG | PHI scatter ติดมา → fix S10 P1 |

**geo_lite_wire.h — design ที่ส่งมา (ยังไม่ wire)**

```c
// XOR drop: ตัด ~80% load ก่อน geo_net
geo_block_t b;
if (!geo_process(input, idx, &b))  →  DROP (corrupt)

// แทน pipeline เดิม ~8-12 ops ด้วย ~2 ops:
addr & 1 == 0  →  MAIN  (ico / POGLS)
addr & 1 == 1  →  TEMPORAL (dodec)

// State machine
geo_state_t = GEO_OK | GEO_DRIFT | GEO_ANOMALY | GEO_SHATTER
geo_is_drift(seq) = (seq % 144 == 1)   ← ใช้ GEO_DRIFT_MOD = 144 ✓
```

**Bug ที่ต้องแก้ S10:**
```
Router test: PHI scatter ติดมาใน SHADOW check
→ ทุก value ไป SHADOW
→ แก้: SHADOW = QRPN หน้าที่ อย่าให้ geo_lite_wire แตะ PHI scatter
```

**Position ที่ถูกต้องใน pipeline:**
```
[geo_lite_wire XOR drop]  ← ตรงนี้ แทนที่จะผ่าน geo_net ก่อน
    → passed → [geo_net route]
    → dropped → return early (no QRPN, no RH, no Geomatrix)
```

---

## Session 10 Priorities

| # | Task | Detail | File |
|---|---|---|---|
| P1 | **Wireless Router bug fix** | แยก SHADOW ออก — geo_lite_wire ไม่แตะ PHI scatter, SHADOW = QRPN เท่านั้น | `geo_lite_wire.h` |
| P2 | **Wire geo_lite_wire หน้า geo_net** | เพิ่ม step 0 ใน `pipeline_wire_process()`: `geo_process()` → ถ้า false → return early | `geo_pipeline_wire.h` |
| P3 | **Benchmark XOR+geo_net vs L3** | วัด % load reduction จริง (expect ~80%) | test `.cu` |
| P4 | **θ_ico → θ_dodec mapping** | dual geometry exact function: addr&1 routing | `geo_lite_wire.h` ต่อ |
| P5 | **pogls_memory.h integration test** | inject PoGLSMemToken เข้า REST `/status` response | `rest_server.py` |

---

## geo_lite_wire.h — Integration Notes

**ไฟล์ที่ส่งมา:** `geo_lite_wire.c` (extension ผิด — จริงๆ คือ `.h` header-only)

**สิ่งที่ดีแล้ว ใช้ได้เลย:**
- `geo_block_t`, `geo_build()`, `geo_process()`, `geo_valid()`
- `geo_state_t` (GEO_OK/DRIFT/ANOMALY/SHATTER) — ชื่อต่างจาก ShatStage ✓
- `geo_is_drift(seq)` ใช้ `seq % 144` ← ตรงกับ `GEO_DRIFT_MOD = 144 = GEO_TE_CYCLE` ✓
- `geo_spoke()`, `geo_spoke_inv()` ← logic เดียวกับ geo_net ✓
- `geo_mix64()` ใช้ `0xBF58476D1CE4E5B9` ← ตรงกับ `QRPN_SEED_B` ✓

**สิ่งที่ต้อง fix ก่อน wire:**
```c
// S10 P1: ลบ geo_tag() ออกหรือ mark INTERNAL
// geo_tag ใช้ core ^ (core>>17) — อาจชนกับ PHI scatter path ใน SHADOW check

// S10 P2: เพิ่มใน geo_pipeline_wire.h step 0:
static inline uint8_t geo_pipeline_step(...) {
    // STEP 0: XOR drop (NEW S10)
    geo_block_t lite_b;
    if (!geo_process(addr, (uint32_t)(addr % CYL_FULL_N), &lite_b))
        return 2;  // early drop = same as frozen code

    // STEP 1: GeoNet route (เดิม)
    GeoNetAddr a = geo_net_route(...);
    ...
}
```

---

## Constants (FROZEN — ห้ามแก้)

```
PHI_UP=1696631  PHI_DOWN=648055  PHI_SCALE=2^20
17, 18, 54, 144, 162, 289
GEO_TE_CYCLE = GEO_DRIFT_MOD = 144   ← geo_lite_wire ใช้ตรงนี้ถูกต้อง
CAN_ANCHOR=144  TC_CYCLE=720
LANE_B_COUNT = 252, LANE_TOTAL = 256
DELTA_MERKLE_V37_SIZE = sizeof(Delta_DualMerkleRecord) ~2200B
DiamondBlock = 64B = 1 cache line (FROZEN)
GEO_BUNDLE_WORDS = 8 (ห้ามเป็น 9)
sig32 = (sig64>>32) ^ (sig64&0xFFFFFFFF)
NORMAL→DRIFT→STRESSED→PRE_SHATTER→SHATTER→REFORM
```

---

## Pattern Anchors

```
144 × 24 = 3456    full geometry
512 × 6  = 3072    gpu_wire
(spoke+3)%6        invert O(1)
hidx = (lane<<3) | rev3(group)
rh_domain_xor(face,group)    audit XOR
fold_fibo_intersect = AND(Z₄ orbit of core.raw)
core ^ invert = 0xFFFF...FF  XOR checksum FREE
geo_is_drift = seq % 144 == 1
addr & 1 → 0:MAIN(ico)  1:TEMPORAL(dodec)
```

---

## All Sessions Summary

```
S6:  GPU wire 3072, QRPN P2, domain XOR P3
S7:  GPU persistent kernel, 256 lanes, DRIFT signal, ORB Shatter
S8:  Shatter wire, Smart mirror, ShatShard data[5..7], V4 API (.so)
S9:  Python bindings, Docker, REST API, LLM memory layer, dl->shat wire
S10: Wireless Hilbert wire, XOR drop layer, benchmark, θ_ico→θ_dodec
```

---

## Open Questions → S10

- **PHI scatter bug** — `geo_lite_wire` ต้องไม่แตะ scatter path ของ QRPN
- **World B lazy open** — pre-open 252 หรือ on-demand? (ยังไม่ตัดสินใจ)
- **REST `/memory` endpoint** — expose PoGLSMemToken สำหรับ LLM consumer
- **θ_ico → θ_dodec** — dual geometry exact function (P4 S10)
- **Pentagon stack** — Trigger detector — AI temporal zone (ไว้ S11+)

---

*Session 9 → 10 · S9 Complete · Wireless Hilbert ready to wire · geo_lite_wire positioned*
