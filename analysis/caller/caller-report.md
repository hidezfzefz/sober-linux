# Caller trace: where RDI becomes guest RVA 0x233263f (Phase 4c)

Owner: **worker-1** (task #400). Scope: `analysis/caller/` only. No code edits.
Target: AppImage `libroblox.so`, BuildID `204ad049…` only. Evidence: REA
`ev_d838d804…` (full `.text` disassembly), `ev_34eea014…` (sections),
plus the two subagent/lead call-chain traces recorded in decisions 027–032.

## 1. The live app-shell caller (confirmed by lead's full call-chain trace)

`0x24abf96` → `0x24ad210` → `0x24ad440` → `0x24ae02b` → `0x23ae51c` →
`0x23ae5c2` → `0x602ef6c` → `0x2391784` → `0x23917da` → `0x23926f4` →
`0x2330410` → `0x23313c2` → `0x6a26e5c` → **F_inner `0x23330e7`** (concrete
`e8` at `0x6a26ef2`).

### 1.1 Exact instruction chain that sets RDI = 0x233263f

Function `0x6a26e5c` (REA `ev_d838d804…`):

```
6a26e5c: push rbp ; mov rbp,rsp ...
6a26e6b: call 0x23314fc            ; unrelated setup -> r15
6a26e73: call 0x231f294            ; <-- SINGLETON GETTER; RAX = handle
6a26e78: mov %rax,%r14             ; <-- RDI source for F_inner (R14)
6a26e7b: mov %rax,(%rbx)           ; also stored into caller's out-pointer
6a26e83: call 0x1dbab02            ; alloc 0x20 wrapper object
6a26e99: mov %r14,0x18(%rax)       ; stores handle in wrapper
6a26ea1: test %r14,%r14
6a26ea4: je 0x6a26ed9              ; Null-handle fallback path
...
6a26eef: mov %r14,%rdi             ; *** RDI = the bogus pointer ***
6a26ef2: call 0x23330e7            ; F_inner
```

F_inner `0x23330e7` then simply preserves that pointer:

```
2333100: mov %rdi,-0x48(%rbp)      ; the slot observed by worker-3's dump
2333109: cmpl $0xc0dedbad,(%rdi)   ; validates magic on the bogus pointer
2333164: lea -0x48(%rbp),%rdi
2333168: call 0x233319a            ; consumer -> reads [rbx+0x68]
```

So **RDI becomes `0x233263f` at `0x6a26e78`**, as the return value of the
getter `0x231f294`; no intervening instruction transforms it.

### 1.2 Where the getter gets it

Getter `0x231f294`:

```
231f29d: mov 0x72293f8(%rip),%rbx  ; JNI/ABI singleton slot
231f2a4: mov (%rbx),%rax           ; saved canary
...
231f2dd: lea -0x18(%rbp),%rdi
231f2e1: call 0x231f31f            ; creator: alloc 0x1100 + magic
231f2e6: test %eax,%eax
231f2ea: mov -0x18(%rbp),%rax      ; *** RETURNED HANDLE ***
231f2ee: jmp 0x231f30a
```

Creator `0x231f31f`:

```
231f32c: lea 0x726c0c0(%rip),%rax  ; allocator slot (rebound at runtime)
231f333: mov $1,%edi
231f338: mov $0x1100,%esi
231f33d: call *(%rax)              ; (1,0x1100) -> handle
231f34b: movl $0xc0dedbad,(%rax)   ; magic at +0
231f430: mov %r14,(%rbx)           ; stores handle into getter's [rbp-0x18]
```

The slot `0x726c0c0` is rebound at runtime from APS2 addend `0x253225e` to
the zeroing calloc wrapper `0x231f0e2` (`mov %r9,0x726c0c0(%rip)` at
`0x231e915`). The value that reaches `0x231f430` in this run is the code/return
address `guest_base + 0x233263f` — i.e. the return address of
`call 0x2332657` at `0x233263a` (function `0x23325xx`). No 8-byte or 4-byte
static reference to `0x233263f` exists in the binary, so it is produced at
runtime (stale return address / corrupted singleton slot), as established by
worker-3's fault dump and worker-4's dispatch map §11.

## 2. The second caller (0x2457721)

```
2457702: push rbp; mov rbp,rsp; push r14; push rbx
2457709: mov %rsi,%rbx           ; RSI (2nd arg) is the object
245770c: mov %rdi,%r14; add $0xa0,%r14
2457719: call 0x6cadc04
245771e: mov %rbx,%rdi           ; RDI = caller's 2nd argument
2457721: call 0x23330e7          ; F_inner
```

This path supplies F_inner with its own **second argument** (a caller-provided
object). It is not the live app-shell path in the confirmed chain; the live
pointer comes from `0x6a26e78`. It matters as an alternative injection point:
fixing `0x245771e` alone would not help the failing run.

## 3. One-line native fix

**At guest RVA `0x6a26e78`**, immediately after `call 0x231f294` returns,
validate RAX and replace a bogus (code-address) value with a valid guest handle:

```c
/* stub at 0x6a26e78, preserves flags/registers:
 *   if RAX lies inside the guest RX mapping (e.g. guest_base <= RAX <
 *   guest_base + guest_size, which covers 0x233263f) or is non-canonical,
 *   set RAX = sober_jni_safe_app_shell_handle();
 *   then execute the displaced `mov %rax,%r14` and return to 0x6a26e7b. */
```

Why this point: it is the single instruction that copies the getter's return
into the register that becomes RDI two instructions later (`0x6a26eef`); fixing
it also fixes the wrapper at `0x6a26e7b`/`0x6a26e99`, so the entire app-shell
chain sees a valid handle. Earlier attempts patched downstream sites
(`0x2333164` consumer call, `0x23331ae` consumer load) and only moved the
fault, because the pointer was already bogus when it got there.

Prerequisites for the substitute (already landed by worker-2, verify before
enabling): `sober_jni_safe_app_shell_handle()` returns a `0xc0dedbad`-magic
object with `+0x20=0`, `+0x68=0`, allocated ≥`0x1200`; and the host bridges
are unified to one allocator family so the guest free bridge
(`0x1dbe559`) accepts the pointer without the `malloc_usable_size` mismatch
seen in the last experiment. If those prerequisites are not met, this fix will
again move the fault downstream — in that case the true root fix is inside the
getter's singleton slot (stop the stale return address being installed at
`0x231f430`), which requires runtime capture that this team has not obtained.

## 4. Evidence

- REA `ev_d838d804…`: disassembly of `0x6a26e5c`, `0x6a26ef2`, `0x231f294`,
  `0x231f31f`, `0x23330e7`, `0x233319a`, `0x233263a`, `0x2457702`.
- REA `ev_34eea014…`: section headers (guest RX mapping bounds for the
  canonical-pointer test).
- APS2 decode (`analysis/bootstrap/decode_aps2.py`): slot `0x726c0c0` addend
  `0x253225e`; runtime rebound to `0x231f0e2` at `0x231e915` (worker-3 dump).
- Worker-3 fault dump: `rax = guest_base + 0x233263f`; slot `[rbp-0x48]` holds
  that code pointer; `si_addr = guest + 0x23326a7` when zeroing `[rax+0x68]`.
- Worker-4 `analysis/dispatch/dispatch-map.md` §11 (pointer provenance).
- Lead's decisions 027–032 (full call-chain confirmation).
