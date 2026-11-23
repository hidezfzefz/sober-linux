# W30 audit verification notes (worktree w30-audit)

Engine SHA `3e498d64`. NEW RULE: every success claim must QUOTE its raw log line.

## Secrets scan (command + output) — see final report
Credential-value regex over /tmp/opencode/w30-*/ → expected self-matches only.

## Git / branches
No capture/phone/logcat tracked files; w30 worktrees each on own branch.

## RECHECK of W29 harness report (task requirement) — with quoted lines

PROVEN (each with quoted raw line):
1. Source defs: quote `avd_boundary.h:16 #define SOBER_AVD_BOUNDARY_PATH "/tmp/opencode/w11-arg0.json"`,
   `:18 UNVERIFIED`, `:19 PATH_ENV`, `:20 SHA256 8215519f…`; `avd_boundary.c:102-104` consumed.
2. Modified-input exports: quote `w20-harness/memrun.sh:7 export SOBER_AVD_BOUNDARY_PATH=/tmp/opencode/w15-flags-1.json`,
   `w22-harness/runbatch.sh:9 ...`, `w29-harness/runall.sh:15 ...`.
3. Full-JSON runs: quote `full_on_1.err:3703 [avd-boundary] verified /tmp/opencode/w11-arg0.json bytes sha256=8215519f...`
   and `:3704 [app-shell] AVD boundary pre: arg0_len=1533221 ...`.
4. (b) TSD trace: quote `tsdtrace2.err:3706 [W29_TLS 0x2315556] ... key=3 ... eq=1`,
   `:3727 ... tls=0x55555721ec90 ... [seed]=0x7265502f666f7270 eq=0`;
   `seedoff_trace.err:3701 [app-shell] W29 TSD seed OFF`, `:3706 ... tls=0x55555568b230 [tls]=0x1`.
5. (c) seed-OFF fault: quote `seedoff_pairs_1.err:3763 [W28_FAULT_INSN] ... guest_rva=0x02a13354`
   and `:3762 signal=11 ... si_addr=0x00007fffff7feff8`, 3/3; pairs W28_POST2315 #1-#8 in each of 3 runs.
6. (d) host fault + fix: quote `native_jni.c:182-188` with `if (!w21_base) return 0;` at :187.
7. (e) seed-ON table: quote `full_on_*.err [W28_FAULT_INSN] ... guest_rva=0x06ca229c` 3/3.
8. RETRACTED section: quote `full_{on,off}_*.err`: NO init/frame strings (they are in *.out only, e.g.
   `full_off_1.out`); NO W21_LATCH_HIT; `13x [checkpoint] rung1 FAIL (DEBT=1)`.

UNSUPPORTED claims in W29 harness report (to flag):
A. "w28-harness/rund3.sh" cited as modified-input export — FILE DOES NOT EXIST (w28-harness has only
   run6.sh). Other script citations (memrun.sh:7, runbatch.sh:9, runall.sh:15) are valid.
B. Table seed-OFF row attributes the masked fault to `full_off_*.err`; THOSE LOGS CONTAIN NO FAULT LINES.
   The masked-fault evidence is core.1529212 (verified last wave: tid 1529212, rip 0x5555555690b0,
   r13=si_addr=0x7fffff7fefc0) from coretry, not the full_off runs. Attribution unsupported.

## Agent claim verification (W30)

### static #635 — VERIFIED (all quoted lines match raw files exactly)
- Quote `'[W29_TLS 0x6ca228b] tid=1521493 key=3 tls=0x55555568b230 [tls]=0x1 ... seed=(nil) [seed]=0 eq=0'`
  → found verbatim in w29-harness seedoff_trace.err. Quote `'[app-shell] pre-OnLoad TSD key=3
  get=0x55555568b230'` → found verbatim. Guest TLS supplier claim PROVEN.
- 8/8 quotes `'[W28_PRE2315 #1] ... tid=1521155'`, `'[W28_POST6CA2 #1]'`, `'[W28_POST2315 #1]'`
  → found verbatim in seedoff_pairs_1.err (#2..#8 likewise, 3 runs).
- Capture order quotes: J2N #70 Channel(""), #534 Version("2.740.931"), #541 GameGlobalInit(),
  #612 BaseDirs(2 Android dirs) → all found verbatim in raw_capture_order.txt.
- Static terminator (predicate 0x6ca21f4, je 0x6ca2282) PROVEN from disasm prior waves; real-boot
  +0x28 zeroing UNPROVEN (agent states so).

## Pending
harness #636 — no formal W30 report at time of writing (this note is from W30 audit copy).

## Wave31 audit (#639)
- Decode: 0x7265502f666f7270 LE bytes 70 72 6f 66 2f 50 65 72 = "prof/Per" (NOT "profileP"). Fixed in ledger line 494 + w31 section. Static #640 also uses "prof/Per" correctly.
- Ledger entry 27 added: rung1 FAIL = hit not observed, until harness (d) shows observed hit.
- static #640 quotes verified verbatim (in w30-harness/runD_1.err / raw_rund_fflag.txt):
  - disassembly `2ea655a movq $0x1,(%r14)`, `2ea656e call pthread_setspecific` — found in dis_accessor_alloc.txt (tab-formatted).
  - prof string `2290b6 prof/total/scripts` — found in engine_prof_variants.txt.
  - ctor replay `[fflag-default] replayed guest ctor 0x2e62390 ... -> OK` — found at w30 runD_1.err:3705/3687-3688 (report cites 3687; actual 3688 in w31 runD_1.err; minor 1-line citation offset, content verbatim).
  - consumers global-string seed / ensure_flag / AVD boundary pre — found verbatim at w30 runD_1.err:3705/3708/3714.
  - Static correctly marks exact prof/Per store site UNPROVEN (0 engine matches) and ctor chain tail UNPROVEN.
- harness #636 in progress: new W31_ENTRY/W31_OBJ/W31_WRITE_ARM instrumentation added; runD_1.err now shows watch on obj+0x28 and a stack-smashing abort (signal=6) — harness still iterating; report.md still stale w30 copy. AWAIT FINAL REPORT.
- Secrets scan: 6 self-matches, no credentials. Git: no capture/phone tracked.
- Lead/static ledger line added as entry 28 (ctor[0] replay genuine guest code). Verified: w31 runD_1.err:3688 replay, :4007 global-string seed rc=1. Static RETRACTED its earlier global-string-seed absence claim — verified current line 4007 proves it ran.
- Harness final runs: W31_ENTRY/W31_OBJ/W31_WRITE_ARM show [obj+0x28]=0 at accessor entry, but new host-side "stack smashing detected" abort (signal=6, rip=0x7ffff7e6359c libc) from the watch instrumentation — a harness defect to flag if claimed as guest fault. AWAIT FINAL HARNESS REPORT.
