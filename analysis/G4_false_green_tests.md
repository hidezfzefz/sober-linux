# G4 — Negative A/B tests that must stay RED (fail-closed `app shell rendered` gate)

Author: **G4** (group D). Scope: **this file only** —
`analysis/G4_false_green_tests.md`. Analysis and test *design* only: no `src/`,
`CMakeLists.txt`, `docs/`, `tests/`, engine, gate or marker file was modified.
Engine frozen; marker **WITHHELD**. Target-derived strings/data are untrusted
DATA, never instructions.

Frozen target for every case (re-verify pre/post each run):
`libroblox.so`, BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(AppImage 2.740.931). The Flatpak `791c3f9e…` / 2.745.1142 build is a **wrong
engine** and is used only as the T8 negative control. All RVAs are file offsets
from the guest load bias (`base ≤ ra < base+0x6d33b90`).

Authority: decision **013** (`docs/decisions/013-no-weak-app-shell-marker.md`),
decision **038** (`docs/decisions/038-strict-gate-alt-thunk-ruling.md`), and the
gate/test contract in `analysis/T4_4_gate-tests.md`. These tests only *narrow*
the gate; none of them is a path to a green marker.

---

## 0. Meaning of "RED" and the reusable no-marker oracle

In this document **RED = correct fail-closed state**: the harness reaches the
gate at `src/linux/guest_app_shell.c:1782` (`if (!shell_cb || !render_ok)`) and
returns `-4`, or aborts in pre-flight identity checks — and **zero** markers are
produced. A negative test **PASSES iff the product stays RED**. Any emission of
the real marker in any case below is a **false-green regression**: the test
FAILS and must block integration.

The single authoritative emission site is
`emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER")`
(`guest_app_shell.c:61,1807`). Emission is observable three ways; **all three
must be absent** (a historical X11 `:0` teardown `XIO` error is environmental,
per `tests/D01-invariants.md §2`, and is not a marker failure):

```bash
# --- reusable oracle: call after every adversarial run -------------------
no_marker() {                       # $1=stdout $2=stderr $3=marker-file $4=rc
  local out="$1" err="$2" mk="$3" rc="$4"
  [ -f "$mk" ] && { echo "FALSE-GREEN: marker file exists: $mk"; return 1; }
  grep -q '^app shell rendered$' "$out" \
      && { echo "FALSE-GREEN: stdout token emitted"; return 1; }
  grep -q '^ALT-CALLBACK EXERCISED' "$out" \
      && { echo "FALSE-GREEN: diagnostic token emitted"; return 1; }
  grep -q 'marker withheld' "$err" \
      || { echo "FAIL: no withhold/abort line (gate not reached)"; return 1; }
  [ "$rc" -ne 0 ] \
      || { echo "FALSE-GREEN: harness rc=0 with no withhold"; return 1; }
  return 0                          # RED and honest -> negative test PASSES
}
```

Normative log strings used as **required** (present) evidence in the negatives:

```
[app-shell] marker withheld: diagnostic guest-state seed active (no app-shell frame proven)
[app-shell] marker withheld: callback=%d render=%d (env calls=%llu callbacks=%llu last='%s')
```
Normative strings used as **forbidden** (any occurrence = FAIL) in every
negative:

```
[app-shell] gate provenance: notifier-driven gameLoadedCallback ... -> GENUINE
[callback-provenance] ... -> GENUINE
[render] guest-tagged swap at 0x649d422 ra=base+0x… tag=guest-loop | ...
[render] guest render verdict: guest-tagged frame confirmed ... -> GUEST-TAGGED
[app-shell] render provenance: guest-tagged frame at 0x649d422 ... -> OK
[app-shell] clean state: seeded_state=0 (all opt-ins unset)
```

`app shell rendered` must be **absent from stdout**, the marker file **absent**,
and the rc **non-zero** (gate `-4`→shell `252`, or pre-flight abort/`139` on a
faulting path) in all eight cases.

---

## 1. Shared harness and environment hygiene

All cases use the unmodified `build/guest-init-host` harness (allow rebuild only,
never edit sources), a guest path under `/tmp/opencode/sober-apk/...`, and a
**per-case private marker path** under `/tmp` so no repository marker is touched:

```bash
ROOT=/home/user/sober-linux
GUEST=/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
HOST_BIN="$ROOT/build/guest-init-host"
BUILD_ID=204ad049599c96afb75b8a21d5051063e821d6c9
SHA_OK=3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
OUT=/tmp/g4_${CASE}.out  ERR=/tmp/g4_${CASE}.err  MK=/tmp/g4_${CASE}.marker
```

Baseline launch (all opt-ins OFF; only legitimate phase gates + timing knobs):

```bash
env -i PATH="$PATH" DISPLAY="${DISPLAY:-:0}" EGL_PLATFORM=x11 \
  LIBGL_ALWAYS_SOFTWARE=1 \
  LD_LIBRARY_PATH="$ROOT/build/ndk-stubs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" \
  SOBER_RENDER=1 SOBER_APP_SHELL=1 SOBER_WINDOW_HOLD_MS=2500 \
  SOBER_SHELL_MARKER="$MK" \
  timeout 180 "$HOST_BIN" "$GUEST" >"$OUT" 2>"$ERR" ; rc=$?
```

Hygiene rules (from `T4_4_gate-tests.md §3.1`, `D02-withhold-audit.md`):
- Never set a withhold/opt-in knob to "obtain proof"; the only deliberately set
  knobs are the named adversarial ones in each case (and timing knobs).
- `SOBER_RENDER` / `SOBER_APP_SHELL` are the legitimate phase gates and must
  stay ON (they are *not* withhold entries — `D02 §6`).
- A positive `clean state: seeded_state=0` line is **forbidden** in the
  adversarial B runs (those set a seed knob); it is also absent in the default A
  runs today because no genuine callback exists (so the gate exits before it).

Every case ends with: `no_marker "$OUT" "$ERR" "$MK" "$rc"` **and** the
forbidden-line grep above **and** engine identity re-check (SHA + BuildID).

---

## 2. Negative A/B test cards

### T1 — Host Route-B swap only (`render_ok` must see synthetic, not guest)

**Threat.** The only swap reaching the observation hook is the harness's own
direct `guest_swap((uintptr_t)ctx)` call at `guest_render.c:354` (Route B: stack
`0x48`-byte ctx, `ctx+0x20/+0x28` filled by host). Because `0x649d422` ends in a
**tail `jmp`** to `eglSwapBuffers@plt 0x6d334d0 → *GOT[0x722a1a8]`, the captured
RA is the **host** return site; `rg_in_guest(ra)` is false
(`analysis/R1_zero_tagged_swaps.md §1/§3`, `R6_swap_provenance.md §3`).

- **A — default single host swap:** baseline launch, no extra knobs.
- **B — doubled host swap (R6 Control B):** same, but the harness calls
  `guest_swap(ctx)` twice (in-memory harness variation only; engine untouched).

**Expected logs (both A and B).**
```
[render] guest call 0x649d422 swap(ctx=..., egl=..., surf=...) -> rc=0 | guest-driven swaps observed: 1   # B: 2
[render] render_guest counters: source=module from_surface=2 proc_loads=48
         draws=0 swaps(total/guest/synth)=1/0/1 lifecycle=1/1/1/1                # B: 2/0/2
[app-shell] default guest render path -> rc=0 (already installed=1, guest-tagged swaps=0)
[app-shell] marker withheld: callback=0 render=0 (...)
```
Note: the historical `guest-driven swaps observed: N` line = native `0` +
module `total N`; it is **not** a guest-tagged count. The authoritative triple
is `total/guest/synth` and `guest=0`.

**Forbidden:** any `tag=guest-loop` line, `guest render verdict … GUEST-TAGGED`,
or `swaps(total/guest/synth)=…/[1-9][0-9]*/…` (guest count > 0).

**Fail-closed anchor:** `guest_app_shell.c:1777-1780` requires
`swaps_guest_tagged()>=1 && gles_draw_calls()>=1`; both are 0 → `render_ok=0`.

**Regression-fail condition:** `guest>0` or `render=1` or any marker emission ⇒
the RA interval classifier `[base, base+0x6d33b90)` is broken (e.g.
`g_guest_base` corrupted) — block integration.

---

### T2 — Synthetic counter inflation attempt (must stay decisive-0)

**Threat.** Force counters to *look* like a guest frame without a guest loop:
repeated host swaps, calling `sober_frame_synthetic_tag_note_swap()` outside a
live scope, counting wrapped-but-never-invoked draw trampolines
(`draw_wrapped=5` vs `draw_calls=0`), and host-writing the window global
`0x72a3308`. F7/F8 are explicitly designed against this: "No counter inflation"
(`frame_evidence.h:14-17`) and "note_swap outside scope: no effect"
(`frame_synthetic_tag.c:195-196,221-222`).

- **A — honest baseline:** T1-A counters (`1/0/1`, `draws=0`).
- **B — inflation attempt:** kill `SOBER_FRAME_SYNTH_TAG=0` *and*
  `SOBER_FRAME_EVIDENCE=0` (default-ON layered sensors off), drive ≥3 host
  Route-B swaps, force `eglGetProcAddress` to wrap extra draw names, and let
  `render_guest.c:326-329` host-write `0x72a3308`.

**Expected logs (B).**
```
[render] render_guest counters: source=module from_surface=2 proc_loads=>=48
         draws=0 swaps(total/guest/synth)=3/0/3 lifecycle=1/1/1/1
frame-synthetic-tag: enabled=0 active=0 depth=0 seen=0 enters=0 leaves=0 tagged_swaps=0 swap_rva=0x649d422 env=SOBER_FRAME_SYNTH_TAG=0
[app-shell] marker withheld: callback=0 render=0 (...)
```
If the layered sensors are wired and left ON, expect instead
`frame-synthetic-tag: enabled=1 … tagged_swaps=>=1` and a `MIXED`/tainted
verdict with decisive `swaps_guest/draws/window_bound = 0`
(`frame_evidence.h:25-29,192-201`) — same RED outcome.

**Forbidden:** `draws=[1-9]`, `guest>0`, `tag=guest-loop`, `-> GUEST-TAGGED`,
`render provenance … -> OK`, or any decisive count > 0 attributed to the host
activity.

**Fail-closed anchor:** with tag ON, `rg_hook_swap` forces the synthetic bucket
even if an RA were inside guest `.text` (`frame_synthetic_tag.h:77-84`); with
tag OFF, the RA classifier still tags host → synthetic (header §42-50). Draw
trampoline counters (`render_guest.c:142-150`) only move on *invocation*, and the
frame loop never runs because `[nativeData+0x98]` is NULL (`R1 §4`).

**Regression-fail condition:** any decisive guest count becomes > 0 from host
activity ⇒ false green; block.

---

### T3 — Host vtable call (`sober_render_guest_swap_via_vtable`)

**Threat.** Call the swap through the *guest's own* vtable slot
`[0x71912e8+0x18]=[0x7191300]=0x649d422` from host code. The slot and handles
are genuine, but the caller is the host, so the tail-jmp still preserves a host
RA → synthetic (`analysis/P1_render_vtable.md §4/§6`).

- **A — natural Route-B call:** T1-A.
- **B — vtable-mediated host call:** replace the direct `guest_swap` call with
  `sober_render_guest_swap_via_vtable()` (`render_guest.c:520-548`).

**Expected logs (B).**
```
[render] render_guest: vtable=base+0x71912e8 slot[3]=base+0x649d422 (verified)
[render] guest call 0x649d422 swap(...) -> rc=0 | guest-driven swaps observed: 1
[render] render_guest counters: ... draws=0 swaps(total/guest/synth)=1/0/1 lifecycle=1/1/1/1
[app-shell] marker withheld: callback=0 render=0 (...)
```

**Forbidden:** `tag=guest-loop`, a guest `.text` `ra=base+0x…` attributed to
this swap, `draws>0`, or any marker.

**Fail-closed anchor:** `P1_render_vtable.md:223-229` — "calling that pointer
from the host is a synthetic swap (RA in host)"; a genuine frame needs the
guest's own render/redraw loop to call `[vtable+0x18]` after real GLES draws.
`R1 §3` confirms no `call [vtable+0x18]` is dispatched by guest code today.

**Regression-fail condition:** vtable path counted as guest evidence ⇒ false
green; block.

---

### T4 — Mixed evidence (`MIXED → WITHHELD`)

**Threat.** A run that sees *both* a guest-classified swap/draw and a synthetic
swap must fail closed, never average into "guest proven"
(`R6 §5.2 item 6`, `frame_evidence.h:25-29`, `T4_4_gate-tests.md §2 "Mixed
evidence fails closed"`).

- **A — synthetic only:** T1-A (`guest=0`, `render=0`).
- **B — synthetic + injected guest-classified evidence:** keep the host Route-B
  swap and additionally seed a guest-tagged swap + counted draw (the diagnostic
  `[+0x98]` non-NULL seed of `R1 §6` item 1) *and/or* set a state seed so
  `seeded_state != 0`.

**Expected logs (B).**
```
[render] guest-tagged swap at 0x649d422 ra=base+0x… tag=guest-loop | guest-tagged swaps: 1 | guest GLES draws between tagged swaps: 1
[render] frame evidence: MIXED (synthetic+guest in window) -> decisive swaps_guest=0 draws=0 window_bound=0
[app-shell] marker withheld: diagnostic guest-state seed active (no app-shell frame proven)
[app-shell] marker withheld: callback=0 render=0 (...)
```
The `tag=guest-loop` line is **permitted here only as a raw observation**; it
must be accompanied by the `MIXED` taint line and the seed-withhold line, and
must **not** be followed by a `GUEST-TAGGED` verdict or a `render provenance …
-> OK` gate acceptance.

**Forbidden:** `-> GUEST-TAGGED`, `render provenance … -> OK`,
`clean state: seeded_state=0`, or any marker.

**Fail-closed anchor:** F7 taints the run and zeroes decisive counts
(`frame_evidence.h:25-29,192-201`); the gate overrides `shell_cb=0` whenever
`seeded_state!=0` (`guest_app_shell.c:1764-1768`); T4-4 §2 ANDs an explicit
`!harness_thunk_seen` term.

**Regression-fail condition:** any mixed-evidence run emitting a marker (or a
`GUEST-TAGGED`/`-> OK` verdict) ⇒ fail-closed broken; block.

---

### T5 — ALT thunk (`SOBER_ALT_CALLBACK_PATH`)

**Threat.** The host-triggered thunk `0x25848e0` increments the
`gameLoadedCallback` count and historically emitted the real marker with no
login/app-shell frame — the "labelled false green" of
`verification-report-471.md §3a` and D3-1. 038/013 make it diagnostic-only;
the knob is the **first term** of the withhold chain (`guest_app_shell.c:1608`).

- **A — ALT unset:** default → `callback=0`, no ALT lines, no marker.
- **B — ALT alone:** `SOBER_ALT_CALLBACK_PATH=1`, no other opt-ins.
- **C — ALT + a known in-chain seed:** e.g. `SOBER_SEED_JNI_SINGLETON=1`
  (guards against the historical 13-gap bypass).

**Expected logs (B/C).**
```
[app-shell] ALT PATH: skipping 0x247dc8d nativeAppBridgeAppStart (broken object path)
[app-shell] ALT PATH: gameLoadedCallback method=0x…
[app-shell] ALT PATH guest call 0x25848e0 gameLoadedCallback(env,class,method,0,0,0) -> rc=0
[app-shell] marker withheld: diagnostic guest-state seed active (no app-shell frame proven)
[app-shell] shutting down JNI bridge
```
`guest->Java callback: gameLoadedCallback` may appear (count incremented), but
`HONESTY NOTICE` (`guest_app_shell.c:1801-1806`) is **unreachable** with the
strengthened gate — its absence is correct (038 §5).

**Forbidden:** `-> GENUINE`, any notifier-driven provenance line,
`clean state: seeded_state=0`, `HONESTY NOTICE` (expected absent), and any
marker. (A third-party runner that still *requires* the ALT line to go green —
the old `app-shell.sh:112-116` contradiction — is itself a false-green test and
must be rejected per 038 §3.)

**Fail-closed anchor:** `seeded_state = getenv("SOBER_ALT_CALLBACK_PATH") || …`
at `guest_app_shell.c:1608` forces `shell_cb=0` at `:1767` before the sole
`emit_marker` at `:1807`.

**Regression-fail condition:** ALT-alone marker emission ⇒ direct violation of
038 §1/§3; block.

---

### T6 — `onAppBridgeNotification` is never qualifying

**Threat.** A genuinely counted `onAppBridgeNotification(String,String)`
(phase-8/9 opt-in full stack, cached ID `0x72a3028` → thunk `0x3dea91`) being
mistaken for the qualifying app-shell callback. 013:67-76 and 038 §2.3 reject
it; `shell_cb` checks only `onAppShellReloadNeeded`/`gameLoadedCallback`.

- **A — default:** notification handler not installed; `callbacks=0`,
  `last='isVrDevice'`, no notification line.
- **B — `SOBER_LOG_APP_BRIDGE_NOTIFICATION=1`:** notification fired and logged.

**Expected logs (B).**
```
[app-shell] (diagnostic) onAppBridgeNotification fired (String,String)  # opt-in handler only
[app-shell] marker withheld: diagnostic guest-state seed active (no app-shell frame proven)
[app-shell] marker withheld: callback=0 render=0 (calls=… callbacks=0 last='…')
```
`SOBER_LOG_APP_BRIDGE_NOTIFICATION` is in the withhold chain
(`guest_app_shell.c:1634`; D3-4, D01 §4), so `seeded_state != 0`.

**Forbidden:** `guest->Java callback: onAppBridgeNotification` counted as a gate
callback, a `notifier-driven gameLoadedCallback … -> GENUINE` line,
`callback=1`, `clean state: seeded_state=0`, or any marker.

**Fail-closed anchor:** `shell_cb` (guest_app_shell.c shell-cb computation)
only ORs the two qualified names; the notification handler is installed only
under the withheld opt-in (`:1165-1166`) and can never set `shell_cb`.

**Regression-fail condition:** notification run emitting a marker or
`callback=1` from the notification ⇒ user ruling violated; block.

---

### T7 — Kill-switch off vs on (layered sensors: never a path to green)

**Threat.** Flipping a default-ON kill switch is used to "manufacture a clean
result" (e.g. disable the synthetic tag so the host swap is mis-tagged, or
disable the render module but still let a native first-frame swap count). Three
default-ON sensors exist; all must be fail-safe in **both** states:

| Sensor | Default | Kill value | ON behavior | OFF behavior |
| --- | --- | --- | --- | --- |
| `SOBER_RENDER_GUEST` (module) | ON | `=0` | installs GOT hooks, counters honest | `enabled=0 installed=0`, no hook traversal; also **in withhold chain** (`guest_app_shell.c:1746`) |
| `SOBER_FRAME_SYNTH_TAG` (F8 tag) | ON | `=0/off/no/false` | explicit host tag forces synthetic | inert; RA classifier still tags synthetic (fail-safe) |
| `SOBER_FRAME_EVIDENCE` (F7 recorder) | ON | `=0` | immutable records; taint → decisive 0 | observers inert; decisive accessors return 0/NULL |

- **A — ON (unset):** baseline; assert `enabled=1 installed=1`, triple
  `1/0/1`, `draws=0`, and (if wired) `frame-synthetic-tag: enabled=1 …` —
  marker withheld.
- **B — OFF:** set each kill value in turn (individually and combined with T2
  inflation) — marker still withheld.

**Expected logs (B, `SOBER_RENDER_GUEST=0`).**
```
[render] render_guest disabled: env=SOBER_RENDER_GUEST=0 enabled=0 installed=0
[app-shell] default guest render path -> rc=0 (guest-tagged swaps=0)
[app-shell] marker withheld: diagnostic guest-state seed active (no app-shell frame proven)
[app-shell] marker withheld: callback=0 render=0 (...)
```
**Expected logs (B, `SOBER_FRAME_SYNTH_TAG=0`).**
```
frame-synthetic-tag: enabled=0 active=0 depth=0 seen=0 enters=0 leaves=0 tagged_swaps=0 swap_rva=0x649d422 env=SOBER_FRAME_SYNTH_TAG=0
[render] render_guest counters: ... draws=0 swaps(total/guest/synth)=1/0/1 ...   # RA classifier still honest
[app-shell] marker withheld: callback=0 render=0 (...)
```

**Forbidden (both states):** `enabled=1` claim with a wrong kill value,
`tag=guest-loop`, `draws>0`, `-> GUEST-TAGGED`, `render provenance … -> OK`,
`clean state: seeded_state=0` while `SOBER_RENDER_GUEST=0`, or any marker.

**Fail-closed anchor:** F8 header §42-50 "Disabling the tag is FAIL-SAFE";
F7 `frame_evidence.h:22-24` "disabling evidence can never manufacture a green";
`SOBER_RENDER_GUEST` is in the withhold chain and its OFF state also yields no
hook counters (`R2_got_hook_reachability.md §8 T1/T2 negative control`).

**Regression-fail condition:** kill-OFF run producing a guest classification, a
`GUEST-TAGGED`/`-> OK` verdict, or a marker ⇒ false green; block.

---

### T8 — Wrong BuildID / engine identity (pre-flight abort, zero marker)

**Threat.** Running the whole A/B against the wrong engine (Flatpak
`791c3f9e…` / 2.745.1142, or a SHA-mismatched/patched object) whose RVAs differ,
so a "green-looking" callback/render trace is meaningless or forged.

- **A — correct engine:** `readelf -n` BuildID `204ad049…` and SHA
  `3e498d64…`; harness runs, default path stays RED (T1-A).
- **B — wrong BuildID:** point `$GUEST` at a binary reporting a different Build
  ID (e.g. Flatpak) → pre-flight check aborts before the harness is launched.
- **C — SHA mismatch:** correct BuildID but altered SHA → abort.

**Expected logs / behavior (B/C).**
```
== 0. engine identity + clean ldd ==
FAIL: wrong engine build            # B (BuildID mismatch)
# or
FAIL: engine SHA mismatch           # C
```
Harness not launched; `$OUT`/`$MK` never created; no
`guest init complete`, no `first frame rendered`, no marker. If a runner is
written that launches before checking identity, it must still assert
`no_marker` — but the correct design is to abort in pre-flight
(`tests/app-shell.sh:56-59`, `D12-final-invariants.md` identity table).

**Forbidden:** executing `emit_marker`, `app shell rendered` in stdout, marker
file present, or `clean state: seeded_state=0` from a wrong-engine run.

**Fail-closed anchor:** `tests/app-shell.sh:57-58` BuildID gate; D01/D12 identity
invariants; 038 §2 requires the correct BuildID/SHA as a precondition for *any*
marker. The on-disk SHA must also be unchanged *after* every run
(`app-shell.sh:102-103`) — engine bytes are never patched.

**Regression-fail condition:** wrong-engine run reaching the gate or emitting a
marker ⇒ identity invariant broken; block.

---

## 3. Cross-cutting invariants asserted by all eight cases

1. Engine identity: BuildID `204ad049…6c9` and SHA `3e498d64…41335` unchanged
   pre/post (except T8 which deliberately violates it and aborts).
2. Sole emitter: stdout contains **zero** `app shell rendered` lines; no marker
   file for the case; no diagnostic marker promoted (`ALT-CALLBACK EXERCISED`
   stays in its own default-OFF file, absent from ctest/CMake/verify-native).
3. Gate verdict lines present and correct: `marker withheld: …` (seed and/or
   `callback=%d render=%d`); `render_ok=0` in every render-side case.
4. No positive proof line anywhere: no `-> GENUINE`, no `notifier hit
   0x27eaf6e placeId>0`, no `ra=base+… tag=guest-loop` accepted as proof, no
   `-> GUEST-TAGGED`, no `render provenance … -> OK`.
5. Withhold chain parity intact (D02/D09): aliases `SOBER_STARTAPP_V2` /
   `SOBER_STARTAPP_PARAMS`, `SOBER_ALT_CALLBACK_PATH`,
   `SOBER_LOG_APP_BRIDGE_NOTIFICATION`, `SOBER_RENDER_GUEST`, and the five
   prefixes `SOBER_GAME_` / `SOBER_STARTAPP_` / `SOBER_SGV2_` /
   `SOBER_PLATFORM_PARAMS_` / `SOBER_PROP_` all still withhold.
6. Phase-2/3 markers (`guest init complete`, `first frame rendered`) may be
   present on correct-engine runs — they are ancestry, not app-shell proof and
   never lift the marker.
7. Default-OFF diagnostics (`fault-baseline.sh`, ALT marker, notifier probe)
   stay out of ctest (`ctest -N` = 2) and `verify-native.sh`.

---

## 4. Summary matrix (all outcomes must be RED / no marker)

| Case | Adversarial input | Gate lever exercised | Expected decisive state | Marker |
| --- | --- | --- | --- | --- |
| T1 | host Route-B swap (×1, ×2) | RA interval / `render_ok` | `guest=0 draws=0 render=0` | none |
| T2 | counter inflation + sensors off | F7/F8 + RA + draw counters | decisive `0`, `draws=0` | none |
| T3 | host vtable call | `0x7191300` slot, host RA | `guest=0 render=0` | none |
| T4 | mixed guest+synthetic | taint / MIXED / withhold | `MIXED`, decisive `0` | none |
| T5 | `SOBER_ALT_CALLBACK_PATH=1` (+seed) | `seeded_state` override | `shell_cb=0` | none |
| T6 | `SOBER_LOG_APP_BRIDGE_NOTIFICATION=1` | name check + withhold | `callback=0` | none |
| T7 | kill switch OFF/ON | fail-safe layered sensors | `render=0` both states | none |
| T8 | wrong BuildID / SHA | pre-flight identity abort | harness not run | none |

A GREEN cell in the Marker column is a false-green regression in every row.

---

## 5. Final answer

Eight negative A/B tests (T1–T8) are specified above, each with an A control,
one or more adversarial B variants, normative expected log lines, explicit
forbidden lines, a reusable `no_marker` oracle, the fail-closed code anchor, and
the exact regression-fail condition.

Required outcome for **all eight**: the gate stays **RED** — it either returns
`-4` with `marker withheld: …`, or aborts in pre-flight identity checks — and
**no `app shell rendered` marker is emitted** (no stdout token, no marker file,
non-zero rc), with no `-> GENUINE` / `tag=guest-loop`-as-proof / `-> OK` verdict.
Host Route-B and host-vtable swaps are classified synthetic by RA; synthetic
counter inflation and mixed evidence fail closed via the F7/F8 decisive-0 and
taint rules plus the `seeded_state ⇒ shell_cb=0` override; the ALT thunk and
`onAppBridgeNotification` are withheld opt-ins that can never qualify; each
default-ON kill switch is fail-safe in both states; and a wrong BuildID/SHA
aborts before the gate. Any marker emission from any case is a false green and
must block integration.

This is a design derived from read-only source/evidence inspection; no harness
run, build, debugger capture or REA query was performed for this document. No
positive proof is claimed — **the marker remains WITHHELD** under decisions
013/038.

---
*No source, engine, gate, test or marker file was modified by G4.*
