# Task #527 — Fail-closed negative A/B audit and marker-gate review

Author: worker-5 (Convio team `TEAM-MD5U-LHXM-7HTN`). Scope: **this file only**.
Analysis-only: no `src/`, `CMakeLists.txt`, `README.md`, `docs/`, or `tests/` file was
edited. `tests/app-shell.sh` and `tests/D09-withhold-audit.sh` were reviewed
read-only. The only command executed against the repo besides reads/`grep` was the
default-OFF, pure-text `bash tests/D09-withhold-audit.sh --run` (its output is
reproduced in §6). The full `tests/app-shell.sh` harness was **not** run: it
rebuilds `build/` and other workers have in-flight uncommitted `src/` changes, so
running it would produce non-attributable evidence rather than an honest A/B.

Line numbers reflect the working tree at review time; several `src/jni/*` files are
being edited concurrently by other workers, so exact line numbers may drift — the
quoted symbols/strings and control flow are the durable evidence.

Frozen engine assumed throughout: BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
**Marker status: WITHHELD. No success claimed. Gate not weakened. Lead commits/pushes.**

---

## 0. Strict contract being audited (decisions 013 / 038)

The sole emitter is `emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER")` at
`src/linux/guest_app_shell.c:1992`. It is reachable only when both
`shell_cb` and `render_ok` are non-zero (early return at `:1967-1984`):

* **Leg C (callback):** `shell_cb = sober_provenance_gate_is_genuine("gameLoadedCallback")
  || ...("onAppShellReloadNeeded")`, then forced to 0 if
  `sober_provenance_gate_is_harness_thunk()` (`:1736-1738`). `onAppShellReloadNeeded`
  is statically dead in this build (`provenance_gate.c:41-48`, `SOBER_PG_QUAL_COUNT=1`),
  so the only effective credential is `gameLoadedCallback`.
  GENUINE requires (`provenance_gate.c:415-418`): frozen BuildID; class/method pointer
  identity at `base+0x72a2f48`/`base+0x72a2fc0`; `place_id > 0` at `base+0x72a31c0`;
  thunk return word `base+0x2584960`; caller return `base+0x27eafb3` (notifier
  `0x27eaf6e`) **and** a guest-code frame above it. Any host-entered thunk sets the
  sticky `g_harness` taint (`:432-435`) which permanently makes `is_genuine()`
  return 0 (`:454-455`). `onAppBridgeNotification` (`0x2eb4bb1`) is explicitly
  disqualified (`:229, 419, 443`).
* **Leg R (render):** `render_ok = guest_render_ok()` (`:1965`) =
  `phase3_done && swaps_guest>=2 && loop_ra_stable && draws>=1 && draws_between>=1
  && window_bound` (`frame_evidence.c:1752-1762`), where `window_bound` is the
  fail-closed G8 verdict (`fe_window_check`, `:1542-1656`, C0–C10).
* **Leg S (clean state):** the `seeded_state` OR-chain (`:1749-1942`) forces
  `shell_cb = 0` and emits `marker withheld: diagnostic guest-state seed active`
  (`:1943-1946`) for any active guest-mutating/opt-in knob. The gate-owned positive
  line `[app-shell] gate provenance: notifier-driven gameLoadedCallback ... -> GENUINE`
  is emitted only from the clean branch (`:1951` → `provenance_gate.c:529-548`).

`tests/app-shell.sh:105-132` encodes this contract as a deliberately RED test;
forbidden in any green run: `ALT PATH`, `HONESTY NOTICE`, `HARNESS-THUNK`,
`marker withheld:...`, `-> ABSENT(`, synthetic swap as proof.

---

## 1. Negative A/B cases that MUST stay RED

All cases below were checked **against the shipped code**. "Blocked at" is the
first mechanism that forces `shell_cb=0`, `render_ok=0`, or a test FAIL. A control
that flips green is a false-green bug.

| # | Negative case (must stay RED) | Blocked at | Verdict |
|---|---|---|---|
| N1 | **Host-triggered swaps only** (Route-B replay through guest PLT, host return address, no guest loop) | `fe_ra_tag()` classifies host RA as `SYNTHETIC` (`frame_evidence.c:556-571`); `s_swaps_guest` stays 0; `guest_render_ok()` needs `swaps_guest>=2` (`:1755`). Test fails at `app-shell.sh:170-174`. | RED ✅ |
| N2 | **Synthetic draw** (host/wrap-time draw, no guest draw invocation) | `draws`/`draws_between` count only guest-tagged draws (`:1712-1722`); wrap-time `g_gles_draw_calls` is *not* a gate input. Test `app-shell.sh:173`. | RED ✅ |
| N3 | **Fake / harness-thunk callback** (host-entered `0x25848e0` thunk, e.g. `SOBER_ALT_CALLBACK_PATH`) | provenance `g_harness` sticky taint → `is_genuine()==0` (`provenance_gate.c:432-435,454-455`); plus `shell_cb=0` at `guest_app_shell.c:1738`; plus forced withhold via `SOBER_ALT_CALLBACK_PATH` in chain (`:1768`). Test forbids `ALT PATH`/`HONESTY NOTICE`/`HARNESS-THUNK` (`app-shell.sh:138-142,158-162`). | RED ✅ |
| N4 | **`seeded_state > 0`** (any guest-mutating opt-in set) | chain forces `shell_cb=0` and emits the withhold line (`guest_app_shell.c:1943-1946`); test fails at `app-shell.sh:143-145,163-164`. | RED ✅ **with gaps — see §3** |
| N5 | **Missing placeId** (`placeId<=0` at `base+0x72a31c0`) | `genuine` requires `place_id>0` (`provenance_gate.c:409-418`); `emit_provenance()` re-checks `pid<=0` (`:537`). `SOBER_PLACEID_VALUE`/`SOBER_PLACEID_DEFAULT` are in the chain (`guest_app_shell.c:1904-1905`). | RED ✅ |
| N6 | **Wrong RA pair** (thunk `0x2584960` without notifier return `0x27eafb3`, or `onAppBridgeNotification`, or host frame above notifier) | `sc.caller==CALLER_NOTIFIER` + `sc.guest_above_notifier` required (`provenance_gate.c:333-345,415-418`); appbridge disqualified. Test requires both RVAs in the gate line (`app-shell.sh:158`). | RED ✅ |
| N7 | **No G8 window proof** (bare non-NULL `0x72a3308`, host store, wrong fromSurface RA, no `mNativeObject`, no H6 log) | `frame_evidence_window_bound()` returns false unless `fe_window_check()==ACCEPT` (`frame_evidence.c:1735-1739`), which rejects on C0–C10 (`R0_UNVERIFIED_ENGINE`, `R1_HOST_PUBLISH`, `R2_RA_NOT_HELPER`, `R2_MIXED_FROMSURFACE`, `R2_OPCODE_MISMATCH`, `R3_NO_MNATIVEOBJECT`, `R4_RET_MISMATCH`, `R5_GENERATION`, `R6_WINDOW_MISMATCH`, `R7_NO_RELEASE`, `R8_NO_ACQUIRE`, `R9_LAYOUT`, `R10_NO_LOG`). | RED ✅ |

**Structural note on N7 / positive reachability:** G8 condition C10 (`frame_evidence.c:1643-1656`)
requires exactly one `SOBER_FRAME_WOBS_GUEST_LOG` observation, but the H6 FLog-sink
observer is installed **only** when `SOBER_FRAME_FLOG_SINK=1`
(`frame_evidence.c:361-369,495-506,1153-1155`). That knob is itself in the
`seeded_state` chain (`guest_app_shell.c:1758`). Therefore a clean run
(`seeded_state==0`) has no H6 log and G8 rejects, while a run that can satisfy G8
is seeded and withheld. The marker is thus **structurally unreachable** today —
correct fail-closed behaviour under 013/038, but it means the render-leg assertions
in `app-shell.sh:170-183` can never co-occur with `seeded_state=0` (see §4 F4).

---

## 2. Wave4 review (commit `348d1b2` + in-flight task #507 work)

### 2.1 crashpad_return — adequately withheld
`sober_crashpad_return_install()` is called unconditionally at
`guest_app_shell.c:1072`, but self-gates on `SOBER_CRASHPAD_RETURN`
(`crashpad_return.c:63-66,177-181`); when unset it returns DISABLED without
seeding state or installing the `__sF` fputs/fflush bridge (`g_bridge_on`, `g_active`
stay 0). All four knobs (`SOBER_CRASHPAD_RETURN`, `_ARGV`, `_PASSCRED`, `_STDIO`)
**and** `sober_crashpad_return_should_withhold()` are in the chain
(`guest_app_shell.c:1750-1754`). `should_withhold()` is sticky
(`g_active || g_bridge_on`, `crashpad_return.c:68-71`). **No false-green found.**
Minor honesty note: the default-on `__sF` bridge routes guest log text to the same
stderr as the gate lines; it cannot synthesize host-formatted gate lines, but it
can add guest text, so keep the forbidden-line greps anchored (`app-shell.sh` uses
plain substring greps at `:138-145` — false-RED, not false-GREEN, risk only).

### 2.2 frame FLOG/EGL knobs — adequately withheld
`SOBER_FRAME_EGL_INIT_DRIVE`, `SOBER_FRAME_FLOG_ENABLE`, `SOBER_FRAME_FLOG_SINK`
are all in the chain (`guest_app_shell.c:1756-1758`) **and**
`sober_frame_evidence_should_withhold()` covers all three at runtime
(`frame_evidence.c:1333-1345`, added to chain at `guest_app_shell.c:1764`). The
FLOG_ENABLE path genuinely mutates a guest settings word via guest setter
(`frame_evidence.c:403-464`) and EGL_INIT_DRIVE re-drives guest EGL init (`:1475`),
so the double coverage is appropriate. **No false-green found** in these three
knobs themselves.

### 2.3 MessageBus preinit — two mechanisms, one gap
* Wave4 direct preinit: `SOBER_MESSAGEBUS_PREINIT` calls genuine guest `0x25448f6`
  (`guest_app_shell.c:1450-1461`) and is in the chain (`:1755`). Adequately withheld.
* Task #507 publish_default preinit (uncommitted working-tree change to
  `src/jni/publish_default.c/.h`): `SOBER_PREINIT_BUS`
  (`publish_default.h:136`) calls genuine guest `0x24a8e44(4)`
  (`publish_default.c:85-169`). This env is **not** in the `guest_app_shell.c`
  chain and is invisible to D09 (macro-based `getenv`). It is *functionally*
  covered only when it actually seeds state: `sober_publish_default_should_withhold()`
  checks `g_bus_preinit_calls != 0` (`publish_default.c:171-191`), and that counter
  is incremented only on the seeding branch (`:158-159`) — the early "bus already
  ready" return at `:153-154` does not increment it. Publication itself always
  increments `g_publish_calls`/`g_register_calls`/hits (`:486,542-543`), so a
  publication that produces the genuine callback is always withheld. **No live
  false-green today, but a brittle, audit-invisible path — see §3 F2.**

---

## 3. New false-green risks identified

F1 — **`sober_frame_callbacks_should_withhold()` is not ORed into `seeded_state`.**
`src/jni/frame_callbacks.c:435` provides the bit explicitly "for the lead's
force-withhold OR-chain" (`frame_callbacks.h:64-68`). The module is DEFAULT ON,
seeds a 0x278-byte `nativeData` with genuine guest RVA callback slots including
redraw `0x649d422` (`frame_callbacks.h:40-58,61-68,124-129`), and its documented
success criteria are exactly the render leg the gate consumes ("guest-RA draws >= 1,
guest-tagged swaps >= 2"). Neither `SOBER_FRAME_CALLBACKS` nor its
`should_withhold()` appears in `guest_app_shell.c` (verified: zero matches). It is
currently **unwired** (no external call sites), so there is no live false-green;
but if wired it would manufacture the render leg while `seeded_state` stays 0.
This is a latent false-green and should be closed before wiring.

F2 — **`SOBER_PREINIT_BUS` / cross-TU knobs are outside the chain and outside D09.**
See §2.3. `D09` hardcodes a single source file (`D09-withhold-audit.sh:50`) and its
regex only matches literal `getenv("SOBER_...")` (`:58`), so macro-based lookups
(`getenv(SOBER_PREINIT_BUS_ENV)`) are invisible. The reported "GAP LIST: (none)"
is therefore an artifact of scope, not proof of closure.

F3 — **`SOBER_PROVENANCE_GATE_GUEST_BASE` is not in the withhold chain.**
`provenance_gate.h:109` (`:115` in the current working tree) exposes an env base override used by `discover_base()`
(`provenance_gate.c:150-154`) when `install(NULL)` is taken (`:377`). The chain
contains the *differently named* `SOBER_CALLBACK_PROVENANCE_GUEST_BASE`
(`guest_app_shell.c:1877`), not this one. Guest-app-shell installs with the real
base (`:1010`), so the override is normally unused, but a run that redirects the
credential base is not marked seeded. This is an opt-in that can influence the
positive credential and should either be chained or removed from gate-facing builds.

F4 — **`tests/app-shell.sh` requires a line no source emits, and its render leg is
unreachable in a clean run.**
`app-shell.sh:182-183` requires
`[app-shell] render provenance: guest-tagged frame at 0x649d422 (swaps=N, draws=M), phase3_done=1 -> OK`.
No `.c` file emits that string — only `frame_evidence.h:51` mentions it as a
"gate-owned" line the lead wanted. Additionally, the G2/G8 lines at
`app-shell.sh:170-181` can only be emitted under `SOBER_FRAME_FLOG_SINK=1` (§1 N7),
which forces the withhold line that `app-shell.sh:143` rejects and suppresses the
`seeded_state=0` line that `:163` requires. Result: the test is permanently RED at
multiple independent assertions. That is fail-closed (good) but it is not a usable
positive gate: a future genuine fix would still fail on a missing emitter. Also
note `app-shell.sh:20` exits 0 on a missing engine ("SKIP"), which a CI dashboard
could misread as green; recommend a distinct skip status.

F5 — **D09 benign allowlist and prefix coverage are not machine-checkable.**
`D09-withhold-audit.sh:81-89` hardcodes six benign knobs; it has no model of
`env_prefix_active("SOBER_GAME_")`, `SOBER_STARTAPP_`, `SOBER_SGV2_`,
`SOBER_PLATFORM_PARAMS_`, `SOBER_EVIDENCE_`, `SOBER_PROP_` (chain at
`guest_app_shell.c:1846-1849,1878,1940`). Extending D09 to all TUs without also
teaching it the prefix forms would produce false positives; leaving it as-is
produces false negatives (F2/F3). Both need fixing together.

---

## 4. Recommended assertions / actions to lead

1. Add `sober_frame_callbacks_should_withhold()` to the `seeded_state` OR-chain in
   `guest_app_shell.c`, plus `SOBER_FRAME_CALLBACKS` (and
   `SOBER_FRAME_CALLBACKS_GUEST_ND`) as literal `getenv` entries. Do this **before**
   any wiring of `frame_callbacks`.
2. Add `SOBER_PREINIT_BUS` (and `SOBER_PREINIT_BUS_ENV`'s literal value) to the
   chain, so MessageBus preinit is withheld by the env itself rather than only by
   a runtime counter. Do not rely on `g_bus_preinit_calls` as the sole withhold.
3. Add `SOBER_PROVENANCE_GATE_GUEST_BASE` to the chain, or make the gate ignore the
   env override when a real base was installed by `guest_app_shell.c`. Keep the
   BuildID check (`provenance_gate.c:120-144,216-218`) mandatory either way.
4. Extend D09 to all `src/**/*.c|h`, resolve `SOBER_*_ENV` macros to their string
   values, and model `env_prefix_active("SOBER_*_")` as chain coverage. Keep its
   default-OFF, always-exit-0 diagnostic posture; do not wire it into ctest.
5. Implement the `[app-shell] render provenance: ... -> OK` emitter in
   `frame_evidence.c` (gate-owned, one-shot, only inside the genuine G2/G8 path)
   **or** remove that assertion from `app-shell.sh` with an explicit lead ruling —
   decide deliberately, do not leave an accidental permanent RED.
6. Decide the H6/G8-vs-clean-state tension explicitly: either a non-seeded way to
   obtain the guest publish log, or a documented ruling that Leg R is
   structurally RED and the marker stays WITHHELD. Do not relax C10 to make the
   test green.
7. Keep and, if anything, tighten the forbidden-line checks in `app-shell.sh:138-145`
   and require the gate-owned provenance line at `:158-164`. Add an explicit
   pre-flight assertion that the wave4 knobs (§2) are unset in the default run.
8. Change the missing-engine SKIP at `app-shell.sh:20` to a non-zero/skip-coded
   status so it cannot be reported as a pass.
9. Re-run the full negative matrix N1–N7 after every future knob/wiring change;
   require zero `app shell rendered` emissions in every RED case.

No gate condition was weakened, no marker emitted, and no evidence fabricated.

---

## 5. Evidence index (all read-only)

* `docs/decisions/013-no-weak-app-shell-marker.md:38-50,52-78`
* `docs/decisions/038-strict-gate-alt-thunk-ruling.md:11-84`
* `tests/app-shell.sh:20,105-132,134-193,195-204`
* `tests/D09-withhold-audit.sh:4-24,50,58,66-99,109-143`
* `src/linux/guest_app_shell.c:1731-1738,1745-1942,1943-1992`
* `src/jni/provenance_gate.c:41-48,120-175,264-346,372-458,529-548`;
  `provenance_gate.h:108-132`
* `src/jni/frame_evidence.c:361-376,403-476,495-517,556-594,1333-1345,
  1542-1656,1705-1820,1905-1921`; `frame_evidence.h:47-51,113-134`
* `src/jni/crashpad_return.c:46-82,170-261`
* `src/jni/publish_default.c:85-103,141-191,500-565`; `publish_default.h:119-136`
* `src/jni/frame_callbacks.h:40-68,97-129`; `frame_callbacks.c:435`
* `analysis/F10_frame_false_green.md`, `analysis/G8_guest_window_detection.md`,
  `analysis/wave4-messagebus-preinit.md`, `analysis/crashpad-return-509.md`

## 6. Executed diagnostic output (D09, default-OFF, pure text)

```
=== D09 withhold-chain audit (DECISION 013, strict) ===
source: /home/user/sober-linux/src/linux/guest_app_shell.c
getenv SOBER knobs referenced : 165
withhold-chain knobs found     : 159
-- referenced knobs NOT in the withhold chain (raw difference) --
SOBER_APP_SHELL_DIAGNOSTIC / SOBER_APP_SHELL_WAIT_MS / SOBER_APP_SLOT_DIAG /
SOBER_JNI_VERBOSE / SOBER_TRACE_FREE_SLOT / SOBER_WINDOW_HOLD_MS
-- GAP LIST: guest-mutating knobs NOT in the withhold chain --
(none)
```

That "(none)" is scoped to one TU and literal `getenv` calls only; it does **not**
establish closure for the cross-TU knobs in §3 F1–F3.

---

## 7. Addendum — worker-2 task #522 strict leg (received after initial review)

Worker-2 added a third-frame guest-walk discriminator to the positive credential
(`provenance_gate.c:240-302,388-397,470-480`; `provenance_gate.h:140,246`). Verified
read-only in the working tree:

* `pg_walk_notifier_to_dispatch()` walks
  `notifier_slot → thunk RBP → notifier RBP → node RBP` and requires
  `invoke_ret == base+0x27eaf32` (not the stale `0x27eaf34`) **and**
  `node_caller_ra = [rbp_node+8]` to be guest `.text` (`base ≤ ra < base+0x6d33b90`).
  Incomplete/malformed frames return 0 → `guest_above_notifier = 0` → not GENUINE.
* New evidence: `sober_provenance_gate_node_caller_return_addr()`,
  `Snapshot.node_caller_addr`, and `node_ret=0x…` in `describe()`.

Two additional MUST-STAY-RED controls (extend the matrix in §1):

| # | Negative case | Blocked at | Verdict |
|---|---|---|---|
| N8 | **Host-entered node dispatch `0x27eaf04`** (publish_default re-drive) that nevertheless produces the full thunk `0x2584960` + notifier `0x27eafb3` RA pair, cached class/method and `placeId>0` | `node_caller_ra` is host code → walk fails → `guest_above_notifier=0` → `is_genuine()==0` (`provenance_gate.c:265-301,396-397,470-480`); independent of `seeded_state`. | RED ✅ |
| N9 | **Wrong invoke RA (`0x27eaf34`) or null/malformed frame** | `invoke_ret != base+0x27eaf32` → walk returns 0 (`:288-292`); frame-pointer bounds/alignment checks reject (`:280-286,294-296`). | RED ✅ |

This strengthens the audit: the publish_default re-drive path I flagged in §2.3 /
F2 as only *runtime-counter*-withheld is now also **structurally** excluded from
Leg C even if a host-entered dispatch were to manufacture the thunk/notifier pair.
It does not change F1/F3/F4/F5. Re-run N8/N9 alongside N1–N7 after every future
provenance-gate change. Marker remains WITHHELD; no gate weakening.

---

## 8. Resolution status (re-checked after lead/worker edits)

* **F1 CLOSED** — `guest_app_shell.c` now includes `frame_callbacks.h` and chains
  `SOBER_FRAME_CALLBACKS`, `SOBER_FRAME_CALLBACKS_GUEST_ND` and
  `sober_frame_callbacks_should_withhold()` (`grep`: lines 61, 1771-1773).
* **F2 CLOSED** — `getenv("SOBER_PREINIT_BUS")` is now in the chain (line 1769).
  A new `SOBER_MESSAGEBUS_PRIME` knob (worker-1 fix) is also chained (line 1767).
* **F3 CLOSED** — `getenv("SOBER_PROVENANCE_GATE_GUEST_BASE")` is now in the chain
  (line 1770).
* **F4 OPEN** — no source emits `[app-shell] render provenance: ... -> OK`;
  `app-shell.sh:182` still cannot be satisfied, and the clean-G8/H6 tension is
  unresolved. `app-shell.sh:20` SKIP still exits 0.
* **F5 OPEN** — D09 remains single-TU with literal-`getenv` extraction; re-running it
  now reports 170 referenced / 164 chain knobs and still prints "GAP LIST: (none)",
  which remains a scope artifact, not cross-TU closure.

N1–N9 were re-spot-checked after these edits: all mechanisms still present; no
weakening. Marker WITHHELD.
