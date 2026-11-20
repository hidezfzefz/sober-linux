# REVIEW A8 — Decision 013/038 gate-compliance audit

Auditor A8 · full-project review · analysis-only
Scope of this deliverable: **`analysis/REVIEW_A8_gate.md` only.** No `src/`,
`tests/`, `docs/`, build or engine file was edited. All statements below are
from static reads plus two read-only harness A/B confirmations.

Governing constraint: strict **decision 013** as reaffirmed/amended by
**decision 038**. `WITHHELD` is the correct marker state. Nothing in this
report proposes, requests or authorises any weakening of the gate.

---

## 0. Domain coverage and headline verdict

**Domain coverage: 100%** — all six requirements in the A8 task were examined,
all four named in-scope areas (`src/linux/guest_app_shell.c`,
`tests/app-shell.sh`, `docs/decisions`, the withhold chain) were read in full,
and two live A/B harness runs (default path and ALT path) were executed to
confirm the coded behaviour. Supporting evidence from `notifier_probe.c`,
`callback_provenance.c`, `native_jni.c`, `D02`, `D11`, `D12` and
`verification-report-471.md` was cross-checked.

| # | Task requirement | Verdict |
|---|---|---|
| 1 | Every guest-mutating opt-in is withheld | **PASS with residual gap** — all 72 Tier-1/Tier-2 knobs (23 + 49, 2 shared = 70 unique) are in the chain; 11 guest-state-relevant knobs remain outside (10 Tier-3 config seeds + 1 provenance-base modifier). None can fire the marker today. |
| 2 | ALT thunk is diagnostic-only | **PASS** — documented, default-OFF, first `seeded_state` term; empirically force-withholds. |
| 3 | Name-only counting is insufficient | **CONFIRMED** — gate uses name-only `callback_was_called`; decision 038 explicitly rules this insufficient. |
| 4 | Provenance requirement is documented | **PASS** — documented in 038 §2/§3/§5, 013, 037, D11 and the test; **not positively enforced by the gate** (by design — see §4). |
| 5 | No marker path can fire falsely | **PASS on current source** — sole `emit_marker` site is behind `shell_cb && render_ok` with `seeded_state ⇒ shell_cb=0`; all callback-producing knobs are in-chain; empirically confirmed. |
| 6 | Identify any remaining hole | **Found** — see §6: (a) structural: gate does not consume native provenance as positive proof; (b) residual: 11 guest-state-relevant knobs outside the chain; (c) alias/modifier parity hazards to keep closed. |

**Overall:** the strict gate is **UPHELD and WITHHELD**. No false-green path
exists in the current source. There is no basis for a success claim.

---

## 1. Withhold-chain coverage (requirement 1)

Evidence: `src/linux/guest_app_shell.c:1449-1546` (the
`int seeded_state = …` OR-chain).

Machine-extracted coverage of all `SOBER_*` environment knobs in `src/`
(resolving `*_ENV` macros and `env_or`/`env_int` first arguments):

- **181** distinct environment-knob names in `src/`.
- **86** explicit `getenv("SOBER_*")` terms in the chain, **1** symbolic
  (`getenv(SOBER_FAITHFUL_JNI_ARRAYS_ENV)` → `SOBER_FAITHFUL_JNI_ARRAYS`,
  `guest_app_shell.c:1536`), and **4** prefix terms
  (`env_prefix_active("SOBER_GAME_")`, `"SOBER_STARTAPP_"`, `"SOBER_SGV2_"`,
  `"SOBER_PLATFORM_PARAMS_"` at `:1526-1529`).
- Effective coverage: **134 / 181** knobs. The four prefixes cover all **47**
  Tier-2 value modifiers that `D02-withhold-audit.md:141-213` listed as the
  outstanding gap when that audit was written.
- Plus `sober_notifier_probe_should_withhold()` (`:1546`) — a fail-closed
  runtime sensor (`notifier_probe.c:412-417`: enabled OR installed OR any
  notifier/descriptor hit ⇒ withhold).

**Tier-1 (23, direct guest mutation)** — all closed. Evidence: `D02:109-139`;
each name appears verbatim in the chain (GOT writes, `app+0x488` seeds,
code-byte patches, ctor injection, singleton strategy/release, bootstrap
seeds, platform-fields). **Tier-2 (49, module value modifiers)** — all closed
by explicit names (ASB strategy/release-old) or the four prefixes.

Historical gaps from `verification-report-471.md:67-80` (13 knobs that used to
emit `app shell rendered` when combined with the ALT callback, e.g.
`SOBER_SEED_JNI_SINGLETON`, `SOBER_HELPER_REPAIR_V2`, `SOBER_VALIDATOR_PATCH`)
were re-checked individually: all 13 are present in the current chain
(`:1484-1498`, `:1489`, `:1491-1498`). The false-green condition that report
described is **closed**.

Benign names intentionally outside the chain (correct): marker output paths,
timing/window knobs, `SOBER_RENDER`/`SOBER_APP_SHELL` legitimate phase gates,
and the `SOBER_TRACE_*` / log-only family — consistent with
`D02:256-280` and 038 §3 ("timing/marker-path knobs are not disqualifying").

---

## 2. ALT thunk diagnostic-only (requirement 2)

- **Decision text:** `docs/decisions/038-strict-gate-alt-thunk-ruling.md:11-24`
  declares `SOBER_ALT_CALLBACK_PATH` and thunk `0x25848e0` diagnostic-only,
  harness-triggered, never proof, never contributing to the gate; 038 §5
  (`076-084`) notes the ALT `HONESTY NOTICE` is unreachable behind the withhold
  and calls that a strengthening. `013:38-50` and `037:1-13` carry the same
  ruling; `013:60-65` records the original open inconsistency that 038 closes.
- **Code:** `guest_app_shell.c:1258-1263` sets `alt_path` and logs
  `ALT PATH: skipping …`; `:1424-1436` calls the thunk directly; the knob is
  the **first** `seeded_state` term at `:1449`; the `HONESTY NOTICE` at
  `:1575-1580` sits *after* the `!shell_cb || !render_ok` early return at
  `:1556-1573`, so it is unreachable whenever ALT is set.
- **Test:** `tests/app-shell.sh:68` unsets the knob; `:109-113` fails on any
  `ALT PATH` / `HONESTY NOTICE` line; the stale thunk-as-proof assertion is
  gone. 038 §3 and §4 are implemented.
- **Empirical confirmation (A/B):** with `SOBER_ALT_CALLBACK_PATH=1` the run
  emitted `ALT PATH guest call 0x25848e0`, `guest->Java callback:
  gameLoadedCallback` (`callbacks=1`) — yet **no** `app shell rendered`, **no**
  shell marker file, no `HONESTY NOTICE`; stderr showed
  `marker withheld: diagnostic guest-state seed active (no app-shell frame proven)`
  and `marker withheld: callback=0 render=1`. The historical labelled
  false-green is fully closed.

---

## 3. Name-only counting is insufficient (requirement 3)

Confirmed. The gate's callback leg is:

```
shell_cb = sober_jni_callback_was_called(j, "onAppShellReloadNeeded")
        || sober_jni_callback_was_called(j, "gameLoadedCallback");   /* :1443-1444 */
```

`sober_jni_callback_was_called` (`native_jni.c:1446-1454`) is a pure per-name
handler `count > 0`. The counter is incremented in `sr_dispatch_call`
(`native_jni.c:478-512`) whenever a dispatched jmethodID's **name** matches a
registered handler — no caller/return-address/notifier check. Both names are
installed as default handlers (`native_jni.c:1068-1091`), so the thunk
(harness-triggered) and a genuine notifier produce the **same** name-only
signal. This is exactly what 038 §2.1 and §3 call "necessary but not
sufficient" and what `D11:077,081,206-216,288-306` flags.

The gate does **not** read `sober_callback_provenance_is_genuine()` or
`sober_notifier_probe_*` as positive conditions — grep over `src/` shows those
API calls only in `native_jni.c:492,875-876`, never in `guest_app_shell.c`.
The only provenance coupling in the gate is a **negative** one (the withhold
terms at `:1530` and `:1546`). The empirical ALT run above demonstrates the
consequence: a name-only `gameLoadedCallback` was observed, and only the
withhold chain — not provenance — prevented the marker.

---

## 4. Provenance requirement documented but not gate-enforced (requirement 4)

**Documented: PASS.** 038 §2.1 (`:32-39`) requires notifier-hit and
thunk-return-address provenance to be "enforced by the gate itself with all
opt-ins unset" and ends "Until native provenance instrumentation exists to
establish leg (C), the coded gate cannot prove it: the marker stays withheld
and no run qualifies." 038 §3 (`:54-63`) makes provenance the decisive test
condition; §5 (`:76-84`) requires zero markers for ALT and all opt-ins.
`tests/app-shell.sh:125-133` requires a gate-emitted
`[app-shell] … notifier-driven gameLoadedCallback … 0x27eaf6e … 0x27eafb3`
line and rejects `HARNESS-THUNK`, with an explicit "Do NOT set
`SOBER_CALLBACK_PROVENANCE`" warning.

**Implemented only as diagnostics:** `notifier_probe.c` (opt-in, fail-closed
detours, `snapshot()`/`describe()` at `:430-457`) and `callback_provenance.c`
(GENUINE/HARNESS-THUNK classification at `:224-245`, hooked at
`native_jni.c:492`). Both are default-OFF and both are **withhold entries**
(`guest_app_shell.c:1534`, `:1546`) — so the only way to obtain provenance
evidence is to activate a knob that itself forces the marker withheld. Net
effect: the gate emits **no** notifier-driven verdict line; grep found no such
string in `src/` (only a comment in `callback_provenance.h:86`). Consequently
`tests/app-shell.sh` is correctly **RED** at §3 and the marker is **WITHHELD**.

This is consistent with 038's stated end-state ("the marker stays withheld and
no run qualifies"). It is a structural non-implementation of 038 §2's
*positive* enforcement clause, not a weakening — and any future attempt to
wire provenance in as proof would be a gate change requiring a new lead
decision, not something this review authorises.

---

## 5. No false-green marker path (requirement 5)

- **Sole emission site:** `emit_marker("app shell rendered\n", "SOBER_SHELL_MARKER")`
  at `guest_app_shell.c:1581` — confirmed by repo-wide grep. The
  `SOBER_APP_SHELL_DIAGNOSTIC` hook at `:1564-1567` only logs inside the
  withheld branch and never emits a marker (038 §4 satisfied). No separate
  ALT diagnostic marker exists in C; 038 §4's "distinct function/token/file"
  rule is therefore not violated.
- **Guard structure:** `seeded_state ⇒ shell_cb = 0` (`:1547-1551`);
  `if (!shell_cb || !render_ok) return -4` (`:1556-1573`) precedes the sole
  emit. `render_ok = sober_render_phase3_done()` (`:1554`) alone is never
  sufficient.
- **All callback-producing knobs are in-chain:** ALT (`:1449`), game-loaded
  path (`:1470,1476`), startgame/game-params/SGV2 (`:1454-1456` + prefix
  `:1528`), start-app/runnable/lua (`:1450-1451,1477`), and the ASB /
  singleton / seed families. On the clean path the guest faults before the
  notifier (`0x2eb4b65`), so no qualifying name is dispatched.
- **Empirical default-path run (no opt-ins):** stdout exactly
  `guest init complete` + `first frame rendered`; init/frame marker files
  present; **shell marker absent**; stderr
  `marker withheld: callback=0 render=1 (env calls=33 callbacks=0 last='isVrDevice')`;
  no ALT/HONESTY/callback lines. This matches `D12-final-invariants.md:39,104-117`.

Verdict: on the current source **no marker path can fire falsely**.

---

## 6. Remaining holes (requirement 6)

**(a) Structural — gate does not positively enforce notifier provenance.**
The callback leg is name-only and the gate never consumes the native
provenance signal as a *positive* condition (§3-§4). Safety today comes
entirely from the withhold chain plus the fact that the clean path never
dispatches a qualifying name. If a future change makes the genuine callback
reachable on a default path, the gate would emit the marker **without**
notifier/thunk-return provenance — a false green under 038 §2. This is a
latent design gap, currently benign and explicitly acknowledged by 038 §2's
"marker stays withheld" clause. **No fix is proposed here**; any corrective
work must add provenance as an *additional* gate condition without relaxing
any existing one.

**(b) Residual withhold coverage — 11 guest-state-relevant knobs outside the
chain.** From the 47 uncovered names (§1), 10 are D02 Tier-3 guest-visible
configuration seeds that D02 recommended withholding but left to a lead
ruling: `SOBER_ASSET_ROOT`, `SOBER_GUEST_PATH`, `SOBER_LANG`, `SOBER_COUNTRY`,
`SOBER_SCREEN_WIDTH_DP`, `SOBER_SCREEN_HEIGHT_DP`, `SOBER_SCREEN_SIZE`,
`SOBER_NAV_HIDDEN`, `SOBER_WINDOW_WIDTH`, `SOBER_WINDOW_HEIGHT`, plus the
dynamic `SOBER_PROP_<name>` family (`android_compat.c:719-730`, backs
`__system_property_get`), and `SOBER_CALLBACK_PROVENANCE_GUEST_BASE`
(`callback_provenance.c:60`, a modifier of an in-chain diagnostic). These do
not write guest memory and cannot dispatch a callback name on their own, so
they do not create a false-green path today; but a run with them set would
violate the *letter* of 038 §3's "every guest-mutating/opt-in `SOBER_*` knob
unset" clean-state leg without the gate noticing. Evidence:
`D02-withhold-audit.md:215-233,282-290`.

**(c) Parity hazards to keep closed (not current holes).** Keep both
`SOBER_STARTAPP_V2` and its alias `SOBER_STARTAPP_PARAMS` in-chain
(`D02:237-243`), keep `SOBER_HELPER_REPAIR` paired with `_V2`
(`D02:244-246`), and keep the four value-modifier prefixes present — the
prefix match is what closes the 47 Tier-2 modifiers. Dropping any one reopens
a concrete bypass. `SOBER_CALLBACK_PROVENANCE` must remain a withhold entry
while it is a diagnostic (`D11:288-306`, 038 §3).

---

## 7. Evidence index

| Claim | File:lines |
|---|---|
| Marker gate expression / sole emit | `src/linux/guest_app_shell.c:1443-1444,1547-1581` |
| Withhold chain (86 + 1 macro + 4 prefixes + notifier sensor) | `src/linux/guest_app_shell.c:1449-1546` |
| ALT thunk harness-trigger path + HONESTY NOTICE | `src/linux/guest_app_shell.c:1258-1263,1424-1436,1575-1580` |
| Name-only counter / handler install | `src/jni/native_jni.c:478-512,1068-1091,1446-1454` |
| Notifier fail-closed probe / withhold sensor | `src/jni/notifier_probe.c:320-417,430-457` |
| Provenance GENUINE/HARNESS classifier (diagnostic only) | `src/jni/callback_provenance.c:224-260`; hook `src/jni/native_jni.c:492` |
| Test strict contract (absence checks + provenance requirement) | `tests/app-shell.sh:68,105-140` |
| Rulings | `docs/decisions/013:5-13,38-50,52-78`; `038:11-84`; `037:1-13` |
| Withhold audit / chain history | `analysis/D02-withhold-audit.md:109-233,282-290`; `D11:288-306`; `tests/verification-report-471.md:67-80`; `tests/D12-final-invariants.md:104-117` |
| Live A/B confirmations | default: `callback=0 render=1`, no shell marker; ALT: `callbacks=1` yet `marker withheld: diagnostic guest-state seed active`, no marker/HONESTY (this review, §2/§5) |

---

## 8. Honesty statement

No login/app-shell frame has been proven. The `app shell rendered` marker is
**WITHHELD** and that is the correct state. This review proposes no gate
change, adds no withhold entry and removes none; it merely records compliance
and the residual gaps for the lead. The provenance requirement remains
documented and intentionally unimplemented in the gate; the test remains
correctly red. No success is claimed.
