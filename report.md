# n6 #683 — mimalloc anchors, ctor skip, 0x7cef650 writer/assert

Read-only; cores 2-3; no runs/device. Every success claim quotes a raw line.
Evidence: `raw_mimalloc_strings.txt`, `raw_mimalloc_xrefs.txt`, `raw_assert.txt`,
`raw_ctor3.txt`, `source_fflag.txt`.

Faulting instruction first (prior wave, host): `7c8ff: 48 8b 47 08 mov
rax,[rdi+0x8]` = `_IO_fflush+0x9f`, NULL rdi.

## (a) Anchors / containing functions — PROVEN

Quoted rodata (`strings -t x`):
> `4c0d2b Expect to have mimalloc working, but it was not detected!`
> `3a3078 [DFLog::Mimalloc] Mimalloc integration is NOT detected`
> `36a5f0 [DFLog::Mimalloc] Mimalloc integration detected, settings:`
> `2bfdf9 [DFLog::Mimalloc] mi_option_verbose={}`
> `231741 environment option "mimalloc_%s" is deprecated ...`
> `287e15 environment option mimalloc_%s has an invalid value.`

Raw xrefs (quoted):
> `239eb44: lea … # 36a5f0` and `239edb2/239ee23: lea … # 2bfdf9`
> `23a1445: lea … # 3a3078`, `23a1465: lea … # 4c0d2b`
> `6c80008/6c8020a/6c80232: lea … # 231741 / # 287e15`

Containing functions: `0x239ead3` (prologue `55 48 89 e5 … sub $0x58,%rsp`) =
mi_option dump printer, reached by #552: quoted caller `239d215: call 239ead3`
(inside #552 body `0x2383c18..0x23c1200`). The "NOT detected" / "Expect to have
mimalloc working" strings are used in the `0x23a14xx` region; the exact
containing function start was not recovered (no standard prologue found in
`0x239f000..0x23a1450`) — **UNPROVEN**. `mimalloc_%s` env-option handling is in
function `0x6c7ff41` (`55 48 89 e5 … sub $0xb0,%rsp`).

## (b) Ctor reach + decision 008 skip — PROVEN

Quoted source `fflag_default.c:109-114`:
> `{ 3u, 0x1db9a40u, "mimalloc usable-size/allocator feature probe (globals 0x7cdedd8/0x7cdede0); only reader 0x239eb14 selects a DFLog string -> no functional effect" }`

Ctor 3 (`0x1db9a40`) is **SKIPPED** by decision 008 (it is one of the three
`k_skipped` entries; ctors 1/3/4 not required). Raw ctor body writes:
> `1db9a4b: movq $0x20,0x7cdedd8` ; `1db9acf: mov %bl,0x7cdede0`

The only reader of `0x7cdede0` is quoted:
> `239eb14: cmpb $0x0,0x7cdede0` (in printer `0x239ead3`), followed by
> `239eb1b: je 23a1422`, `239eb21: test %al,%al`, `239eb23: je 239eb6b`,
> then `239eb44: lea … # 36a5f0` (a DFLog string).

So the mimalloc feature probe is read only to select DFLog text ⇒
**`mimalloc probe=DFLog-only` is PROVEN** (both source comment and the quoted
disassembly show no functional consumer). The `mimalloc_%s` env-option
functions (`0x6c7ff41`) are **not** the skipped ctor 3; their ctor membership
is UNPROVEN.

## (c) 0x7cef650 writer and assert test — PROVEN

Quoted raw lines:
> writer: `2279a8f: mov %al,0x7cef650` — in function `0x2279a47`, which is
> **init_array ctor index 3602** (from `init-array.json`: `{"index":3602,"rva":"0x2279a47"}`).
> That index is inside decision 008's replay range `[5,3618)` — it is **NOT
> skipped** (the three skipped are 1/3/4/0 per `fflag_default.c`), so it runs.

Assert (quoted `6caef4f..6caef69`):
> `cmpb $0,0x7cdd149` (latch) ; `je 6caef66`
> `cmpb $0,0x7cef650` ; `je 6caef66`
> `call 6d320d0 <abort@plt>`

The assert is a **flag-nonzero byte test** (`cmpb $0` then abort when nonzero),
gated by the latch byte — **NOT** a pointer-range check and **NOT** a version
call. A separate version-style check does exist in the printer
(`239eb03: cmp $0x6,%bl`; `239eb09: test $0xff00,%ebx`), but that is a
different site, on `0x7cdf560`, not the `0x7cef650` assert.

## ADD: mi_option dump printer + ctor — PROVEN/UNPROVEN
Printer = `0x239ead3` (quoted prologue and `239eb44: lea … # 36a5f0`,
`239edb2: lea … # 2bfdf9`); reached from #552 via `239d215: call 239ead3`. It
is **not** an init_array ctor itself (no ctor RVA equals it); whether its
ctor-3 caller chain is reachable when ctor 3 is skipped is UNPROVEN — the
printer is still reached from #552 through a non-ctor path.

## What NOT done
No apply/build, no runs/gdb/perf, no device/AVD/Frida/phone, no new captures,
no guest writes, gate edits, seeder, fake returns or path remaps, no
credentials, no captures/logs/tokens in git.

## First divergence + next step
First divergence: skipped ctor 3 leaves mimalloc flag `0x7cdede0` unset, so the
printer selects "NOT detected"/"Expect to have mimalloc working" DFLog lines;
ctor 3602 still writes `0x7cef650`. Next: harness logs which mimalloc DFLog
line is emitted in Run D.
