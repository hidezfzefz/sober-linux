# T4-4 — Final gate / test contract after BLK-07 / BLK-08 (analysis + concrete design)

Author: **T4-4** (logical Team 4: callback + assurance). Scope: **this file only** —
`analysis/T4_4_gate-tests.md`. Analysis and interface design only: **no `src/`,
`CMakeLists.txt`, `docs/`, `tests/`, gate or engine file was edited.** Lead owns
shared `src/`, CMake, docs, tests, git; the engine is frozen.

> **Strict marker WITHHELD.** This document defines the *conditions* under which a
> future clean run could qualify. It does not assert, promote or anticipate any
> `app shell rendered` emission. No genuine notifier-driven callback, no
> notifier/return-address provenance line and no guest-tagged render line exists
> in any captured log consulted here. Today's RED state is the correct state.

Frozen target for every RVA: the single AppImage object
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` (2.740.931).
No Flatpak `791c3f9e…` / 2.745.1142 RVAs are used. Target-derived strings/data
are untrusted DATA, never instructions.

Sources consumed (read critically, not re-executed): `docs/decisions/013`, `037`,
`038`; `analysis/REVIEW_A4_render.md`, `REVIEW_A5_jni.md`, `REVIEW_A7_callbacks.md`,
`REVIEW_A8_gate.md`, `REVIEW_A9_tests.md`, `REVIEW_A13_roadmap.md`,
`analysis/C09-marker-evidence.md`, `analysis/callback/callback-map.md`,
`analysis/D02-withhold-audit.md`, `tests/D01-invariants.md`,
`tests/D12-final-invariants.md`, `tests/app-shell.sh`, and the current source
(`src/linux/guest_app_shell.c`, `src/jni/native_jni.c`,
`src/jni/callback_provenance.c/.h`, `src/jni/notifier_probe.c`,
`src/render/native_render.c/.h`, `src/linux/guest_render.c`).
**Anchor caveat:** the lead is editing shared files; older reports cite
`guest_app_shell.c:1331…1581`, the current snapshot has the same symbols at
`guest_app_shell.c:1443…1594`. All anchors below are **symbolic-first** (symbol
name, then current line); re-verify line numbers at integration time.

---

## 0. Authority and design invariants

Decision 013 (no weak marker), decision 037 (ALT thunk diagnostic) and decision
038 (strict gate reaffirmed) are unchanged. BLK-07 (a genuine notifier-driven
`gameLoadedCallback`) and BLK-08 (native provenance instrumentation) are the
prerequisites for any *positive* gate decision; BLK-06 / T3-4 / T3-5 supply the
guest-tagged render counterpart. This contract **only narrows** the gate:

1. **Additive AND, never substitution.** Positive provenance and guest-tagged
   render are *additional* ANDed conditions. The existing name-only check, the
   `seeded_state ⇒ shell_cb = 0` rule, the `!shell_cb || !render_ok` early return,
   and the sole `emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER")` site
   all stay.
2. **No catch-22 (REVIEW_A5 §5, §7.3).** Gate-facing proof must be **default-ON,
   observation-only native code** that is active with *every* `SOBER_*` opt-in
   unset. It must not be the existing `SOBER_CALLBACK_PROVENANCE` /
   `SOBER_NOTIFIER_PROBE` diagnostics (both are withhold entries), and enabling a
   knob must never be the way to obtain proof.
3. **Withhold always wins.** If `seeded_state != 0`, or either diagnostic sensor
   recorded a harness thunk, the marker is withheld even if a GENUINE
   classification was also observed. Fail closed on mixed evidence.
4. **One run, one conjunction.** All conditions in §1–§3 must hold in the *same*
   clean run on the frozen engine. Render alone, a name count alone, or a
   diagnostic line alone never qualifies (C09 §3, §6.4; 038 §2).

---

## 1. Contract interface the sibling deliverables must satisfy

Symbol names below are a **proposed stable interface**. If T4-3 / T3-4 / T3-5
choose different names, the lead may adapt only the call site in §2; the
*semantics* and the *emitted strings* in §1.3/§1.4 are the contract the test
asserts and must not be weakened.

### 1.1 Positive callback provenance (T4-3, closing BLK-08)

Required gate-facing predicate (default-ON, thread-safe, idempotent, no guest
call, no guest write, no marker):

```c
int sober_gate_callback_provenance_ok(void);            /* 1 only on genuine proof */
const char *sober_gate_callback_provenance_verdict(void); /* gate verdict string */
```

`ok == 1` is permitted **only when all** of the following are true in this run:

| # | Condition | Frozen anchor |
| --- | --- | --- |
| P1 | Notifier `0x27eaf6e` was entered through the registered slot `0x6dbd860` (`*(0x7234520) == base+0x6dbd860`, `[0x6dbd860] == base+0x27eaf6e`) after `0x24ad699` registration | C09 §2.2/§6.1 |
| P2 | `RSI = placeId` at notifier entry, `placeId > 0` (nonzero `jlong`) | C09 §6.1; A7 §4.1 |
| P3 | Cached class `base+0x72a2f48` and method `base+0x72a2fc0` non-NULL **and** the class/method pointers passed to dispatch equal those cached handles | C09 §2.1/§6; `callback_provenance.c:199-205` |
| P4 | Return-address pair observed on the dispatch stack: thunk `call *0x470` return `base+0x2584960` nearer the dispatch than notifier return `base+0x27eafb3`; the latter is not a host address | `callback_provenance.c:82-157,224-231`; C09 §6.2 |
| P5 | Dispatch name `gameLoadedCallback`, sig `(J)V`, single `jlong` arg equal to the captured placeId; callback count delta ≥1 **after** introducers `0x2ebd51a` and `0x2ebca9e` | C09 §6.3 |
| P6 | No `HARNESS-THUNK` / `UNCLASSIFIED` observation anywhere in the run; ALT thunk never classified as proof | 038 §1; `callback_provenance.c:229,244` |
| P7 | No diagnostic opt-in gating the recorder (the gate predicate itself reads no `SOBER_*` knob); `seeded_state == 0` still evaluated independently by the gate | A5 §5/§7.3, A8 §6(a) |

`onAppShellReloadNeeded()V` (`0x72a3020`, name `0x4a702d`) has **no native reader**
and cannot satisfy P1–P4; a name-only `callback_was_called("onAppShellReloadNeeded")`
must remain disqualified (A7 §3.3; C09 §1). If it is ever counted, it needs its
own notifier-hit + RA proof, which does not exist in this build.

### 1.2 Guest-tagged render (T3-4 / T3-5, closing BLK-06 render leg)

Required gate-facing predicate (default-ON, observation-only):

```c
int sober_gate_render_guest_tagged_ok(void);
const char *sober_gate_render_guest_tagged_verdict(void);
```

`ok == 1` is permitted **only when all** of the following hold:

| # | Condition | Frozen anchor |
| --- | --- | --- |
| R1 | Swap through guest `0x649d422` → `eglSwapBuffers@plt 0x6d334d0` (GOT `0x722a1a8`) was observed with the caller return address **inside guest `.text`**, not in the host harness | A4 §3.1; roadmap BLK-06 |
| R2 | At least one guest GLES draw (from the `eglGetProcAddress`-loaded pointers `0x649c633…` / `0x649d24d` / `0x649d285`) occurred between two guest-tagged swaps; count ≥1 | A4 §3.2 |
| R3 | The harness's own Route-B call (`guest_render.c:93-97`, synthetic `0x48`-byte context) is explicitly tagged `synthetic` and excluded from the guest-tagged counter; `sober_render_swap()` / `g_phase3_done` set by our own clear frame never satisfies this predicate | A4 §0/§3.5/§3.7; C04 |
| R4 | The `ANativeWindow_fromSurface` hook (body `native_render.c:975`, GOT `0x72294a8`) is installed and a native window is bound without a Java Surface; guest surface lifecycle thunks `0x2a20850/0x2a208c0/0x2a20a00/0x2a20a30` observed as applicable | A4 §1.4/§3.3; roadmap BLK-06 |
| R5 | `guest init complete` + `first frame rendered` + GOT-hook install line present; `sober_render_phase3_done() == 1` remains a separate, still-required condition | C09 §3 |

`render_ok` stays `phase3_done && guest_tagged_ok`; the persisted phase flag alone
is explicitly insufficient (A4 §0: today `callback=0 render=1` is *correct* RED,
not proof).

### 1.3 Exact log strings — positive proof (emitted on the clean path)

These are **normative contract strings**. The gate/test consumer requires these
substrings; if a sibling emitter uses different wording, the emitter must be
aligned (preferred) or the consumer patterns adapted without dropping any field.
Use the existing prefixes (`[app-shell]`, `[callback-provenance]`, `[render]`).

```
[callback-provenance] notifier hit 0x27eaf6e placeId=<N> cached_class=base+0x72a2f48 cached_method=base+0x72a2fc0 slot=0x6dbd860
[callback-provenance] gameLoadedCallback: class=base+0x72a2f48 (match) method=base+0x72a2fc0 (match) thunk_ret=base+0x2584960(seen,+) notifier_ret=base+0x27eafb3(seen,+) -> GENUINE
[callback-provenance] dispatch arg placeId=<N> matches notifier placeId=<N> name=gameLoadedCallback sig=(J)V count_delta=<D>
[app-shell] gate provenance: notifier-driven gameLoadedCallback at 0x27eaf6e placeId=<N> (RSI>0), class/method cached non-NULL, return 0x27eafb3 over thunk 0x2584960 -> GENUINE
[render] guest-tagged swap at 0x649d422 ra=base+<RA_IN_GUEST_TEXT> tag=guest-loop | guest-tagged swaps: <N> | guest GLES draws between tagged swaps: <M>
[render] guest render verdict: guest-tagged frame confirmed (swaps=<N> draws=<M> fromSurface=installed window=bound) -> GUEST-TAGGED
[app-shell] render provenance: guest-tagged frame at 0x649d422 (swaps=<N>, draws=<M>), phase3_done=1 -> OK
[app-shell] clean state: seeded_state=0 (all opt-ins unset)
```

Notes:
- The `[app-shell] gate provenance: notifier-driven gameLoadedCallback … 0x27eaf6e
  … 0x27eafb3` line deliberately **satisfies the existing decisive grep** at
  `tests/app-shell.sh:129-133`, so that leg need not be rewritten — only
  extended with the new placeId / RA-pair / render / clean-state checks in §3.
- `<RA_IN_GUEST_TEXT>` must be a `base+0x…` address within guest `.text`; a host
  address or an empty/`<unknown>` value fails R1.
- `placeId=<N>` must be decimal and `> 0`; `placeId=0` is the ALT thunk's value
  and is never proof.

### 1.4 Exact log strings — negative / withheld (must remain available)

Keep these unchanged (they are the current honest-state lines):
```
[app-shell] marker withheld: diagnostic guest-state seed active (no app-shell frame proven)
[app-shell] marker withheld: callback=%d render=%d (env calls=%llu callbacks=%llu last='%s')
[app-shell] DIAGNOSTIC ONLY (no marker): JNI bridge exercised, app shell frame NOT proven
[app-shell] ALT PATH: skipping 0x247dc8d nativeAppBridgeAppStart (broken object path)
[app-shell] ALT PATH: gameLoadedCallback method=%p
[app-shell] ALT PATH guest call 0x25848e0 gameLoadedCallback(env,class,method,0,0,0) -> rc=%ld
[app-shell] HONESTY NOTICE: SOBER_ALT_CALLBACK_PATH exercised a genuine guest->Java callback via a harness-triggered thunk; NO login/app-shell frame is proven by this run.
```
Proposed additive (diagnostic, emitted only on withhold, never on proof):
```
[app-shell] marker withheld: callback provenance absent (no notifier-driven gameLoadedCallback)
[app-shell] marker withheld: guest-tagged render absent (phase-3 swap not guest-provenanced)
```
A green run must contain none of the above lines (§3.3).

---

## 2. Gate design — how `shell_cb` and `render_ok` consume the proof

Current shape (symbolic; `guest_app_shell.c:1443-1594`):
```c
int shell_cb = sober_jni_callback_was_called(j, "onAppShellReloadNeeded")
            || sober_jni_callback_was_called(j, "gameLoadedCallback");
... int seeded_state = <OR-chain + sober_notifier_probe_should_withhold()>;
if (seeded_state) { ...; shell_cb = 0; }
int render_ok = sober_render_phase3_done();
if (!shell_cb || !render_ok) { ... return -4; }
... emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER");   /* sole site */
```

Required post-BLK-07/08 shape (additive ANDs; exact placement matters):

```c
/* (1) name leg — UNCHANGED, necessary but not sufficient */
int name_cb = sober_jni_callback_was_called(j, "onAppShellReloadNeeded")
           || sober_jni_callback_was_called(j, "gameLoadedCallback");

/* (2) T4-3 positive provenance — default-ON predicate, no SOBER_* dependency */
int prov_cb = sober_gate_callback_provenance_ok();

/* (3) conjunction. Do NOT replace name_cb with prov_cb; AND them. */
int shell_cb = name_cb && prov_cb;

/* (4) withhold chain — UNCHANGED, wins over provenance */
int seeded_state = /* existing OR-chain */ || sober_notifier_probe_should_withhold();
if (seeded_state) {
    fprintf(stderr, "[app-shell] marker withheld: diagnostic guest-state "
                    "seed active (no app-shell frame proven)\n");
    shell_cb = 0;
}
if (!seeded_state)
    fprintf(stderr, "[app-shell] clean state: seeded_state=0 (all opt-ins unset)\n");

/* (5) T3-4/T3-5 guest-tagged render, ANDed with the persisted phase flag */
int phase3 = sober_render_phase3_done();
int tagged = sober_gate_render_guest_tagged_ok();
int render_ok = phase3 && tagged;

/* (6) emit the gate's own verdict lines (§1.3) BEFORE the early return */
fprintf(stderr, "[app-shell] gate provenance: %s\n",
        sober_gate_callback_provenance_verdict());
fprintf(stderr, "[app-shell] render provenance: %s\n",
        sober_gate_render_guest_tagged_verdict());

/* (7) existing gate, existing failure log, sole emit site — unchanged */
if (!shell_cb || !render_ok) {
    fprintf(stderr, "[app-shell] marker withheld: callback=%d render=%d "
                    "(env calls=%llu callbacks=%llu last='%s')\n", ...);
    ... sober_notifier_probe_uninstall(); sober_jni_shutdown(j); return -4;
}
emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER");   /* exactly once */
```

Rules that make this safe:
- **Ordering:** name → provenance → `shell_cb` AND → withhold override → phase3
  AND guest-tag → verdict lines → existing early return → sole emit. Never
  evaluate the marker before all of these.
- **Mixed evidence fails closed:** if `prov_cb == 1` but any `HARNESS-THUNK` was
  recorded in the run (or `seeded_state != 0`), T4-3 must make `prov_cb == 0` or
  the gate must AND an explicit `!harness_thunk_seen` term. Same for a run with
  both `tag=guest-loop` and an unexcluded synthetic swap counted as proof.
- **`onAppShellReloadNeeded` stays non-qualifying** without its own notifier/RA
  proof; the disqualification text in `tests/app-shell.sh:117-120` remains.
- **No second emitter:** diagnostics keep distinct token/file/function (038 §4;
  e.g. `ALT-CALLBACK EXERCISED (DIAGNOSTIC) (NO APP-SHELL FRAME PROVEN)` /
  `SOBER_APP_SHELL_ALT_MARKER`) and never call `emit_marker`.
- **No new opt-in:** if T4-3/T3-5 need a debug switch, it is a withhold entry
  and the default path is the proof path (§4 rule 2).

---

## 3. `tests/app-shell.sh` — full-conjunction assertion contract

The script already has the right skeleton (engine identity §0, startup §1,
phase-2/3 preservation §2, gate evidence §3, marker §4, window §5). Extend §1
(environment hygiene) and §3 (assertions); keep §4/§5. `set -uo pipefail` plus
explicit `exit 1` on every failure is correct — do not add `-e` changes that
would mask the diagnostic `tail` output.

### 3.1 Environment hygiene (§1, before launch)

Currently only `unset SOBER_ALT_CALLBACK_PATH` (`:68`). Required: unset the
**canonical withhold list** (gate OR-chain, current `guest_app_shell.c:1449-1559`)
including aliases/prefix families, or launch under `env -i` with a minimal
allowlist (`PATH`, `DISPLAY`, `EGL_PLATFORM=x11`, `LIBGL_ALWAYS_SOFTWARE=1`,
`LD_LIBRARY_PATH`, optional `XDG_RUNTIME_DIR`). Timing-only knobs
`SOBER_WINDOW_HOLD_MS` and `SOBER_APP_SHELL_WAIT_MS` may stay set for log
collection (C09 §4). Never set `SOBER_CALLBACK_PROVENANCE`,
`SOBER_NOTIFIER_PROBE`, `SOBER_GAME_LOADED_PATH` or any seed knob to "obtain"
proof. The positive `clean state: seeded_state=0` line (§1.3) is the assertion
that hygiene actually held.

### 3.2 Positive assertions (all required; exact strings from §1.3)

```bash
# notifier hit + placeId > 0
grep -Eq '^\[callback-provenance\] notifier hit 0x27eaf6e placeId=[1-9][0-9]* .*cached_class=base\+0x72a2f48 .*cached_method=base\+0x72a2fc0' "$ERR" \
  || { echo "FAIL: no notifier hit with placeId>0 (DECISION 038)"; tail -40 "$ERR"; exit 1; }
if grep -Eq 'notifier hit 0x27eaf6e placeId=0([^0-9]|$)' "$ERR"; then
  echo "FAIL: notifier placeId=0 is not proof"; tail -40 "$ERR"; exit 1; fi

# RA pair + GENUINE (same line), argument match
grep -Eq '^\[callback-provenance\] gameLoadedCallback: .*thunk_ret=base\+0x2584960\(seen.*notifier_ret=base\+0x27eafb3\(seen.*-> GENUINE' "$ERR" \
  || { echo "FAIL: no 0x2584960/0x27eafb3 RA pair (DECISION 038)"; tail -40 "$ERR"; exit 1; }
grep -Eq '^\[callback-provenance\] dispatch arg placeId=[1-9][0-9]* matches notifier placeId=[1-9][0-9]* name=gameLoadedCallback sig=\(J\)V' "$ERR" \
  || { echo "FAIL: dispatch arg/placeId mismatch"; tail -40 "$ERR"; exit 1; }

# gate's own decisive verdict (keeps the existing :129-133 leg valid)
grep -Eq '^\[app-shell\] gate provenance: notifier-driven gameLoadedCallback .*0x27eaf6e .*0x27eafb3 -> GENUINE' "$ERR" \
  || { echo "FAIL: no native notifier-driven callback provenance (DECISION 038)"; tail -40 "$ERR"; exit 1; }

# guest-tagged render (N>=1, M>=1), not our own test-frame swap
grep -Eq '^\[render\] guest-tagged swap at 0x649d422 ra=base\+0x[0-9a-f]+ tag=guest-loop \| guest-tagged swaps: [1-9][0-9]* \| guest GLES draws between tagged swaps: [1-9][0-9]*' "$ERR" \
  || { echo "FAIL: no guest-tagged swap+draw (BLK-06)"; tail -40 "$ERR"; exit 1; }
grep -q '^\[render\] guest render verdict: guest-tagged frame confirmed' "$ERR" \
  || { echo "FAIL: no guest render verdict"; tail -40 "$ERR"; exit 1; }
grep -Eq '^\[app-shell\] render provenance: guest-tagged frame at 0x649d422 \(swaps=[1-9][0-9]*, draws=[1-9][0-9]*\), phase3_done=1 -> OK' "$ERR" \
  || { echo "FAIL: gate did not accept guest render leg"; tail -40 "$ERR"; exit 1; }

# clean state + phase-2/3 + name leg + marker (skeleton already present)
grep -qx '^\[app-shell\] clean state: seeded_state=0 (all opt-ins unset)' "$ERR" \
  || { echo "FAIL: seeded_state not proven 0"; tail -40 "$ERR"; exit 1; }
grep -Eq 'guest->Java callback: (gameLoadedCallback|onAppShellReloadNeeded)' "$ERR" || FAIL
grep -q 'guest call 0x649d422' "$ERR" || FAIL
[ "$rc" -eq 0 ] && [ -f "$SHELL_MARKER" ] && grep -qx 'app shell rendered' "$OUT" || FAIL
```

### 3.3 Negative assertions (any hit ⇒ immediate FAIL)

Keep the existing absence checks (`:109-116`, `:130`) and add the new ones:

```
ALT PATH guest call 0x25848e0
ALT PATH: gameLoadedCallback method=
ALT PATH: skipping 0x247dc8d
HONESTY NOTICE
marker withheld: diagnostic guest-state seed active
marker withheld: callback=
DIAGNOSTIC ONLY (no marker)
HARNESS-THUNK
UNCLASSIFIED
ALT-CALLBACK EXERCISED (DIAGNOSTIC)
tag=synthetic            # only allowed in a non-counting/diagnostic line
```
`onAppBridgeNotification` must not be the qualifying name; the only permitted
occurrence is the opt-in diagnostic handler under `SOBER_LOG_APP_BRIDGE_NOTIFICATION`,
which is withheld and must be absent in this run (038 §2.3; A7 §3.2).

### 3.4 Documented green exit criterion (closes A9 gap 7 / D10 R2)

The script's comments must state the *exact* flip-to-green condition:

> GREEN iff, on the frozen engine with every `SOBER_*` opt-in unset and engine
> SHA unchanged, all §3.2 lines are emitted by the named code paths — notifier
> hit by T4-3's default-ON recorder hooked at `sr_dispatch_call`
> (`native_jni.c:478-522`); RA pair by the same recorder's stack scan; guest-tagged
> swap/draw by T3-4/T3-5's default-ON hook at GOT `0x722a1a8`; the gate verdict
> and `seeded_state=0` lines by the gate in `guest_app_shell.c` — and all §3.3
> lines are absent, `rc==0`, `SOBER_SHELL_MARKER` exists and stdout contains
> exactly one `app shell rendered`. Absence of any one line is a correct RED.

Count the marker in `$OUT` only: the script's own final `echo "app shell rendered"`
(`:150`) is its success banner and must not be mistaken for a second emission.

---

## 4. Withhold-chain parity rules

1. **Canonical source of truth.** The `seeded_state` OR-chain in
   `guest_app_shell.c` is authoritative. Any new knob from T4-3 / T3-4 / T3-5 that
   can disable, redirect, filter or fake a gate predicate is guest-mutating by
   effect and must be added to that chain in the same change that adds the knob.
2. **Default-ON proof, withheld diagnostics.** Gate-facing predicates read no
   `SOBER_*`. `SOBER_CALLBACK_PROVENANCE`, `SOBER_CALLBACK_PROVENANCE_GUEST_BASE`,
   `SOBER_NOTIFIER_PROBE`, `SOBER_GSEED_DIAG`, `SOBER_APP_SHELL_DIAGNOSTIC` and
   every seed/GOT/ctor knob stay withheld; a run that needs one is not proof.
3. **AND-not-OR.** Proof narrows only. Withhold is never relaxed because
   provenance exists; provenance is never bypassed because a name was counted.
4. **Withhold wins / fail closed.** `seeded_state ⇒ shell_cb = 0` stays before
   the early return. `sober_notifier_probe_should_withhold()` remains the
   fail-closed sensor (enabled OR installed OR any notifier/descriptor hit ⇒
   withhold). Mixed GENUINE + HARNESS-THUNK ⇒ withhold.
5. **Alias + prefix parity (A8 §6(c)).** Keep both `SOBER_STARTAPP_V2` and
   `SOBER_STARTAPP_PARAMS`, both `SOBER_HELPER_REPAIR` and `_V2`, and the five
   prefixes `SOBER_GAME_`, `SOBER_STARTAPP_`, `SOBER_SGV2_`,
   `SOBER_PLATFORM_PARAMS_`, `SOBER_PROP_`; keep the symbolic
   `SOBER_FAITHFUL_JNI_ARRAYS_ENV` term. Dropping one reopens a concrete bypass.
6. **Suppression parity.** A knob that suppresses the notifier detour, the RA
   stack scan, the swap-tag hook or the guest-draw counter is equivalent to
   manufacturing a clean-looking result and must be in-chain.
7. **Test/gate symmetry.** Every positive line asserted in §3.2 must be emitted
   by the gate on the clean path (no test-only string synthesis); every
   forbidden line in §3.3 must be reachable only under a withheld knob. Anchor
   greps to the `[app-shell]` / `[callback-provenance]` / `[render]` prefixes.
8. **Harness hygiene parity.** The test's unset list (§3.1) must be generated
   from / cross-checked against the gate's canonical list; the positive
   `seeded_state=0` line is the runtime proof they agree.
9. **Audit scope parity.** `D09-withhold-audit.sh` currently parses only
   `guest_app_shell.c`; extend it (or add a source-tree-wide audit) to the new
   T4-3/T3-5 modules and all TUs reading `SOBER_*` before claiming "0 unclassified
   gaps". `D09` exit 0 is not itself evidence of gate integrity.
10. **Diagnostic isolation (038 §4).** ALT/diagnostic markers use a distinct
    token/file/function, never `emit_marker("app shell rendered\n", …)`, and stay
    out of ctest/CMake/verify-native. `tests/app-shell.sh` asserts their absence.

---

## 5. What remains RED today (explicit, evidence-anchored)

| Leg | Required evidence | Today (A4 §0, A7 §0/§2, A8 §4, C09 §6.4) |
| --- | --- | --- |
| Notifier hit (P1/P2) | `0x27eaf6e` hit, `placeId>0` | **Never observed.** No hit at `0x27eaf6e` / `0x27eaf4c` / `0x27eaf04`; no captured RSI; no `rbx.jni … placeId:%lld` (`0x36c7c9`) line. |
| RA pair (P4) | `0x2584960` over `base+0x27eafb3` | **Never observed.** Only the ALT thunk returning into host code. |
| Genuine classification (P3/P5/P6) | `[callback-provenance] … -> GENUINE` | **No line emitted.** The recorder is opt-in/withheld and had a bootstrap deadlock (fixed at `native_jni.c:877` but still self-gated); grep over all logs: zero provenance lines. |
| Callback name | `guest->Java callback: gameLoadedCallback` | Only the harness-triggered ALT thunk and the explicitly rejected `onAppBridgeNotification`; default `callbacks=0 last='isVrDevice'`. |
| Guest render (R1–R4) | guest-tagged swap + ≥1 guest draw | **Not present.** `0x649d422` is invoked by the harness with a synthetic `0x48`-byte context; `g_guest_swaps==1` is undifferentiated; no fromSurface hook install, no guest GLES draw, no RA tag. |
| Gate proof | gate's own provenance verdict, `seeded_state=0` line | **Absent.** `shell_cb` is still the name-only OR (`guest_app_shell.c:1443-1444`); `render_ok = sober_render_phase3_done()` only (`:1567`). |
| Marker | one `app shell rendered` + marker file | Zero emissions; default gate log `marker withheld: callback=0 render=1 (env calls=33 callbacks=0 last='isVrDevice')`. |
| Test | `tests/app-shell.sh` §3 decisive leg | RED at `:129-133` (line absent by design); no documented green exit until this contract is adopted; only two trivial ctest self-checks gate CI (~15% CI-gating). |
| Assurance | whole-tree withhold audit, docs, final re-run | D09 is single-file; README "done = all true" 0/4; `abi-map.md` stale; two manual-only done notices; no post-integration invariant re-run. |
| Engine | SHA/BuildID unchanged | Holds — `3e498d64…41335` / `204ad049…6c9`; must be re-checked after integration. |

Consequences: BLK-07 (genuine notifier-driven callback) is not landed; BLK-08
(native provenance *consumed by the gate*) is not landed; BLK-06's render leg is
synthetic; BLK-09 assurance work is open. Until these land with all opt-ins
unset, **the marker stays WITHHELD and `tests/app-shell.sh` staying RED is the
correct outcome.** Re-enabling the ALT thunk to turn the test green is a false
green and is forbidden (038 §1/§3).

---

## 6. Final invariant checklist (post-integration, all ANDed)

Run on the frozen engine, all `SOBER_*` unset (timing knobs excepted). Any FAIL ⇒
marker withheld, report the gap, do not promote.

1. [ ] Engine identity: BuildID `204ad049…6c9`, SHA `3e498d64…41335` unchanged pre/post; no Flatpak build used.
2. [ ] `file build/sober-native` = native x86-64 PIE; `ldd` = libc/ld-linux only (no wine/proton/box64/qemu/translation).
3. [ ] `ctest --test-dir build`: 2/2 baseline self-checks pass; no new diagnostic script registered in ctest/CMake/verify-native.
4. [ ] `guest init complete` + init marker; `first frame rendered` + frame marker; GOT-hook install line `installed guest-swap counter hook at GOT 0x722a1a8`.
5. [ ] Notifier hit line present with `placeId=[1-9]…`; no `placeId=0` notifier line; `0x6dbd860 == base+0x27eaf6e` after `0x24ad699` registration.
6. [ ] RA pair line present: `thunk_ret=base+0x2584960(seen,…)` nearer than `notifier_ret=base+0x27eafb3(seen,…)`; verdict `-> GENUINE`.
7. [ ] Dispatch `name=gameLoadedCallback sig=(J)V`, arg == notifier placeId, count delta ≥1 after introducers `0x2ebd51a` / `0x2ebca9e`; no `HARNESS-THUNK` / `UNCLASSIFIED` anywhere.
8. [ ] `[app-shell] gate provenance: notifier-driven gameLoadedCallback … 0x27eaf6e … 0x27eafb3 -> GENUINE` emitted by the gate itself.
9. [ ] Guest render: `guest-tagged swap at 0x649d422 ra=base+<guest .text> tag=guest-loop`, `swaps>=1`, `draws>=1`; `-> GUEST-TAGGED`; gate's `render provenance … -> OK`; synthetic Route-B explicitly excluded.
10. [ ] `sober_render_phase3_done()==1` **and** guest-tagged predicate ==1 (both legs; neither alone).
11. [ ] `[app-shell] clean state: seeded_state=0 (all opt-ins unset)` present; no `marker withheld: diagnostic guest-state seed active`.
12. [ ] No `ALT PATH…`, `ALT PATH guest call 0x25848e0`, `HONESTY NOTICE`, `DIAGNOSTIC ONLY`, `ALT-CALLBACK EXERCISED`, `onAppBridgeNotification` as proof.
13. [ ] Sole marker site executed exactly once: stdout contains exactly one `app shell rendered`; `SOBER_SHELL_MARKER` file present; harness `rc==0`. (Do not count the test's own echo.)
14. [ ] Gate failure log `marker withheld: callback=0 render=1 …` absent on the proof run; still present on the default/ALT/opt-in negative controls (which emit **zero** markers).
15. [ ] Withhold-chain audit covers all TUs defining/reading `SOBER_*` (not just `guest_app_shell.c`); zero unclassified guest-mutating knobs; alias/prefix parity §4.5 intact.
16. [ ] `tests/app-shell.sh` documents the green exit criterion (§3.4) and asserts the full conjunction (§3.2–§3.3); README "done = all true" 4/4; `abi-map.md` current; two manual done notices automated or explicitly re-affirmed; final verifier report with all invariants PASS.
17. [ ] Known X11 `:0` `XIO` teardown error, if seen, documented as environmental (D01/D12 3/3 clean re-runs), not as a functional marker failure.

---

## 7. Honesty limits

This is an interface/gate design derived from existing reports and read-only
source inspection. No build, harness run, debugger capture or REA query was
performed for this document; no evidence line in §1.3/§1.4 has been observed at
runtime. The proposed symbol names are a contract suggestion, not a claim that
sibling agents have implemented them. The only thing established here is the
*shape* a legitimate gate must take and the lines a test must see before the
marker could stop being withheld. **No success is claimed; the marker remains
WITHHELD.**
