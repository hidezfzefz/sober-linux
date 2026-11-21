# Full project review — percent complete, roadmap and risks

Date: 2026-11-20. Scope: every phase (1–10), every source/test/doc area, every
decision (000–038), and the private-repo delivery. Fourteen independent auditors
(A1–A14) reviewed separate domains in parallel; their reports are
`analysis/REVIEW_A*.md`. This document is the lead synthesis.

**Engine identity used throughout:** AppImage BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
No Flatpak (`791c3f9e` / 2.745) RVA is used anywhere in the shipped source.

## 1. Headline verdict

**Overall: ≈46% complete (sensitivity band 43–52%).**

The original goal — a native Linux ELF running the real Roblox client with a
genuinely rendered login/app-shell frame witnessed by a notifier-driven
guest→Java callback under clean state — is **NOT PROVEN**. Infrastructure is
strong; the decisive claims are at zero. The `app shell rendered` marker is
correctly **WITHHELD** under decisions 013/038, and `tests/app-shell.sh` is
intentionally RED.

| Milestone | Status |
| --- | --- |
| Native ELF, no translation layer | ✅ proven |
| Guest init: 3,618 ctors / 565,390 APS2 relocations | ✅ proven |
| Native window + a guest-rendered first frame (`0x649d422`) | ✅ proven (narrow) |
| Full JNIEnv / typed JNI surface | ⚠️ partial, mostly opt-in |
| Real app state (singleton/controller/delegate/488) | ⚠️ diagnosis mostly done, no clean default-path creation |
| Genuine `gameLoadedCallback(J)V` from notifier `0x27eaf6e` | ⬜ never observed |
| Login/app-shell frame | ⬜ not proven |
| Strict gate + decisions 013/038 | ✅ enforced; provenance not yet coded positively |
| `app shell rendered` marker | ⬜ WITHHELD |

## 2. Weighted score

| Category | Weight | Score | Contribution |
| --- | --- | --- | --- |
| Guest init / loader / bootstrap | 20% | 0.95 | 19.00 |
| Native render / frame | 15% | 0.60 | 9.00 |
| JNI bridge / typed fidelity | 15% | 0.85 | 12.75 |
| App state (singleton, controller, delegate, params) | 20% | 0.40 | 8.00 |
| Genuine app-shell callback | 20% | 0.00 | 0.00 |
| Gate / tests / docs / delivery | 10% | 0.85 | 8.50 |
| **TOTAL (after integration team)** | **100%** | — | **57.25 ≈ 57%** |

The original audit was 46%. The integration team raised JNI (default slot modules
wired, `jni_probe` PASS), render (guest GOT hooks/lifecycle installed, though
guest-tagged swaps/draws are still zero) and gate (positive provenance wired).
App state still faults when prerequisites are missing, and the decisive callback
leg remains zero — so the marker is correctly WITHHELD.

Even perfect scores everywhere else would reach only 60% while Callback and App
state remain zero — those two legs carry 40% of the goal.

## 3. Domain audit results

| Auditor | Domain | Result |
| --- | --- | --- |
| A1 | Original goal and decisions 000–038 | Infrastructure partial at goal level: **NOT DELIVERED** |
| A2 | Native build / hard constraints | **≈90%**; bit-identical reproducible ELFs, libc-only ldd, frozen identity; gaps are automation/hygiene |
| A3 | Guest init / relocs / allocator / TSD | **≈88%**; 565,390 relocations, 3,614 default ctors + explicit arena/thread init, signal/TSD shim; ctor[0] still opt-in |
| A4 | Native render | **≈55%**; native frame proven, guest frame 0%; `render_ok` set by our own swap |
| A5 | JNI bridge | Default 74/231 = 32% faithful; even with opt-ins 138/231 slots stay stubs; 22/49 cached IDs are placeholders; provenance recorder was dead code (now fixed) |
| A6 | App state | **≈37%**; root causes ~90% mapped, env-free realized state ~20%, admissible evidence 0% |
| A7 | Genuine callback | Static chain fully mapped, runtime occurrence **0%**; no notifier hit, placeId, or RA pair ever observed |
| A8 | Strict gate | Gate upheld, no live false-green; all Tier‑1/2 mutators withheld; provenance required by policy but not positively coded |
| A9 | Tests / verification | **≈57%** intent, **≈15%** CI-gated; strict test correctly RED |
| A10 | Documentation | **≈67%**; honest but numerically stale and internally inconsistent |
| A11 | Delivery | **≈80%**; private repo synced at `afed56d`; single squashed root commit |
| A12 | File / binary inventory | 680 tracked files, 124,314 text lines, 78 binaries; source clean, 387 generated `build/` files committed |
| A13 | Roadmap | Nine blockers, 25 effort units to 100% |
| A14 | Honesty / false-green | Core posture sound, but several public facts stale — not safe to publish as-is until fixed |

## 4. Roadmap to 100%

Effort: L=1, M=2, H=3, VH=5. Critical path: BLK‑01→02→03→04→06→07→08→09
(BLK‑05 parallel). Total **25 units**.

| ID | Blocker | RVAs | Acceptance (all opt-ins OFF) | Effort |
| --- | --- | --- | --- | --- |
| BLK‑01 | TSD / JavaVM / scheduler substrate | GOT `0x72298b0`/`0x72295c8`; keys `0x7282000`/`0x72a1b08`; TLS `obj+0x70`; int3 `0x6caef66`; fault `0x2eb4b65` | TSD bound by default, no int3/realloc abort, JNI_OnLoad globals live | H |
| BLK‑02 | FFlag ctor[0] + registry control block | `0x2e62390→0x2e618b0→0x2e61280`; `0x72a0520+0x38`; faults `0x2e7b8e2`/`0x2e5d42a` | control block non-NULL, faults absent, ordered before post-settings | M |
| BLK‑03 | App singleton + service vector | writer `0x238497d`; gate `0x6096928`; reader `0x30ef840`; fault `0x24ad562` | `0x72cb5e0` non-NULL via guest code, fault gone | H |
| BLK‑04 | Controller / delegate / `app+0x488` | `0x23389f7→0x24ad50a→0x24ae1fe→0x24aea9c`; `0x24af2e6`; `0x25d7e75`/`0x25f7e7e`; faults `0x31a1360`/`0x2772403` | all three fields guest-written, no fake seeds | H |
| BLK‑05 | Typed JNI surface | getters `0x232dbbc`/`0x26feb9b`; slots 90/96/98, 111/112, 114–136, 167–172; `0x227b2c0`/`0x227b4eb` | no generic-String substitution; no `0x6c8f340` retry loop | H |
| BLK‑06 | Guest render / surface binding | `0x649c21e→0x649c272→0x649d422`; GOT `0x722a1a8`/`0x72294a8`; vtable `0x71912e8`; `0x2773194` | guest-tagged swap + ≥1 guest GLES draw; `render_ok` not self-set | M |
| BLK‑07 | Genuine GameLoaded callback | notifier `0x27eaf6e`, publish `0x27eaf2f`, invoke `0x27eaf4c`, slot `0x6dbd860`, thunk `0x25848e0`, RA `0x27eafb3`, introducers `0x2ebd51a`/`0x2ebca9e` | notifier hit with `RSI=placeId>0`, RA pair, callback log; no ALT | VH |
| BLK‑08 | Positive provenance in gate | `sr_dispatch_call` / `sr_CallStaticVoidMethodV` in `native_jni.c` | gate consumes notifier RA as a positive condition with opt-ins unset | M |
| BLK‑09 | Assurance and clean publication | withhold chain, `app-shell.sh`, README, `abi-map.md` | zero unclassified mutators, green exit criterion documented, 4/4 DoD, final re-run | M |

## 5. Risk register (documentation and delivery)

| ID | Risk | Fix status in this review |
| --- | --- | --- |
| R1 | README says provenance "required" in a ✅ row while gate is name-only | reword: required by policy, not yet positively coded |
| R2/R4 | README `guest-init-host` SHA and counts stale; progress.svg marks provenance/probe green | regenerate with current SHA, set both tiles orange |
| R3 | "22 withhold knobs" understates the live OR-chain | cite live chain, drop fixed count |
| R5 | verification badge links stale report 471 | link D12, stamp 463/471 superseded |
| R6–R9 | phantom `SOBER_SEED_JNI_STRING_BRIDGE`, "open inconsistency", "B+C not enacted", stale HONESTY claims | amend 013/037/PROGRESS/memo |
| DOC‑03 | `abi-map.md` cites Flatpak-bound Ghidra evidence as authoritative | add wrong-build caveat |
| H1 | 387 generated `build/` files committed; orphan Win32 object; stale SHA | hygiene follow-up |
| H2 | `SOBER_PROP_*` and Tier‑3 config seeds outside withhold chain | add for strict parity before any future green |

## 6. Integration follow-up (team `TEAM-HCUY-MAGB-VPX7`)

A three-worker integration team is wiring the delivered default-ON modules into
the real default path. `analysis/MODULE_INTEGRATION.md` is the authoritative
one-module-per-slot table and boot order; it resolves duplicate pairs
(`fields_default` over `platform_fields`, `alloc_default` over `alloc_contract_fix`,
`tsd_default` over the inline TSD knob, `javavm_default` over `javavm_semantics`,
etc.). The render contract is now
`render_ok = phase3_done && guest_tagged_render_ok()`, with a guest-tagged swap
and counted guest draw decisive; `ANativeWindow_fromSurface` is supporting
evidence, not a hard requirement (T3‑5 question resolved by lead).

The 46% score stands until the wiring is built and A/B verified; it will be
recomputed in the final verification task (#480). Marker stays WITHHELD.

`analysis/T2_5_flags_latch.md` has now landed: the latch write at `0x2eb7520`
is unconditional, but `nativeInitClientSettings 0x2eb7155` exits early on parse
ctx byte `0x7697d08 == 1` (and on empty jstrings), so our calls return rc=1 with
latch 0. The legitimate sibling writer is `CrashpadHandlerMain 0x2ec9c30(int,
char**)`: argv `"--channel" "LIVE"` populates global `0x72a3710` and reaches
`0x2ec9eed → 0x2ec9fae`, writing `0x7cdd149 = 1`. This was routed to worker-2
for default-path wiring; it is guest initialization, not a crash fix, and emits
no callback.

### Worker hand-offs (2026-11-21)

- **worker-2 (#477, harness)** wired all default modules in dependency order and
  added every new knob plus four `should_withhold()` terms to `seeded_state`. Its
  default A/B still fails: FFlag ctor[0] replay reports guard `257` but
  `handle+0x38` control block is NULL, so the singleton gate returns `-3`, the
  flag setter is skipped, and a late guest worker faults through
  `0x2e5c3c1 → 0x2e5d454 → 0x2ed3ad8 → 0x6caee44 → 0x6caed7c` (the known
  `0x2e5d42a` family). `app488` ctor `0x253c93c` also faults when the FFlag
  control block is missing, so app-state calls are now guarded. No genuine
  notifier callback; `guest-tagged swaps=0`; marker WITHHELD.
- **worker-3 (#478, bootstrap/render)** wired all eight allocator bridges and
  `render_guest` GOT hooks. Build, ctest 2/2, verify-native, guest-dlopen and
  first-frame all PASS; engine SHA unchanged. `render_guest` reports installed
  with context vtable `0x71912e8`, all four lifecycle thunks called once, 48
  proc loads, 5 draw procs wrapped — but `swaps(total/guest/synth)=1/0/1` and
  `draws=0`: the only swap is our synthetic host swap. Strict gate correctly
  WITHHELD; fromSurface is present but not decisive per the lead contract.
- **worker-1 (#476)** is finishing the JNI/CMake integration (one module per
  slot, provenance dispatch hook). Until that lands, `is_genuine()` cannot fire.


## 7. Final statement

The project has built a genuinely native, translation-free host for the frozen
Android guest, with verified init, relocation, window and render mechanics, and
an extensive honest diagnostic apparatus. What it has **not** done is present a
login/app-shell frame or observe a guest-driven app-shell callback. The marker is
withheld, the strict test is red, and no success is claimed. The next unit of
work is BLK‑01 (TSD/JavaVM/scheduler substrate), not another callback shortcut.
