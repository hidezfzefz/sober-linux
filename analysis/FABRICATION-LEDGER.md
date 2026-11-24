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

## Wave29 audit (task #634) — leaked-credit downgrade + seeded-state inventory

Owner: audit (worktree `/tmp/opencode/w29-audit`, branch `w29-audit`). No device, no runs by audit.
Engine SHA `3e498d64`. Faulting instruction named first (`0x6ca229c = call *0x548(%rax)`); stack
RA `0x231555c` is not the fault site.

**LEAKED CREDIT — PROVEN (lead mid-wave correction, verified from raw logs):** across
`/tmp/opencode/w2[2-8]-harness/*.err` there are **88 `[avd-boundary]` lines, ALL naming
`/tmp/opencode/w15-flags-1.json` (MODIFIED INPUT, sha256 `4d50a0ec…`)** — no run since wave 22 used
`w11-arg0.json`. The preceding `[app-shell] AVD boundary provenance` line names
`file=/tmp/opencode/w11-arg0.json sha256=8215519f…` — that provenance line is misleading: the actual
boundary input is the modified 78-byte file (example: `w22-harness/runA_1.err:3709-3711`). Therefore
**all 16 `rung1 PASS-BOUNDARY-REPLAY (DEBT=1)` lines are DOWNGRADED to MODIFIED-INPUT (no credit)**,
each cited: w23 runA_{1,2,3}:3714, runC_{1,2,3}:3706; w24 ringD:3720, runD_{1,2,3}:3720;
w25 runD_{1,2,3}:3720; w27 runD_{1,2,3}:3720. The seeder-clobber finding (entry 13) is KEPT — it is
an independent host-side defect, not a checkpoint credit.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 22 | **W29 leaked-credit finding: all 16 rung1 PASS lines w22-w28 used MODIFIED INPUT (w15-flags-1.json, sha 4d50a0ec), not authoritative w11-arg0.json — PASS downgraded, no credit** | harness logs w22-w28 (`/tmp/opencode/w2[2-8]-harness/*.err`) | lead/audit | 88 `[avd-boundary]` lines all name the modified 78B file; provenance line naming w11-arg0.json is misleading; rung-1 PASS from a modified input cannot count toward any checkpoint | Remove when a rung-1 PASS is reproduced with the authoritative 1,533,221B input (w11-arg0.json) and no modified input, verified from the actual `[avd-boundary]` line |

**Harness-seeded state still active (each with justifying ledger entry):**
1. AVD boundary replay of 1,533,221B JSON — ledger #1/#2/#3 (provenance class + canonical jstring);
   in practice superseded by the 78B modified input — see #9/#22.
2. Modified input `SOBER_AVD_BOUNDARY_PATH` = `/tmp/opencode/w15-flags-1.json` (78B, sha 4d50a0ec) —
   ledger #9 (diagnostic only, never counts toward a rung), reinforced by #22.
3. Remaining seeder slots `0x72a2f48..0x72a3050` (34 of 35) still seeded by the host cached-ID
   seeder; only `0x72a3058` excluded — ledger #13 (defect removal partial). `SOBER_SEEDER_DISABLE`
   (Run B) — ledger #14 (default OFF).
4. TSD seed: approved host `seed_stub_tsd` key 3, get non-NULL — ledger #16 (default off opt-in
   `SOBER_STUB_TSD_SEED` / approved host TSD diag). Guest key `0x72a1b08` created by `0x2ea6640`.
5. Host value fixes: two distinct mkdtemp base dirs — ledger #19; empty channel — #20; captured-order
   replay `SOBER_W28_REORDER` — #21.
6. Default-off diagnostic knobs (no separate numbered entry; all in the `seeded_state` withhold chain
   in `guest_app_shell.c` ~:1977-2088): SOBER_SEED_JNI_VM, SOBER_SEED_GLOBAL_STRING,
   SOBER_SEED_JNI_SINGLETON, SOBER_STUB_TSD_SEED, SOBER_NATIVE_OBJECTS, SOBER_JSON_FLAG_BRIDGE,
   SOBER_SCHEDULER_GUEST, etc. — withheld, not counted as clean.

**Secrets-scan command + output (shown in report):** credential-value regex over `/tmp/opencode/w29-*/`
→ 3 self-matches (the ledger's own recorded scan command in 3 worktree copies); no credential values.
**Git:** no capture/phone/logcat-named tracked files; w29 worktrees each on own branch. Marker WITHHELD.

### Additional W29 verifications (audit)

- **static #633 — PROVEN:** guest setspecific writers of key `0x72a1b08` at `0x2ea656e` (accessor
  `0x2ea6440`) and `0x2ea6695` (dtor `0x2ea6670`); key via once cb `0x2ea6640` /
  `pthread_key_create` `0x2ea664f`; `0x2ea61b6` is a different key (excluded). TLS value +0 = JNI
  function-table pointer (holder object, not sched obj). Missing natives: SetPreferencesFile
  `0x246515d` (capture #613 "prefs"), InitFlags `0x2465210` (capture #614) — symbols confirmed;
  harness calls only the opt-in `0x2465210`, never `0x246515d`. Threads/boot UNPROVEN.
- **harness #632 — largely PROVEN, one claim RETRACTED:** default arg0 = authoritative
  `w11-arg0.json` (`avd_boundary.h:16`, SHA const `:20`), full-JSON runs log `arg0_len=1533221`;
  TSD seed ON eq=1 for first two call-pairs then guest replaces TLS (writes `0x7265502f666f7270`
  = bytes `70 72 6f 66 2f 50 65 72` LE = **"prof/Per"**, NOT "profileP" — prior decode RETRACTED),
  seed OFF guest-owned TLS `[tls]=0x1`; seed-OFF fault `guest+0x2a13354 = call
  0x2a0de60` (bytes `e8 07 ab ff ff`), all 8/8 JNI pairs returned; host fault `0x555555586e9b` =
  `sober_w21_boundary_ok()` (`native_jni.c:184`) caused by `w21_init` early-return leaving
  `w21_base=0`, host-only NULL guard `if (!w21_base) return 0;` applied at `:187`.
  **RETRACTED** harness report line 54 claim "Both rows print `guest init complete` + `first frame
  rendered`": grep of all `/tmp/opencode/w29-harness/*.err` finds NEITHER string; there is NO
  `W21_LATCH_HIT`; 13× `[checkpoint] rung1 FAIL (DEBT=1)` despite verified SHA — because rung1
  requires a real latch hit (caller RA `0x2359342`) which is absent. No credit from that line.
- Harness ledger entries 23-25 added below (exact text, verified in source by audit20-equivalent
  check: `avd_boundary.h:16/20`, `guest_app_shell.c:121/2257`, `native_jni.c:187`). The 2×3 table
  and TSD trace are recorded here as audit-verified facts, not as a counted checkpoint.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 23 | **W29 INPUT FIRST: default InitClientSettings arg0 = /tmp/opencode/w11-arg0.json (avd_boundary.h:16), SHA 8215519f verified before call; rung prints MODIFIED-INPUT (no credit) on mismatch; DEBT** | `avd_boundary.h:16/20`, `avd_boundary.c:127`, `guest_app_shell.c:2257` | harness/lead | Restores authoritative default input and makes modified input explicit/non-crediting | Remove when a genuine Java/DEX startup supplies the call and no modified input is used |
| 24 | **W29 host-only TSD seed OFF switch SOBER_W29_TSD_SEED_OFF added (seed_stub_tsd early return); DEBT** | `guest_app_shell.c:121-122` | harness/lead | Opt-in diagnostic to disable the host TSD seed (default on path unchanged) | Remove when seed-OFF behaviour becomes the default or the diagnostic is no longer needed |
| 25 | **W29 host-only fix: sober_w21_boundary_ok NULL-guards w21_base before deref at native_jni.c:184 (removed no-bp host fault at [0x7cdd149]); DEBT** | `native_jni.c:184-187` | harness/lead | Prevents the no-breakpoint host crash in the boundary credential check; guest faults remain | Remove when the guest-side boundary path is genuine and the guard is folded into final code |

## Wave30 audit (task #637) — recheck of W29 PROVENs + quoted-line verification

Owner: audit (worktree `/tmp/opencode/w30-audit`, branch `w30-audit`). No device, no runs by audit.
Engine SHA `3e498d64`. NEW RULE honored: every success claim below QUOTES its raw log line.

**Recheck of all W29 harness report PROVENs (`/tmp/opencode/w29-harness/report.md`, updated 03:32):**
- PROVEN (quoted): `avd_boundary.h:16` default w11-arg0.json / `:18` UNVERIFIED / `:19` PATH_ENV /
  `:20` SHA 8215519f; `avd_boundary.c:102-104` consumed. Modified-input exports quoted from
  `w20-harness/memrun.sh:7`, `w22-harness/runbatch.sh:9`, `w29-harness/runall.sh:15`.
  `full_on_1.err:3703 [avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519f…`
  and `:3704 [app-shell] AVD boundary pre: arg0_len=1533221 …`.
  `tsdtrace2.err:3706/3727` TSD eq=1 then `[seed]=0x7265502f666f7270` eq=0; `seedoff_trace.err:3701`
  seed OFF / `:3706 [tls]=0x1`. `seedoff_pairs_1.err:3762-3763` fault guest_rva 0x2a13354, si_addr
  0x7fffff7feff8, 3/3; post2315 #1-#8 each 3 runs. `native_jni.c:182-188` NULL guard at :187.
  `full_on_*.err` fault guest_rva 0x6ca229c 3/3. RETRACTED section: init/frame strings only in *.out,
  13× `[checkpoint] rung1 FAIL (DEBT=1)`, no W21_LATCH_HIT.
- **UNSUPPORTED claim A:** W29 report cites `w28-harness/rund3.sh` as a modified-input export —
  that file DOES NOT EXIST (w28-harness has only `run6.sh`). Other script citations valid.
- **UNSUPPORTED claim B:** W29 report 2×3 table seed-OFF row attributes the masked fault to
  `full_off_*.err`; THOSE LOGS CONTAIN NO FAULT LINES. Masked-fault evidence is `core.1529212` only
  (audited prior wave: tid 1529212, rip 0x5555555690b0, r13=si_addr=0x7fffff7fefc0).

**W30 agents (all quotes verified verbatim against raw files):**
- static #635: `'[W29_TLS 0x6ca228b] tid=1521493 key=3 tls=0x55555568b230 [tls]=0x1 … seed=(nil) eq=0'`,
  `'[app-shell] pre-OnLoad TSD key=3 get=0x55555568b230'`, 8/8 pair quotes, J2N #70/#534/#541/#612
  — all found verbatim. Real-boot +0x28 zeroing UNPROVEN (agent states so).
- harness #636: `'[W28_FAULT_INSN] rip=0x00007fffef8e3f95 guest_rva=0x06ca7f95'` 3/3,
  `'[W30_SI_RSP_DELTA] 8'` 3/3, `'[W30_SIGALTSTACK] installed sp=0x5555555e3a80 size=65536'`,
  `'[app-shell] W30 TSD seed REMOVED (default OFF; harness-fabricated state)'`, verified SHA line,
  `'[checkpoint] rung1 FAIL (DEBT=1)'` 3/3, 0 W21_LATCH_HIT, 24× fp guest+0x6ca8014,
  `'[W30_RET64] rsp=0x00007fffff7fefd0'`. Static `0x6ca800e call [rax+0x720]` verified by objdump.
  FAULTING INSTR: `guest+0x6ca7f95 = e8 b6 b3 d6 fb = call 0x2a13350`, si_addr=rsp-8 stack overflow.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 26 | **W30 TSD seed removed: harness-fabricated state caused the 0x6ca229c fault; DEBT** | `guest_app_shell.c` (W30 harness worktree, default OFF) | harness/lead | Raw `'W30 TSD seed REMOVED (default OFF; harness-fabricated state)'`; with seed gone the guest recurses at 0x6ca8014 and faults at 0x6ca7f95 call 0x2a13350 (stack overflow) — no fabricated TSD state | Remove when the guest's own TLS state produces a genuine callback and no harness seed exists |

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/w30-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 5 self-matches (the ledger's own recorded scan command in 3 worktree copies + 2 verification-notes copies); no credential values.
**Git:** no capture/phone/logcat/core files tracked (1 false-positive "core." prose match in
`G6_progress_score.md`); w30 worktrees each on own branch. Marker WITHHELD.

## Wave31 audit (task #639) — decode fix + rung1 meaning + quoted-line verification

Owner: audit (worktree `/tmp/opencode/w31-audit`, branch `w31-audit`). No device, no runs by audit.
Engine SHA `3e498d64`. QUOTE rule honored: every success claim below quotes a raw line.

**DECODE FIX (lead-directed):** the value `0x7265502f666f7270` decodes little-endian to bytes
`70 72 6f 66 2f 50 65 72` = **"prof/Per"**, NOT "profileP". Prior W29/W30 reports (e.g.
`/tmp/opencode/w30-static/report.md:34`) and this ledger's W29 section (line 494) used the wrong
"profileP" decode — RETRACTED and corrected above. Raw anchor quoted:
`[W29_TLS 0x2315556] tid=1519045 key=3 rax=0x5555558fe0e8 [rax]=(nil) rsi=0x5555574b3c30 seed=0x5555559f60f0 [seed]=0x7265502f666f7270 eq=0`.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 27 | **rung1 FAIL = latch hit NOT observed WITH WATCHER ARMED; absence with watcher UNARMED is a watcher-configuration effect, NOT proof the latch never fires. rung1 PASS requires a real W21 latch hit (caller RA 0x2359342). STATUS: task (d) MET — hit observed in W31 runD_1 + latchrun (watcher armed)** | raw `[checkpoint] rung1 FAIL (DEBT=1)` (W29 13×, W30 runD 3/3; 0 W21_LATCH_HIT); MET: w31 `runD_1.err` + `latchrun.err` raw `[W21_LATCH_HIT] rip=base+0x2eb7520 callerRA=base+0x2359342 latch=0x0` + `[checkpoint] rung1 PASS-BOUNDARY-REPLAY (DEBT=1)`; runs 2-3 raw `[W31_ARM] ... latch=-1` + rung1 FAIL | lead/audit | Qualified per lead: distinguish "hit not observed with watcher armed" from "watcher not armed" — the latter is configuration, not latch absence. Consolidated from W29 (line 54 retracted), W30 and W31 audit | Retire the FAIL-reading where watcher is armed and a hit is quoted; keep it for unarmed-watcher runs. PASS is DEBT=1 replay on authoritative input — not a clean marker |
| 28 | **W31 harness replays genuine guest `ctor[0] 0x2e62390` (FFlag registry) — real guest code, no byte patches/callbacks/JNIEnv; the real boot runs it at library load. Replay precedes JNI_OnLoad and nativeInitClientSettings** | `src/jni/fflag_default.c:161-175` (`ctor()` via RVA `SOBER_FFLAG_DEFAULT_RVA_CTOR0`, no writes); raw w30 `runD_1.err:3687 [fflag-default] replayed guest ctor 0x2e62390 ... -> OK`, `:3702 JNI_OnLoad FIRST ver=0x10006`, `:3707 nativeInitClientSettings`; w31 `runD_1.err:3688` same replay line | lead/static | Confirms ctor[0] is active in Run D and is genuine guest code (Decision 008 skips it at bootstrap; real boot runs it at library load), not a harness shim | Remove when a real-boot capture demonstrates the same ctor[0] chain without harness replay, or replay is proven to alter guest semantics |

**Harness #638 final verification (all quotes found verbatim in raw files at 04:08):**
- FAULTING INSTR: objdump `6ca7f95: e8 b6 b3 d6 fb call 2a13350` — PROVEN. Raw (latchrun.err)
  `[W30_FAULT] rip=0x00007fffef8e3f95 si_addr=0x00007fffff7fefb8 rsp=0x00007fffff7fefc0 ...`,
  si_addr=rsp-8 stack overflow. With W31 bps armed the fault moves to host `0x7ffff7e30017`
  (raw runD_1.err:78803) — still si_addr=rsp-8.
- (a) PROVEN: raw `[W31_ENTRY #1] tid=1585707 ...` / `#37537 tid=1585707 ...` (37,537 entries,
  37,536 obj), `37536 [obj+0x28]=0000000000000000` and `37536 ascii28=0` — all zeros, never ASCII.
- (b) PROVEN: raw `[W31_WRITE_ARM] obj=0x7fffe86a08c0 watch=0x7fffe86a08e8 tid=1585707`;
  zero `[W31_WRITE]` lines. Writer identity UNPROVEN (agent states so).
- (d) PROVEN: raw `runD_1.err [W21_LATCH_HIT] rip=base+0x2eb7520 callerRA=base+0x2359342
  latch=0x0 tid=1585707` + `[checkpoint] rung1 PASS-BOUNDARY-REPLAY (DEBT=1)`. Input provenance
  raw `runD_1.err:3704 file=w11-arg0.json sha256=8215519f…`, `:3705 [avd-boundary] verified … sha256=8215519f…`,
  `:3706 arg0_len=1533221` — AUTHORITATIVE input, not the w15-flags-1.json modified input of entry 22.
  Source `native_jni.c:182-192 sober_w21_boundary_ok()` requires hits>0, before=0, after=1, RA=0x2359342;
  `guest_app_shell.c:2243-2266` prints PASS only when `!modified_input`. Runs 2-3 raw
  `[W31_ARM] ... latch=-1` + rung1 FAIL — watcher-config effect, NOT a latch absence.
  NOTE: PASS is `DEBT=1` (replay), NOT clean — marker WITHHELD. Earlier 16 PASS downgrades (entry 22)
  were for MODIFIED input; this PASS is on authoritative input and is not covered by that downgrade.
- STALE-QUOTE flag: an earlier report revision (04:07:11) quoted tids 1579693/1582405 and
  obj=0x7fffe852e280, which do NOT occur in the final runD_1.err; the agent corrected these at
  04:08:35. Final report quotes verified; no unsupported claim remains.

## Wave32 audit (task #642) — quoted-line verification

Owner: audit (worktree `/tmp/opencode/w32-audit`, branch `w32-audit`). No device, no runs by audit.
Engine SHA `3e498d64`. QUOTE rule applied. `/tmp/opencode/uploads` OUT OF SCOPE — not read.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 29 | **W32 watcher-free rung1: before=0 after=1, SHA 8215519f, no harness write -> PASS-BOUNDARY-REPLAY (DEBT=1) label only; DEBT** | raw `nowatch_*.err [W32_LATCH_RO] before=0 after=1` + `[checkpoint] rung1 PASS-BOUNDARY-REPLAY (DEBT=1)`; source `guest_app_shell.c:1194/1228/1239-1242`, `2258-2270` | harness/lead | Read-only before/after latch credential with SHA verified; label-only, no gate change; DEBT=1 replay | Remove when the label is earned without boundary replay or the credential is proven to reflect a non-guest write |

**static #641 — all quoted lines verified verbatim:** wrapper `6ca800e call *0x720(%rax)`,
`6ca8014 mov (%r14),%rcx`, guard `6ca7f95 call 2a13350`/`6ca7f9a test al,al`/`6ca7f9c je 6ca7ffa`;
obj chain `2a0de64 lea #722e560`/`2a0de84 call 2ea6440`/`2a0deba add $0x410,%rax`;
writers `2a0ed45 mov %r15,0x28(%rbx)`, `2a0e83b call 2a0e8c0`, `2a0ed1d call 2a13620`,
`2a1362f movq $0x0,0x28(%rdi)` — found in `dis_wrapper_6ca7f72.txt`, `dis_sched_getter.txt`,
`dis_writer1_region.txt`, `raw_writer_callers.txt`, `dis_2a1362f.txt`. Minor: report's evidence
list omits `dis_2a1362f.txt` (source of the `2a1362f` quote). `0x6ca8014` is epilogue/RA, not a
call instruction — consistent with W24. Exact vtable, size, writer entry path UNPROVEN (agent states so).

**harness #643 — mostly verified; two QUOTE-rule defects flagged:**
- PROVEN (verbatim): `[W32_ARM] base=0x7fffe8c3c000 entry=0 obj=0 writer0=0 writer1=0` 3/3;
  `[W32_WRITER 0x2a1362f] hit=1 tid=1606363 guest_RA=guest+0x2a0ede0 entries_so_far=27
  precedes_first_entry=0` and `hit=68 ... guest_RA=guest+0x2a0e598 entries_so_far=156 ...`;
  `precedes_first_entry=1` count 0/0/0; 68 hits of `0x2a1362f` and 0 of `0x2a0ed45` per run;
  single tid; 37,547 `[W32_OBJ]`, first `obj=0x7fffe85c2500 [obj+0x28]=0000000000000000`, 1 distinct
  obj/run; `[W32_LATCH_RO] before=0 after=1` + `[checkpoint] rung1 PASS-BOUNDARY-REPLAY (DEBT=1)`;
  `verified w11-arg0.json sha256=8215519f…`, `arg0_len=1533221 latch=0`, `nativeInitClientSettings
  ... latch=1`, `[flags-latch] route=none`; watcher-token count 0.
- **QUOTE-RULE DEFECT 1:** report's FAULTING-INSTRUCTION quote has `rbp=0x00007fffff7ff090`;
  the raw `nowatch_1.err` line reads `rbp=0x00007fffff7ff0a0`. NOT verbatim — quote must be corrected.
- **QUOTE-RULE DEFECT 2:** report lists caller RAs as only `0x2a0ede0, 0x2a0e598, 0x2a0e1d8`;
  raw logs show five: also `0x2a0e840, 0x2a0f492`. Incomplete list.
- Derived (not a raw line): `w0=0 w1=68 precede=0` is agent shorthand; counts independently
  verified (0/68, zero precede=1).
- Watcher-free credential scrutinized: `g_w32_latch_before` sampled at `guest_app_shell.c:1194`
  (before call), `after` at `:1228` (after call); no host byte write to `0x7cdd149` (grep of src:
  reads/comparisons only); engine `movb $1` writers per `flags_latch_default.h:12-19` are
  `0x2eb7520` (the replayed nativeInitClientSettings), crashpad and unreachable `0x56d*` family;
  corroborated by W31 watcher's `[W21_LATCH_HIT] callerRA=0x2359342`. Sound as DEBT=1 replay credit,
  NOT clean — marker WITHHELD. Observed writer `0x2a1362f` not proven to target this obj's +0x28
  (agent states UNPROVEN).

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/w32-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 6 self-matches (ledger's own recorded scan command, 2 per worktree × 3); no credential values.
Glob `/tmp/opencode/w32-*/` excludes the out-of-scope uploads directory.
**Git:** no capture/phone/logcat/core tracked; w32 worktrees each on own branch. Marker WITHHELD.

### W32 root-cause addendum (lead-directed, verified by audit)

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 30 | **W32 ROOT CAUSE — JNIEnv fn[224] self-pointer feedback loop: slot `[rax+0x720]` changes from host `sr_ExceptionCheck` (host+0x2e690) to `guest+0x6ca7f72` (the prologue of the very wrapper containing `call *0x720(%rax)` at `0x6ca800e`) ⇒ infinite self-recursion ⇒ stack overflow at `0x6ca7f95 call 0x2a13350`, si_addr=rsp-8. W24 scheduler TRUE-path attribution RETRACTED: `[obj+0x28]=0` on all 37,547 entries ⇒ FALSE path always, not TRUE path. Writer of fn[224] UNPROVEN; identity/route of host+0x2e690 beyond sr_ExceptionCheck UNPROVEN; DEBT** | raw `call720.err`/`call720c.err`: `[W32_CALL720_ARM] base=0x7fffe8c3c000 host=0x555555554000 fd_rc=0`; `hit=1 … [rax+0x720]=0x555555582690 decode=host+0x2e690`; `hit=20/100 … [rax+0x720]=0x7fffef8e3f72 decode=guest+0x6ca7f72`; disasm `6ca7f72: push rbp/mov rbp,rsp` and `6ca800e: call *0x720(%rax)`; `nm guest-init-host: 2e690 t sr_ExceptionCheck`; `native_jni.c:1560 SETFN(*t,224,sr_ExceptionCheck)`; W32 `[obj+0x28]=0` 37,547/37,547 | lead/static/harness, audited | Replaces the scheduler-TRUE-path causal story with a directly observed callback self-pointer loop; explains the recurring 0x6ca8014 frame and the rsp-8 guard-page fault | Remove when the fn[224] writer is identified with a raw line and the loop is reproduced without harness replay, or a genuine guest route restores the host callback |

## Overnight n1-1 audit (task #646) — quoted-line verification

Owner: audit (worktree `/tmp/opencode/n1-audit`, branch `n1-audit`). Cores 6-7 for all heavy cmds.
No device/runs; no questions. Engine SHA `3e498d64`. QUOTE rule enforced.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 31 | **Fault site is CONFIGURATION-DEPENDENT — RETRACTED: "faulting instruction is always `0x6ca7f95`". WITH breakpoints: `guest+0x2a1335e mov rax,[rax]`, si_addr=0 (NULL), 3/3. WITHOUT breakpoints: non-deterministic call site (`0x6ca7f95` / `0x2ea645b` / `0x2a0de6b`, 1/3 each), all si_addr=rsp-8 stack-guard push, 3/3** | raw with_1 `[W28_FAULT_INSN] … guest_rva=0x02a1335e`; without_1/2/3 `guest_rva=0x06ca7f95/0x02ea645b/0x02a0de6b`; `[W30_SI_RSP_DELTA] 8` 3/3 without, huge deltas with | harness/lead, audited | A breakpoint set changes the observed fault site; no single "always" instruction may be claimed | Remove when a single configuration is shown to produce one fault site across all runs |

**Input SHA on every run — PROVEN.** `grep -c 'verified /tmp/opencode/w11-arg0.json bytes sha256=8215519f…'` = 1 in each of
`with_{1,2,3}.err`, `without_{1,2,3}.err` (6/6). No MODIFIED-INPUT line in any run.

**static-A #644 — all quotes verified verbatim:** `2a0de64/6b lea #722e560; call 2ea6440`,
`2a0de7d/84 lea #722e540; call 2ea6440`, `2a0de89 lea #2a0dfe0`, `2a0de90 lea #6d37bc0`,
`2a0de9a call 2ea6130`, `2a0deba add $0x410,%rax`; getter `2ea645b call pthread_getspecific@plt`,
`2ea656e call pthread_setspecific@plt`; predicate `2a13361 cmpq $0,0x28(%rax)`, `2a13366 setne %al`;
writers `2a0ed45 mov %r15,0x28(%rbx)`, `2a1362f movq $0,0x28(%rdi)`. Returns `&slot = TLS[0x722e540]+0x410`,
not obj itself; exact obj type/size and writer entry UNPROVEN (agent states so).

**harness #645 — all quotes verified verbatim:** `[N1_ARM] base=… pred=0 al=0 call720=0`;
`[N1_PRED #1] … [[rax]+0x28]=0000000000000000` at #1/2/3/10000/20000/30000 (3 runs);
`[N1_AL #1] … al=0x00 al_dec=0` at all sampled hits (no nonzero al found); `[N1_CALL720] hit=1 …
[rax+0x720]=0x55a82eb7e690 decode=host+0x2e690 bridge_env=… rax_is_env=0` → at `hit=100 …
decode=guest+0x6ca7f72`, rbx==bridge_env constant. Fault quotes verified exactly (with_1
`rsp=0x00007fffd71ba630`, without_1 `si_addr=0x00007fff4ff2eff8 rsp=0x00007fff4ff2f000`).

**static-B #647 — QUOTE-RULE DEFECT (3 lines):** report writes `6c89e50/6c89e70/6c89e90: lea … #7282048/
7282068/7282088`, but raw `dis_6c89e50_70.txt` places those `lea`s at `6c89e54 / 6c89e74 / 6c89e94`
(addresses off by 4; substance — keys and getter 2ea6440 — correct). Under "any success claim without
a quote → RETRACTED", the three misattributed `lea` lines are RETRACTED as quoted; the underlying key
claim is separately VERIFIED by the raw lines at the corrected addresses. All other static-B quotes
verified: `2ea644c mov 0x10(%rdi),%r13`, `2ea6455 mov … #72a1b08,%edi`, `2ea645b call pthread_getspecific@plt`,
`2a13569 call 2a0de60`, `2a1356e mov (%rax),%rax`, `2a13571 mov 0x70(%rax),%rdi`,
`2a13575 and $0xffffffffffffffc0,%rdi`, `2a13579 mov %rbx,%rsi`, `2a13582 jmp 2a12e80`,
`2a13374/79 call 2a0de60`, `2a13381 movzbl 0x20(%rax),%eax`, `2a13385 and $1,%al`, callers
`55f5748/55f58a1/560ad0b`. Real-vs-wrapper env identity, exact real-boot fn224 pointer, key-object
+0x10 writers and +0x20 bit0 writer UNPROVEN (agent states so).

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n1-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 16 self-matches (ledger recorded commands ×3 + verification-notes ×1 in each of 4 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked. Tracked `.log` files are 18
pre-existing `appData/logs/*_last.log` (780B–1.5K FLog snippets, added in afed56d) + `build/Testing/Temporary/LastTest.log`;
no credentials/flag values inside; NOT introduced by n1-1. No new captures this wave. n1 worktrees at 702fae9, own branches. Marker WITHHELD.

## Overnight n1-2 audit (task #651) — quoted-line verification

Owner: audit (`/tmp/opencode/n1-audit`, branch `n1-audit`). Cores 6-7. No device/runs; no questions.

Input SHA: `grep -c 'verified … sha256=8215519f…'` = 1 in each of `n12_with_{1,2,3}.err`,
`n12_without_{1,2,3}.err` (6/6); `[app-shell] W24 JNI_OnLoad FIRST 0x227af5a -> ver=0x10006` 6/6.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 32 | **Fault site varies WITHIN a configuration too — RETRACTED: "with-bp fault is always `mov rax,[rax]` / si_addr=0". WITH bps: 1/3 guest `0x2a1335e mov rax,[rax]` si_addr=0; 2/3 fault INSIDE perf handler `n1_pred_sig` at host+0x372ef (`mov [rsp+0x30],rbp`, si_addr=rsp+0x30; handler lacks SA_ONSTACK). WITHOUT bps: 2/3 `0x6ca7f95 call` (rsp-8), 1/3 `mov [rsp-0x10],rsi` (rsp-0x10) — stack guard sole cause 3/3** | raw n12_with_1 `[W28_FAULT_INSN] … guest_rva=0x02a1335e`; with_2/3 `rip=0x0000563ef03452ef`; without_1/2 `guest_rva=0x06ca7f95`; without_3 `[W30_FAULT] rip=0x00007f05b53beef4 si_addr=0x00007ffe5a9afff8 rsp=0x00007ffe5a9b0008` | harness/lead, audited | Breakpoint handler perturbs the fault site; no single instruction may be claimed even within one configuration | Remove when both configurations are shown to each yield one invariant fault site |

**static-A #650 — all quotes verified verbatim:** only proven writer of TLS state slot
`2a0df25 call 2ea6440`, `2a0df41 mov %rax,0x408(%r14)`, `2a0df48 mov %rax,0x410(%r14)`,
value from `2a0df3c call 2a16390` (esi=0x80); NULL path `2a0df52 je 2a0dfc1` (assert 0x727e910);
caller `2a0de78 call 2a0ded0` in getter 0x2a0de60; predicate `2a1335e mov (%rax),%rax`,
`2a13361 cmpq $0,0x28(%rax)`. Destructor 2ea6670 has NO slot write (`2ea6677 mov (%rdi),%rax`,
`2ea6682 mov %rax,(%rbx)`, `2ea6695 jmp pthread_setspecific@plt`; alt free `2ea66b8/2ea66c6`).
Writer entry native/ctor/tid and indirect slot-128 free UNPROVEN (agent states so).

**harness #649 — all quotes verified verbatim:** SHA+JNI_OnLoad 6/6; fault sites per entry 32;
`[N1_RING] last 20 pre-call entries at 0x2a1335e …` and exact line
`[000000000000928f] rax=0x0000562b91e759b0 [rax]=0x00007f4b0da9a280 guard=0x0000000000000000 tid=1641790`;
20 entries/run, one distinct valid triple, guard=0 — NULL deref NOT explained by ring (agent states UNPROVEN).

**static-B #648 — quotes verified (one citation defect):** writer is genuine guest code
`6c8e4cc lea … # 6ca7f72`, `6c8e4d3 mov %rax,0x720(%r14)`, neighbours `6c8e4c5 mov %rax,0x718(%r14)`,
`6c8e4da lea … # 6ca80a6`, `6c8e4e1 mov %rax,0x728(%r14)` — all verbatim in dis_fn224_writer.txt.
Callers `227bde8: e8 33 27 a1 04 call 6c8e520` and `6c8e5f2: e8 9a 00 00 00 call 6c8e691`
were quoted in the report but absent from saved evidence files; audit independently verified both
verbatim by objdump → PROVEN, not retracted. Host source: only `SETFN(*t,224,sr_ExceptionCheck)`
(actual `native_jni.c:1560`; report's `:1651` is a stale line number — citation defect, substance
verified), `javavm_semantics.c:435 fj_ExceptionCheck` in its own table. Real-boot ART pointer UNPROVEN.

**Secrets-scan command + output (final):**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n1-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 17 self-matches (ledger's own recorded scan commands + verification-notes), no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n1-2. Marker WITHHELD.

## Overnight n1-3 audit (task #652) — quoted-line verification + host-only fix audit

Owner: audit (`/tmp/opencode/n1-audit`, branch `n1-audit`). Cores 6-7. No device/runs; no questions.

Input SHA: `verified … sha256=8215519f…` = 1 in each `n13_{1,2,3}.err` (3/3);
`W24 JNI_OnLoad FIRST … ver=0x10006` 3/3; `W30 TSD seed REMOVED` present.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 33 | **SA_ONSTACK fix is HOST-ONLY: `w21_arm()` sets `sa.sa_flags = SA_SIGINFO \| SA_ONSTACK` on the process altstack (installed by install_crash_handlers, 64 KB). Effect: n1-2 perf-handler fault (host+0x372ef, 2/3) GONE — all n1-3 faults guest-mapped. No guest bytes/gate/fake returns** | raw n13 all 3 faults carry `guest_rva` (`0x02ea6446` 2/3, `0x06ca7f95` 1/3); source diff harness 28c067f→6a92bd7 `src/jni/native_jni.c` adds `sa.sa_flags = SA_SIGINFO \| SA_ONSTACK`; no mprotect/ptrace_poke/guest patch in diff | harness/lead, audited | Host signal delivery fix removes handler self-fault; guest behaviour unchanged | Remove when the fix is folded into final host code or a guest-side cause is proven |
| 34 | **Rule-6 replay is HOST-ONLY and did NOT terminate: calls genuine guest natives `nativeSetPreferencesFile 0x246515d("prefs")` and `nativeInitializeNativeFlags 0x2465210(String[4])` with verbatim capture tokens (w12-j2n-300.log:629); no guest byte writes, fake returns or path remaps (per-run mkdtemp temp-root env only). rc=139 3/3 — RETRACTED any claim rule 6 terminates** | raw n13 3/3 `[app-shell] N1_RULE6 1 nativeSetPreferencesFile 0x246515d("prefs") -> rc=-6066930334832433271`, `[app-shell] N1_RULE6 2 nativeInitializeNativeFlags 0x2465210(String[4]) -> rc=0`; source diff `guest_app_shell.c` calls `(base+0x246515du)` via host bridge, comment "No guest bytes/gate writes, no fake returns, no path remapping" | harness/lead, audited | Pre-approved host replay of captured natives; measured effect recorded, no termination credit | Remove when a clean Java/DEX boot supplies these natives without harness replay or termination is proven |

**Harness #654 — PROVEN:** fault sites `guest+0x2ea6446: 41 54 push r12` (rsp-8, 2/3),
`guest+0x6ca7f95 call 0x2a13350` (rsp-8, 1/3); `[N1_AL #1] … al=0x00 al_dec=0`;
ring `[00000000000092a7] rax=0x00005557be4119b0 [rax]=0x00007f91a8c0c140 guard=0x0000000000000000`;
fn224 `[N1_CALL720] hit=1 … decode=host+0x2e750` → `hit=100 … decode=guest+0x6ca7f72`;
`[checkpoint] rung1 PASS-BOUNDARY-REPLAY (DEBT=1)` 3/3 — label only, marker WITHHELD.
**RETRACTED:** rule-6 termination. UNPROVEN: fn224 guest-pointer writer identity in harness,
why `prefs` returns the bridge sentinel rc, 4th flag token (capture truncated).

**static-B #653 — all quoted disasm lines verified verbatim:** `2a12e80` dispatcher ENTER
(`2a12ed4 inc %r13` / `2a12edb or` / `2a12ede mov %r15,0x70(%r12)`), LEAVE (`2a12f6a dec %r15` /
`2a12f71 or` / `2a12f74 mov %r14,0x70(%rbx)`), RSL asserts via `727e910` (`2a12ed2 call *%rax`),
closure test `2a12eef cmpq $0x0,0x28(%rbx)` / `2a12ef4 je 2a12f21`; TRUE terminal `2a12f1a call
2a1e3a0` (guest, not JNI), FALSE `2a12f27`/`2a12f31 call *(%r14)` twice + `2a12f34 movq $0,(%r14)`
clear = re-entry stop; `2a0ed45 mov %r15,0x28(%rbx)`, `2a0ed0f mov (%rax),%r15`, `2a0e83b call
2a0e8c0`. Closure type/concrete TRUE-path closure/2a1e3a0 re-entry UNPROVEN (agent states so).

**static-A #655 — claims verified:** thunk `2a14330 movq $0,(%rdi)` / `2a14337 jmp 2a0e7c0`;
callers `23c1136`, `251e4ce`, `29cb5c5`, `3187fc7` in evidence file; the other five cited callers
(`251fb4d`, `29f91c9`, `36ddccc`, `447547b`, `49677fe`) and capture line `J2N #612` were absent
from the saved evidence files but audit independently verified all six verbatim (objdump /
w12-j2n-300.log) → PROVEN, not retracted. `2a0ed0f mov (%rax),%r15` / `2a0ed12 and %r14,%r15`
verified. Writer tid and the real-boot `+0x28`-setting native UNPROVEN (agent states so).

**Secrets-scan command + output (final):**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n1-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 19 self-matches (ledger's own recorded scan commands + verification-notes in 4 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n1-3. Marker WITHHELD.

## Overnight n1-4 audit (task #659) — quoted-line verification + rule-7 boundary

Owner: audit (`/tmp/opencode/n1-audit`, branch `n1-audit`). Cores 6-7. No device/runs; no questions.

Input SHA: `verified … sha256=8215519f…` = 1 in each `n14_{1,2,3}.err` and `n14s_{1,2,3}.err`
(6/6); `W24 JNI_OnLoad FIRST … ver=0x10006` 6/6; rc=139 in all 6.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 35 | **RULE-7 BOUNDARY — recursion-removal replays are NOT rung-2 attempts, and are host-only. Replaying captured pre-BaseDirs natives `nativeOnPreCreated 0x23239c5` / `nativeOnResumed 0x2328066` via genuine host bridge changes no guest bytes/gate; guard stays 0, recursion persists (rc=139 6/6), no `[checkpoint] rung2` line emitted in any n14 run, no rung1/rung2 source change in the diff. DEBT** | raw n14 3/3 `[N14_REPLAY 1/3] nativeOnPreCreated 0x23239c5("ActivitySplash") -> rc=-6066930334832433271`, `[N14_GUARD after nativeOnPreCreated] slot=… obj=… guard=0000000000000000 tid=…`, same for 2/3 nativeOnResumed; zero `N14_REPLAY 3/3` / rung2 lines; source diff harness 6a92bd7→d6ac7ed touches only `src/linux/guest_app_shell.c`, no rung1/rung2/guest writes | harness/lead, audited | Prevents recursion-removal diagnostics from being miscredited as rung-2 progress; keeps the ladder boundary explicit | Remove when a genuine rung-2 sequence (app_start→lua_dm) is quoted on the authoritative input without replay diagnostics |

**Harness #657 — PROVEN:** fault sites full replay 3/3 inside 3rd native before its log line
(`rip=0x00007f6508f078ff si_addr=0x0000000000000008`, bytes `48 8b 47 08 74 05 48 85` =
`mov rax,[rdi+8]; je; test rax,rax` — NULL+8, not stack guard); diagnostic 3rd skipped 3/3
stack-guard (`0x06ca7f95` 1/3, `...bef4 mov [rsp-0x10],rsi` 2/3). UNPROVEN: #552 opaque object
reconstruction (generic Object and real `shared_param_obj` both fault at `[rdi+8]` si_addr=0x8),
host module at `...08ff`; no fake handle fabricated.

**static-B #658 — all quoted disasm verified:** TRUE path `2a1e3a0` closure call `2a1e4e9 call *%r12`
(`r12 = 0x40(%rbx)`, `r15 = 0x48(%rbx)`), prior indirect calls `2a1e408`/`2a1e4d9`,
`2a1e476 jmp *%rdx` → context-copy helper 0x2a1b4e0 (NOT closure call), NULL test `2a1e4ec test
rax,rax` / `2a1e4ef je 2a1e61f` / `2a1e62d ret` (verified by direct objdump; saved dump truncated
before that address). `0x6ca7f72` has 0 refs in `2a1e3a0` (whole binary only `6c8e4cc lea` + func
start) → no static TRUE→wrapper path; runtime re-entry UNPROVEN. `2a132c0` FunctionMarshaller writes
ONLY `2a1333c mov %r15,0x70(%r14)` (nesting +0x70), zero `0x28(` refs → cannot set obj+0x28 (only
`2a0ed45` can). UNPROVEN: concrete +0x40 closure value, real-boot TRUE-path correctness.

**static-A #656 — all quoted lines verified:** prologues `23239c5/23239d1`, `2328066/2328072`,
`2383c18/2383c23/2383c26`; only `nativePostClientSettingsLoadedInitialization3` reaches installer
(`23c1136 call 2a14330` → `2a14337 jmp 2a0e7c0` → `2a0e83b call 2a0e8c0` → `2a0ed45`), the two
lifecycle natives do not; closure mask `2a0ebce movabs $0xffffffffffff,%r14`,
`2a0ed0f mov (%rax),%r15`, `2a0ed12 and %r14,%r15`, `2a0ed18 mov $3,%edx`, `2a0ed1d call 2a13620`,
`2a0ed45 mov %r15,0x28(%rbx)`, complement `2a0e8e4 movabs $0xffff000000000000`. obj+0x28 =
canonicalised closure pointer (`tagged & 0x0000ffffffffffff`), not jmethodID/length/flag. Exact
closure type UNPROVEN.

**Secrets-scan command + output (final):**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n1-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 19 self-matches (ledger's own recorded scan commands + verification-notes); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n1-4. Marker WITHHELD.

## Overnight n1-5 audit (task #663, final investigate wave) — BLOCKED + premise correction

Owner: audit (`/tmp/opencode/n1-audit`, branch `n1-audit`). Cores 6-7. No device/runs; no questions.

Input SHA: `verified … sha256=8215519f…` = 1 in each of the 8 `n15scan*.err` runs (8/8);
`W24 JNI_OnLoad FIRST … ver=0x10006` and `W30 TSD seed REMOVED` present.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 36 | **BLOCKED (task rule): #552 argument unreconstructable and no genuine closure pointer found. Capture records #552 only as `[object Object]` (w12-j2n-300.log:567) — no class/fields/handle/type; generic Object and real shared_param_obj both fault at `[rdi+8]` si_addr=0x8. Live scan of obj±0x2000 found only a structural candidate at obj-224 whose fn is 0x755ec0 BELOW guest base in anonymous RW heap — not genuine. NO installer call, NO fabricated handle. DEBT** | raw `J2N #552 … ([object Object])` (w12:567); n14 `[N14_REPLAY]` both natives rc=-6066930334832433271, guard=0; n15 `[N15_CLOSURE] obj-224 raw=0x17f5bdf370380 … fn=0x7f5bdf326140`, `[N15_MAP obj] … rw-p 00000000 00:00 0`; math rip 0x7f5be6723f95 − 0x6ca7f95 → base 0x7f5bdfa7c000, fn−base = −0x755ec0 | static-A/harness, audited | Both reconstruction avenues exhausted read-only; any further attempt would fabricate a handle (forbidden) | Remove when a new genuine capture supplies the #552 argument's type/fields/native handle or a genuine closure pointer is quoted in executable guest text |

**Premise CORRECTION (static-B #662, audited):** `0x2a0ed0f` and the `+0x28` store `0x2a0ed45`
are NOT inside `0x2a0e8c0` — that function ends at `2a0eb92: ret`; `2a0eb0f: call 2a0eba0`
transfers to a new function `0x2a0eba0` (`2a0eba0: push %rbp`) which contains the store.
Earlier ledger lines (n1-2 static-A #650, n1-3 static-B #653 / static-A #655) loosely said
"`0x2a0ed45` in fn `0x2a0e8c0`" — those attributions are RETRACTED; the call chain
`0x2a0e7c0 → 2a0e83b call 2a0e8c0 → 2a0eb0f call 2a0eba0 → 2a0ed45` stands. All static-B #662
quotes verified verbatim: entry saves `2a0ebb1/5/9/bd/c1/ce`, caller setup `2a0eaa0..2a0eb0f`,
closure source `2a0ed04/08/0b/0f/12/15/18/1d/45`, self-bookkeeping `2a0ed22/2e/32/35/39/3d`.
Contract: rdi=tagged list-head, rsi=out-ptr, rdx=refcount ptr, rcx=&arg2, r8=&arg3 (pointer-to-word,
dereferenced at 2a0ed0f), mask 0x0000ffffffffffff. UNPROVEN arg provenance, +0x68 object type,
whether 0x2a1e630 is the intended closure; the +0x40 closure is installed only on TRUE path which
requires +0x28 already nonzero (circular) — no substitution/fabrication performed.

**static-A #661 — all quotes verified:** `J2N #552 … ([object Object])` (w12:567), `#555 …
[object Object]`; w11-first10 has no PostClientSettings (count 0), w11-arg0.json has no #552
(count 0); harness-built `shared_param_obj=0x555556be4eb0 (blob=0x1000)` (w31 runD_1.err:4021)
is not captured device state. BLOCKED conclusion sound.

**harness #660 — all quoted lines verified, one overstatement:** SHA 8/8; fault sites 0x6ca7f95
4/8, 0x2a13354 3/8, 0x2ea6446 1/8, all rc=139. `[N15_SCAN]`, `[N15_CLOSURE]`, `[N15_MAP obj]`
quotes verbatim; fn−base = exactly −0x755ec0 (recomputed by audit) → anonymous RW heap, not
executable guest text. Minor defect: report says the ±0x2000 window was used in "8 runs" but
raw shows only 6/8 (`0x0..0x100` in 2 early scans); BLOCKED conclusion unaffected.

**Secrets-scan command + output (final):**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n1-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 20 self-matches (ledger's own recorded scan commands + verification-notes); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n1-5. Marker WITHHELD.

## Overnight n2 step 4 audit (task #666) — adjudication + retraction

Owner: audit (`/tmp/opencode/n2-audit`, branch `n2-audit`). Cores 6-7. No device/runs; no questions.

Input SHA: `verified … sha256=8215519f…` = 1 in each `n2_{1,2,3}.err` (3/3);
`W24 JNI_OnLoad FIRST … ver=0x10006` and `W30 TSD seed REMOVED` present; rc=139 3/3.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 37 | **RETRACTED: "guest patches harness bridge table IN PLACE" (n2 step-1 kill-switch premise). VERDICT (lead-accepted): POINTER REPLACEMENT — guest builder writes self-pointer into its OWN private copy and installs that copy as the env table; original bridge table unchanged** | raw n2_1.err builder bp `[N2_TABLE builder 0x6c8e4d3] tid=1814063 r14=0x55c5b900c078 bridge_table=0x55c5b91c63e0 r14_is_bridge_table=0 [r14+0x720]=(nil) self=0x7f8671c6ff72`; call720 bp `rax=0x55c5b91c63e0 bridge_table=0x55c5b900c078 rax_is_bridge_table=0 [rax+0x720]=0x55c585a89fd0 [bridge_table+0x720]=0x7f8671c6ff72 bridge_patched=1`; static-B disasm `6c8d84d call 6c89e90`/`6c8d85b mov %rax,%r14`/`6c8e4d3 mov %rax,0x720(%r14)`/`6c8e512 mov %r14,(%rbx)` | audit adjudication (lead accepted) | Original table's +0x720 still holds host bridge fn 0x55c585a89fd0 (not overwritten); the `bridge_table` variable changed from 0x55c5b91c63e0 to guest copy 0x55c5b900c078 — detector followed the replaced pointer. n1 root cause (guest self-pointer in installed table) STANDS; only the in-place framing is retracted | Remove when a raw WRITE watch proves an actual guest store into the original host bridge table's +0x720 |
| 38 | **Host-only pristine-table fix: `struct SoberJni` keeps pristine `env_table` plus disposable `guest_env_table`; `sr_GetEnv` returns guest env with the copy, `sober_jni_env()` keeps the pristine table. No guest writes/gate/fake returns/path remaps** | `src/jni/native_jni.c:1037-1042` (struct comment), `:1671-1674` (sr_GetEnv), `:1811-1813` (`memcpy(&j->guest_env_table,&j->env_table,…)`, `guest_env_obj.functions=&j->guest_env_table`); host-only, no diff needed (already in base f8a0069) | harness/lead, audited | Prevents guest table mutation from reaching the bridge table; fix premise comment ("patches IN PLACE") is stale per entry 37 but the copy itself is harmless host-only isolation | Remove when the guest uses the pristine env table directly without patching any table, or the isolation is folded into final host code |

**Post-fix verification (n2x_1/2/3):** SHA 3/3, rc=139 3/3. `bridge_patched=1` count 0/0/0,
`bridge_patched=0` count 30/30/30. Raw `n2x_1.err` call720 (audit-corrected quote):
`[N2_TABLE call720 0x6ca800e] tid=1821202 rax=0x55b804596b38 bridge_table=0x55b8045963e0
rax_is_bridge_table=0 guest_copy=0x55b804596b38 rax_is_guest_copy=1 [rax+0x720]=0x55b7d8363fd0
[bridge_table+0x720]=0x55b7d8363fd0 expect=0x7f1aca3abf72 bridge_patched=0` — real bridge table
`+0x720` now holds host bridge fn 0x55b7d8363fd0 (same as disposable copy), self-pointer gone.
Builder bp matches quoted fragments (`r14=0x55b8043dc078 ... r14_is_guest_copy=0 ... [r14+0x720]=(nil)`)
— a third guest-allocated table, as the report honestly notes. Fault unchanged (`rip=...08ff`,
si_addr=0x8). **QUOTE-RULE DEFECT flagged and FIXED:** report initially quoted `tid=1814063`
(pre-fix tid) for the post-fix call720 line; actual raw is `tid=1821202`. Harness corrected
report to `tid=1821202` (verified in pushed report line 35); no claim remains unsupported.

**Step 2 (static-A #668) — all quoted claims verified:** authoritative DEX classes2.dex from
`/tmp/opencode/auth-base.apk` sha 114a6f8e (rivals w24-phone/base.apk 2badef75db6f and
sober-rea-fresh/work/base.apk fff6fcb4b29a rejected), extracted OUTSIDE git to `/tmp/opencode/n2-dex/`.
Quoted `METHOD idx=15180 class=Lcom/roblox/engine/jni/NativeGLInterface; name=nativePostClientSettingsLoadedInitialization3
proto='(Ljava/util/List;)V'`, `CLASS_DEF idx=1953 super=Ljava/lang/Object; interfaces_off=0`,
static_fields=0 instance_fields=0; caller `Luh/e$f;->b(Ljava/lang/Integer;)V` @0x335152; factory
`Lyj/l2;->a(Landroid/content/Context;)Ljava/util/List;`. JNIEnv calls verified verbatim in raw
disasm/strings: `2383cb6 call *0x30 (%rax)` FindClass `java/util/List`,
`2383cfc call *0x108` GetMethodID `get (I)Ljava/lang/Object;`, `2383d2a` GetMethodID `size ()I`,
`23846b6 call *0x190` CallIntMethodV, `2315d92 call *0x118` CallObjectMethodV (verified by direct
objdump; absent from saved evidence files), `2383dac call *0x2f0` GetFieldID `mPid I`,
`2383dbf call *0x320` GetIntField. Needs 2 methods + 1 field, 2 framework classes — within
<=5 methods / <=3 classes limits (lead-confirmed). Concrete List impl UNPROVEN.

**HARNESS-JAVA-OBJECT status:** NONE built or credited at step 4. static-A stopped object work
per kill switch; only prep occurred (DEX extracted OUTSIDE git to `/tmp/opencode/n2-dex/`:
classes.dex 9057172 B, classes2.dex 6145160 B, classes3.dex 6983328 B; apktool; authoritative
base.apk `/tmp/opencode/auth-base.apk` sha 114a6f8e). No DEX method/field extraction, no #552
param type, no JNIEnv call list, no object tier at step 4. If a HARNESS-JAVA-OBJECT is later built,
it MUST carry an owner + removal condition in this ledger before any credit.

### Step 3 object tier (task #669) — audit verified, all limits respected

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 39 | **HARNESS-JAVA-OBJECT: DEX-typed `java/util/List` with exactly `size()I`, `get(I)Ljava/lang/Object;`, `mPid:I` on `ApplicationExitInfoCpp` (2 methods + 1 field, 2 framework classes); built from authoritative DEX 114a6f8e with NO fabrication, HARD-FAIL on unimplemented methods (no silent defaults), env switch `SOBER_N2_JAVA_OBJECT` default OFF; rung1 stays `PASS-BOUNDARY-REPLAY (DEBT=1)` label only, gate never CLEAN, marker WITHHELD. Result: guard still 0, recursion NOT gone — STOP, no extension** | raw obj_on_1/2/3 `[N2_OBJ BUILT] DEX-typed java/util/List size=0 get=(I)Ljava/lang/Object; mPid=I class=ApplicationExitInfoCpp list=0x…`, `[N2_OBJ size] List.size()I -> 0 (Collections.EMPTY_LIST DEX path)`; source `guest_app_shell.c:145/157 [N2_OBJ HARD_FAIL]`, `:1751 if (getenv("SOBER_N2_JAVA_OBJECT"))`; raw 6 runs `[[rax]+0x28]=0` (18 lines), rung1 PASS DEBT=1 6/6, rc=139 6/6, fault `rip=...08ff si_addr=0x8` | harness (owner), audited | Minimal DEX-typed probe of #552 object within <=5 methods / <=3 classes; did not change guard or terminate recursion; no credit claimed | "a genuine Java runtime supplies this" (lead's exact wording) — remove when a real Java/DEX runtime supplies the List/ApplicationExitInfoCpp object on the authoritative input and no harness-built object is needed |

**Hard-fail/stop limits — all respected:** class/fields/signatures quoted from authoritative DEX
114a6f8e (static-A #668); 2 methods + 1 field (<=5), 2 framework classes (<=3); `HARD_FAIL`
handlers at `guest_app_shell.c:145/157` (no silent defaults; 0 HARD_FAIL lines because `size()=0`
skips the loop at `0x2383d40: jle 23844e9`); env `SOBER_N2_JAVA_OBJECT` opt-in default OFF
(`:1751`); rung1 PASS (DEBT=1) label only, gate never CLEAN, marker WITHHELD; guard 0 in all
18 observations; rc=139 6/6; STOP honored (no extension, no broader object, no extra methods).
Source diff (f8a0069→HEAD) touches only `src/jni/native_jni.c` + `src/linux/guest_app_shell.c`;
only `memcpy` is host-struct (`guest_env_table`), no guest byte writes; no rung/gate/marker changes.

**Hard-fail/stop limits:** kill switch stopped steps 2-3 object work immediately (respected);
no host fix applied while adjudication pending (respected); read-only exec bps only, no
guest writes/gate/seeder/fake returns/path remaps; no object credited.

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n2-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 38 self-matches (ledger's own recorded scan commands + verification-notes in 4 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n2. Marker WITHHELD.

## Overnight n3 audit (task #671) — adjudication + retractions

Owner: audit (`/tmp/opencode/n3-audit`, branch `n3-audit`). Cores 6-7. READ-ONLY, no device/runs; no questions.

Input SHA: `verified … sha256=8215519f…` = 1 in each `n3_{1,2,3}.err` (3/3);
`W24 JNI_OnLoad FIRST … ver=0x10006` and `W30 TSD seed REMOVED` present; object OFF, rc=139 3/3.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 40 | **RETRACTED: "faulting `rdi=0` is the NULL #552 jobject argument (object OFF), not global/TLS". n3 entry bp disproves it — jobject at entry is VALID; NULL rdi is INNER, producer UNPROVEN. Also RETRACTED: "zero JNI calls = guest never read the list" — `size`=1/run with a0==entry rdx** | raw n3_1.err `[N3_552_ENTRY] tid=1876773 rip=base+0x2383c18 rdi=0x56302bd802d0 rsi=0x56302d66d260 rdx=0x56302c1ba750 rcx=0x56302c1ba750 [rdx]=0x56300e0ebec0 [rdx+8]=0x1 rdi_is_env=1` + `[W25_FAULT_ARGS] rdi=0x0000000000000000 rax=0x0000000000000000` (3/3); ring `[0x…20] slot=0x…8e name=size a0=0x000056302c1ba750 …` with a0==entry rdx; counts size=1/get=0/GetObjectClass=0/GetMethodID=0 | audit adjudication (lead notified) | Entry jobject non-NULL ⇒ fault rdi cannot be the entry jobject; static-A inferred it from n14 logs with no entry bp. Only `rdi==0` at fault is PROVEN; its producer and the "not global/TLS" exclusion are UNPROVEN | Remove when a raw line identifies the exact inner producer of the NULL rdi (or confirms it is the jobject) |

**Faulting instruction first (n3, PROVEN):** `rip=0x00007fe629ee08ff` host/shared-lib, no guest_rva,
bytes `48 8b 47 08 74 05 48 85` = `mov rax,[rdi+8]; je; test rax,rax`, `si_addr=0x8`
(`[NULL+8]`), rc=139 3/3. static-A independently located the same RIP in n1-harness/n14_1.err
and confirmed no guest RVA (`rip - guest_base = 0xf20f8ff` > engine size); exact host symbol UNPROVEN.

**harness #672 — all quotes verified verbatim:** SHA 3/3; fault line/args; `[N3_ENTRY_ARM]
base=0x7fe61acd0000 fd_rc=0`; `[N3_552_ENTRY] … rdi_is_env=1`; ring `name=size a0==entry rdx`;
counts size=1/get=0/GetObjectClass=0/GetMethodID=0 per run. UNPROVEN: inner NULL rdi producer,
host module at `...08ff`, why `size` returned 0 yet fault persists.

**static-A #670 — claims verified except the adjudicated rdi provenance:** order lines (`JNI_OnLoad
FIRST` → `nativeInitClientSettings 0x2359277` → `controller ensure_flag` → `N14_REPLAY 1/3`
`nativeOnPreCreated` → `N14_REPLAY 2/3` `nativeOnResumed` → fault, no 3/3 line); capture
`J2N #551 nativeInitClientSettings` (w12:566) before `J2N #552 … ([object Object])` (w12:567),
verified verbatim in w12-j2n-300.log; #552 pre-BaseDirs preserved but order not identical
(harness inserts lifecycle replays + JNI_OnLoad FIRST). **QUOTE-RULE DEFECT flagged:** report line 9
condensed `insn` bytes (`48 8b 47 08 74 05 48 85`) vs raw zero-padded
`insn=0000000000000048 000000000000008b …`; substance verified, quote not verbatim.
static-A subsequently added an explicit RETRACTION section to report.md (commit b47f2a9)
quoting the n3 entry bp and accepting the inner-rdi verdict; the condensed-insn line remains
in the (now retracted) original section.

**static-B #673 — all quoted disasm verified verbatim:** `rbx@6c8e512` = builder entry rdi via
`6c8d84a mov %rdi,%rbx`; caller `6c8e8bd mov %rsi,%rbx` / `6c8e9a5 mov (%rbx),%rdi` =
host/bridge callback-arg slot; host registration `227b979 lea #6c8e8a8`, `227b980 mov %rax,
#7cdec50`, `227b9b1 mov %r14,(%rbx)`; seeding `6c8eb79 mov 0x18(%r15),%rax` /
`6c8eb7d mov %rax,(%rbx)` / `6c8eb83 call 6c8d840`; wrapper `6ca7ffa call 6c89e50`,
`6ca7fff mov 0x18(%rax),%rbx` (original table), `6ca8003 call 6c89e70`, `6ca8008 mov (%rax),%rax`,
`6ca800e call *0x720(%rax)`; guard `6c8d861/864` skips after swap. `6c8e512` verified in
`dis_builder_full.txt` (omitted from report evidence list, minor). RETRACTED: wrapper uses the
copy (it reads the original); harness pointer-identity conflict UNPROVEN.

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n3-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 46 self-matches (ledger's own recorded scan commands + verification-notes in 4 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n3. Marker WITHHELD.

## Overnight n4 audit (task #677) — host fault resolved + retractions

Owner: audit (`/tmp/opencode/n4-audit`, branch `n4-audit`). Cores 6-7. READ-ONLY, no device/runs; no questions.

Input SHA: `verified … sha256=8215519f…` = 1 in each `n4_{1,2,3}.err` (3/3); object OFF, rc=139 3/3.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 41 | **Host-only maps-aware crash handler: `/proc/self/maps` read via `open/read/close` into a fixed buffer + `write(2)` only — no stdio/malloc in the signal path. Prints `[W40_RIP_MAP]` for RIP; RET64 maps lines implemented but NOT EXERCISED (|si_addr−rsp| ≫ 64 KiB, RET64 count 0/0/0). DEBT** | raw n4_1.err `[W40_RIP_MAP] rip0x00007fa51a6518ff offset=0x00000000000588ff maps='7fa51a5f9000-7fa51a749000 r-xp 00024000 00:17 482353 /usr/lib64/libc.so.6'`; source `src/linux/guest_dlopen_host.c:165 open /proc/self/maps`, `:169 read`, `:173 close`, `:144 'No stdio, no malloc — safe inside signal handler'` | harness (owner), audited | Resolves a host fault site without ptrace/stdio in the handler; no guest state touched | Remove when the handler is folded into final host code or maps resolution is no longer needed |
| 42 | **RETRACTED: fault offset `_IO_fflush+0x3f`. Correct offset is `+0x9f` — `nm -D /usr/lib64/libc.so.6` gives `_IO_fflush@@GLIBC_2.2.5 = 0x7c860`; objdump shows `<...+0x9f>` at `0x7c8ff`; `0x7c8ff−0x7c860 = 0x9f`. Core finding (host libc `_IO_fflush`, NULL rdi, not guest) STANDS** | harness report_n4.md:41/47 and static-B report.md:4 still state +0x3f with self-contradictory arithmetic; audit nm/objdump verification above. static-A CORRECTED to +0x9f with explicit retraction (report.md:72) | audit (lead notified, flagged to harness+static-A+static-B) | A quoted success claim with wrong arithmetic; substance (function identity) independently confirmed | Remove when harness and static-B correct the documents to +0x9f |

**Faulting instruction first (n4, PROVEN):** `rip=0x00007fa51a6518ff`, `48 8b 47 08` =
`mov rax,[rdi+0x8]`, `si_addr=0x8` = `[NULL+8]`, fault rdi=0, rc=139 3/3. Resolved via
`[W40_RIP_MAP]` to `/usr/lib64/libc.so.6` mapping-relative offset `0x588ff`, ELF file offset
`0x24000+0x588ff = 0x7c8ff` = `_IO_fflush@@GLIBC_2.2.5+0x9f` (NULL-FILE `fflush` path), not guest.
No RET64 line ⇒ no first-guest-RVA call site; `guest+0x72a0818` from `[W23_STACK_SCAN]` lies inside
`.bss` (`vaddr 0x7282500 size 0xb1ad1c`) ⇒ DATA, not code — RETRACTED as a call site (lead's caution).

**harness #674 — all raw quotes verified:** SHA 3/3, fault line/insn, `[W40_RIP_MAP]`, `[W23_STACK_SCAN]`,
RET64 counts 0/0/0, maps reader source (open/read/close, no stdio/malloc). Only defect = entry 42 offset.

**static-A #676 — all quoted claims verified:** PLT `6d32020 <fflush@plt>: ff 25 … # 7229750` (verified
by direct objdump; absent from saved evidence files), call sites `232b8d9/237d747/237d9d3/2e61d13`
loading rdi from object fields/registers (not global/TLS), shim `bionic_libc.c:487/490/653` +
`gen_bionic_libc.py:262` skip ⇒ direct stub forwards NULL stream to glibc `_IO_fflush`; `.bss`
covers 0x72a0818 (DATA). Initially repeated +0x3f; CORRECTED to +0x9f with retraction
(report.md:72) after audit flag. Exact #552 NULL-passing site UNPROVEN.

**static-B #675 — all quoted disasm verified verbatim:** `2383d16 lea "size"` / `2383d1d lea "()I"`,
`2383cfc/2383d2a call *0x108` (GetMethodID), `2383d3b call 238463c` (CallIntMethodA via
`23846b6 call *0x190`), `2383d40 test eax,eax` / `2383d42 jle 23844e9` (size<=0 ⇒ EARLY RETURN,
`238450a ret`, not immediate abort); fatal helper `23845d5 call 2ea6a10`, inside it `2ea6a6b add
# 72281d8` (__sF+0x130=stderr), `2ea6a8c call fprintf@plt`, `2ea6a94 call fflush@plt`, `2ea6a99
call abort@plt`; source `bionic_sf_full.c:4-7` documents NULL `_lock` at FILE+0x88. UNPROVEN which
exception producer fires after size()==0 and why rdi=0 vs __sF+0x130; no fabricated guest call site.

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n4-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 54 self-matches (ledger's own recorded scan commands + verification-notes in 4 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n4. Marker WITHHELD.

## Overnight n5 audit (task #681) — SF_FULL defect removal + new first divergence

Owner: audit (`/tmp/opencode/n5-audit`, branch `n5-audit`). Cores 6-7. READ-ONLY, no device/runs; no questions.

Input SHA: `verified … sha256=8215519f…` = 1 in each `n5_{1,2,3}.err` (3/3); object OFF, rc=139 3/3.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 43 | **Bionic __sF ABI fidelity, defect removal; DEBT=1; gate/withhold list unchanged** (host-only: `SOBER_BIONIC_SF_FULL=1` widens exported `__sF` to 0x200 and maps guest `__sF+0x98/+0x130` to real host stdout/stderr with valid `_lock`, removing the earlier host libc `_IO_fflush+0x9f` NULL-FILE fault) | raw n5_1.err `[bionic-sf] full __sF stdio bridge installed (host __sF=0x55829d0d5140, size=0x200)`, `[bionic-sf] stdio GOT bound: fwrite 0x7229748 …; fflush 0x7229750 …`; script `rund3_n5.sh:10 export SOBER_BIONIC_SF_FULL=1`; libc objdump `7c8e8 mov rdi,[rdi+0x88]` (_lock load) then `7c8ff mov rax,[rdi+0x8]` NULL with tiny __sF; source `bionic_libc.c:443 sober_libc_obj___sF[0x200]`, `bionic_sf_full.c:4-7` | harness (owner), audited | A genuine host-side ABI fix: no guest bytes/gate/fake returns/remaps; libc fault gone 3/3, replaced by a guest assert (below) | Remove when a real device/AVD boot supplies the full `__sF` bridge natively (no host shim needed) or the opt-in is folded into default host code |

**NEW FAULT (first divergence, PROVEN):** with SF_FULL ON the libc fault is GONE (3/3); new fault
`guest+0x6caef67`, bytes `89 c0 e8 62 …`, `si_addr=0x0`, **signal=5 SIGTRAP (guest HardAssert)**.
Raw `[W28_FAULT_INSN] rip=0x00007f12da5d6f67 guest_rva=0x06caef67`, `signal=5 tid=1952583 …`,
last-15 stderr `RBXCRASH: HardAssert (Expect to have mimalloc working, but it was not detected!)`.
3rd `N14_REPLAY` never printed (assert fires before #552 returns). This is a GUEST-side defect newly
exposed by removing the host one — not credit; exact assert meaning and mimalloc-detection cause UNPROVEN.

**A/B (#682, one session, ALL-3 criteria) — all quoted claims verified:** SHA 6/6; banner
`[N6_BANNER] SOBER_BIONIC_SF_FULL=1` 3/3 in A, `=UNSET` 3/3 in B; A faults at guest RVA
`0x06caef67` 3/3, B at host `...08ff` 3/3; `N14_REPLAY 3/3` count 0/0/0 in BOTH arms;
rc=139 6/6. Success criteria: (1) libc fault gone under A = YES, (2) 3rd replay prints = NO
in both arms, (3) faulting instruction differs = YES → **overall NOT SUCCESS** (criterion 2
fails). Last-15 A ends with HardAssert, B with replay 2/3 + guard 0. Gate/withhold UNCHANGED:
source diff (12e2407→HEAD) touches only `bionic_libc.c`/`bionic_sf_full.c`/`guest_dlopen_host.c`
+ reports/scripts, no rung/gate/marker changes; rung1 PASS (DEBT=1) 6/6 label only. Ledger
text recorded EXACTLY as lead specified (entry 43).

**harness #678 — all quoted claims verified verbatim:** SHA 3/3; SF_FULL installed/GOT-bound lines;
`[W40_STDIO_RING] last 32 fflush/fwrite/fclose calls …` header present 3/3 but `in_sF` entries 0/0/0
(GOT rebinding bypasses shims under SF_FULL — UNPROVEN exact bypass path); RET64 count 1/1/1 (vs
0/0/0 in n4), first guest RVA `guest+0x3a30e8`; last-15 lines (replays 1/3+2/3, HardAssert, no 3/3);
libc objdump independently confirmed at 0x7c8e8/0x7c8ff. No quantifier overreach found.

**static-A #679 — all quoted claims verified** (fatal helper disasm `2ea6a66/6b/72/79/80/8c/91/94/99`,
strings `libunwind: %s - %s\n` / `_Unwind_Resume` / `_Unwind_Resume() can't return`, caller `1dba6b7
call 2ea6a10` verified by direct objdump; `__sF[3]` 0x198 / +0x98 / +0x130 / NULL `_lock` at +0x88
from source comments; `SOBER_BIONIC_SF_FULL` opt-in). Two of three cited caller addresses
(`1dceb4f`, `1dcf4c8`) not independently confirmed by spot objdump — not used as a success claim;
"no caller in #552 body" consistent with prior-wave scans. Fatal helper flushes +0x130 (stderr);
other logger FILE* from object fields → global "all flushes use one stream" UNPROVEN.

**static-B follow-up (report2.md, new guest fault) — quotes verified:** reporter fn `0x6caed7c`
via dispatcher `0x6caed5d` (`6caed5d push rbp`, `6caed61 cmp $0xa,%edi`, `6caed6c lea #721fbd0`,
`6caed77 call 6caed7c` — verified by direct objdump), trap `6caef66 cc int3` (reported RIP 6caef67
is the following instruction), message `23a1465 lea #4c0d2b` / `23a146e call 6caed5d`, strings
`0x4c0d2b Expect to have mimalloc working…`, `0x3a30e8 HardAssert`, `0x3a3078 [DFLog::Mimalloc]
… NOT detected` verified from engine bytes, matching harness RET64 `guest+0x3a30e8/0x4c0d2b`;
predicate bytes `0x7cdd149` (W18 latch, written by `2eb7520 movb $1`) and `0x7cef650` (init zero
`2279a4b xor %eax,%eax` / `2279a8f mov %al`, FFlag `DebugForceTerminateOnRbxCrash`), both must be
nonzero else int3; weak `mi_malloc` at `bionic_bootstrap/bionic_bootstrap.c:175` (README:246
"glibc malloc when mimalloc absent"). UNPROVEN (as agent states): exact detection call at
`23a1422 test %al,%al`, which byte is specifically "mimalloc detected", whether either byte alone
clears the assert. Several dispatcher/string lines absent from saved evidence files but verified
by direct objdump/strings.

**static-B #680 — all quoted lines verified verbatim:** after-#552 host calls (`0x24650a9/0x232d484/
0x2326e2e/0x246515d/0x2465210/0x233762c/0x24abf96`) are host `fprintf(stderr,…)`, NOT guest fflush —
RETRACTED any host-log-as-guest-fflush claim; peract2 thread lines seq71/89/100 (AsyncTask #3 /
FunctionMarshal / RBX Worker A); 3rd N14 replay `0x2383c18` source; J2N #67/#105/#552
(w12-j2n-300.log). Thread timing vs #552 UNPROVEN.

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n5-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 62 self-matches (ledger's own recorded scan commands + verification-notes in 4 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n5. Marker WITHHELD.

## Overnight n7 WAVE1 audit (task #686) — ctor3602 adjudication

Owner: audit (`/tmp/opencode/n7-audit`, branch `n7-audit`). Cores 6-7, MANDATORY. READ-ONLY, no device/runs; no questions.

Input SHA + banner verified on EVERY run: `verified … sha256=8215519f…` = 1 and
`[N6_BANNER] SOBER_BIONIC_SF_FULL=1` = 1 in each `n7_{1,2,3}.err` (3/3);
`W24 JNI_OnLoad FIRST … ver=0x10006` 3/3; `[W28_FAULT_INSN] … guest_rva=0x06caef67`, rc=139 3/3.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 44 | **RETRACTED: "ctor3602 never runs in Run D (zero bp hits)". The exec bps WERE armed, but 30 log lines TOO LATE — ctor3602 demonstrably executed first. Zero hits are a watcher-timing artifact, not proof of non-execution** | raw n7_1/2/3.err line **3652** `[harness] ctor[3602] rva=0x2279a47` BEFORE line **3682** `[N7_CTOR_ARM] base=0x… entry=0 store=0`; source `native_jni.c:840-849` prints `entry=a1` where `a1 = f1>=0 ? w21_arm(...) : -1` and `w21_arm` (`:154+`) returns 0 on success (negative on sigaction/fcntl/ioctl failure) => `entry=0 store=0` = BOTH ARMED; `guest_dlopen_host.c:867` logs ctor index immediately BEFORE `:875 ((void (*)(void))fn)()`; `w21_init()` called at `guest_app_shell.c:2337` (after ctor replay loop `:833-875`) | audit adjudication (lead notified) | A zero-hit observation was misread as non-execution; log order disproves it. ctor3602 DID run 3/3; tid/order/stored-value remain UNPROVEN (no bp observation) | Remove when an exec bp armed BEFORE ctor replay quotes a hit/tid/value at 0x2279a8f, or the early-execution log is shown to be non-causal |

**Faulting instruction first (PROVEN):** guest `int3` at `0x6caef66` (`cc`, HardAssert trap);
crash handler reports next RIP `guest+0x6caef67` (`89 c0 mov eax,eax`; then `e8 call abort@plt`).
Raw `[W28_FAULT_INSN] rip=0x00007fdcec9f2f67 guest_rva=0x06caef67`, `signal=5 tid=2053520
rip=0x00007fdcec9f2f67 si_addr=0x0`. objdump decode `6caef66: cc int3`, `6caef67: 89 c0`,
`6caef69: e8 … call abort@plt`. `[N7_FAULT_BYTES] [0x7cdd149]=0x…01 [0x7cef650]=0x…00`
(3/3, tids 2053520/2053705/2053936).

**harness #685 — all quoted claims verified, one interpretation RETRACTED (entry 44):** SHA+banner
3/3, fault line/bytes, predicate bytes 3/3, `[N7_CTOR_ARM]`, zero ctor-hit counts (arithmetically
correct but causally misattributed — see entry 44). Last order markers are host lines
`[app-shell] W24 JNI_OnLoad FIRST` / `AVD boundary nativeInitClientSettings … latch=1`.
`[0x7cef650]=0` at fault is consistent with BSS-zero; whether ctor3602's (late-unobserved) store
caused it remains UNPROVEN.

**static-A #687 — all quoted disasm verified verbatim:** ctor3602 `0x2279a47` writes
`0x7cef650` UNCONDITIONALLY — `2279a4b xor %eax,%eax`, `2279a8f mov %al,0x7cef650`, no
test/getenv/GOT/import/syscall/CPU-feature/detection call between; only later calls
`2279adf call 1dc1208` / `2279b48 call 2e8afb0` build objects. RETRACTED the detection-call premise.
Assert polarity: `6caef4f cmpb $0,0x7cdd149` (latch FIRST), `6caef56 je 6caef66`,
`6caef58 cmpb $0,0x7cef650` (mimalloc SECOND), `6caef5f je 6caef66`, `6caef61 call abort@plt`,
`6caef66 int3` — `je` on ZERO ⇒ int3 when EITHER byte zero; explicit abort only when BOTH nonzero.
static-A could not quote a local SHA banner (UNPROVEN from its side; harness covers it 3/3).
NOTE: static-A reported its branch push BLOCKED ('unable to write new index file'), files on disk.

**static-B #688 — all quoted source/log lines verified verbatim:** `guest_dlopen_host.c:819-875`
std ranges `{2,3},{5,3618}` (3602 in replay set), loop `:833`, call `:875`; log
`[harness] ctor[3602] rva=0x2279a47` (n5_1.err:3652, also n7 line 3652); ctor3602 runs before
ctor0 replay/scheduler install/latch flip (n5 order 3652/3687/3689/3706); real boot runs skipped
ctor 0/1/3/4 via Android linker (decision-008 ctor0 skip); `dl_iterate_phdr` export
(bionic_libc.map:348), TRAMP macro (:459-470: `:464 movq fn,%rax`, `:465 test`, `:466 jz 1f`,
`:467 jmp *%rax`, `:468 xorl %eax,%eax`, `:469 ret`), fn fill `:919 native_lookup`; returns 0
with NO callback if pointer NULL — which branch runs UNPROVEN (no runs by static-B).

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n7-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 70 self-matches (ledger's own recorded scan commands + verification-notes in 4 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n7. Marker WITHHELD.

## Overnight n7 WAVE2 audit (task #690) — early-arming confirms zero write; BLOCKED

Owner: audit (`/tmp/opencode/n7-audit`, branch `n7-audit`). Cores 6-7, MANDATORY. READ-ONLY, no device/runs; no questions.

SHA+banner verified on EVERY run: `verified … sha256=8215519f…` = 1 and
`[N6_BANNER] SOBER_BIONIC_SF_FULL=1` = 1 in each `n7b_{1,2,3}.err` (3/3); rc=139 3/3;
`[N7_FAULT_BYTES] [0x7cdd149]=1 [0x7cef650]=0` 3/3; fault int3 `0x6caef66` (RIP `0x6caef67`).

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 45 | **Wave1 timing artifact resolved by EARLY arming: ctor3602 exec bps armed at log line 8 (before ctor replay at 3653) now hit 1/1/1 — entry, store (`al=0x00` → `0x7cef650`), and after-bp readback `[0x7cef650]=0x00`. This closes entry 44's UNPROVEN stored-value/tid** | raw n7b_1.err line 7 `[harness] guest base=0x7f7031c70000`, line 8 `[N7B_ARM] base=0x7f7031c70000 fds=4/5/6 rc=0/0/0`, line 3653 `[harness] ctor[3602] rva=0x2279a47`, 3654 `[N7B_ENTRY 0x2279a47] hit=1 tid=2074238`, 3655 `[N7B_STORE 0x2279a8f] hit=1 tid=2074238 al=0x00 store_to=0x7cef650`, 3656 `[N7B_AFTER 0x2279a95] hit=1 tid=2074238 [0x7cef650]=0x00`; counts entry=1/store=1/after=1 in all 3 runs; arming hook at `guest_dlopen_host.c:942-944` before `drive_constructors()` (`:1048`) | harness, audited | Directly observes the runtime zero write that wave1's late arming could not; confirms entry 44 retraction and static-A's unconditional-write decode | Remove when a different guest path is shown to write 0x7cef650 nonzero before the assert |
| 46 | **BLOCKED: no nonzero writer for `0x7cef650` exists in the engine — premise "a genuine boot leaves it nonzero so the assert passes" RETRACTED/UNPROVEN. Exhaustive scan: exactly 3 refs (`2279a8f` write al=0, `227d51f` lea argument, `6caef58` assert read); the only nonzero-capable writer is ctor3 → `0x7cdede0`, which decision 008 SKIPS** | raw scans `raw_all_7cef650.txt` (3 lines), `raw_all_7cdede0.txt` (`1db9acf mov %bl,0x7cdede0` only write, `239eb14 cmpb $0` read), ctor3 probe `1db9aab mov %eax,%ebx` / `1db9aca setae %al` / `1db9acd and %al,%bl`, decision 008 skip `fflag_default.c:111-113` ctor3 entry, `227d51f lea #7cef650` (external/FFlag path UNPROVEN) | static-A, audited (lead/harness scope decision) | If true, the int3 HardAssert fires on every boot unless an external actor writes the byte — a gate/scope decision, not a credit claim | Remove when a raw line shows `0x7cef650` written nonzero by any code path (or a real capture shows the byte set) |

**harness #689 — all quoted claims verified verbatim:** SHA+banner 3/3, fault line/bytes,
`[N7B_ARM] … fds=4/5/6 rc=0/0/0` (all armed), hit lines/counts/readback (entry 45), order
ctor 3653-3656 → JNI_OnLoad 3706 → nativeInitClientSettings 3712. Host-only arming added at
`guest_dlopen_host.c:942-944`, no guest writes/remaps/hand-setting.

**static-A #691 — all quoted claims verified verbatim:** exhaustive reference scan counts
(3 for 0x7cef650, 2 for 0x7cdede0) with all cited addresses present in saved raw files;
ctor3 probe chain and decision-008 skip entry; ctor3602 has no input before the zero store
(`2279adf`/`2279b48` calls come after) — no capture/DEX value can change the zero write (PROVEN).
`227d51f lea #7cef650` (address taken as argument, possibly FFlag `DebugForceTerminateOnRbxCrash`)
not shown to write the byte — UNPROVEN. static-A reported its own git push BLOCKED (index write
fault); files on disk.

**static-B #692 — all quoted real-boot-order lines verified verbatim:** linker APS2/all-3618 ctors
(`android_relocs.h:8-12`, `guest_dlopen_host.c:824`), ctor0 deliberate skip + opt-in replay
(`guest_app_shell.c:1411-1419`, n5_1.err:3687), ctor1/3/4 skip (`:850-857`), ctor3602 replay
(n5_1.err:3652), JNI_OnLoad (n5:3702), lifecycle #67/#100/#105 (`w12-j2n-300.log:82/115/120`),
latch write (`w18-avd/peract2.log:19 HOOK LATCH_WR 0x2eb7520 OK`, n5:3706), #551/#552
(w12:566/567), AppStart `0x247dc8d` (`guest_app_shell.c:1919-1931`, `:1961-1964`) never reached.
Why real boot does not trap on zero `0x7cef650` UNPROVEN.

**Secrets-scan command + output (final wave2):**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n7-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 70 self-matches (ledger's own recorded scan commands + verification-notes in 4 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n7w2. Marker WITHHELD.

## Overnight n7 WAVE3 audit (task #696) — 3rd replay entered, two fatal reporters

Owner: audit (`/tmp/opencode/n7-audit`, branch `n7-audit`). Cores 6-7, MANDATORY. READ-ONLY, no device/runs; no questions.

SHA+banner verified on EVERY run: `verified … sha256=8215519f…` = 1 and
`[N6_BANNER] SOBER_BIONIC_SF_FULL=1` = 1 in each `n7c_{1,2,3}.err` (3/3); rc=139 3/3;
fault int3 `0x6caef66` (RIP `0x6caef67`), `[N7_FAULT_BYTES] [0x7cdd149]=1 [0x7cef650]=0` 3/3.

**harness #695 — all quoted claims verified:** `[N7B_ARM] base=0x7f7ade34c000 fds=4/5/6/7 rc=0/0/0/0`
(log line 8, before ctor[3602] 3653); 3rd native ENTERED 1/1/1:
`[N7C_THIRD 0x2383c18] hit=1 tid=2081900 rdi=0x000055ec633cc2d0 rsi=0x000055ec64cb9260
rdx=0x000055ec63806e80 rcx=0x000055ec63806e80` — audit confirmed `rdx==rcx==shared_param_obj`
from `[app-shell] JNI args: … shared_param_obj=0x55ec63806e80 (blob=0x1000)`. Order: line 3735 bp
hit → 3736 `RBXCRASH: HardAssert (Expect to have mimalloc working, but it was not detected!)` →
3737 `W30_FAULT`; `N14_REPLAY 3/3` count 0/0/0. **Refinement (PROVEN):** 3rd replay is not merely
"faulted before log line" — it is ENTERED and the HardAssert fires INSIDE its execution; no host
return to the logger. UNPROVEN: exact inner instruction triggering the assert, why no return.

**static-A #694 — all quoted claims verified:** source `guest_app_shell.c:1760-1761` call
`((long (*)(void*,void*,void*))(base+0x2383c18u))(env, app_class, opaque)` with `opaque = param_obj`
at `:1736` (comment "real native-backed shared_param_obj, not a fabricated empty object"),
`:1755-1758` opt-in DEX-typed List; fprintf at `:1762-1764` immediately follows the call with NO
intervening statement → absent log means call never returned (consistent with harness bp). Raw
n14_1.err lines 3724/3726/3728/3730 verified; whole-file `N14_REPLAY 3/3` count 0. Exact inner
`fflush@plt` caller supplying NULL rdi UNPROVEN (entry rdx valid per n3; named sites FLog 0x2e61ca0
and fatal helper 0x2ea6a10 not proven to be the one). Separate opt-in call at `:897` noted.

**static-B #693 — all quoted disasm verified:** two DISTINCT fatal reporters RETRACTED-from-conflation:
(A) unwind reporter `0x2ea6a10` prints `libunwind: _Unwind_Resume - _Unwind_Resume() can't return`
(strings 0x2345cf/0x517ec0/0x2a4d76, fprintf 2ea6a8c, fflush 2ea6a94, abort 2ea6a99) called from
#552 catch `23845d5 call 2ea6a10` (with `23845cd call 1dbe5be` cleanup); (B) HardAssert reporter
`0x6caed7c` via dispatcher `0x6caed5d` (`23a1465 lea #4c0d2b`, `23a146e call 6caed5d`, table
#721fbd0 relocation-filled), assert `6caef4f/6caef58 cmpb` + `je 6caef66 cc int3` (the actual n7
fault). `_Unwind_Resume` needs `dl_iterate_phdr` callbacks; shim TRAMP (bionic_libc.c:459-470,
`:919 native_lookup`) returns 0 with no callback if pointer NULL. After `size()==0` the path
`2383d40 test eax,eax` / `2383d42 jle 23844e9` is a NORMAL early return — no `__cxa_throw` located;
exception driving reporter A UNPROVEN, and whether HardAssert is reached via unwind failure UNPROVEN.
RETRACTED: size()==0 directly throws / unwind proven to cause the mimalloc HardAssert.

**Secrets-scan command + output (final wave3):**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n7-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 71 self-matches (ledger's own recorded scan commands + verification-notes in 4 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n7w3. Marker WITHHELD.

## Overnight n7 WAVE4 audit (task #699) — HardAssert path, table tag, log-fire sequence

Owner: audit (`/tmp/opencode/n7-audit`, branch `n7-audit`). Cores 6-7, MANDATORY. READ-ONLY, no device/runs; no questions.

SHA+banner verified on EVERY run: `verified … sha256=8215519f…` = 1 and
`[N6_BANNER] SOBER_BIONIC_SF_FULL=1` = 1 in each `n7e_{1,2,3}.err` (3/3); rc=139 3/3;
fault int3 `0x6caef66` (RIP `0x6caef67`), signal=5, si_addr=0.

**harness #698 — all quoted claims verified:** `[N7B_ARM] base=0x7f5e98424000 fds=-1/-1/-1/-1
rc=-1/-1/-1/-1 inner_fd=4/5/6/7/-1 inner_rc=0/0/0/0/-1` — 4 of 5 inner bps armed (FLog fflush,
unwind fflush, assert dispatcher, assert reporter); the trap bp at 0x6caef66 FAILED (perf HW
breakpoint slot limit), so no direct trap-hit observation. Hits per run: FLOG_FFLUSH=2,
UNWIND_FFLUSH=0, ASSERT_DISPATCH=1, ASSERT_REPORT=1, TRAP=0, all on replay thread tid 2093725.
Order (lines 3732-3737): dispatch → reporter → FLog fflush #1 → `RBXCRASH: HardAssert (Expect to
have mimalloc working, but it was not detected!)` → FLog fflush #2 → W30_FAULT. **Which reporter
fires (PROVEN among armed bps):** the HardAssert reporter, logging via FLog fflush `0x2e61d13`;
the unwind-reporter fflush `0x2ea6a94` has ZERO hits — the `_Unwind_Resume` path is NOT taken here.
UNPROVEN: exact trap instruction (bp unarmed; identified by RIP decode + static), any path between
last FLog fflush and the trap.

**static-A #697 — all quoted disasm verified (some via direct objdump):** no direct size()==0 →
HardAssert edge — `2383d42 jle 23844e9` → canary (`23844e9 mov 0x72293f8`, `23844f9 add $0x128,%rsp`)
→ `238450a ret`, with no 6caed5d/6caed7c call. Message call at `23a146e call 6caed5d` with
`rsi = 0x4c0d2b` mimalloc string and `edi=0` (index 0), dispatcher `6caed61..6caed77`, table
`0x721fbd0` → reporter `0x6caed7c` → byte tests 6caef4f/6caef58 → int3. Reach chain via mi_option
printer `239d215 call 239ead3` → `239eb14 cmpb $0,0x7cdede0` (ctor3 flag, zero in harness) →
`239eb1b je 23a1422` → `23a1424 je 23a1465` → call. Entry edge to 239d215 UNPROVEN.

**static-B #700 — all quoted disasm verified (dispatcher lines by direct objdump):** dispatcher
`6caed61 cmp $0xa,%edi` / `6caed64 mov $0xa,%eax` / `6caed69 cmovb %edi,%eax` ⇒ index clamped
0..10, 11 qword entries `0x721fbd0..0x721fbe8`; `6caed6c lea #721fbd0` / `6caed73 mov
(%rcx,%rax,8),%rdi` / `6caed77 call 6caed7c`. Call site `23a1465 lea #4c0d2b,%rsi`,
`23a146c xor %edi,%edi` (index 0), `23a146e call 6caed5d`. **RETRACTED: describing 0x721fbd0
as a message table whose index selects the string** — the message is passed in `%rsi` from the
call site; the table supplies `%rdi` (tag/severity); the two meet only at this call site. Table
file bytes are zero (filled by DT_ANDROID_RELA/APS2 at load); APS2 magic present at RVA 0x15e00
but SLEB decode implausible ⇒ contents UNPROVEN (no runtime read).

**Secrets-scan command + output (final wave4):**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n7-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 72 self-matches (ledger's own recorded scan commands + verification-notes in 4 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n7w4. Marker WITHHELD.

## Overnight n8 WAVE1 audit (task #702) — ctor3 replay A/B, no benefit

Owner: audit (`/tmp/opencode/n8-audit`, branch `n8-audit`). Cores 6-7, MANDATORY. READ-ONLY, no device/runs; no questions.

Input SHA + banner verified on EVERY run: `verified … sha256=8215519f…` = 1 and
`[N6_BANNER] SOBER_BIONIC_SF_FULL=1` = 1 in each of `ab_{off,on}_{1,2,3}.err` (6/6); rc=139 6/6;
fault int3 `0x6caef66` (RIP `0x6caef67`), signal=5, si_addr=0.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 47 | **Host-only genuine ctor3 `0x1db9a40` replay in init_array order (after ctor0, before ctor3602), gated `SOBER_N8_CTOR3` default OFF for A/B; DEBT=1; gate/withhold unchanged. A/B shows NO benefit — flag stays 0x00 and HardAssert persists, so NOT promoted to ON** | raw ab_on_1/2/3 `[N8_CTOR3] before 0x7cdede0=0x00` / `[N8_CTOR3] after 0x7cdede0=0x00` (counts 1/1/1 ON, 0/0/0 OFF), `[harness] STATE AFTER ctor3 0x1db9a40 replay:`; source ctor3 full disasm (`1db9a4b movq $0x20,0x7cdedd8`, `1db9acf mov %bl,0x7cdede0`, 9 allocator-only calls, `1db9add ret`, no getenv/syscall/JNIEnv/pthread); A/B table HardAssert 1/1/1 both arms, N14_REPLAY 3/3 0/0/0 both arms, same fault RIP | harness (owner), audited | Minimal genuine guest-code probe of the skipped ctor3 flag; no hand-set byte, no guest writes; did not remove the assert | "a genuine runtime sets 0x7cdede0 nonzero via its own allocator probe and the HardAssert clears without host replay" — then remove the opt-in replay |

**static-A #701 — all quoted disasm verified verbatim:** ctor3 writes exactly two globals
(`1db9a4b movq $0x20,0x7cdedd8`, `1db9acf mov %bl,0x7cdede0`), makes 9 internal allocator calls
(`1dbad62` malloc ×2, `1dbe4fc` usable-size ×2, `1dbdfba` end-ptr ×2, `1dbe559`/`1db9ac2` free,
`1dbab02` alloc #2) then `1db9add ret`; no getenv/syscall/JNIEnv/pthread; probe predicate
`1db9a8a test %r15b,%r15b`/`1db9a8d je`, `1db9a8f cmp %r12,%r14`/`1db9a92 jb`,
`1db9aca setae %al`/`1db9acd and %al,%bl`. Polarities: `239eb14 cmpb $0,0x7cdede0` +
`239eb1b je 23a1422` (ZERO → assert path); `23a1422 test %al,%al` + `23a1424 je 23a1465`
(al==0 skips DFLog, al!=0 logs NOT-detected then falls through — both converge); `23a1465` is
`lea #4c0d2b,%rsi` (message pointer, not a branch) → `23a146c xor %edi,%edi` →
`23a146e call 6caed5d` → reporter → `6caef4f`/`6caef58` → `6caef66 int3`. SAFE statically
(allocator-only, needs ctor[2] already replayed); UNPROVEN that allocations succeed / flag goes
nonzero (harness A/B answered: flag stayed 0).

**harness #703 — all quoted claims verified:** ctor3 replay counts 1/1/1 ON, 0/0/0 OFF;
`[N8_CTOR3] before/after 0x7cdede0=0x00` flag unchanged; no fault in ctor3 (KILL SWITCH not
triggered); A/B table — HardAssert 1/1/1 both arms, N14_REPLAY 3/3 0/0/0 both arms, same fault
RIP `guest+0x6caef67`. Initial `/dev/shm` build rc=126 (noexec) explicitly NOT credited; all 6
fresh runs from `n8bin/` rc=139. Gate/withhold unchanged (source diff has no rung/gate/marker
changes). UNPROVEN: why ctor3 leaves 0x7cdede0 zero under the host allocator; genuine-runtime behavior.

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n8-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 67 self-matches (ledger's own recorded scan commands + verification-notes in 3 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n8w1. Marker WITHHELD.

## Overnight n8 WAVE2 audit (task #706) — why ctor3 replay is inert

Owner: audit (`/tmp/opencode/n8-audit`, branch `n8-audit`). Cores 6-7, MANDATORY. READ-ONLY, no device/runs; no questions.

SHA+banner verified on EVERY run: `verified … sha256=8215519f…` = 1 and
`[N6_BANNER] SOBER_BIONIC_SF_FULL=1` = 1 in each `n8p_{1,2,3}.err` (3/3); rc=139 3/3;
fault int3 `0x6caef66` (RIP `0x6caef67`), HardAssert, signal=5.

**static-A #705 — all quoted disasm verified:** exact bl=1 conditions at `1db9acf` require all four:
(A) `1dbe4fc(p1)!=0`, (B) `endptr1 >= 0x20`, (C) `1dbe4fc(p2)!=0`, (D) `endptr2 >= 0x20`
(chain `1db9a66/6b`, `1db9a71/76`, `1db9a8a test %r15b,%r15b` / `1db9a8d je 1db9acf`,
`1db9a8f cmp %r12,%r14` / `1db9a92 jb`, `1db9aa6/ab`, `1db9ab0/b5`, `1db9ac7/aca/ acd`,
`1db9acf mov %bl,0x7cdede0`; `r12 = [0x7cdedd8] = 0x20` from `1db9a79`). `1dbe4fc` is a
boolean validity predicate (`1dbe500 call 1dbe50d`, `1dbe505 test %rax,%rax`, `1dbe508 setne %al`)
checking mimalloc PRIVATE arena globals `[0x7cd8ff8]` low bound, `[0x7cd9048]` commit bitmap,
`[0x7cd9040]` page table (`1dbe514/51d/52b/531/53f/551`). `1dbdfba` is BRIDGED to glibc
`malloc_usable_size` (`bootstrap_profile.h:63 SOBER_BRIDGE_USABLE_SIZE_RVA 0x1dbdfbau`,
`bionic_bootstrap.c:892-893`, in-place trampoline `:855-861`, `host_allocate :271-277` plain
`malloc(n)` no registration, `host_usable_size :532-551`), but `1dbe4fc` is NOT in the bridge
list (`:62-67`) and host_allocate never populates those globals ⇒ (A)/(C) fail ⇒ bl stays 0.
PROVEN by construction; runtime values UNPROVEN by static-A.

**harness #704 — all quoted claims verified:** SHA+banner 3/3; `[N8P_ARM] base=0x7f117a250000
fds=4/5/6/7/-1/-1 rc=0/0/0/0/-1/-1` — 4/6 armed (MALLOC 0x1dbad62, USABLE 0x1dbe4fc,
ENDP 0x1dbdfba, CMP1 0x1db9a8f); CMP2 0x1db9ac7 and STORE 0x1db9acf FAILED (perf slot limit).
Counts/run: MALLOC 4346, USABLE 1, ENDP ~72k, CMP1 **0/0/0**, CMP2/STORE 0 (unarmed). First
ctor3-attributable hits after `[N8_CTOR3] before`: MALLOC `rdi=0x20`, USABLE `rdi=alloc base`,
ENDP `rdi=same base`; final `[0x7cdede0]=0x00` 3/3. **Control-flow conclusion (PROVEN among armed
bps):** CMP1 zero hits with final byte 0 ⇒ `je` at `1db9a8d` was taken ⇒ `r15b` (usable-size
return low byte) was ZERO ⇒ the FIRST check zeroes `0x7cdede0`, before either `cmp r14,r12`.
This independently confirms entry 47's "no benefit" A/B result and static-A's construction argument.
UNPROVEN: exact numeric usable-size/end-pointer returns (entry bps only), CMP2/STORE hits (unarmed).

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n8-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 68 self-matches (ledger's own recorded scan commands + verification-notes in 3 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n8w2. Marker WITHHELD.

## Overnight n8 WAVE3 audit (task #709) — mimalloc globals: runtime confirms unregistered pointer

Owner: audit (`/tmp/opencode/n8-audit`, branch `n8-audit`). Cores 6-7, MANDATORY. READ-ONLY, no device/runs; no questions.

SHA+banner verified on EVERY run: `verified … sha256=8215519f…` = 1 and
`[N6_BANNER] SOBER_BIONIC_SF_FULL=1` = 1 in each `n8g_{1,2,3}.err` (3/3); rc=139 3/3;
fault int3 `0x6caef66` (RIP `0x6caef67`), HardAssert, signal=5.

**static-A #708 — all quoted disasm verified (several by direct objdump / harness source):**
Three mimalloc-private globals — `0x7cd8ff8` (arena low-bound MASK) sole writer `1dbb91d mov %rdx`
(value computed `1dbb906 mov $1,%eax`, `1dbb90b shl %cl,%rax`, `1dbb912 mov $0xffffffffffff0000,%rdx`,
`1dbb919 cmovb`), reader `1dbe514 cmp %rdi,0x7cd8ff8` / `1dbe51b jbe ret 0`; `0x7cd9040` (page-table
BASE) sole writer `1dbb9a2 mov %rax` (`1dbb99d call 1dbbad9`), reader `1dbe531`/`1dbe53f` →
`1dbe543 test; je ret 0`; `0x7cd9048` (commit BITMAP) two writers `1dbb9fc mov %rcx` and
`1dbc2b6 lock or %r15`, reader `1dbe51d`/`1dbe52b bt (p>>41)`. Writers live in arena-init `0x1dbb178`
and a commit fn, NOT ctor entries (nearest ctor[1] `0x1dba8f3` SKIPPED); written by explicit host call
`bionic_bootstrap/bionic_bootstrap.c:1150 BDEBUG("replaying arena init…")`, `:1155 ((void(*)(void))
(base + SOBER_ARENA_INIT_RVA))()`; raw log `ab_off_1.err:22 arena_table@0x7cd9040=(nil)` →
`:31 = 0x7fb949753000`. `1dbe4fc` has exactly 2 call sites (both ctor3: `1db9a66`, `1db9aa6`),
`1dbe50d` only from `1dbe4fc` (`1dbe500`), no indirect/data/jmp — tiny blast radius, a trampoline
(`bionic_bootstrap.c:855-861`) is mechanically safe; BUT forging a nonzero return for glibc pointers
(not in bb_reg) would SYNTHESIZE "mimalloc detected" — semantic fake, gate/DEBT policy, not a static
blocker. REPORT ONLY, no apply.

**harness #707 — all quoted claims verified:** `[N8G AFTER_CTOR3] [7cd8ff8]=0x0000800000000000
[7cd9048]=0xffffffffffffffff [7cd9040]=0x00007f37a01ab000 alloc=0x0000560b82a97df0 bit41=43
bitmap_bit=1 ptidx=0x2b05c ptword=0x0000000000000000`, `[N8G AT_FAULT]` byte-identical. Audit
recomputed all derived values: `(p>>41)&63 = 43`, `p>>29 = 0x2b05c`, `p < 0x800000000000` = True.
Two independent rejections: alloc pointer BELOW arena low bound (`1dbe514 cmp/jbe ret 0`) AND
page-table word at `ptidx` is ZERO (`1dbe531/53f test; je ret 0`); commit bitmap bit IS 1 — so
registration is missing specifically in the page table. Thus `1dbe4fc(alloc)=0` → `test r15b,r15b`
at `1db9a8a` sees zero → `je 1db9acf` → bl=0 → `0x7cdede0=0` → HardAssert int3. This confirms
wave2's control-flow inference with runtime values and static-A #705's prediction. UNPROVEN: who
populates `[0x7cd9040]` in a real boot; whether a genuine mimalloc runtime would register the pointer.

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n8-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 69 self-matches (ledger's own recorded scan commands + verification-notes in 3 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n8w3. Marker WITHHELD.

## Overnight n8 WAVE4 audit (task #710) — size==0 to HardAssert chain confirmed

Owner: audit (`/tmp/opencode/n8-audit`, branch `n8-audit`). Cores 6-7, MANDATORY. READ-ONLY, no device/runs; no questions.

SHA+banner verified on EVERY run: `verified … sha256=8215519f…` = 1 and
`[N6_BANNER] SOBER_BIONIC_SF_FULL=1` = 1 in each `n8e_{1,2,3}.err` (3/3); rc=139 3/3;
fault int3 `0x6caef66` (RIP `0x6caef67`), HardAssert, signal=5.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 48 | **RETRACTED (refined): n7w4 static-A #697 "no static edge from size()==0 to HardAssert". The edge EXISTS via worker return + wrapper unconditional tail-jump — held only inside the worker `0x2383c84`** | static chain `2383c6d call 2383c84` → `2383d42 jle 23844e9` → `238450a ret` → `2383c72 call 23847db` → `2383c7f jmp 239d1c7` (unconditional tail-jump into same-ELF-symbol prologue block) → printer preds `239d205 je 239d215`/`239d743 je`/`239d760 jmp` → `239d215 call 239ead3` → `239eb14 cmpb $0,0x7cdede0` → `239eb1b je 23a1422` → `23a1424 je 23a1465` → `23a146e call 6caed5d` → int3; runtime n8e_1.err 3748-3751 | static-A #711, audited (harness #712 runtime confirms) | Prior "no edge" was true only within the worker function; the wrapper's tail-jump bridges it. Runtime order independently observed | Remove when a raw line shows an alternate entry to the printer (invalidating the tail-jump chain) |

**static-A #711 — all quoted disasm verified verbatim:** chain above, all addresses present in saved
files (`dis_552_full.txt`/`dis_552_body.txt`/`raw_cfg_chain.txt`); `239d1c7: push %rbp` is a separate
prologue block in the same ELF symbol (nm: only `2383c18`). Printer predecessors `239d205/239d743/
239d760` all target `239d215`. UNPROVEN: which of the 3 predecessors / gate globals selects the path.

**harness #712 — all quoted claims verified:** `[N8E_ARM] base=0x7f03448c4000 fds=4/5/6/7/-1/-1/-1
rc=0/0/0/0/-1/-1/-1` (4/7 armed: SIZE_RET 0x238463c, AFTER_SIZE 0x2383d42, PRINTER 0x239d215,
FLAG_TEST 0x239eb14; FLAG_BR/AL_TEST/ASSERT_CALL FAILED perf slot limit). Order 3/3 identical on
tid 2756178: 3748 `[N8E SIZE_RET 238463c] rax=0 ZF=1`, 3749 `[N8E AFTER_SIZE 2383d42] rax=0 ZF=1`,
3750 `[N8E PRINTER 239d215] hit=1`, 3751 `[N8E FLAG_TEST 239eb14] hit=1`, then HardAssert + W30_FAULT.
`jle 0x23844e9` TAKEN (ZF=1 directly logged) ⇒ normal early return, NOT a throw — RETRACTS any
"size()==0 raises exception" claim; fatal path reached LATER via printer → flag test → int3.
`je 0x23a1422` inferred from zero flag byte + static decode (FLAG_BR bp unarmed) — caveat recorded.

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n8-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 70 self-matches (ledger's own recorded scan commands + verification-notes in 3 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only (afed56d), not introduced by n8w4. Marker WITHHELD.

## Overnight n9 WAVE1 audit (task #715) — ledger-cited files DELETED; SHA unverifiable

Owner: audit (`/tmp/opencode/n9-audit`, branch `n9-audit`). Cores 6-7, MANDATORY. READ-ONLY, no device/runs; no questions.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 49 | **LEDGER-CITED FILE INTEGRITY: FAIL. At wave start all 46 unique absolute paths cited in this ledger were MISSING after the n8 disk emergency cleanup — including authoritative inputs `w11-arg0.json`, `auth-base.apk`, `w15-flags-1.json` and all n1-n8 / w18-w32 worktree files. Quoted raw lines survive inline in ledger text, but cited originals could not be re-opened. MID-WAVE PARTIAL RESTORE: `/tmp/opencode/auth-base.apk` restored (97621690 B, SHA `114a6f8ed6f0f…` verified by audit) and guest engine restored from basalt; `w11-arg0.json` STILL MISSING** | audit script `grep -oE '/tmp/opencode/[A-Za-z0-9._/-]+' analysis/FABRICATION-LEDGER.md | sort -u` → 46 paths, `test -e` → 0 exist at wave start; named files: `/tmp/opencode/w11-arg0.json`, `/tmp/opencode/w15-flags-1.json`, `/tmp/opencode/auth-base.apk`, `/tmp/opencode/w25-phone/base.apk`, `/tmp/opencode/w18-audit/audit-notes.md`, `/tmp/opencode/w19-harness/report.md`, `/tmp/opencode/w29-harness/report.md`, `/tmp/opencode/w30-static/report.md`; audit reverified `auth-base.apk` exists with SHA 114a6f8e; `find /home/user /tmp -size 1533221c` → NO file of authoritative arg0 size | audit (lead notified) | The task required confirming no cited file was deleted; the opposite was true at wave start. Evidence quotes preserved in-ledger but most originals cannot be re-opened | Remove when all cited files are restored from backup or the ledger is rewritten to rely only on surviving sources |

**SHA on every run — NOT MET (harness correctly RETRACTED the expectation):** `grep -c 'avd-boundary] verified'` in `n9off_1/2/3.err` = 0/0/0; raw line `[avd-boundary] open failed: /tmp/opencode/w11-arg0.json` in all 3 — the authoritative input file (entry 49) is gone, so no boundary SHA can be verified. The provenance line naming `sha256=8215519f...` and `[N6_BANNER] SOBER_BIONIC_SF_FULL=1` are present, but the `verified ... bytes sha256=...` line is absent. **No success claim of SHA verification is made for this wave.** Lead confirmed: no local copy of the 1,533,221 B SHA-8215519f input exists; the basalt alternative `/home/user/.local/share/basalt/linux/basalt-mocktail-state/cache/clientsettings/GoogleAndroidApp.json` is 1,529,539 B SHA `466c644fe1756d47b81b…` — AUDIT VERIFIED, but different. Per lead, any Run D with the open-failed input is DIAGNOSTIC ONLY, no rung credit. A SHA-matching guest engine copy survives at `/home/user/.local/share/basalt/linux/payloads/3170-204ad049.../libroblox.so` (120041712 B, verified) — that is the engine SHA `3e498d649421...`, NOT the w11-arg0 boundary SHA.

**Harness startup failure (earlier n9_*.err, NOT credited):** three 177-byte logs rc=3: `[harness] guest: /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` / `[harness] fopen guest: No such file or directory` — the old engine path was deleted. Harness copied the SHA-matching engine to `n9bin/` and re-ran (flagged to lead/harness before their report); no old-path run credited.

**Faulting instruction first (n9off, PROVEN 3/3):** guest `int3 0x6caef66`, reported RIP `0x6caef67` (`89 c0 mov eax,eax`; `e8 call abort@plt`), signal=5, si_addr=0, rc=139. With allocator patch OFF the fatal message CHANGED to `RBXCRASH: FatalRuntimeError (Can't initialize the TaskScheduler before flags have been loaded)` (3/3), not HardAssert; `[N8G AT_FAULT]` shows all three mimalloc globals ZERO (`[7cd8ff8]=[7cd9048]=[7cd9040]=0x0`, `alloc=0x0`). `[N9_ALLOC_PATCH] OFF (default); guest allocator unpatched; 1dbe4fc NOT bridged` (3/3); ledger exact text **"harness patched guest allocator, defect removal"**, DEBT=1. `1dbe4fc` bp (`[N9_PTR_ARM]`) armed but 0 hits with ctor3 OFF → no caller RVA (UNPROVEN). STOP honored.

**static-A #713 — all quoted claims verified** (caller-chain lines and thunk bytes re-verified by direct objdump on the surviving SHA-matching engine): `1dbe4fc` has exactly 2 callers, both ctor3 — `1db9a56 mov $0x20,%edi` / `1db9a5b call 1dbad62` → `1db9a66 call 1dbe4fc` (p1); `1db9a94 mov 0x7cdedd8,%rdi` / `1db9a9b call 1dbab02` → `1db9aa6 call 1dbe4fc` (p2). Pointers come from in-engine mimalloc thunks (`1dbad76 call pthread_getspecific@plt`, `1dbab02 → 1dbab44`), not libc malloc imports (`objdump -T`: only `mmap`/`madvise` UND, no malloc/free/realloc/calloc). Eight patched bridge RVAs verified from `bootstrap_profile.h:62-67,71,74` (`0x1dbad62`, `0x1dbdfba`, `0x227aafa`, `0x1dbab02`, `0x1dd60e5`, `0x1dbe559`, `0x253225e`, `0x231f0e2`), all mapped to host allocator shims (`bionic_bootstrap.c:271/532/476/519/452/556/404`); ALL EIGHT are allocator entries, NONE is a page-source (`mmap`/`madvise`) bridge — consistent with n8 #708/#705. Literal `mi_*` names UNPROVEN.

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n9-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 81 self-matches (ledger's own recorded scan commands + verification-notes in 3 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log` fixtures only. Marker WITHHELD.

## Overnight n9 WAVE2 audit (task #718) — restoration status + affected claims

Owner: audit (`/tmp/opencode/n9-audit`, branch `n9-audit`). Cores 6-7, MANDATORY. READ-ONLY, no device/runs; no questions.

**Ledger-cited files — recheck after wave2 restorations:** re-extraction yields 49 unique absolute
paths; 27 now EXIST (restored mid-wave), 22 remain MISSING. Restored and verified by audit:
`/tmp/opencode/auth-base.apk` (97621690 B, SHA `114a6f8ed6f0f974…`), `/tmp/opencode/w15-flags-1.json`
(78 B, SHA `4d50a0ec2eb3f10a8…`), guest engine `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
n1-n5 audit worktrees, w18-w32 audit worktrees, `w29-harness/report.md`, `w30-static/report.md`.
Remaining MISSING (22): 18 glob-prefix/dir entries (e.g. `/tmp/opencode/n1-`, `/tmp/opencode/w28-`,
`/tmp/opencode/uploads`, `/tmp/opencode/n2-dex/`) and 4 named files:
`/tmp/opencode/w11-arg0.json`, `/tmp/opencode/w18-audit/audit-notes.md`,
`/tmp/opencode/w19-harness/report.md`, `/tmp/opencode/w25-phone/base.apk`.
`find /home/user /tmp -size 1533221c` → no file of authoritative arg0 size; no `w11-arg0.json` copy.

**Claims marked UNPROVEN (NOT deleted, per instruction) — originals unavailable for re-verification:**
- **Entry 22 (W29 leaked-credit finding)** — evidence glob `/tmp/opencode/w2[2-8]-harness/*.err`:
  all w22-harness … w28-harness directories MISSING. The 88 `[avd-boundary]` lines / 16 rung1 PASS
  citations survive only as inline ledger quotes; no raw log can be re-opened. Status: **UNPROVEN as
  re-verifiable** (finding was quoted-verified at the time; not retracted, not deleted).
- **Line 419 wording** "authoritative `w11-arg0.json` remains 1,533,221 B / SHA `8215519f…`" —
  file gone, no copy; **UNPROVEN** (size/SHA cannot be re-measured; provenance line in logs is a
  hardcoded constant, not verification).
- **w18 line 74** "Full redacted notes: `/tmp/opencode/w18-audit/audit-notes.md`" — file MISSING;
  claims sourced only from that file are **UNPROVEN as re-verifiable**.
- **w19 line 149** "Final re-verification after harness19 #589 reported (`w19-harness/report.md`,
  scripts `memread.py`/`memread2.py`, `stop.err`, source `native_jni.c:1416-1429`)" — report file
  MISSING; those specific log/script citations are **UNPROVEN as re-verifiable** (source `.c` survives).
- **w25 line 323** `/tmp/opencode/w25-phone/base.apk` — already recorded in-ledger as "cleaned up
  (out of git), cannot be re-audited locally"; no change (consistently UNPROVEN).

**Wave1 quotes re-verified again (all still present):** static-A `1db9a66/1db9aa6 call 1dbe4fc`
(two ctor3 callers), fault `0x6caef66 int3`; harness `[N9_ALLOC_PATCH] OFF (default)`,
`[N8G AT_FAULT]` all globals zero, `[W28_FAULT_INSN] … guest_rva=0x06caef67`,
`RBXCRASH: FatalRuntimeError (Can't initialize the TaskScheduler before flags have been loaded)`,
`[avd-boundary] open failed: /tmp/opencode/w11-arg0.json`.

**SHA per wave1 run — reconfirmed diagnostic-only:** `grep -c 'avd-boundary] verified'` in
`n9off_1/2/3.err` = 0/0/0 (verified=0, provenance=1, openfail=1 each). Per lead, Run D with
open-failed input is DIAGNOSTIC ONLY, no rung credit. No SHA-verified-input success claim.

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n9-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 112 self-matches (ledger's own recorded scan commands + verification-notes in 3 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only. Marker WITHHELD.

### n9w2 static-B #717 — allocator patch semantics, message-switch RETRACTED

**static-B #717 — all quoted claims verified:** eight patched bridge RVAs (`bootstrap_profile.h:62-74`
`0x1dbad62/0x1dbdfba/0x227aafa/0x1dbab02/0x1dd60e5/0x1dbe559/0x253225e/0x231f0e2`) all allocator
entry points; `install_bridge` (`bionic_bootstrap.c:838-873`) overwrites 12 B with
`movabs rax,imm64 ; jmp rax` to host `g_bb_ops` → `host_allocate` (`:274` plain glibc `malloc(n)`),
weak `mi_*` symbols dead. Control at `:1606-1613`/`:1638-1644`: default OFF, opt-in
`SOBER_ALLOCATOR_PATCH=1`, kill-switch note "8 patches all allocator entry points, never page source;
do not make 1dbe4fc see registered pointer; 1dbe4fc NEVER bridged"; OFF log `n9off_1.err:19`.
Two fatal messages share dispatcher: `6caed5d → 6caed7c → int3 6caef66` — mimalloc message
`23a1465 lea #4c0d2b,%rsi` / `23a146c xor %edi,%edi` (idx 0, reached via printer `239d215` →
flag `0x7cdede0`); TaskScheduler message `23862b0 cmpb $0,0x7cdd149` followed by `23862b7 je 2386588`
(flags-latch byte == 0 selects TaskScheduler path), `2386588 lea #36a389,%rsi`, `238658f mov $6,%edi`,
then `2386594 call 6caed5d` — SAME dispatcher as the mimalloc message. **RETRACTED: any ON/OFF message switch** — audit verified line 3717 in BOTH
arms (`n9_1.err:3717` patch ON, `n9off_1.err:3717` patch OFF) reads
`RBXCRASH: FatalRuntimeError (Can't initialize the TaskScheduler before flags have been loaded)`.
No runs/device/writes/gate/apply; 1dbe4fc not bridged.

### n9 WAVE3 audit (task #720) — latch=0 in both configs; all runs diagnostic-only

Owner: audit (`/tmp/opencode/n9-audit`, branch `n9-audit`). Cores 6-7, MANDATORY. READ-ONLY, no device/runs; no questions.

**harness #721 — all quoted claims verified against surviving logs (NO new runs):**
Faulting instruction first `guest+0x6caef67` (int3 `0x6caef66`, bytes `89 c0 e8 62`, signal=5,
si_addr=0, rc=139) 6/6. `[N7_FAULT_BYTES] [0x7cdd149]=0x0000000000000000 [0x7cef650]=0x0000000000000000`
at line 3720 in ALL SIX logs — tids 415380/415922/416825 (patch ON `n9_*`) and
422575/423081/423844 (patch OFF `n9off_*`); audit verified each line. **LATCH IS ZERO IN BOTH
CONFIGS** — earlier waves reported latch=1 at fault, but `/tmp/opencode/n8-harness/` and all
`n8*_*.err` are GONE (audit verified: directory MISSING, 0 logs), so those earlier latch=1 /
mimalloc-HardAssert claims are **RETRACTED as unverifiable from surviving evidence** (values remain
only as inline quotes in earlier ledger text — not deleted). All other latch lines also zero:
`[flags-latch] route=none (inert; no guest call, latch=0, withhold=0)`,
`[app-shell] flags-latch module -> rc=-2 latch=0`, `[app-shell] crashpad-return module -> rc=1
latch=0` (6/6 each). Fatal message 6/6 `RBXCRASH: FatalRuntimeError (Can't initialize the
TaskScheduler before flags have been loaded)` at line 3717; no mimalloc HardAssert survives in
these logs (consistent with static-B #717 message-switch retraction). Allocator-patch banner
`[N9_ALLOC_PATCH] OFF (default); guest allocator unpatched; 1dbe4fc NOT bridged` only in n9off
(patch-ON runs have no banner by design). ctor3 replay OFF in all 6 (`grep -c N8_CTOR3` = 0).

**SHA per wave1 run — reconfirmed diagnostic-only:** `grep -c 'avd-boundary] verified'` = 0/0/0/0/0/0;
instead `[avd-boundary] open failed: /tmp/opencode/w11-arg0.json` 6/6 at line 3705. Per lead,
runs with open-failed input are DIAGNOSTIC ONLY, NO rung credit; any "SHA 8215519f verified" claim
from these logs would be UNSUPPORTED. Audit made no rung claim and deleted no file.

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n9-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 113 self-matches (ledger's own recorded scan commands + verification-notes in 3 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only. Marker WITHHELD.

### n9w3 static-A #719 (addendum) — flags-loaded latch writers

All quoted claims verified: exactly six writers of `0x7cdd149`, all `movb $0x1`, in
`raw_all_7cdd149.txt` — `2eb7520` (fn `2eb7155`, caller `235933d` in nativeInitClientSettings
`2359277`), `2ec9fae` (fn `2ec9eed` Crashpad), `56d31a3`/`56d3386` (fn `56d2d35` dismissAll),
`56d6a4b`/`56d6b80` (fn `56d6779`); 2269 other refs are reads. Host gates
`flags_latch_default.c:73-75` (route 1 nativeInitClientSettings → `2eb7155` → `2eb7520`, skipped
because parse-ctx `0x7697d08 == 1` in default state, latch stays 0) and `:91-95` (route 2 Crashpad
`2eca720` → `2ec9fae`, gated by channel `0x72a3710/0x72a3718` from genuine String[] argv). Harness
`n9_1.err:3695 [flags-latch] route=none (inert; no guest call, latch=0, withhold=0)` 6/6. Capture
`0b970` ×6 order: `nativeInitClientSettings` → `Exception=HttpError:InvalidUrl` (flags FAILED) →
`nativePostClientSettingsLoadedInitialization3` (#552) → `FatalRuntimeError (Can't initialize the
TaskScheduler before flags loaded)`; writer precedes #552, guard `23862b0`/`je 23862b7` inside #552.
UNPROVEN: 56d* reach/gates. Consistent with wave3 latch=0 finding.

### n9 WAVE4 audit (task #723) — TaskScheduler prerequisites + gate correction

Owner: audit (`/tmp/opencode/n9-audit`, branch `n9-audit`). Cores 6-7, MANDATORY. READ-ONLY, no device/runs; no questions.

**Faulting instruction first (unchanged, from surviving logs):** guest int3 `0x6caef66` (RIP
`0x6caef67`), signal=5, si_addr=0, rc=139; `[N7_FAULT_BYTES] [0x7cdd149]=0` 6/6; fatal message
`RBXCRASH: FatalRuntimeError (Can't initialize the TaskScheduler before flags have been loaded)` at
line 3717 in all 6. All runs remain DIAGNOSTIC ONLY (SHA 0 verified, open-failed input), NO rung credit.

**static-B #722 — all quoted disasm verified:** TaskScheduler setup `0x2385fa6` config init +
allocators (`2386269 call 1dd5ee7`, `23862a0 call 2a15dd0`); guard `23862b0 cmpb $0,0x7cdd149`
/ `23862b7 je 2386588`; reporter `2386588 lea #36a389,%rsi`, `238658f mov $6,%edi`,
`2386594 call 6caed5d` (same dispatcher). Prereqs: (1) latch writers static-A #719, route
`route=none` latch=0 6/6; (2) scheduler prime `guest_app_shell.c:1176-1177 install`,
`:1897-1901 on_attach FunctionMarshaller` opt-in, `2a132c0/2a0daa0/2a0daf7` UNPROVEN (A06 gone);
(3) MessageBus prime `24a8e44(4)` before `0x24abf96` (task507:32,59-70). Capture `#727 0x247dc8d`,
`#734 0x24abf96`, `#736 0x25448f6` from guest_app_shell.c; numbering/order UNPROVEN (capture logs gone).

**static-A #724 — all quoted disasm verified (several by direct objdump on surviving engine):**
gate fn `0x2eb7155` — `2eb71a1 movb $0,0x7697d08` (clear), `2eb71de call 235a58c` (logger),
`2eb71e8 cmpb $0,0x7697d08` / `2eb71ef je 2eb72bd` ⇒ byte==0 selects LATCH PATH `0x2eb7520`;
setters `2eb7945/2eb8512/2ee4ad0 movw $0x100,0x7697d08` (low byte 0), byte=1 via parser
`235af5d mov %eax,0x58(%rbx)`, `235a6b6`/`235ab85 movb $1,0x58(%reg)` in `235a58c`. T2_5 trace
'g7697d08=1 → G2 FAILS' (which writer UNPROVEN). Needed for genuine latch: real `2359277` +
valid non-empty jstrings + settings JSON leaving parse_ctx+0x58==0 (JSON UNPROVEN). **`#719 CORRECTION`:**
56d* DO have static callers — audit verified by direct objdump `2eb8296 call 56d2d35` and
`56d6765 call 56d6779`; the earlier "no direct callers" was too strong (header says no genuine
CppProxy route). Latch=0 in harness explained by `route=none` 6/6.

**Secrets-scan command + output:**
`grep -rniE '(Cookie:[[:space:]]*[^"]{8,}|Authorization:|Bearer +[A-Za-z0-9._-]{12,}|eyJ[A-Za-z0-9_-]{10,}\.|-R[0-9]{4,}-[A-Za-z0-9]{8,}|[A-Za-z0-9._%+-]+@(roblox|gmail|yahoo|hotmail|outlook)[A-Za-z0-9.-]*)' /tmp/opencode/n9-*/ --include='*.md' --include='*.txt' --include='*.err' --include='*.log' | head -20`
→ OUTPUT: 114 self-matches (ledger's own recorded scan commands + verification-notes in 3 worktrees); no credential values.
**Git:** no flag*.json / token / .pcap / .dump / w11-arg0 / core.* tracked; pre-existing FLog `.log`
fixtures only. No file deleted by audit. Marker WITHHELD.

## Weekend n10 WAVE1 audit (task #727) — evidence preservation (COPY only)

Owner: audit (`/tmp/opencode/n10-audit`, branch `n10-audit`). Cores 6-7, MANDATORY. READ-ONLY re evidence; no device/runs; no questions. No deletions, no network.

| # | Entry | Where | Owner | Why | Removal condition |
| - | ----- | ----- | ----- | --- | --- |
| 50 | **Evidence preservation: 9 surviving cited artifacts COPIED (never moved) to `/home/user/sober-evidence/` with verified SHA-256 manifest `MANIFEST.sha256`; all sources remain intact at original paths** | `/home/user/sober-evidence/` — files + manifest below; `sha256sum -c MANIFEST.sha256` → 9/9 OK | audit (owner) | Prior waves lost cited files to cleanup; snapshot preserves remaining authoritative artifacts for re-verification | Remove when evidence is archived to durable storage with independently verified manifest |

**Copied files (9), SHA-256 from `MANIFEST.sha256` (all `sha256sum -c` OK):**
- `libroblox.so` — `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` (engine, 120041712 B)
- `auth-base.apk` — `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4` (97621690 B)
- `w15-flags-1.json` — `4d50a0ec2eb3f10a8c2c4a987cfc2c3d305e2bac8e0e49f33566b35565296453` (78 B, modified input)
- `w29-harness-report.md` — `953df81ce8c3fbbea8f4e7627ade2e60e7a50ea0cfd0b98319122c98d85fe61b`
- `w30-static-report.md` — `579c1d4790b098c7a651ad30eb2626a9f9097a93928795808efe8a1c085a9dbb`
- `n9_1.err` — `e2063e6b4b889ef30d5e3901affc6db065458a35898fe7aed4c2339e768d300a` (148866 B)
- `n9off_1.err` — `5f8c61195ea785c6504c4c9cc572ea464228f19c7e8e56786a24207fcf4fdbb5` (148885 B)
- `repro-interposed.err` — `afb14264cf65977fdf1be74eca76534ff21d56f861e851f9e2939cc654064b58` (133015 B)
- `LastTest.log` — `5673f046d370c902fc5a597305381020de6bb8afc88ad30aa70427e246bdef3f` (1964 B)

**Sources INTACT:** all 9 original paths re-checked present after copy (COPY never MOVE).
**Evidence dir size:** `/home/user/sober-evidence` = 209 M.
**Disk (`df -h`):** `/dev/nvme0n1p3` 230 G total, 194 G used, **36 G available, 85 %** (same for `/`, `/home`, `/tmp`).

**Cited paths no longer existing (23 of 50):** four named files — `/tmp/opencode/w11-arg0.json` (authoritative input SHA 8215519f, unrecoverable), `/tmp/opencode/w18-audit/audit-notes.md`, `/tmp/opencode/w19-harness/report.md`, `/tmp/opencode/w25-phone/base.apk` — plus 19 truncated-prefix/dir entries (`n1-`, `n2-`, `n2-dex/`, `n3-`, `n4-`, `n5-`, `n7-`, `n7-audit`, `n8-`, `n8-audit`, `n8-harness/`, `n9-`, `uploads`, `w2`, `w20-harness/run`, `w28-`, `w29-`, `w30-`, `w32-` under `/tmp/opencode/`). No file was deleted by audit.

### n10w1 harness #725 (addendum) — fail-closed input gate verified

All quoted claims verified. Baseline `baseline.err` (rc=9): `[harness] guest: /tmp/opencode/n9-harness/n9bin/libroblox.so` / `[avd-boundary] INPUT MISSING/MISMATCH (no rung): open failed: /tmp/opencode/w11-arg0.json` / `[checkpoint] rung1 INPUT MISSING/MISMATCH (no rung)`; audit confirmed source gate `guest_dlopen_host.c:1282-1285` (`sober_avd_boundary_fail_closed()` before memfd copy, exit 9), `avd_boundary.c:95` `g_n10_arg0_sha_hex` published only after open+hash, `:218-219` hardcoded SHA banner removed (was the misleading provenance constant), and both rung printers fail closed on `!g_w29_arg0_sha_ok`. Ledger: host-only input gate, owner=harness, DEBT=1, gate/withhold unchanged; removal condition = genuine 1,533,221 B SHA-8215519f input available again.

**static-A #728 (DEX settings fetch, READ-ONLY, no network) — quotes verified:** endpoint string
`STR 30766: 'https://clientsettingscdn.'` and `'https:///v2/settings-compressed/application/
ca4e…dcz'` (empty host in capture); request builder `raw_request_builder.txt` adds only
`Pragma: akamai-x-cache-on` before `Request$Builder->build` (no auth header on this path);
`raw_dex_hits.txt` shows `FLAG_CACHE_COMPRESSION_TYPE_ZSTD_WITHOUT_DICTIONARY`,
`preload_zstd_dictionary`, `responseBodyZstdCompressed`, `X-Rbx-Zstd-Dict-SHA`;
`X-Signature-Ed25519` is response verification. **Minor quote caveat:** the report's phrasing
"I scanned every method of `Lcom/roblox/client/startup/b;` … zero hits" is NOT backed by a
verbatim scoped line in `raw_auth_strings.txt` — that file lists auth strings in OTHER classes
(`bg/c`, `uk/j`, `ci/s`) but has no `startup/b` section; the substantive "no auth header added
on this path" claim IS supported by the request-builder quote above. Scoped "zero hits" wording
marked UNPROVEN as quoted (not retracted); exact runtime domain UNPROVEN; cookie-jar UNPROVEN.

A/B (allocator patch OFF x3 / ON x3, modified-input opt-in `SOBER_N10_MODIFIED_INPUT=1` default OFF): all 6 audit-verified verbatim `[avd-boundary] MODIFIED-INPUT (no rung): diagnostic opt-in SOBER_N10_MODIFIED_INPUT=1; sha256=4d50a0ec… want=8215519f…`; `PASS-BOUNDARY-REPLAY` count 0 in all 6; rc=139 6/6; fault `guest+0x06caef67` int3 `0x6caef66`; latch `[0x7cdd149]=1 [0x7cef650]=0` 6/6 (latch=1 because boundary replay now RUNS with modified input, unlike n9 input-missing latch=0); `0x2eb7520` fires 6/6 with `guest_RA=0x2359342`; `N14_REPLAY 3/3` 0/6; patch banner only in OFF arm; no A/B behavioral difference. All runs DIAGNOSTIC ONLY, NO rung credit. static-b #726 quotes verified: CHECKPOINT-LADDER.md:20-26 rung2 order (#727/#734/#736) and surviving raw_capture J2N #67/#70/#105/#534/#541/#551/#552/#612; rung2 credit RETRACTED, #736 impossible without authoritative input.
