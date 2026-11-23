# WAVE31 #640 — accessor layout / prof-Per / Decision 008 chain

Read-only; no runs/device/phone. Engine SHA `3e498d64…`. Every success claim
quotes a raw line (evidence files: `raw_accessor_lines.txt`, `raw_prof_users.txt`,
`raw_profper_value.txt`, `raw_decision008.txt`, `raw_chain.txt`, `raw_rund_fflag.txt`).

Faulting instruction first (W30 harness, seed OFF + full JSON + no bp):
`guest+0x6ca7f95: call 0x2a13350`, `si_addr=rsp-8` (stack overflow), repeated
frame `0x6ca8014` — quoted in the W30 harness report. Seed ON fault was
`0x6ca229c: call *0x548(%rax)`.

## (a) Accessor `0x2ea6440` / lazy branch `0x2ea656e` — PROVEN allocation/layout

Raw disassembly (`dis_accessor_alloc.txt`, quoted):
> `2ea6528: lea -0x10(,%r12,8),%r15`
> `2ea6530: lea 0x10(%r15),%rdi`
> `2ea6534: call 1dbad62`  (allocator)
> `2ea6555: call 6d31d00 <memset@plt>`
> `2ea655a: movq $0x1,(%r14)`
> `2ea6561: mov %r12,0x8(%r14)`
> `2ea656e: call 6d31d10 <pthread_setspecific@plt>`

It allocates **8 × r12 bytes** (r12 = descriptor slot count; the sibling path
clamps it to ≥8 at `2ea6585: mov $0x8,%r15d`). Layout: `+0x00 = 1`
(refcount/presence marker), `+0x08 = r12` (slot count), `+0x10..` = zeroed
8-byte slots. The `+0x28` of **this state block** is simply a zeroed slot
(index `(0x28-0x10)/8 = 3`) — UNPROVEN that any consumer assigns it here. The
**scheduler object's** `+0x28` (a different object, reached via
`0x2a0de60 → TLS[0x722e540]+0x410`) is the installed-callback word tested by
`0x2a13350: cmpq $0x0,0x28(%rax)` (W24/W28 evidence). PROVEN for the predicate;
UNPROVEN that the accessor state's `+0x28` is the same field.

**"prof/Per" bytes:** runtime value from `tsdtrace.err:3727`:
> `[W29_TLS 0x2315556] ... [seed]=0x7265502f666f7270 eq=0`

`0x7265502f666f7270` LE = `70 72 6f 66 2f 50 65 72` = "prof/Per". A raw
8-byte search of the engine returned **0 occurrences** (`engine_profper.txt`
empty) and `src/` has no such literal (only the analysis note in
`FABRICATION-LEDGER.md:494-495`). Engine `prof/` strings and their users:
> `2290b6 prof/total/scripts`  → used at `23a3257: lea … # 2290b6`
> `2310c3 prof/Physics/Simulation_other_max` → used at `23a2feb: lea … # 2310c3`

No exact-byte match; the guest store site of those 8 bytes is **UNPROVEN**
(no `movabs $0x7265502f666f7270` in the disassembly).

## (b) Decision 008 / ctor[0] `0x2e62390` — PROVEN / partly UNPROVEN

`docs/decisions/008-packed-relocations.md:1`:
> `# Decision 008 — Android packed RELA must be applied before bootstrap`
and line 14:
> `packed relocations. Without applying them, ctor[0] is a NULL call`.

Bootstrap profile skips ctor 0 (`bootstrap-map.md:97`: `| 0 (SKIP) | 0x7220e58 | 0x2e62390 |`). Harness replays it; Raw Run D
`runD_1.err:3687`:
> `[fflag-default] replayed guest ctor 0x2e62390 (FFlag registry): guard=257 (expect 257) handle=0x7fffef9f0008 ctrl=0x7fffefedcb88 -> OK; ctors 1/3/4 not required ...`

So ctor[0] **is active in Run D** (PROVEN by that line). Chain links:
> `2e623a0: call 2e618b0` — PROVEN
> `2e618d8: call 2e61280` — PROVEN
> `2e61298: call 2e5bf00` — the next hop is `0x2e5bf00`, **not** directly `0x2e5bc10`; the remainder of the stated chain (→ `0x2e5bc10` → `0x2e7b850`) is **UNPROVEN** as a direct call chain.

Consumers of FFlag registry `0x72a0520` that ran in Run D (quoted):
> `3708:[app-shell] global-string seed at 0x72a0520 (SOBER_SEED_GLOBAL_STRING, early) -> rc=1`
> `3714:[app-shell] controller ensure_flag -> rc=0 flag=1`
> `3705:[app-shell] AVD boundary pre: ... fflag_ctx=0x7fffefedf1c8 fflag_obj=0x555555c50880`

## What NOT done
No diff/applied change, no runs/gdb/perf, no device/AVD/emulator/Frida/phone,
no new captures, no guest writes, no gate edits, no ledger edits, no credentials.
Exact prof/Per store site and the tail of the ctor chain are UNPROVEN.

## First divergence + next step
First divergence: guest-created TLS state (`[tls]=0x1`) vs seeded value later
overwritten with "prof/Per"; ctor[0] replay succeeds but the recursion
terminator remains unidentified. Next: harness logs the `+0x28` writer with tid.

## FINAL ADDENDUM (lead-appended from static's delivered corrections; read-only)

- **RETRACTED:** the earlier claim that `SOBER_SEED_GLOBAL_STRING` / JNI_OnLoad /
  `nativeInitClientSettings` were absent from w31 Run D. The log was still being
  written when first read (3,791 lines then, 78,945 later). Corrected quoted order
  in w31 `runD_1.err`: `:3688 [fflag-default] replayed guest ctor 0x2e62390 ... -> OK`,
  `:3992 [app-shell] W24 JNI_OnLoad FIRST 0x227af5a -> ver=0x10006`,
  `:4006 [app-shell] AVD boundary nativeInitClientSettings 0x2359277 -> rc=0 ... latch=1`,
  `:4007 [app-shell] global-string seed at 0x72a0520 (SOBER_SEED_GLOBAL_STRING, early) -> rc=1`,
  `:4013 [app-shell] controller ensure_flag -> rc=0 flag=1`. So ctor[0] replay runs
  BEFORE both JNI_OnLoad and InitClientSettings; the global-string seed runs AFTER
  InitClientSettings. Same order in w30 (`3687 / 3702 / 3707 / 3708 / 3714`).
- **Exact #462 reason** (`guest_app_shell.c:1268-1272`, quoted): without the replay
  "the global at 0x72a0520 stays NULL and the worker faults at 0x2e5d42a"; the replay
  is "Opt-in replay of the genuine guest ctor — no byte patches, no fake vtables,
  no callbacks." Critical for audit ledger entry 28.
- What ran BEFORE ctor[0] replay (w31 `runD_1:3680-3687`): TSD GOT rebind, W31 arm,
  `SOBER_SEEDER_DISABLE`, cached-ID seeding, JavaVM install, provenance gate install.
- In w31 the global-string seed DID run (`rc=1`); any claim it wrote then was replaced
  by a later guest ctor write is UNPROVEN — `global_string_seed.c` has "Do not clobber
  a value the guest installed itself." and logs "already non-NULL ... leaving untouched".
