# WAVE32 #641 — wrapper RA, obj provenance, +0x28 writer chains

Read-only; no runs/device. Engine SHA `3e498d64…`. Every success claim quotes
a raw line. I did not read anything under `/tmp/opencode/uploads`.
Evidence: `dis_wrapper_6ca7f72.txt`, `dis_sched_getter.txt`, `dis_constructor.txt`,
`dis_writer1_region.txt`, `raw_wrapper_call.txt`, `raw_obj_chain.txt`,
`raw_writers.txt`, `raw_writer_callers.txt`.

Faulting instruction first (W30 harness, seed OFF): `guest+0x6ca7f95:
call 0x2a13350`, `si_addr=rsp-8`, repeated frame `0x6ca8014`.

## (a) Call with RA `0x6ca8014` — PROVEN

Full wrapper `0x6ca7f72..0x6ca8020` (quoted):
> `6ca800e: ff 90 20 07 00 00   call   *0x720(%rax)`
> `6ca8014: 49 8b 0e           mov    (%r14),%rcx`

The call whose return address is `0x6ca8014` is the indirect call at `0x6ca800e`,
target `JNIEnv fn[224]` at byte offset `0x720` = **ExceptionCheck** (the bridge
`SETFN(*t,224,sr_ExceptionCheck)`). `0x6ca8014` itself is the following canary
check — an **epilogue**, not a call site; the repeated `0x6ca8014` frames are
re-entries of the wrapper reaching that epilogue.

Guard and polarity (quoted):
> `6ca7f95: e8 b6 b3 d6 fb   call   2a13350`
> `6ca7f9a: 84 c0           test   %al,%al`
> `6ca7f9c: 74 5c           je     6ca7ffa`

`0x2a13350` returns `*(obj+0x28) != 0`. The `je` at `0x6ca7f9c` is taken when
`al == 0` — i.e. predicate **FALSE / ZERO**. The FALSE path `0x6ca7ffa →
0x6ca8008 → 0x6ca800e` reaches the `ExceptionCheck` call. On NONZERO the TRUE
path dispatches the closure (lambda `0x6ca805b`), which calls `*0x720` at
`0x6ca8090`. **Reconciliation with wave-24:** W24 correctly identified
`*0x720 = ExceptionCheck`, the FALSE-path call, and the TRUE-path lambda call;
the only refinement is that `0x6ca8014` is the epilogue/RA of the FALSE-path
call, not the call instruction itself. No contradiction; W24's claim stands.

## (b) obj provenance — PROVEN chain, type/size partly UNPROVEN

Quoted from `0x2a0de60`:
> `2a0de64: lea … # 722e560` → `2a0de6b: call 2ea6440` (TLS descriptor accessor, guard byte)
> `2a0de7d: lea … # 722e540` → `2a0de84: call 2ea6440`
> `2a0de89: lea … # 2a0dfe0` ; `2a0de90: lea … # 6d37bc0` ; `2a0de9a: call 2ea6130`
> `2a0deba: add $0x410,%rax` ; `2a0dec1: ret`

So `obj = *(TLS[0x722e540] + 0x410)` — a **TLS-derived field**, not a plain
global or argument. The object is a per-thread scheduler-state object built
from the template at `0x2a0dfe0` with type descriptor/vtable `0x6d37bc0`
(`.data.rel.ro`, populated by packed RELA) via `0x2ea6130`. Type: guest
scheduler thread state (W24/A06); +0 is a vtable/type pointer — UNPROVEN which
exact vtable the live object holds (the `lea # 6d37bc0` is the descriptor
passed to the constructor). Size: fields through `+0x78` are written
(`2a0ecf3: mov %rax,0x78(%rbx)`), so ≥`0x80` bytes; exact size UNPROVEN. The
constructor helper allocates a 0x18-byte registration wrapper
(`2ea6173: mov $0x18,%edi; call 1dbad62`), not necessarily the object itself.

## (c) `+0x28` writer chains — PROVEN links, entry UNPROVEN

`0x2a0ed45` is inside function `0x2a0e8c0`; quoted:
> `2a0ed1d: call 2a13620` (zero-init helper, `edx=3`)
> `2a0ed45: mov %r15,0x28(%rbx)` (stores the callback word)

Its only static caller (quoted):
> `2a0e83b: call 2a0e8c0`

inside function `0x2a0e7c0`; no static direct caller of `0x2a0e7c0` was found →
invocation via function pointer / queued scheduler task is likely but
**UNPROVEN**. `0x2a1362f` is in the leaf helper `0x2a13620` (quoted
`2a1362f: movq $0x0,0x28(%rdi)`), called from seven sites in the
`0x2a0e..0x2a0f` scheduler region (`2a0e3d0`, `2a0e793`, `2a0ea3a`,
`2a0ed1d`, `2a0efda`, `2a0f2ab`, `2a0f699`). Which of these lies on a
capture-order native, JNI_OnLoad, ctor, thread start or queued-task path in a
real boot is **UNPROVEN** — no raw log line ties a writer to a tid/native.

## What NOT done
No diff applied, no harness edit, no runs/gdb/perf, no device/AVD/Frida/phone,
no new captures, no guest writes, no gate edits, no ledger edits, no credentials,
no reading of `/tmp/opencode/uploads`.

## First divergence + next step
First divergence: `*(obj+0x28)` non-NULL on the harness thread (TRUE path,
queued task recursion) vs NULL on device. Next: harness logs the `+0x28`
writer with tid and entry native.

## FINAL ADDENDUM (lead-appended from static's delivered root-cause correction)

- **Slot `0x720` target (PROVEN, quoted):** host bridge `sr_ExceptionCheck` is a
  pure leaf — `native_jni.c:1397-1398, quoted: 'static sr_jboolean sr_ExceptionCheck(void *env)' /
  '{ (void)env; return 0; }'`, installed at `native_jni.c:1560 SETFN(*t,224,sr_ExceptionCheck)`.
  But at runtime `[rax+0x720]` changes: `call720.err:3703 '[W32_CALL720] hit=1 ...
  [rax+0x720]=0x555555582690 decode=host+0x000000000002e690'` →
  `':3732 ... hit=100 ... [rax+0x720]=0x7fffef8e3f72 decode=guest+0x0000000006ca7f72'`.
  With base `0x7fffe8c3c000` (`:3682`), the latter is guest RVA `0x6ca7f72` — the
  **prologue of the very wrapper containing `call *0x720(%rax)` at `0x6ca800e`**.
  Calling it re-enters `0x6ca7f72` → infinite recursion → stack overflow at
  `0x6ca7f95` (`si_addr=rsp-8`).
- **WAVE-24 scheduler TRUE-path attribution RETRACTED.** Harness logged
  `[obj+0x28]=0` on all 37,547 predicate entries ⇒ `6ca7f9c je 6ca7ffa` always
  takes the FALSE path to `6ca800e call *0x720`; the TRUE path / lambda
  `0x6ca805b` never runs. The recursion is the fn[224] self-pointer feedback
  loop, not the `*(obj+0x28)!=0` scheduler path.
- **UNPROVEN:** which guest code writes `0x6ca7f72` into fn[224], and the
  identity/route of `host+0x2e690`. (Audit ledger entry 30.)
