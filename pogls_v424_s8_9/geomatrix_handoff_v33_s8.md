# GEOMATRIX — Handoff v3.3 → Session 8
Session 7 Complete · March 2026

---

## ⚠️ NAMING CLARIFICATION (อ่านก่อนทุกครั้ง)

| ชื่อใน code | คือ | หมายเหตุ |
|---|---|---|
| `QRPN_NORMAL/STRESSED/ANOMALY` | ThirdEye state enum | อยู่ใน `geo_thirdeye.h` |
| `ShatStage` | ORB Shatter state machine | อยู่ใน `geo_shatter.h` (NEW S7) |
| `qrpn_ctx_t` | POGLS verify context | อยู่ใน `pogls_qrpn_phaseE.h` |
| `qrpn_fails` ใน PipelineWire | counter ของ sig+audit+qrpn fail | ไม่ใช่ QRPN verify โดยตรง |
| `pogls_qrpn.h` | เวอร์ชันเก่า | **ใช้ `pogls_qrpn_phaseE.h` เท่านั้น** |
| `geo_compute_sig32/64` | อยู่ใน `geomatrix_shared.h` | ไม่ใช่ `geo_net.h` |
| `GEO_BUNDLE_WORDS` | ต้องเป็น **8** เสมอ | เคยมี bug เป็น 9 (center face) |
| `CYL_FULL_N` | ต้องเป็น **3072** (512×6) ใน gpu_wire | geo_config.h ยังเป็น 3456 (full geometry) |
| `rh_audit_group(buf,spoke,0)` | **เลิกใช้แล้ว** | ใช้ `rh_audit_group_domain()` แทน |
| `LANE_B_COUNT` | **252** (S7) | เคยเป็น 4 — อย่า hardcode 4 |

**กฎ include order** (ห้ามเปลี่ยน):
```
pogls_platform.h → geo_config.h → geomatrix_shared.h
    → geo_thirdeye.h → geo_cylinder.h → geo_net.h
    → geo_radial_hilbert.h → geo_pipeline_wire.h
    → geo_shatter.h          ← NEW S7 (หลัง geo_thirdeye)
    → pogls_geomatrix.h → pogls_qrpn_phaseE.h
    → pogls_pipeline_wire.h
```

---

## Architecture Stack (updated S7)

| Layer | File | Role | Status |
|---|---|---|---|
| L3 Quad | (pogls existing) | upstream | ✓ unchanged |
| Config | geo_config.h | SINGLE SOURCE — all constants | ✓ locked |
| Shared | geomatrix_shared.h | GeoPacket, sig helpers, PHASE_MASK | ✓ locked |
| ThirdEye | geo_thirdeye.h | OBSERVE — 3-layer + drift signal | ✓ **S7 updated** |
| Shatter | geo_shatter.h | ESCALATE — 6-stage ORB state machine | ✓ **NEW S7** |
| Cylinder | geo_cylinder.h | SPACE — 3456 geometry | ✓ locked |
| GeoNet | geo_net.h | ROUTE — spoke/slot/mask | ✓ locked |
| Radial Hilbert | geo_radial_hilbert.h | HILBERT + domain XOR audit | ✓ **S7 updated** |
| Pipeline | geo_pipeline_wire.h | CPU integration: GeoNet→RH→GeoPacketWire | ✓ **S7 updated** |
| Geomatrix Filter | pogls_geomatrix.h | FILTER — 18-path score [128..144] | ✓ locked |
| QRPN Verify | pogls_qrpn_phaseE.h | VERIFY — Pythagorean + GPU witness | ✓ locked |
| Platform | pogls_platform.h | PHI constants + cross-platform | ✓ locked |
| Master Pipeline | pogls_pipeline_wire.h | MASTER — full stack + QRPN + GPU wire | ✓ **S7 updated** |
| GPU Wire | geomatrix_gpu_wire_v33_patched.cu | GPU kernel — 3072 space | ✓ locked |
| QRPN Persistent | pogls_qrpn_persistent.cu | GPU persistent kernel | ✓ **S7 wired** |

---

## Session 7 Deliverables

| # | File | สิ่งที่ทำ | ผล |
|---|---|---|---|
| T1 | pogls_pipeline_wire.h | Wire GPU persistent kernel — swap `cpu_fallback` → `push/poll` via `#ifdef QRPN_GPU_ENABLED` | ✓ |
| T2a | geo_thirdeye.h | 145-drift secondary signal: `TE_DRIFT_*` constants, `drift_state` field, `te_is_drift()` | ✓ |
| T2b | pogls_delta_world_b.h | Expand World B lanes 4→256 index space: `LANE_B_COUNT=252`, `LANE_B_SPOKE/LOCAL` macros | ✓ |
| T3 | geo_shatter.h | NEW FILE — ORB Shatter 6-stage state machine (warm→shatter→reform) | ✓ |
| T4a | geo_radial_hilbert.h | Merge `rh_domain_xor()` + `rh_audit_group_domain()` from S6 handoff into file | ✓ |
| T4b | geo_pipeline_wire.h | Replace `rh_audit_group(buf,spoke,0)` → `rh_audit_group_domain()` | ✓ |

---

## T1 — GPU Persistent Kernel Integration (LOCKED)

```c
// pogls_pipeline_wire.h — compile flags:
//   Colab T4:  nvcc -DQRPN_GPU_ENABLED ...
//   CPU G4400: gcc  (no flag) — cpu_fallback ทำงานอัตโนมัติ

// struct PipelineWire เพิ่ม:
#ifdef QRPN_GPU_ENABLED
    QrpnPersistentCtx qrpn_gpu;
    uint32_t          gpu_fail_count;
#endif

// step 4 swap point:
#ifdef QRPN_GPU_ENABLED
    qrpn_persistent_push(&pw->qrpn_gpu, qrpn_value);   // async
    // poll fail queue non-blocking ทุก op
#else
    uint32_t Cg = qrpn_gpu_witness_cpu_fallback(qrpn_value);
    int qr = qrpn_check(qrpn_value, addr, Cg, &pw->qrpn, NULL);
#endif

// ก่อน snapshot commit:
pipeline_wire_drain_gpu(&pw);   // flush GPU fail queue
```

**คำอธิบาย cpu_fallback:** `witness_ok = always 1` (ตรวจตัวเอง) — pyth_ok ยังทำงาน
**CPU G4400:** ยอมรับ blind spot นี้ได้ — ไม่กระทบ throughput

---

## T2 — 145-Drift + World B 256 (LOCKED)

### 144² insight
```
144² = 20736 = 2⁸ × 3⁴ = 256 × 81

256 = delta index space (2⁸)
81  = ternary depth     (3⁴)
162 = 81×2 = icosphere nodes (mirror domain)
6   = แกนกลาง (GEO_SPOKES) — sync ทุกโลก
```

### Drift signal
```c
// geo_thirdeye.h
te->drift_state = (op_count % 144 == 1) ? TE_DRIFT_FLAG : 0;
// query: te_is_drift(&te)  → non-zero = soft drift active
// แสดงใน te_status(): [DRIFT+1]
```

### World B lanes
```
LANE_B_COUNT  = 252   (index 4-255)
LANE_TOTAL    = 256   (World A[0-3] + World B[4-255])
LANE_B_SPOKE(lid) = lid % 6   ← align cylinder
LANE_B_LOCAL(lid) = lid / 6   ← position within spoke

Delta_DualMerkleRecord.seq_b[252]  ← size: ~2200B (เพิ่มจาก 180B)
⚠️ pogls_delta_world_b.c + pogls_delta_ext.c ต้อง update size ref
```

---

## T3 — ORB Shatter State Machine (LOCKED)

```
NORMAL → DRIFT → STRESSED → PRE_SHATTER → SHATTER → REFORM → NORMAL
```

### Trigger (3-condition AND)
```c
DRIFT active          // te_is_drift()
anomaly_cycles ≥ 2   // sustained, ไม่ใช่ spike
drift_events   ≥ 3   // ghost pressure
```

### Flow
```
PRE_SHATTER : freeze writes (shat_writes_ok=0)
              capture anchor (ThirdEye snapshot)
              duration ≤ 144 ticks

SHATTER     : scatter → isolate (lane 53)
              reconstruct: majority(dominant_spoke, invert+3)
              shat_make_shard() → data[5..7] detach block

REFORM      : re-enable writes
              bleed counters, 2 cycles
              → NORMAL เมื่อ qrpn_state == NORMAL
```

### API
```c
ShatCtx sc;
shat_init(&sc);

// ทุก op (หลัง te_tick):
ShatStage st = shat_tick(&sc, &te, new_drift_ev, at_cycle_end);

if (!shat_writes_ok(&sc))      // block writes ใน PRE_SHATTER
if (shat_in_shatter(&sc))      // build shard → lane 53
    ShatShard sh = shat_make_shard(&sc, spoke);
```

---

## T4 — rh_domain_xor (LOCKED)

```c
// geo_radial_hilbert.h
uint8_t rh_domain_xor(uint8_t face, uint8_t group) {
    base    = GEO_FACE_UNITS (64)
    face_c  = face  × GEO_GROUP_SIZE
    group_c = group × GEO_SPOKES
    return ((base + face_c + group_c) & 0xFF) ^ (face × group)
}

// ใช้งาน (แทน rh_audit_group ทั้งหมด):
rh_audit_group_domain(buf, spoke, face, group)
//   face  = addr.unit >> 6
//   group = addr.unit >> 3

// Update point — เมื่อมี real domain data:
// แก้แค่ rh_domain_xor() บรรทัดเดียว
```

---

## geo_config.h Constants (FROZEN — unchanged)

| Constant | Value | Meaning |
|---|---|---|
| GEO_FULL_N | **3456** | 6×576 = 144×24 |
| GEO_SLOTS | 576 | 24² per spoke |
| GEO_SPOKES | **6** | แกนกลางทุกอย่าง |
| GEO_FACES | 9 | 8 outer + 1 center |
| GEO_FACE_UNITS | **64** | 8² = 1 face |
| GEO_BUNDLE_WORDS | **8** | outer faces only |
| GEO_GROUP_SIZE | **8** | lines per audit group |
| GEO_TE_CYCLE | **144** | ThirdEye + Drift sync |
| GEO_ANOMALY_HOT | 96 | 576/6 = 1/6 spoke |

⚠️ gpu_wire ใช้ `CYL_FULL_N = 3072` (512×6) — ต่างจาก GEO_FULL_N=3456

---

## Sacred Numbers (FROZEN)

```
PHI_UP=1696631  PHI_DOWN=648055  PHI_SCALE=2^20
17, 18, 54, 144, 162, 289
CAN_ANCHOR=144  CAN_GRID_A=12  CAN_GRID_B=9  TC_CYCLE=720
sig32 = (sig64>>32) ^ (sig64&0xFFFFFFFF)

144² = 2⁸ × 3⁴ = 256 × 81   ← S7 insight (locked)
6    = แกน sync ทุกโลก        ← ยืนยัน S7
```

---

## Pattern Anchors (Quick Reference)

```
144 × 24 = 3456    ThirdEye × closure = full geometry space
512 × 6  = 3072    gpu_wire space (8-face bundle)
576 / 6  = 96      anomaly ceiling
576 / 9  = 64      1 face = origin unit
8 × 9    = 72      imbalance threshold
(spoke+3)%6        invert O(1) — core of everything
hidx = (lane<<3) | rev3(group)   pseudo-hilbert zig-zag
rh_domain_xor(face,group)        audit XOR (S7 merged)
144² = 256 × 81    binary × ternary lock point (S7)
op%144==1          drift signal (S7)
DRIFT+ANOMALY(2)+GHOST(3) → PRE_SHATTER (S7)
```

---

## Session 8 Priorities

| # | Task | Detail |
|---|---|---|
| 1 | **pogls_delta_world_b.c update** | BLOCKING — `seq_b[252]` size ref, commit loop 4→252 |
| 2 | **pogls_delta_ext.c update** | BLOCKING — `Delta_DualMerkleRecord` size ref update |
| 3 | **geo_shatter wire into pipeline** | ใส่ `shat_tick()` ใน `pipeline_wire_process()` หลัง `te_tick` |
| 4 | **ShatShard → detach data[5..7]** | map `ShatShard` struct เข้า detach block field |
| 5 | **V4 C API (.so)** | downstream — หลัง geometry lock complete |

---

## Open Questions → Session 8

- **World B lane open strategy** — lazy open (per-op) หรือ pre-open ทั้ง 252 ตอน init?
- **ShatShard lane 53 format** — ต้องตกลง layout data[5..7] กับ detach_lane.h
- **rh_domain_xor real data** — ยังเป็น geometry-derived, รอ domain data จริง
- **REFORM → NORMAL boundary** — ปัจจุบัน 2 cycles, อาจปรับตาม test result

---

## Files Changed S7

| File | Change |
|---|---|
| `pogls_pipeline_wire.h` | GPU kernel wire + drain/destroy |
| `geo_thirdeye.h` | drift_state + te_is_drift() |
| `pogls_delta_world_b.h` | LANE_B_COUNT 4→252 + macros |
| `geo_shatter.h` | **NEW** — 6-stage ORB Shatter |
| `geo_radial_hilbert.h` | rh_domain_xor + rh_audit_group_domain |
| `geo_pipeline_wire.h` | rh_audit_group_domain replace |

---

*Session 7 → 8 · T1/T2/T3/T4 Complete · All headers consistent · Ready for Colab*
