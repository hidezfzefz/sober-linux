# Runtime tracer survey and allocation-site evidence — Phase 4b

Owner: **worker-1** (task #391). Scope: `analysis/trace/` only.
Target: AppImage `libroblox.so`, BuildID `204ad049…`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
No Flatpak RVAs. Evidence IDs: REA `ev_d838d804…` (full `.text` disassembly),
REA runtime capture `ev_12b97ce1…`; new runtime observations below.

## 1. Tracer survey on this Gentoo host

| Tracer | Available? | Result |
| --- | --- | --- |
| `perf` | MISSING | — |
| `valgrind` | MISSING | — |
| `rr` | MISSING | — |
| `ltrace` / `strace` | MISSING | — |
| `bpftrace` / `systemtap` | MISSING | — |
| **LD_PRELOAD malloc interposer** | yes | **BLIND to the guest allocation**: worker-2's `src/trace` interposer and my own `analysis/trace/preload_trace.c` both armed successfully but captured **no** guest allocation/free (worker-3: no `[trace] ARMED`, MAGIC or mmap evidence). The guest carves its own arena from mmap; its allocation does not pass through glibc malloc. |
| **gdb 18.1** (`/usr/bin/gdb`) | yes | Only working tracer. Note `ptrace_scope=1`: `gdb -p` from a sibling is denied; gdb must be the target's **parent** (lead's finding). |

My LD_PRELOAD tracer (`analysis/trace/preload_trace.c`, built to
`/tmp/sober-preload*.so`) DID work mechanically: it interposed
malloc/calloc/realloc/free/_exit/backtrace, ring-buffered 1.3M host allocations,
and on crash emitted a magic-block report
(`=== TRACE CRASH-BACKTRACE ptr=… size=0x2460 ===`) plus a full `/proc/self/maps`
scan finding 40 `0xc0dedbad` occurrences in the **host** heap (worker-2's
synthetic objects) — but **none in the guest mapping**, proving the real handle
is allocated by the guest's own allocator and invisible to malloc hooks.

## 1b. Correction (worker-3 runtime dump): the slot is rewritten at runtime

Worker-3's `SOBER_DUMP_HANDLE=1` runtime dump at the fault showed the calloc
slot QWORD `guest+0x726c0c0 == guest+0x231f0e2` — **not** the APS2 initial
value `0x253225e`. Disassembly confirms why:

```
231e900: mov %rdx,0x726c0b8(%rip)
231e90e: mov %rcx,0x726c0b0(%rip)
231e915: mov %r9 ,0x726c0c0(%rip)   <-- overwrites the slot at runtime
```

So worker-2's 7th bridge patched at `0x253225e` is never reached via this
slot. The correct runtime target is **`0x231f0e2`**, the guest's calloc
wrapper: it multiplies `nmemb*size` with an overflow check, calls raw
allocator `0x231f13a`, then zero-fills the whole block via
`jmp memset@plt (0x6d31d00)`. Therefore the `0x1100` handle from
`0x231f33d` is already zeroed, which contradicts the observed stale `+0x68`.
Conclusion: the `0xc0dedbad` handle consumed at `0x23331ae` is most likely
**not** created at `0x231f31f` (only one immediate magic writer exists, but
a copy/memcpy or our own JNI SrObject graph can carry the same magic). The
allocation-site hypothesis is narrowed accordingly; check worker-2's SrObject
creation and worker-4's dispatch map for the actual object source.

## 2. The candidate allocation site (disassembly + initial APS2 decode; see §1b)

APS2 packed-relocation decode (lead / parallel research):

| Slot | Addend | Meaning |
| --- | --- | --- |
| `0x726c0b8` | `0x1dbe559` | free bridge (patched by phase 2) |
| `0x726c0c0` | **`0x253225e`** | **guest calloc — NOT one of our six patched bridges** |

The `0xc0dedbad` handle is created in the function starting at **RVA `0x231f31f`**:

```
231f31f: push rbp ; mov rbp,rsp ; push r15 ; push r14 ; push rbx ; push rax
231f329: mov %rdi,%rbx
231f32c: lea 0x726c0c0(%rip),%rax     # calloc slot
231f333: mov $0x1,%edi                # nmemb = 1
231f338: mov $0x1100,%esi             # size  = 0x1100
231f33d: call *(%rax)                 # *** ALLOCATION SITE ***
231f33f: test %rax,%rax ; je fail
231f348: mov %rax,%r14
231f34b: movl $0xc0dedbad,(%rax)      # writes the magic at +0
231f351: movdqu %xmm0,0x8(%rax)       # +8..+0x17 = 0xff... (pcmpeqd)
```

**Exact findings:**
- **Allocation RIP: guest RVA `0x231f33d`**, a `call *(%rax)` through slot
  `0x726c0c0`, allocating **0x1100 bytes** via the guest's own calloc
  (`0x253225e`).
- The creator writes the magic `0xc0dedbad` at `+0` (`0x231f34b`) and a
  16-byte 0xff pattern at `+8` (`0x231f355`), but **never writes `+0x68` or
  `+0x20`** — those fields remain whatever the guest arena contained,
  i.e. stale code bytes. That is the provenance of the invariant garbage
  pointer `0xffffff3085894cff` consumed at `0x23331ca`.
- The 8 garbage bytes decode as the final `ff` of a preceding instruction plus
  `4c 89 85 30 ff ff ff` = `mov [rbp-0xd0],%r8`. I located the exact 8-byte
  sequence `ff 4c 89 85 30 ff ff ff` **42 times** in the guest `.text`
  (e.g. `0x1dbfb7f`, `0x1e47c7c`, `0x231f82f`, `0x2331664`, …); in each case
  the leading `ff` is the tail of the previous instruction's disp32. So the
  stale value is an **unaligned slice of stack-save code**, matching the
  ASLR-invariant constant and the parallel subagent's sentinel research
  (no known Android sp<>/wp<> sentinel; `0xc0dedbad` is Stagefright-style
  hexspeak, not an AOSP constant).

## 2b. Follow-up: creator does NOT write +0x68 — a later copier must be watched

Full disassembly of the creator `0x231f31f..0x231f440` shows writes at `+0`
(magic `0xc0dedbad`, `0x231f34b`), `+8` (0xff pattern, `0x231f355`), and
`+0x88/+0x90/+0x98/+0xa0/+0xa8/+0x970/+0x980/+0x988/+0xe90/+0xea0/+0xeb0/
+0xef8/+0xf08` — but **no write to `+0x68`**. Worker-2's 8th bridge at the
runtime-rebound calloc `0x231f0e2` now fires and returns zeroed `0x1100`
blocks (logged `guest_rva=0x231f33f`, `ptr=…`), yet the fault is unchanged.
Therefore the stale code bytes in `[handle+0x68]` are written by a **later
copier**, which must be caught with a write-watch on the exact returned
pointer (worker-2's DSO supports `SOBER_TRACE_WATCH=<ptr>`; worker-3 can
auto-arm it on the first 0x1100 allocation). That watch is the single missing
piece of runtime evidence.

## 3. Fix class implied by the evidence

The defect is **not** a wrong vtable, JNI callback, or allocator bridge: the
creator at `0x231f31f` allocates from the unpatched guest calloc and leaves
pointer fields uninitialised. The minimal, targeted fixes (for worker-3 to
choose exactly one) are:

1. **Zero the two consumed fields at creation**: after the allocation at
   `0x231f33d`, seed QWORD `[handle+0x68] = 0` and `[handle+0x20] = 0`
   (guest `0x2333593`/`0x23331e1` both have null-checks that then take safe
   early-exits). This is the smallest targeted change.
2. **Zero the whole 0x1100 block** after the guest calloc, before the magic
   store at `0x231f34b` — equivalent and simpler if a single patch site is
   preferred (the guest calloc may not zero reused arena chunks).
3. **Patch the guest calloc slot `0x726c0c0`** to a native zeroing allocator —
   broader, not recommended (affects all allocations through that slot).

Do NOT re-try: allocator zero-fill on our six bridges (the handle is not
allocated through them), object substitution, A/B/C/D graph expansion,
per-field normalisation at the consumer, or RefBase-vtable construction —
all were empirically ruled out and the real site is now known.

Also relevant (lead's parallel finding): `src/jni/native_jni.c` wires JNI
string functions at wrong slots — canonical indices are
`NewStringUTF=163`, `GetStringUTFChars=165`, `ReleaseStringUTFChars=166`,
but 159/161/162 were used. That is a separate, real bug for worker-4's
dispatch map / worker-2's bridge to fix; it does not explain the stale
`+0x68` bytes (the creator never writes that field).

## 4. Evidence

- New runtime: LD_PRELOAD tracer source `analysis/trace/preload_trace.c`;
  logs `/tmp/tr-final.log`, `/tmp/tr9.log`, `/tmp/tr13.log`; host-heap magic
  scan with no guest-mapping hits — guest allocator invisibility to malloc hooks.
- REA `ev_d838d804…` — disassembly of `0x231f31f`, `0x233319a`, `0x245605d`.
- APS2 packed-relocation decode (`analysis/bootstrap/decode_aps2.py`):
  slot `0x726c0c0` addend `0x253225e`; slot `0x726c0b8` addend `0x1dbe559`.
- REA runtime capture `ev_12b97ce1…` — failing app-shell harness.
- Lead's parent-attach gdb experiment (in progress): expected to confirm the
  return address at `($rsp)` when guest+`0x1dbab02`/`0x231f33d` is hit.
