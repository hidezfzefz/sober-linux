# W29 #632 — harness (one page)

Worktree `/tmp/opencode/w29-harness`, branch w29-harness. Engine SHA `3e498d64`.
flock held for all runs. No device/guest writes/gate edits; seeder OFF.
Faulting instruction named FIRST; stack RAs are not faults. Evidence:
`tsdtrace2.err`, `seedoff_pairs_*.err`, `seedoff_trace.err`, `full_{on,off}_*.err`, `runall.sh`.

## (a) INPUT FIRST — PROVEN
Default arg0 file is `/tmp/opencode/w11-arg0.json`, set at
`src/jni/avd_boundary.h:16` (`SOBER_AVD_BOUNDARY_PATH`) and consumed at
`src/jni/avd_boundary.c:101-102`; SHA constant at `avd_boundary.h:20`.
W28 Run D overrode it to the 78-byte `w15-flags-1.json` via
`SOBER_AVD_BOUNDARY_PATH` + `SOBER_AVD_BOUNDARY_UNVERIFIED=1`.
W29: default unchanged (already authoritative); added SHA result export
`g_w29_arg0_sha_ok` (`avd_boundary.c`) and the rung printer now emits
`rung1 MODIFIED-INPUT (no credit) DEBT=n` on mismatch/unverified
(`guest_app_shell.c`). Full-JSON runs log `verified ... sha256=8215519f...`,
`arg0_len=1533221`. Ledger entry sent audit.

## (b) TSD trace at 0x2315556 / 0x6ca228b — PROVEN
TSD seed **is active** in Run D: `seed_stub_tsd()` (`guest_app_shell.c:117`)
called from the OnLoad-first path (~:1109) and again (~:1360); key=3,
`[seed+0]=0` initially, later `0x7265502f666f7270` ("profileP" LE — guest wrote it).
With seed ON the live TLS equals the seed for the first two call-pairs (`eq=1`),
then the guest replaces it (`tls=0x55555721ec90 != seed`, `eq=0`).
With seed OFF the live TLS is still `0x55555568b230`, `[tls]=0x1` — **the guest
sets its own TLS value**; no seed required.

## (c) Run D x3, TSD seed OFF (host-only env, modified input, pairs bp) — PROVEN
rc=139 3/3. Faulting instruction **changed**:
`guest+0x2a13354`, bytes `e8 07 ab ff ff` = **`call 0x2a0de60`** — the
TLS-object accessor used by the predicate at `0x6ca21f4` (static #629).
`si_addr=0x7fffff7feff8`. Pair counts `pre2315=8, post2315=8` — **all JNI calls
returned**; no unmatched instance (contrast seed ON: 3 pre / 2 post).

## (d) Host fault resolution — PROVEN + FIXED
`0x555555586e9b` (PIE base `0x555555554000` → file offset `0x32e9b`, W29 build
`0x3329b`) = `sober_w21_boundary_ok()` at `native_jni.c:184`,
`movzx edx, BYTE PTR [rax+0x7cdd149]` with `rax=w21_base=0`.
Cause: `w21_init()` returns early when no watch env is set, leaving
`w21_base=0`; the function dereferenced before its `w21_base &&` check.
Depends on `SOBER_W28_PAIRS`: **yes** — that env sets `w21_base=base`, so the
with-breakpoint path never hit the host fault; the no-bp path did.
Host-only fix applied: `if (!w21_base) return 0;` before the read. Ledger sent.
After the fix, seed-ON no-bp faults in the GUEST (`0x6ca229c`), not the host.

## (e) 2×3 table — FULL captured JSON, NO breakpoints

| seed | rc | faulting instruction | insn bytes |
|---|---|---|---|
| ON (3) | 139 ×3 | `guest+0x6ca229c` `call QWORD PTR [rax+0x548]`, `si_addr=0x548` | `ff 90 48 05 00 00 00 00` |
| OFF (3) | 139 ×3 | crash handler ENTERED but faulted again while dumping (`guest-init-host+0x150b0` = `mov rax,[r13]`, `r13=0x7fffff7fefc0`); primary fault masked. Pairs-bp primary: `guest+0x2a13354` `call 0x2a0de60`, `si_addr≈0x7fffff7feff8` | pairs: `e8 07 ab ff ff` |

**RETRACTED (lead/audit, verified against raw logs):** neither `guest init complete` nor `first frame rendered` appears in ANY `/tmp/opencode/w29-harness/*.err`; there is no `W21_LATCH_HIT` in any w29 log. All 13 full-JSON runs print `[checkpoint] rung1 FAIL (DEBT=1)` despite the verified SHA — the rung requires a real latch hit (RA `0x2359342`), which is absent.

Evidence for the masked no-bp case: `core.1529212`, parsed via `readelf -n` /
NT_PRSTATUS: crashing tid 1529212 `rip=0x5555555690b0` (`guest-init-host+0x150b0`,
inside `crash_handler`), `r13=0x7fffff7fefc0`, NT_SIGINFO `si_addr=0x7fffff7fefc0`
= `r13`, so the handler itself faulted dereferencing that pointer while hex-
printing a ring field. This is a separate host-only defect (ledger entry sent).

## NOT DONE / first divergence
No device, no guest bytes/gate edits; no ledger edit by me; the masked primary
fault in seed-OFF no-bp is inferred from the pairs-bp run (same code path) —
gdb batch could not complete on the 450 MB core (killed). **First divergence:**
seed ON → TLS is the seeded object but is later replaced, and the 3rd
`GetStringUTFChars` call (`0x6ca229c`) faults; seed OFF → guest-owned TLS
`[tls]=1` makes the predicate's `call 0x2a0de60` (`0x2a13354`) fault. Next
smallest step: resolve that accessor's bad target (static scope) and make the
crash handler's ring dump robust against guest-derived pointers (host scope).
