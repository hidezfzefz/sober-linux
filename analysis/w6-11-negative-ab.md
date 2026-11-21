# Task #540 — Wave6 negative A/B expansion N21–N30 (oracle / DEX / JNIEnv / seed relabeling)

Author: worker-11 (Convio team `TEAM-C2YA-ZTU9-RMAV`). Scope: **this file only**.
Analysis-only: no `src/`, `tests/`, `CMakeLists.txt`, `README.md`, `docs/`, vault or
git edits; no commit; lead commits/pushes. All review was read-only — no harness
rebuild, no emulator run, no DEX-tool run was required for this note (the DEX and
engine facts are taken from the already-verified vault note `CA01` and the frozen
identity in `EngineFacts.md`). Target strings/data were treated as untrusted.

Snapshot: clean tree at wave5 head `a64b603` (one commit past vault `State.md`'s
`1cb6fc5`); `git status --short` empty. Line numbers are for that tree; the quoted
symbols/strings are the durable evidence and should be re-checked after integration.

Frozen engine: BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
**Marker status: WITHHELD. No success claimed. Gate not weakened.**

---

## 0. What wave6 changes, and why the negative matrix must grow

Wave6 adds three *research* oracles that wave5 did not have: the authoritative
SHA-matched base APK/DEX, the Craft Anything CLI (`--deep`), and the installed
Android SDK/emulator. The team goal explicitly says these are **research tools
only**, never the final runtime. That creates four new false-green surfaces that
N1–N20 (`analysis/task534-negative-ab.md`) did not name:

1. **Emulator-oracle relabeling** — an observation made under the emulator is
   presented as if it were made in the native harness.
2. **Authoritative vs modded DEX** — a conclusion is drawn from the modded
   `~/Downloads/basalt-arm64-2.740.931.apk` (classes2.dex `6c2875cd…`) instead of
   the authoritative SHA-matched `base.apk` (SHA `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4`,
   classes2.dex `5afac2bb…`) (`CA01:10-12`).
3. **JVM-provided JNIEnv/JavaVM** — a host/stock JVM or the "faithful JNIEnv"
   modules supply the JNI surface, and that is mistaken for the guest's real
   two-level JavaVM/JNIEnv path.
4. **Seeded state relabeling** — an oracle-driven run is presented as
   `seeded_state==0`.

Each is a *negative control*: it must stay RED/withhold. A control that flips
green is a false-green bug, not a success.

The gate architecture these controls rely on (re-validated read-only):

* **Leg C** `shell_cb = is_genuine("gameLoadedCallback") || is_genuine("onAppShellReloadNeeded")`,
  forced 0 on harness taint (`guest_app_shell.c:1748-1750`). GENUINE requires, at
  one dispatch: qualifying name (`SOBER_PG_QUAL_COUNT=1`, `provenance_gate.c:45-46,403-409`),
  frozen BuildID (`g_build_verified`, `provenance_gate.c:110-145,217-222,478-479`),
  cached class/method identity (`:441-445`), `place_id>0` (`:464-469`), live JavaVM
  at `0x7cdec78` (`:471-476`), thunk RA `0x2584960`, notifier RA `0x27eafb3`,
  invoke RA `0x27eaf32`, and node dispatch `0x27eaf04` entered from guest `.text`
  via the 3-frame walk (`:255-304,389-400,480-490`). A host-entered dispatch or
  any harness thunk sets sticky `g_harness` → `shell_cb=0` (`:491,504-507,513-517`).
* **Leg R** `render_ok = guest_render_ok()` (`guest_app_shell.c:1983`) is owned by
  `frame_evidence`: ≥2 guest-tagged swaps, one recurring guest RA, ≥1 bracketed
  guest draw, and G8 window proof (real ACQUIRE/DIMS RAs, `Created ANativeWindow`,
  real guest window/store); mixed guest/synthetic taint zeroes every decisive
  accessor (worker-5 note §1–§3, N7/N16).
* **Leg S** `seeded_state` presence-chain (`guest_app_shell.c:1761-1960`) forces
  `shell_cb=0` and prints `marker withheld: diagnostic guest-state seed active`
  (`:1961-1964`). The positive `clean state: seeded_state=0` line (`:1970-1971`)
  and gate-owned provenance lines (`:1969`; `:1988-1993`) are emitted only in a
  clean, genuine pass.
* **Test** `tests/app-shell.sh` re-runs the native harness with all inherited
  `SOBER_*` scrubbed except a small diagnostic allowlist (`:68-76`), checks BuildID
  + on-disk SHA (`:57-59,109-110`), bans compatibility-layer deps in `ldd`
  (`:60-63`), and requires exact provenance/verdict lines plus exactly one marker
  (`:141-199`). Missing engine now `exit 1` (`:20`), so SKIP is not a pass.

---

## 1. New negative controls N21–N30 — must stay RED/withhold

"Blocked at" is the first mechanism that forces `shell_cb=0`, `render_ok=0`, a
withhold line, or a test FAIL. All ten are **RED** on the shipped tree.

| # | Wave6 risk / negative case | Blocked at | Verdict |
|---|---|---|---|
| **N21** | **Emulator log relabeling.** Agent runs the guest under the Android emulator, captures a logcat/screenshot showing `gameLoadedCallback`, `Created ANativeWindow`, or a frame, and cites it as native-harness success. | The strict test runs only `$HOST_BIN` and greps fresh `$ERR`/`$OUT` (`app-shell.sh:83,99-102`); the positive lines are emitted by native code (`guest_app_shell.c:1969-1971,1988-1993`; `provenance_gate.c:598-617`) and the marker file is created by the host process (`:82,196`). `ldd` bans `android|qemu|emulator|wine|box64` (`:60-63`). An emulator observation cannot produce any accepted artifact. | RED ✅ |
| **N22** | **Emulator-derived RVA used to patch/relabel the guest.** An emulator trace is used to justify a guest byte patch, RVA rewrite, or "faithful" seed while claiming a clean boot. | No-byte-patch rule; on-disk engine SHA before/after (`app-shell.sh:59,109-110`) and exact `PT_NOTE` BuildID bytes (`provenance_gate.c:110-145,169-172`); any seed knob is in the withhold chain (`:1761-1960`). A patched engine fails either the SHA or the BuildID conjunct. | RED ✅ |
| **N23** | **Oracle "render proof."** An emulator SurfaceFlinger frame, or the bare host X11 window (`win_seen`, `app-shell.sh:202-208`), is presented as G8 guest-window proof. | Render leg is `frame_evidence` alone: real ACQUIRE/DIMS RAs (`0x649c2d6/303/30f`), real `Created ANativeWindow` log, real guest window/store (C0–C10); ≥2 guest-RA-tagged swaps with one recurring RA and a bracketed guest draw; synthetic taint zeroes decisive accessors. `win_seen` is only a WARN path and never a pass by itself. | RED ✅ |
| **N24** | **Modded DEX as evidence for a "clean boot route."** A conclusion such as "the notifier has a static caller" or "the latch can be set in boot" is drawn from the modded `~/Downloads/basalt-arm64` DEX (`6c2875cd…`) and wired into a "clean" path. | Authoritative-DEX rule (`CA01:10-12`): only the SHA-matched `base.apk` (SHA `114a6f8e…`, classes2.dex `5afac2bb…`) may be evidence. The gate does not consume DEX at runtime, so this is an analysis-discipline control: positive credentials still need the live guest stack chain + cached identity + placeId + BuildID; any knob derived from a false DEX conclusion is in the withhold chain. Machine check is **OPEN** — see F8. | RED ✅ (discipline); F8 OPEN |
| **N25** | **Relabel modded engine/DEX as authoritative at runtime.** Harness loads a rebuilt/modded `libroblox.so` (or repacked DEX) that looks similar but has different bytes, then claims GENUINE. | BuildID check (`app-shell.sh:57-58`) and on-disk SHA (`:59,109-110`); `sober_provenance_gate_verify_build` compares exact BuildID note bytes (`provenance_gate.c:110-145`); base discovery prefers the r-x mapping with matching BuildID (`:152-176`). Note BuildID alone is **not** byte identity — the SHA checks are essential; do not drop them. | RED ✅ |
| **N26** | **Host/stock JVM supplies JNIEnv/JavaVM to satisfy `java_vm != NULL`.** A host JVM (or the faithful JNIEnv modules) writes `base+0x7cdec78` with a host JavaVM and the callback is presented as genuine. | `SOBER_SEED_JNI_VM`, `SOBER_FAITHFUL_JAVAVM`, `SOBER_JAVAVM_DEFAULT`, `SOBER_JNISLOTS_DEFAULT`, `SOBER_FAITHFUL_JNI_ARRAYS` are all chained (`guest_app_shell.c:1800-1802,1902,1934,1874`); test env-scrub (`:68-76`); the GENUINE predicate still needs the notifier/thunk RA pair and guest-entered node dispatch (`:480-490`) — a JNIEnv alone cannot manufacture the 3-frame walk. Vtable-identity leg is **OPEN** — see F6. | RED ✅; F6 OPEN |
| **N27** | **Host-provided 231-slot JNIEnv seen as guest's.** The faithful-JNIEnv/slots module hands the guest a host-shaped env; a callback dispatched through it is counted as guest. | Same knobs chained as N26; name-only callback counters are diagnostic only (`guest_app_shell.c:1751-1756`); any harness thunk sets sticky `g_harness` → `shell_cb=0` (`:1750`, `provenance_gate.c:504-507`); test forbids `HARNESS-THUNK` (`app-shell.sh:165-169`). | RED ✅ |
| **N28** | **"Oracle seed relabeled clean."** Wave6 drives guest state via emulator/JVM oracle, then claims `seeded_state==0` because the final shell set no `SOBER_*`. | The oracle cannot run inside the native harness without a chained knob (N26/N27) or an engine swap (N22/N25); the chain is presence-based over 160+ literal knobs and 6 prefixes (`guest_app_shell.c:1761-1960`, `env_prefix_active` at `:242-248`); `clean state: seeded_state=0` is printed only in the clean branch (`:1965-1971`); test requires that line (`:170-171`) and forbids the withhold line (`:150-152`). | RED ✅ |
| **N29** | **New oracle prefix escapes the chain (forward-looking).** Wave6 adds e.g. `SOBER_WAVE6_ORACLE=1`, `SOBER_EMULATOR_REPLAY=1`, `SOBER_JVM_BRIDGE=1`, none matching the 6 existing prefixes. | No such knobs exist at `a64b603` (tree grep: only `SOBER_GAME_`, `SOBER_STARTAPP_`, `SOBER_SGV2_`, `SOBER_PLATFORM_PARAMS_`, `SOBER_PROP_`, `SOBER_EVIDENCE_` at `:1864-1867,1896,1958`), so **not a live gap**. Release control: any wave6 oracle knob must be added by name to the chain (or a `SOBER_WAVE6_`/`SOBER_ORACLE_` family added to `env_prefix_active` and to D09) before integration. | RED now; re-run after edits |
| **N30** | **Splice oracle output into the native log.** Emit two markers, or copy a `-> GENUINE` / `-> OK` line from an oracle log, to force a pass. | Marker count exactly 1 (`app-shell.sh:193-195`); forbidden lines (`:145-152`); gate-owned provenance is one-shot and re-derived by the native process (`guest_app_shell.c:1969`, `provenance_gate.c:598-617`); render provenance line only when `!seeded_state && shell_cb && render_ok` (`:1988-1993`). | RED ✅ |

These are additive to N1–N20; every earlier control (especially N8/N14 3-frame
guest walk, N10 presence-not-truthiness, N11 guest-base overrides, N17 C10, N19
new-knob coverage) remains mandatory. Cross-reference: `analysis/task534-negative-ab.md`.

---

## 2. Per-risk detail

### 2.1 Emulator-oracle relabeling (N21–N23)

The emulator is valuable for *discovering* what the real Java/Android side does —
e.g. which native entry points the lifecycle listener calls — but it is a different
process, on a different runtime, with different mappings. None of its artifacts are
accepted by the gate:

* The test executes exactly one binary, `$HOST_BIN` (`tests/app-shell.sh:83`), on the
  frozen engine, and derives every positive assertion from that process's stderr
  (`:99-102,141-190`). There is no code path that reads an emulator log.
* The positive credential lines are emitted only from native code compiled into the
  harness: `sober_provenance_gate_emit_provenance()` (`guest_app_shell.c:1969`,
  `provenance_gate.c:598-617`), `clean state: seeded_state=0` (`:1970-1971`), and the
  gate-owned render-provenance `-> OK` line (`:1988-1993`).
* The BuildID/SHA and `ldd` checks (`:57-63,109-110`) bind the run to the x86_64
  frozen engine with no compatibility/translation layer. Emulator-derived RAs are
  irrelevant here: the stack-walk checks require the live native guest stack
  (`provenance_gate.c:272-304,323-401`).

Conclusion: an emulator observation can *guide* analysis but can never be relabeled
as gate evidence. Any report that cites an emulator log line as satisfying an
`app-shell.sh` assertion is a false-green attempt.

### 2.2 Authoritative vs modded DEX (N24–N25)

`CA01:10-12` is the controlling fact: the authoritative `base.apk` is the
SHA-matched companion at
`/home/user/.local/share/basalt/linux/payloads/3170-204ad049599c96afb75b8a21d5051063e821d6c9/sober_apk/base.apk`
(SHA `114a6f8e…`; classes2.dex `5afac2bb…`), while
`~/Downloads/basalt-arm64-2.740.931.apk` (classes2.dex `6c2875cd…`) is modded and
**must never be used as evidence** for this engine. The x86_64 split has no
classes.dex.

The gate does not read DEX at runtime; therefore this is primarily an
evidence-provenance control. The danger is epistemic: a modded DEX could appear to
show a boot-time latch write or a static caller of notifier `0x27eaf04`, leading an
agent to wire a "clean" path that is really just the modded behavior. Two defenses
hold: (a) the authoritative-DEXcitation rule, and (b) the gate still requires the
live guest stack chain, BuildID, cached identity, and placeId — a conclusion that
is false for the frozen engine cannot manufacture those at runtime, and any knob
added to force them is in the withhold chain. The runtime SHA/BuildID checks
(N25) prevent a modded artifact from being swapped in.

### 2.3 JVM-provided JNIEnv/JavaVM (N26–N27)

`0x7cdec78` is the JavaVM* captured from `JNI_OnLoad` argument 1; consumers use the
genuine two-level layout `JNIInvokeInterface_::GetEnv (+0x30 → 0x6c8e8a8)` then
`JNIEnv::NewStringUTF (+0x538)` (`PROGRESS.md:179-186`). The repo contains several
*opt-in* faithfulness modules (`SOBER_SEED_JNI_VM`, `SOBER_FAITHFUL_JAVAVM`,
`SOBER_JNISLOTS_DEFAULT`, `SOBER_FAITHFUL_JNI_ARRAYS`, javavm_semantics) — all of
which are in the withhold chain and all of which are diagnostics, never evidence.

A stock host JVM is not a substitute: it would have to (i) write the guest global,
(ii) satisfy the cached class/method and placeId reads, and (iii) produce a
guest-entered notifier stack frame. (i) alone is covered by the withhold chain;
(ii) and (iii) are guest-memory/stack facts a host JVM cannot supply. The one place
to watch is **F6** below: the gate samples `java_vm != NULL` but does not validate
the vtable identity, so a host JavaVM written to the global *without any SOBER_*
knob* would pass that single leg. It still cannot pass the rest of the predicate.

### 2.4 Seeded state (N28–N29)

`seeded_state` is presence-based: `getenv(...) != NULL` and `env_prefix_active`
checks `*e[n]=='='` (`guest_app_shell.c:242-248`), so setting a knob to `""` or `0`
does not make a run clean (worker-5 N10). The chain spans 160+ literal knobs and
6 prefixes (`:1761-1960`) and includes module verdict functions such as
`sober_publish_default_should_withhold()` (`:1943`) so an "enabled but inert"
module does not withhold while an active one does. The test additionally scrubs
inherited `SOBER_*` (`:68-76`). Wave6's new risk is not a known knob but an
*unknown new one* (N29): if an oracle module is introduced with a fresh prefix,
the chain will not see it until a human wires it in. That is why N29 is a
release-control, not a claim of a current live gap.

---

## 3. Residual findings / open items (honest reporting)

* **F6 — OPEN, not a live false-green.** The GENUINE predicate uses
  `java_vm != NULL` only (`provenance_gate.c:475,488`); it does not verify the
  two-level vtable contract (`GetEnv +0x30 → 0x6c8e8a8`, `NewStringUTF +0x538`,
  `PROGRESS.md:181-184`). A host JVM that writes the guest global with no SOBER_*
  knob would satisfy that leg. It still fails the guest stack walk, BuildID,
  cached identity, and placeId legs, so the overall gate stays RED; but the leg is
  weaker than it looks. Recommend adding a read-only vtable-identity conjunct
  (fail-closed only) so "JVM-provided JNIEnv" is structurally excluded.
* **F7 — forward-looking.** No `SOBER_WAVE6_*` / `SOBER_ORACLE_*` / `SOBER_JVM_*`
  prefix is in `env_prefix_active` (tree grep confirms only the 6 legacy families).
  There are no such knobs yet, so no live gap — but adding an oracle module without
  updating the chain and D09 would create one.
* **F8 — analysis-discipline gap, not machine-enforced.** DEX-derived claims are
  correct only if the authoritative SHA-matched APK is used. The gate cannot check
  which APK an analyst read. Recommend lead denylist the modded `~/Downloads` path
  in evidence and require the authoritative SHA (`114a6f8e…`) in every DEX-derived
  claim, consistent with `CA01`.
* **F9 — evidence hygiene.** `app-shell.sh:76` removes marker/log files before a
  run, which blocks pre-seeded `/tmp` artifacts. Keep this; an oracle-produced log
  must never be substituted for a fresh native run. D09 remains single-TU and
  literal-only (worker-5 F5), so its `(none)` is not cross-TU closure.

No live false-green found in the shipped tree. Marker WITHHELD; gate fail-closed.

---

## 4. Recommendations to lead (do NOT weaken the gate)

1. Re-run N1–N30 after every wave6 knob/wiring change; require **zero** `app shell
   rendered` emissions in every RED case. Treat N21–N30 as release controls for
   oracle-assisted callback/render work.
2. Wire any new oracle knob into the `seeded_state` chain **by name or via a new
   `env_prefix_active` family** before wiring it into the guest path; extend D09 to
   resolve `SOBER_*_ENV` macros and model prefix families (default-OFF, exit 0).
3. Strengthen the `java_vm` leg from `!= NULL` to a read-only two-level vtable
   identity check (F6); keep it conjunctive and fail-closed.
4. Keep BuildID + on-disk SHA checks (`app-shell.sh:57-59,109-110`) and both
   guest-base override entries mandatory (N11/N25). Denylist the modded APK as
   evidence (F8).
5. Keep emulator/SDK/Craft Anything strictly research-only: no emulator-derived
   line may satisfy an `app-shell.sh` assertion; lead re-runs the test clean.
6. Do not relax C10 (N17), the placeId/BuildID checks, the 3-frame guest walk
   (N8/N14), the clean-state requirement, the exactly-one-marker rule, or the
   `exit 1` SKIP.

---

## 5. Evidence index (all read-only)

* Vault: `Home.md`, `Goal.md`, `EngineFacts.md`, `Decisions.md`, `State.md`,
  `Ownership.md`, `notes/CA01-craft-anything-verification.md`.
* `src/linux/guest_app_shell.c:242-248,1748-1756,1761-1960,1961-1972,1983-1993,1996-2012`
* `src/jni/provenance_gate.c:45-46,110-145,152-176,208-226,255-304,323-401,403-409,427-518,522-524,598-617`
* `src/jni/provenance_gate.h:114-115,154`; `PROGRESS.md:179-192`
* `tests/app-shell.sh:20,57-63,68-76,82-83,99-102,109-110,141-190,193-199,202-208`
* `analysis/task534-negative-ab.md` (N1–N20 baseline); `analysis/D02-withhold-audit.md`;
  `docs/decisions/013-no-weak-app-shell-marker.md`; `docs/decisions/038-strict-gate-alt-thunk-ruling.md`

No source, test, docs, README, gate file, or vault note was changed by this review.

---

## 6. Addendum — engine-relabeling trap from teammate hand-off (worker-14 / #549)

Worker-14's provenance-ledger hand-off (Convio team message, task #549 —
`analysis/w6-14-provenance-ledger.md`, not opened or edited by me) reports a third
engine-relabeling trap that belongs under **N25**: REA work copies such as
`/home/user/sober-linux/analysis/rea/work/lib/x86_64/libroblox.so` hold the
**Flatpak 2.745 build** (SHA `8afce3e6…`, BuildID `791c3f9e…`), not the frozen
engine. `EngineFacts.md` already forbids mixing that build's RVAs. This is the same
false-green shape as N25 (a wrong engine relabeled as authoritative) and N24 (a
wrong DEX relabeled as authoritative): the gate rejects it via the exact BuildID
`PT_NOTE` check (`provenance_gate.c:110-145`) and the on-disk SHA check
(`app-shell.sh:59,109-110`), but *analysis* must also denylist it so RVA claims are
never derived from it. Add it to the forbidden-evidence list alongside the modded
`~/Downloads` APK (F8). All other controls and the WITHHELD verdict are unchanged.

Two further teammate rulings corroborate controls above (team messages, not opened
as evidence by me):

* **worker-9 / #537** (`analysis/w6-09-java-stubs.md`): Java `Surface`/`Activity`/
  `Looper` stubs in OpenJDK to drive DEX paths are **forbidden** as an
  Android-framework compatibility layer and/or a withheld seed (decisions
  000/004/012 + 013/038); a native JVM is allowed (012) but Java Android framework
  shims are not. Minimal stand-ins merely re-skin the synthetic C bridge and cannot
  produce a guest-entered notifier `0x27eaf6e` with `seeded_state==0`. This confirms
  **N26/N27**: a JVM-provided JNIEnv/Java surface is never a clean route, and any
  such path is a withheld diagnostic at best.
* **worker-12 / #553** (`analysis/w6-12-build-safety.md`): new build/link work must
  put every new module knob into the `seeded_state` withhold chain — the build-side
  counterpart of **N29 / F7**. Keep symbol ownership single-owner and avoid
  `-fvisibility=hidden` dlsym-null hazards so a new oracle module cannot silently
  bypass the gate.
* **worker-2 / #539** (`analysis/w6-02-jvm-readiness.md`): `0x227b92d` is the only
  `JNI_OnLoad` store of JavaVM global `0x7cdec78`; `0x227b9e2` is its `GetEnv(+0x30,
  0x10006)` consumer. Critically, the default-ON `javavm_default` module writes
  `0x7cdec78` **by default**, so `READY_JAVAVM` (`java_vm != NULL`) is
  necessary-not-sufficient and is already satisfied in a stock native run. This
  sharpens **F6**: the vtable-identity conjunct requested by lead is important,
  but the gate still stays closed via the guest stack walk, cached identity,
  BuildID and placeId legs — do not treat a live `java_vm` as a clean credential.
* **worker-16 / #551 + lead** (`analysis/w6-16-event1-producer.md`): a fourth
  forbidden evidence source for **N24 / F8** — `/tmp/opencode/mini/out/smali` is a
  *different modded mini APK* (DEX class names such as `uh/h0` vs the authoritative
  `uh/j0`); authoritative smali is `/tmp/opencode/auth_smali`, derived from the
  SHA-matched `base.apk` (`114a6f8e…`). Any DEX-derived claim must be re-verified
  against the authoritative source, never mini/out or the modded Downloads APK.
