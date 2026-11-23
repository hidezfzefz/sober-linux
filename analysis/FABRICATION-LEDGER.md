# Fabrication ledger (wave13)

Owner: lead. Rule: every host-invented object/value is debt with an owner and
a removal condition. Nothing here is a "fix"; entries are temporary, opt-in,
kill-switchable, and must be removed before any clean marker claim.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 1 | `SOBER_AVD_BOUNDARY_REPLAY` exact captured arg0 | `src/jni/avd_boundary.c`, `guest_app_shell.c` | lead | Replays AVD-captured 1,533,221-byte JSON (SHA `8215519f…`) because the clean harness has no Java caller | Remove when a genuine Java/DEX startup supplies this call |
| 2 | Provenance class "AVD-captured boundary input" | `guest_app_shell.c` gate | lead | Distinguishes oracle input from seeded_state; fail-closed | Remove with #1 |
| 3 | Canonical jstring install for the boundary call | `guest_app_shell.c` | lead | GetStringUTFChars needs exact bytes; without it the guest sees NULL | Remove with #1 |
| 4 | stdio GOT rebind (`SOBER_BIONIC_SF_FULL`) | `src/compat/bionic_sf_full.c` | manager (wave10) | Guests' `fflush`/`fwrite` PLT GOT slots bypass the Bionic `__sF` bridge | Remove when the guest's own relocations resolve those slots correctly |
| 5 | Worker-scoped scheduler prime (`SOBER_SCHEDULER_GUEST`) | `src/jni/scheduler_guest.c` | manager (wave10) | Compensates for missing real worker attach; itself seeded | Remove when the real JavaVM attach path is present |
| 6 | `SOBER_SCHED_DIAG` read-only dump | `guest_app_shell.c` | lead | Diagnostic only, no guest writes | Remove when the wave13 question is answered |
| 7 | `SOBER_STOP_BEFORE_SETTINGS` + `prctl(PR_SET_PTRACER_ANY)` | `guest_app_shell.c` | lead | Diagnostic SIGSTOP attach point; no guest state change | Remove after the writer-RA question is answered |
| 8 | `SOBER_HW_WATCH` / `SOBER_HW_WATCH_ADDR` perf breakpoint | `src/jni/hw_breakpoint.c` | lead | Read-only in-process DR watch; logs RIP in a signal handler | Remove after writer analysis |
| 9 | `SOBER_AVD_BOUNDARY_UNVERIFIED` / `SOBER_AVD_BOUNDARY_PATH` | `src/jni/avd_boundary.c` | lead | **MODIFIED INPUT, diagnostic only** bisect path; SHA verification deliberately skipped and labeled | Remove after bisect; never counts toward a rung |
| 10 | Enhanced perf-watch handler (RIP/regs/bytes/stack RAs) | `src/jni/hw_breakpoint.c` | lead | Read-only diagnostic dump; first 16 hits only | Remove after writer attribution |
| 11 | Activation-scoped exec+data perf watchpoints | `src/jni/hw_breakpoint.c` | lead | Exec breakpoint after prologue `0x2312009` re-arms a write watch on this activation's `rbp-0xb8` | Remove after writer attribution |
| 12 | Fault-time thread census | `src/linux/guest_dlopen_host.c` | lead | Reads `/proc/self/task/*/comm|stat`; no guest writes | Remove after thread comparison |

**Not applied (rejected):** the proposed "real guest-addressable FFlag object"
shim at `jni_native_objects.c`. Read-only diagnostic at `guest_app_shell.c`
shows `fflag_obj = *(0x72a3058 + 0x18)` is a real host pointer
(`0x55b7d51c2dc0`), not the `0x30` token claimed in wave12. The shim would
target the wrong object; applying it would be fabrication.

## Marker reproducibility status

NOT VERIFIED on the AVD. Three attempts:
- w11 hook: reached `nativeInitClientSettings`, captured the exact arg0 bytes.
- w12 150 s / 300 s Java→native capture: reached `nativeInitClientSettings`,
  `nativeAppBridgeAppStart`, `V2InitWithParams`, `StartLuaAppDM`,
  `MessageBus.publishRaw("Credentials.isAvailableResponse")`; **zero** surface
  lifecycle, placeId, `StartGame`, or `gameLoadedCallback`.
- w13 stimulus attempt (`am start` deep link `roblox.com/games/606849621`):
  app process died; Frida attach lost; logcat shows repeated HTTP 401
  (`voice.roblox.com`, auth-dependent endpoints).

Answer to "is the marker reachable": **(c) never verified on AVD** — the
contract came from `Goal.md`/decisions 013/038. Evidence leans toward
(a)+(b): a logged-in session/valid credentials and an actual game join are
probably required, and neither was supplied. That remains a hypothesis, not a
fact.

Kill switch: the AVD cannot reach the marker in this environment and the
harness has not reached `StartLuaAppDM`. Per the agreed stop rule, this
approach is not converging; further fault-chasing waves should not continue
without first solving the oracle/auth problem.

## Wave15 additions (no shim, no guest writes)

- AVD path hook run (`w15-avd-paths.log`, not committed): ordered events
  `PARSE_ENT(0x2311516)` → `PARSE_MID(0x231c8fb)` → `PARSE_RET(0x2eb7334)` →
  `LATCH_WR(0x2eb7520)`; `SLOT_READ(0x23121ae)` never fired in 144 events.
  The earlier "path divergence" claim based on a non-firing hook is **retracted**:
  that run was stuck at an upgrade dialog, not evidence of a path difference.
- Harness writer search: SIGSTOP + `gdb -p` blocked by `ptrace_scope=1`;
  remote gdbserver stalled on `int3` SIGTRAPs; in-process `perf_event_open`
  hardware watch armed correctly and fired, but every observed write of `0x30`
  to the watched stack slot came from **host** RIPs, never guest code. No
  guest writer RA for `0x30` was found. No shim return log exists that records
  `0x30`, so the shim-return hypothesis is also unconfirmed.
- Diagnostic bisect (**MODIFIED INPUT, diagnostic only**): 1, 100, 1000 flags
  → SIGSEGV in `pthread_mutex_lock` (libc+0x95aa4, `si_addr=0x13f/0x160`,
  CF0=0, D08=0); 10000 flags → SIGTRAP at the TaskScheduler guard `0x6caef67`
  with CF0=0x41, D08=1; all 26044 flags → SIGSEGV as before. Smallest crashing
  count = **1**, first flag = `DFFlagConsumePlatformNameOverAlternateName`.
  Behaviour is non-monotonic in flag count, so this is not a clean threshold.
  None of these runs counts toward any checkpoint rung.

## Wave18 audit (task #586) — re-check of waves 14-17 PROVEN claims

Owner: audit (branch `w18-audit`). Full redacted notes: `/tmp/opencode/w18-audit/audit-notes.md`
(out of git). Engine SHA `3e498d64` / BuildID `204ad049`. No guest writes, gate patch, shim,
parser edits or captured data added to git. Labels supersede earlier wording where noted.

| Claim (wave) | Audit verdict | Evidence file |
| --- | --- | --- |
| W14 Rung1 AVD reference (CF0=0,D08=0,LATCH=0 → `LATCH_WR@0x2eb7520` RA `base+0x2359342` → LATCH=1) | **PROVEN (AVD only)** | `w12-gate-avd.log` |
| W14 Rung2 AVD boot chain #727/#734/#736 (no login) | **PROVEN (AVD)** | `w12-j2n-300.log` lines 742/749/751 |
| W14 Rung3 UNDEFINED | **CONFIRMED UNDEFINED** | 300 s oracle: 0 surface/placeId/StartGame/callback |
| W15 bisect (min crashing count=1; first flag `DFFlagConsumePlatformNameOverAlternateName`; non-monotonic) | **PROVEN — MODIFIED INPUT, diagnostic only, never a rung** | `w15-bisect-{1,100,1000,10000}.err`, `w15-flags-1.json` |
| W15 path-divergence | **RETRACTED** (already recorded) | ledger W15 section |
| W15 "every 0x30 write from host RIPs, never guest code" / "no guest writer RA found" | **DOWNGRADE to scope-limited, SUPERSEDED by W17** (true only for inner-frame watch at `0x...b218`) | `w15-hwbp5.err` vs `w17-act5-1.err` |
| W16 ledger entry 10 (enhanced dump exists) | **PROVEN (diagnostic only)** | `w16-det-1-1.err` |
| W17 store `0x2312032 = mov %rsi,-0xb8(%rbp)`, perf next-RIP `0x2312039` | **PROVEN** | authoritative engine disasm + `w17-act5-1.err:3708` |
| W17 "writes **value 0x30**" | **DOWNGRADE: RUN-DEPENDENT, not a constant** — PROVEN only that the store writes RSI. Across 3 runs: `0x30` (act5-1), stack ptr `0x...562928` (act5-2), host heap ptr `0x...256070` (act5-3) | `w17-act5-{1,2,3}.err` |
| W17 "caller RA from fp#1 = `0x2311628`" | **PROVEN as fault-frame-chain observation; CLARIFY** — the ACT WRITE's own `callerRA=` field is `base+0x536424` ("first guest value above RSP", `hw_breakpoint.c:125-130`), a different metric; do not conflate | `w17-act5-1.err:3771` + `src/jni/hw_breakpoint.c` |
| W17 "faulting inner-frame slot `rbp-0xb8`, rbp=`0x7fffffffb2d0`" | **PROVEN (ASLR-off env dependent)** | `w17-act5-1.err:3711`, `:3704` |
| W17 "latch tested at `0x231202b` with je `0x23121b5`" | **PROVEN (precision fix)** — cmp at `0x231202b`; je at `0x2312039` targeting `0x23121b5` | authoritative engine disasm |
| W18 static #585 "AVD latch@ENTER=0 PROVEN by static ordering" | **RETRACTED by its own author** after #588 130-activation data | `w18-static/report.md` correction |
| W18 static #585 RSI origin `0x23115c8` → `0x2311620`; `0x30` = native handle (shim-origin UNPROVEN) | **PROVEN (static)** | `w18-static/report.md` + 3 dis files |
| W18 avd #588 130 activations: PRE 74 latch=0 TAKEN (RSI real ptr), POST 56 latch=1 FALLTHROUGH; LATCH_WR caller RA | **PROVEN / UNPROVEN (caller RA not captured)** | `w18-avd/peract2.log`, `w18-avd/report.md` |

### Secrets scan (values redacted, none recorded)
- `w11-arg0.json` hits for token/cookie/session/etc. are flag **names**, not values; the two long
  config strings contain no JWT, >=32-hex, token/auth query param or numeric userid param.
  `w15-flags-1.json` CLEAN; 100/1000/10000 flag files are flag-name hits only.
- AVD/harness logs checked: no cookie/bearer/set-cookie/JWT/RBX-token header patterns. Device IDs
  present are emulator-standard (`sdk_gphone64_x86_64`), not personal IDs.
- Tracked git files contain no wave 14-18 captures; pre-existing tracked `appData/logs/*` and
  `IxpSettings.json` scanned clean; 5 long numerals in `analysis/abort/repro-interposed.err` are
  negative guest return codes, not IDs. All audit/capture artifacts remain OUT of git in
  `/tmp/opencode/w18-audit/`.

### First divergence + next smallest step
Both harness and AVD-PRE take the latch=0 `je 0x23121b5` bypass; the real divergence is the RSI
value at `0x23115c8` (harness `0x30` token vs AVD `0x7c9ec02fa098` real object pointer), which
becomes the mutex pointer (`value+0x120`) and faults. Secondary: AVD `LATCH_WR 0x2eb7520` fires
once (latch 0→1); harness writer never fires. Next smallest step: read-only RSI comparison at
`0x23115c8` on both sides + AVD caller RA of `0x2eb7520` (still UNPROVEN). Gate/scope/account
decisions belong to the user. Marker WITHHELD.

### Missing wave18 harness entry (task #587) — added in wave19 audit

Owner: harness (wave18). Reconstructed from artifacts by audit19 because that agent's report never
arrived in-session. All artifacts OUT of git in `/tmp/opencode/w18-harness/`.

- **Attempt:** read-only gdb per-activation logging at entry `0x2312009`, branch `0x2312039`,
  crash activation `0x23121ae`, plus a latch-byte (`0x7cdd149`) watch, on the harness binary.
  Scripts: `act_trace.gdb`, `act_trace2.gdb`, `attach.gdb`, `run_trace.sh`, `run_trace2.sh`,
  `run_stop.sh`, `min.gdb`.
- **Result: UNPROVEN (task goal not achieved).** The hardware-watchpoint run failed outright:
  `act_trace2.out` = `No hardware breakpoint support in the target.` The attach-based software
  watcher produced **no activation data**: `attach.out` contains only `[New LWP …]` attach lines,
  with zero `ACT_ENTER` / `ACT_BRANCH` / `CRASH_ACT` / `LATCH_WRITE` records; `act_trace.out` has
  only startup lines. No crashing activation was isolated, no write watch armed on its slot, and
  the "who last set `0x7cdd149` before first ENTER (RIP, caller RA)" question was **not answered**.
- **Only usable datum:** `stop.err` at the SIGSTOP before `0x2359277` shows `D08=0`, `latch=0`,
  `fflag_ctx` and `fflag_obj` as **real host pointers** (`0x55…`) — consistent with the
  "Not applied (rejected)" shim note above; it does not attribute the latch writer.
- **RA:** none captured for the latch writer or crashing activation (0x23121ae watch never logged).
- **Evidence files:** `/tmp/opencode/w18-harness/{act_trace,act_trace2,attach}.{gdb,out}`,
  `hwbp.err`, `stop.err`, `direct.err`, `direct2.err` (all out of git).
- **What NOT done:** no guest writes, gate patch, shim, parser edits, or captured data in git;
  no activation log, no latch-writer RIP/RA. Superseded for the latch-count question by avd
  #588 (130 activations) on the AVD side; the harness side of that comparison remains UNMEASURED.

## Wave19 audit (task #591) — harness claim re-verification

Owner: audit19 (branch `w19-audit`). Engine SHA `3e498d64`. Read-only otherwise; no guest writes,
shim, parser edits or gate changes; captures out of git (`/tmp/opencode/w19-audit/`).

- **Initial check (before harness19 reported):** `probe.gdb`/`probe_fast.gdb` define correct-looking
  breakpoints (`0x2359277` SETTINGS_ENTER, `0x23115c8` RSI_ORIGIN, watch on `0x72a3058`) but
  `probe.out`/`probe_fast.out` contain only `[New LWP …]` lines — zero probe records. Those two
  artifacts alone support no claim.
- **Final re-verification after harness19 #589 reported** (`/tmp/opencode/w19-harness/report.md`,
  scripts `memread.py`/`memread2.py`, log `stop.err`, source `src/jni/native_jni.c:1416-1429`):
  - **PROVEN (corroborated):** `GLOBAL[0x72a3058] = 0x5555574528f0` and
    `ctx = [G+0x18] = 0x5555558f8ac0` — both match `stop.err` boundary-pre log
    `fflag_ctx=0x5555574528f0 fflag_obj=0x5555558f8ac0` (independent log source).
  - **PROVEN (source-verified):** last writer of that global is the harness's own
    `native_jni.c:1416-1429` cached-jmethodID seeding over the **inclusive** range
    `0x72a2f48..0x72a3058`; final iteration writes `sr_find_method_object` named
    `cachedMethod@%lx` into `0x72a3058`, clobbering the guest FFlag context global. `[ctx+0] =
    0x654d646568636163` ("cachedMe") is consistent with that name prefix. No perf write-watch
    was captured — writer attribution is by source + memory/log, not a runtime writer RIP (agent
    states this under NOT DONE).
  - **UNPROVEN (no saved raw output):** the derived byte/field values `[ctx+0x20]=0x30`,
    `[0x72a31e0]=0` and LATCH/CF0/D08/GUARD bytes appear only in `report.md`; the read-only
    `/proc/PID/mem` scripts exist but their stdout was not saved to a file. `[ctx+0x20]=0x30` is
    *indirectly* consistent with the wave11/17 fault (`mutex=0x150=0x30+0x120`), not independently
    re-measured here. Minor discrepancy: report lists `D08=0x1e000000` while `stop.err` boundary-pre
    logs `D08=0` — likely different read times; not reconciled.
  - **Verdict:** accept the global-pointer and writer-clobber claims as PROVEN; treat the numeric
    field values as UNPROVEN until raw output is saved. No guest bytes were patched by this agent.

**Global at 0x23115c8 comparison:** harness `0x30` (small token; wave18 fault-frame evidence, and
wave19 `report.md` `[ctx+0x20]=0x30` — corroborated indirectly by the `mutex=0x150` fault, raw read
output UNPROVEN) vs AVD `0x7c9ec02fa098` (real pointer, avd #588, 130/130 activations).

## Wave20 audit (task #594) — approved defect removal ledger entries

Owner: audit20 (branch `w20-audit`). Engine SHA `3e498d64`. Audit artifacts out of git in
`/tmp/opencode/w20-audit/`. No guest writes, gate patch, parser edits or shim by this role.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 13 | **harness seeder clobbered guest FFlag ctx global; defect removal** | `src/jni/native_jni.c:1416-1443` (host-side only) | lead (approved) / harness20 (implemented) | The cached-jmethodID range `0x72a2f48..0x72a3058` was **inclusive**, so the final iteration wrote a synthetic `cachedMethod@72a3058` object over the guest FFlag context global consumed at `0x23232ab → 0x23115c8`; genuine guest code then read `[host+0x18]`/`[host+0x20]` and produced `RSI=0x30`. Approved host-side removal now skips `0x72a3058` by default (Run A). Verified by audit20: git HEAD shows the original inclusive loop; w19 `report.md` + `stop.err` corroborate `[0x72a3058]=0x5555574528f0`, `ctx=0x5555558f8ac0`, `[ctx+0]="cachedMe"`, `[ctx+0x20]=0x30`; static20 #596 confirms the guest has its own writer at `0x230cce4` (helper `0x230cc82`) but it is NOT reached when JNI_OnLoad is skipped. | Defect is removed in default path; entry stays until Run A/B re-measurement confirms `[0x72a3058]` is guest-owned and `[ctx+0x20]` is a real pointer, and lead closes it. |
| 14 | **Run B env debt — `SOBER_SEEDER_DISABLE=1`** | `src/jni/native_jni.c:1429-1431`, harness run env | harness20 / lead | Opt-in env knob (presence = disable) that turns off the **whole** cached-jmethodID seeder, used only as the Run B diagnostic. Default OFF; withhold-worthy because it changes harness input fidelity. | Remove when Run B is no longer needed as a diagnostic, or fold into a permanent default behaviour; must stay out of any clean marker run. |

**Run A / Run B verification status (audit20, read-only):** NOT YET ACCEPTED. First batch
(`/tmp/opencode/w20-harness/run{A,B}_*.err`, 10 runs each) is invalid: only `runA_1` reached the
app-shell boundary (it logs `[SEEDER_WRITE]` slots up to `0x72a3050` with **no** `0x72a3058` write —
the exclusion works), while runs A2-A4 and all B runs hit `rc=124` (timeout) during constructor
replay, and runs A5-A10 failed with `dlopen FAILED: libOpenMAXAL.so` (env/stub missing). Lead asked
harness20 to rerun with a longer per-run timeout; until that report arrives with saved evidence,
any Run A/B numeric claim stays **UNPROVEN**. Device (avd20 #595) is also **UNPROVEN** (phone not
debuggable / no frida-server; AVD boot killed by server restart), so no device-side read exists.

**Comparison `[0x72a3058]` / `[ctx+0]` at `0x23115c8` (Run A vs Run B vs device):** currently
UNPROVEN for all three sides; w19 baseline (before removal) was harness synthetic host object /
"cachedMe" vs AVD `0x7c9ec02fa098` real pointer. Marker WITHHELD.

## Wave22 audit (task #608) — claim verification + user decisions

Owner: audit (branch `w22-audit`). Engine SHA `3e498d64`. Audit work out of git in
`/tmp/opencode/w22-audit/`. No device work, no new captures, no runs by audit.

**Claim verdicts vs raw files (all PROVEN):**
- static-skip #610: FindClass-wrapper recursion path `0x6c8f340 → 0x2a13350 → 0x2a0de60 →
  0x2ea6440 → getspecific(0x72a1b08)`; stale `0xABCDEF0123456789`; TSD GOT rebind at
  `guest_app_shell.c:1018`, typed getters `native_jni.c:1187-1215`, `JNI_OnLoad` returns `0x10006`
  at `0x227b840` — all verified in raw `src_*.txt`/`recurse_note_*.txt` + source + objdump.
  "Recursion fixed" is UNPROVEN (diff not applied by that agent; Run C later FAILED).
- static-jni3 #609: last-third calls; sole writer of `0x72a3058` at `0x230cce4` (1 write / 50 refs
  in `refs_72a3058.txt`), value `&0x72a31c8` singleton, read `0x227b204` / virtual call `*0x8(%rax)`
  `0x227b213`, 32/35 slots via JNI lookups, 3 exceptions — verified in `last_third.dis`.
  Exact vtable callee RVA UNPROVEN (relocation-resolved; agent states so).
- harness #607: `runC.err` shows `ver=0x10006`, `[W21_LATCH_HIT] callerRA base+0x2359342`,
  `rcx=0xabcdef0123456789`, `si_addr=0x7fffff7feff8`, ld-linux+0x18ef4, fp#0 `0x2a13359` /
  fp#1 `0x6ca7f9a` / 10+ repeated `0x6ca8014` frames — all match. Run A: 1 latch hit per run,
  10/10 rc=124 with fault banner (verified in `runA_*.err`). Dumps: `[0x72a3058]` NULL at entry →
  guest singleton after (lazy-init PROVEN); `ctx+0` genuine guest code pointer (not host
  "cachedMe"); RSI at `0x23115c8` UNPROVEN (perf 0/10).

**User decisions recorded and verified in source:** seeder removed / replaced by genuine
`JNI_OnLoad` (`guest_app_shell.c:1288-1301`, `SOBER_LEGACY_JNI_SEED` fallback), stays **ledger debt
until Run C works** — Run C FAILS, so debt remains. Rung output two tiers, **labeling only, no gate
logic change** (`guest_app_shell.c:2127-2143`: `rung1` variable and marker decision untouched).

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 15 | **Run C genuine JNI_OnLoad: FindClass-wrapper recursion / stack exhaustion (`rcx=0xabcdef0123456789`), DEBT** | `guest_app_shell.c:1288-1301` | harness/lead | Genuine JNI_OnLoad returns `0x10006` and latch fires, but execution recurses in `0x6ca8014` with stale TSD sentinel → stack overflow in ld-linux; seeder removed but Run C does not work | Remove when Run C reaches the boundary without recursion (Bionic TSD rebind on the Run C thread verified) and lead closes it |

**Secrets scan:** clean — only 2 `<redacted>@rbxinfra.com` infra addresses in authoritative flag
JSON; no tokens/cookies/user/device IDs. **Git:** no wave/capture-named tracked files, no
untracked capture-like files in repo. Marker WITHHELD.

## Wave23 audit (task #614) — claim verification vs raw files

Owner: audit (branch `w23-audit`). Audit work out of git `/tmp/opencode/w23-audit/`. No device,
no new captures, no runs by audit. Engine SHA `3e498d64`.

- **static-onload #612 — PROVEN** (minor line drift): `ver=0x10006` is the RETURN of guest
  `JNI_OnLoad` (`guest_app_shell.c:1316-1318` calls `jni_onload_fn(sober_jni_java_vm(j),NULL)`
  at `base+0x227af5a`; `if (ver<=0) seed_rc=ERR` at :1321); `runC.err:3715` logs
  `genuine JNI_OnLoad 0x227af5a -> ver=0x10006` after the call ⇒ JNI_OnLoad DID return. Separate
  GetEnv `0x10006` at `native_jni.c:1617` / `runC.err:3732 rdx=0x10006`. Settings run before
  JNI_OnLoad (`guest_app_shell.c:1169` → log ~3705 vs :1318 → 3715). `runC.rc=139`.
- **static-tsd #613 — PROVEN incl. RETRACTION:** sentinel `0xABCDEF0123456789` is host
  `__stack_chk_guard` (`android_compat.c:45`) exported via GOT `0x72293f8` — **the canary, not a
  TSD result**; `runC.err:3732 rcx` is incidental canary. **RETRACTED:** "shim getspecific returns
  sentinel for unset key" — FALSE: `bionic_libc.c:550-554` returns a zero-init `_Thread_local`
  array ⇒ NULL; `gen_bionic_libc.py:260` emits no TSD trampoline. Rebind at
  `guest_app_shell.c:1018` was a no-op in Run C (`runC.err:3679` before==after). Key `0x72a1b08`
  created by guest `pthread_once` cb `0x2ea6640` (verified: `lea 0x72a1b08,%rdi; lea
  0x2ea6670,%rsi; call pthread_key_create@plt`); creating thread UNPROVEN. Fault thread tid
  `1376232` = Main. **Actual recursion trigger (`*(obj+0x28)!=0` at `0x6ca8014`) remains
  UNPROVEN.** Proposed host-only TSD-seed+diag diff NOT APPLIED (lead go-ahead required).
- **harness #611 — VERIFIED (PROVEN):** `runC_{1,2,3}.err` all show
  `pre-OnLoad TSD key=3 get=0x555555bef090/0x555555bef010` (non-NULL — agent2's "get=(nil)"
  hypothesis not observed), `genuine JNI_OnLoad 0x227af5a -> ver=0x10006`, `[W23_ASYNC_SAFE]`
  dumps, `rip=ld-linux+0x18ef4`, `si_addr=0x7fffff7feff8` beside `rsp=0x7fffff7ff008`,
  `rcx=0xabcdef0123456789` (canary per agent2), repeated `guest+0x6ca8014` (30 stack-scan
  hits), `runC_*.rc=139` (3/3), seeder OFF (`SOBER_SEEDER_DISABLE=1`). Run A: 1
  `PASS-BOUNDARY-REPLAY (DEBT=1)` line in each of runA_{1,2,3}.err; never CLEAN. All match raw
  logs. Async-safe dump rewrite in `guest_dlopen_host.c` — code change by harness, out of audit scope.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 16 | **Run C x3: approved TSD seed non-NULL (get=0x555555bef090), genuine JNI_OnLoad ver=0x10006, recursion at guest+0x6ca8014 -> stack overflow rc=139, DEBT** | `guest_app_shell.c:1288-1301` + host TSD diag | harness/lead | TSD key 3 seeded on OnLoad thread (non-NULL) yet recursion persists identically 3/3; canary rcx is incidental; seeder OFF, no gate change | Remove when Run C reaches the boundary without `0x6ca8014` recursion and lead closes it |

Wave22 ENTRY 15 corroboration update: its `rcx=0xabcdef0123456789` descriptor is now known to be
the canary, not TSD state; the entry's DEBT stands until Run C works, but the stated TSD-sentinel
causal framing is RETRACTED by static-tsd #613. No gate edits; seeder stays OFF in Run C; rung1
stays `PASS-BOUNDARY-REPLAY(DEBT=n)`, never CLEAN.

**Secrets:** clean (2 redacted `@rbxinfra.com` in flag JSON, 1 local `@sober-linux.local` author
email in a worktree doc; no tokens/cookies/IDs). **Git:** no wave/capture-named tracked files, no
untracked capture-like files. Marker WITHHELD.

## Wave24 audit (task #618) — claim verification vs raw files

Owner: audit (branch `w24-audit`). Audit files out of git `/tmp/opencode/w24-audit/`. No device
handling, no runs by audit. Engine SHA `3e498d64`.

- **static-recursion #615 — PROVEN** (minor source line drift): `0x6ca8014` is the epilogue of
  the ExceptionCheck wrapper `0x6ca7f72`; predicate `0x2a13350` calls `0x2a0de60` and tests
  `*(obj+0x28)!=0`; FALSE path `call *0x720(%rax)` at `0x6ca800e` (fn[224] ExceptionCheck, pure),
  TRUE path lambda `0x6ca805b` → `0x2a13560` → dispatcher `0x2a12e80` (nesting `obj+0x70`,
  schedule `0x2a1e3a0`, clear `0x2a12f34`, `0x3f` panic). **NOT FindClass** (`*0x30` is in the other
  wrapper `0x6c8f340`). Verified in `dis_*.txt`. No diff applied; guest-state fix forbidden.
- **phone #616 — PROVEN for (a)+(d); logcat correctly NOT started.** Phone base.apk SHA
  `2badef75…` ≠ authoritative `114a6f8e…` (I independently re-ran sha256sum; on-device build
  differs, not a modded APK by the agent); ARM64. `roblox://placeId=…` resolves to
  `.ActivityProtocolLaunch` (passive `resolve-activity`). **No logcat capture exists** and none
  will be verified until the user says "logged in" — that signal has not occurred in team
  messages. No credentials/cookies handled.
- **harness #617 — PROVEN:** `runD_{1,2,3}.err` all show `JNI_OnLoad FIRST ver=0x10006`, rung1
  `PASS-BOUNDARY-REPLAY (DEBT=1)`, `rc=139`, and **no `0x6ca8014` recursion**; new fault
  `si_addr=0x548`, `rdi=0`, `rax=0`, fp#0 `guest+0x231555c` / fp#1 `guest+0x24650d6` (NULL
  `GetStringUTFChars` at JNI slot 0xa5). `ringD.err` ring dump shows non-NULL FindClass/GetStringUTFChars
  returns. `bigstack.err` fails earlier in host code. Seeder OFF throughout; no gate change.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 17 | **W24 host order fix JNI_OnLoad-first removes 0x6ca8014 recursion; new fault NULL GetStringUTFChars at guest+0x231555c (RA 0x24650d6), DEBT** | `guest_app_shell.c` (`SOBER_JNI_ONLOAD_FIRST`) | harness/lead | Ordering fix eliminates the ExceptionCheck-wrapper recursion (3/3) but exposes a NULL jstring at the GetStringUTFChars bridge; still no genuine callback/frame, debt remains | Remove when Run D/E reaches a genuine callback + guest frame with clean state and lead closes it |

**Secrets:** clean (APK bundled Roblox "Cookie:" log strings are app code, not live credentials;
`@2x.png` false-positive emails). **Git:** no captures, no phone logs, no wave/capture/phone-named
tracked files, no untracked capture-like files. Rung1 stays `PASS-BOUNDARY-REPLAY(DEBT=n)`, never
CLEAN; seeder OFF. Marker WITHHELD.

## Wave25 audit (task #622) — claim verification vs raw files

Owner: audit (branch `w25-audit`). Audit files out of git `/tmp/opencode/w25-audit/`. No device
handling, no runs by audit. Engine SHA `3e498d64`.

- **static #620 — PROVEN incl. RETRACTION:** at `0x231555c` rax is a JNIEnv **field load**
  (`0x231554b mov (%rsi),%rax`), not a global; `0x2315556 call *0x548(%rax)` (fn[165]
  GetStringUTFChars, RA `0x231555c`); caller `nativeSetBaseDataDirectories 0x24650a9` →
  `0x24650d1 call 0x2315532` (RA `0x24650d6`); env is the JNI method's first arg from
  `guest_app_shell.c:1498`. Verified in `dis_*.txt`. **RETRACTED** "0x548 = fn[169]": with 4
  reserved words and fn[] at +0x20, `0x548` = fn[165] (`native_jni.c:1068/1103`), `0x550` = fn[166],
  fn[169] = GetObjectArrayElement at `0x568` (`:1073`). `jni_json_bridge.c:24` "idx 169" counts raw
  slots without the 4 reserved words. No functional change applied.
- **phone #621 — version gap PROVEN on authoritative side; phone values report-only.** I
  independently verified authoritative APK sha256 `114a6f8e…` and aapt badging `2.740.931` /
  code `3170`. Phone side (`2.748.1527` / `22294`, sha `2badef75…`) is quoted from the phone's
  report: the pulled APK at `/tmp/opencode/w25-phone/base.apk` was cleaned up (out of git), so it
  cannot be re-audited locally. Gap is a newer client build, not a repack; ARM64, no RAs, no logcat
  (no "logged in" signal), no credentials handled.
- **harness #619 — PROVEN:** `runD_{1,2,3}.err` all show fault `rdi=0`, `rax=0`, bridge
  `env=0x…ae8` with `rdi_is_env=0`, stack `env_hits=4/vm_hits=0`, `W25_VM_RING` header with **no
  entries** (GetEnv/Attach/Detach/GetJavaVM never called on this path), fault tid == latch-hit tid
  (1432402 in runD_1), fp#0 `guest+0x231555c` / fp#1 `guest+0x24650d6`, `si_addr=0x548`, rc=139,
  rung1 `PASS-BOUNDARY-REPLAY(DEBT=1)`, no recursion. Seeder OFF, JNI_OnLoad-first ON.

No new numbered entry (no ledger entry requested by agents this wave). Entry 17's "NULL
GetStringUTFChars" fault is refined: the NULL is the **JNIEnv argument** (`rdi=0`/`rax=0`) at the
GetStringUTFChars call, while the bridge env is valid and on the stack; cause still UNPROVEN.

**Secrets:** clean (ledger's own "Cookie:" note, 2 redacted `@rbxinfra.com`, 1 local author email).
**Git:** no captures, no phone logs, no wave/capture/phone-named files. Marker WITHHELD.

## Wave26 audit (task #624) — claim verification vs raw files

Owner: audit (branch `w26-audit`). Audit files out of git `/tmp/opencode/w26-audit/`. No device,
no phone agent, no runs by audit. Engine SHA `3e498d64`.

- **static #623 — PROVEN:** captured `w12-j2n-300.log:627` J2N #612 gives 2 Java String args
  (Android internal/external dirs); harness `guest_app_shell.c:1498-1504,1530` calls
  `0x24650a9` as `(env, app_class, s_base1, s_base2)` with both bases the same Linux path —
  4 native vs 2 Java args is consistent with the `(JNIEnv*, jclass, …)` prefix; **values mismatch**.
  `nativeSetRobloxVersion` matches (`2.740.931`); `nativeSetRobloxChannel` mismatches (`"LIVE"`
  vs `""`). Disassembly: `0x24650b9 mov %rdi,%r14` (env), `0x24650c3 mov (%r15),%rax` is the
  **canary** from `0x72293f8`, helper `0x231554b mov (%rsi),%rax` is the JNIEnv function-table
  field, `0x2315556 call *0x548(%rax)` = fn[165] GetStringUTFChars. Verified in raw
  `capture_line.txt`/`capture_others.txt`/`dis_*.txt`. No usernames in captured paths.
- **harness #625 — PROVEN incl. RETRACTION:** `runD_{1,2,3}.err` perf breakpoints at
  `0x24650a9` and `0x2315556` show `rdi=0x555555ad9ae8` = bridge env (`is_bridge_env=1`) with
  non-NULL `fn_table=[rdi]=0x5555558fc018` at **both** entry and GetStringUTFChars call (3/3).
  **RETRACTED** W25's "NULL JNIEnv / rdi=0 at GetStringUTFChars": the W25 fault-time `rdi=0,rax=0`
  was a **post-call** snapshot (registers overwritten after the call); the call-time env was valid.
  Fault `rip=base+0x6ca229c` (arithmetic verified: `0x7fffef8de29c - 0x7fffe8c3c000`), fp#0
  `guest+0x231555c` / fp#1 `guest+0x24650d6`, rc=139 3/3, no `0x6ca8014` recursion. Seeder OFF,
  JNI_OnLoad-first ON.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 18 | **W26 env valid at 0x24650a9 and GetStringUTFChars call (bridge env, non-NULL table); W25 NULL-env claim RETRACTED; fault guest+0x6ca229c, DEBT** | harness perf breakpoints / `guest_app_shell.c` | harness/lead | Call-time env and fn table valid (3/3); W25's NULL-env reading was a post-call register snapshot; fault now in guest post-return assert path `0x6ca229c` | Remove when Run D/E reaches genuine callback + guest frame with clean state and lead closes it |

**Secrets:** clean (2 redacted `@rbxinfra.com`, 1 local author email; captured paths contain no
usernames). **Git:** no captures, no phone logs, no wave/capture/phone-named files. Marker WITHHELD.

## Wave27 audit (task #627) — claim verification vs raw files

Owner: audit (worktree `/tmp/opencode/w27-audit`, branch `w27-audit`). No device, no runs by audit.
Engine SHA `3e498d64`. **NEW RULE obeyed: no conclusion from fault-time registers** — all verdicts
use entry/call-site breakpoints, ring data or static disassembly.

- **static #626 — PROVEN incl. RETRACTION:** helper `0x2315532` post-return at `0x231555c` is a
  plain NULL test on the GetStringUTFChars result (`test rax,rax` / `je 0x2315583`, independently
  re-disassembled), not a length/equality/file check. `0x6ca229c` is inside scheduler-gated JNI
  wrapper `0x6ca21c6` (predicate `0x2a13350` → `*(obj+0x28)!=0`, `obj` from `0x2a0de60`), FALSE path
  `call *0x548`, TRUE lambda `0x6ca22ee` calls `*0x548` at `0x6ca2340`. **RETRACTED** "assert with
  message string": no string RIP-ref in the function (only code `0x6ca22ee` / canary `0x72293f8`).
  Native sequence: capture Channel("") → Version → GameGlobalInit → InitClientSettings → BaseDirs →
  PreferencesFile → InitFlags; harness InitClientSettings → JNI_OnLoad → BaseDirs → Version →
  Channel → GameGlobalInit — 3 inversions, 2 missing, 1 extra, 2 value mismatches; no usernames.
- **harness #628 — PROVEN:** call-site perf breakpoints (not fault regs) at `0x231555c` and
  `0x6ca22a2` show non-NULL valid returned strings (strlen 78 JSON / 16 "GoogleAndroidApp") 3/3;
  applied host-only distinct base dirs (`/tmp/sober-w27-XXXX/data` + `/sdcard`, `distinct=1` logged
  3/3) and channel `""` (source `guest_app_shell.c:1528`). Run D x3 rc=139, rung1
  `PASS-BOUNDARY-REPLAY (DEBT=1)`, no recursion; other order/missing-call mismatches NOT applied.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 19 | **W27 host-only base-dir fix: two DISTINCT real dirs under per-run temp root (data + sdcard), DEBT** | `guest_app_shell.c:1511-1523` (w27-harness worktree) | harness/lead | Replaces the single reused Linux path with two distinct mkdtemp dirs, matching the capture's two Android data dirs | Remove when the full native sequence matches the authoritative capture and lead closes it |
| 20 | **W27 host-only channel fix: pass empty string matching authoritative capture (was LIVE), DEBT** | `guest_app_shell.c:1528` (w27-harness worktree) | harness/lead | Capture shows `nativeSetRobloxChannel("")`; harness previously passed `"LIVE"` | Remove with entry 19 / when ordering + missing natives are reconciled and lead closes it |

**Secrets:** clean (ledger's own "Cookie:" note, 2 redacted `@rbxinfra.com`, 1 local author email).
**Git:** no captures, no phone logs, no wave/capture-named files; no untracked capture-like files.
**Branches:** w27-audit / w27-static / w27-harness worktrees each on their own branch; main has
only build artifacts. Marker WITHHELD.

## Wave28 audit (task #630) — claim verification vs raw files

Owner: audit (worktree `/tmp/opencode/w28-audit`, branch `w28-audit`). No device, no runs by audit.
Engine SHA `3e498d64`. **RULE CHANGE honored/recorded:** the W27 "no fault-time registers" rule is
RETRACTED; new rule = name the faulting instruction first, then interpret registers; a stack return
address is NEVER the faulting instruction.

- **static #629 — PROVEN:** faulting instruction `0x6ca229c = ff 90 48 05 00 00 = call *0x548(%rax)`
  (bytes verified at both `0x6ca229c` and `0x2315556`); return point `0x6ca22a2`, stack RA
  `0x231555c` is NOT the fault. rax provenance: `0x6ca228b call 0x6c89e70` (TLS desc `0x7282068`
  → `0x2ea6440` getspecific key `0x72a1b08`), `0x6ca2290 mov (%rax),%rax` = field +0 of a
  TLS-resident object. Predicate `0x6ca21f4 call 0x2a13350` tests `*(obj+0x28)!=0`. Guest `+0x28`
  writers `0x2a0ed45` / `0x2a1362f` verified as instructions; exhaustiveness UNPROVEN. All in raw
  `bytes.txt` / `dis_*.txt`.
- **harness #631 — PROVEN:** 6 runs (3 with `SOBER_W28_PAIRS=1` breakpoints, 3 without), rc=139
  all. Faulting instructions differ: WITH bp → guest `0x6ca229c` (`ff 90 48 05 00 00`,
  `call *0x548(%rax)`, si_addr `0x548`); WITHOUT bp → host `0x555555586e9b`
  (`0f b6 90 49 d1 cd 07`, `movzx edx, byte ptr [rax+0x7cdd149]`, si_addr `0x7cdd149`). Byte dumps
  confirm both. Pair counts `pre2315=3/post2315=2` (3rd unmatched) 3/3. arg0 = 78-byte modified
  input (`w15-flags-1.json`); authoritative `w11-arg0.json` remains 1,533,221 B / SHA
  `8215519f…` — not used in Run D. Full captured-order replay NOT applied (UNPROVEN/NOT DONE).

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 21 | **W28 host-only captured-order replay applied (OnLoad, Channel empty, Version, GameGlobalInit before InitClientSettings/BaseDirs); fault unchanged; DEBT** | w28-harness worktree (`SOBER_W28_REORDER=1`, `guest_app_shell.c:1122`) | harness/lead | Opt-in reorder verified in `reorder_{1,2,3}.err` 3/3: OnLoad (3702) → Channel("") (3707) → Version (3712) → GameGlobalInit (3713) → InitClientSettings (3727) → BaseDirs; fault UNCHANGED at `guest+0x6ca229c`, rc=139. Supersedes the earlier "order replay not applied" wording. PreferencesFile("prefs")/InitFlags still a GAP (no safe call contract) — UNPROVEN | Remove when full captured order incl. the 2 missing natives reconciles and lead closes it |

**Mechanical offset check** `offset = 0x20 + 8*idx` (fn[0]=GetVersion, standard JNI idx = idx+4):
fn[2] FindClass 0x30, fn[17] NewGlobalRef 0xa8, fn[109] GetStaticMethodID 0x388, fn[165]
GetStringUTFChars 0x548, fn[166] Release 0x550, fn[169] GetObjectArrayElement 0x568, fn[224]
ExceptionCheck 0x720 — all 7 calc == observed (OK). Confirms W27's `0x548→fn[165]` retraction.

**Secrets-scan command (run from w28-audit worktree):**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/w28-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ **output: (empty)** — no credential values. A broader keyword scan returned only code/API names
(`session`, `token`, `userId`) in analysis docs.

**DEBT (one line):** count of active host-side workarounds, diagnostic toggles or value substitutions
that must be removed before a clean marker claim; `DEBT=n` means a rung passes with n such items.

**Git:** no capture/phone/logcat-named tracked files, no untracked capture-like files; w28-audit /
w28-static / w28-harness each on its own branch. Marker WITHHELD.
