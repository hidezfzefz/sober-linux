# Trace of `0xABCDEF0123456789` on the game/app-shell path (task #475)

Owner: **sentinel** (team `TEAM-HC46-FCMT-PZ4G`). Analysis only — no `src/`, CMake,
gate, test or engine edits. Frozen engine `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`
(BuildID `204ad049…`, SHA `3e498d6494…`) was not modified. All RVAs below are from
`/tmp/opencode/libroblox.dis`; relocation slot check with `/tmp/opencode/reloc.py`.
Date 2026-11-20. Coordinates with `analysis/jni/recursion-note.md` Addendum 2,
`analysis/tls-2a0de60-r70-correction.md`, `analysis/tsd-static-buffer-abort.md`,
`analysis/abort/abort-report.md`, and decision 013.

## 1. Verdict (read this before the rest)

The task premise needs one correction, backed by the fault record and the
disassembly:

> **`rax = 0xabcdef0123456789` at fault `0x31a1360` is not a value read out of a
> guest/JNI object field. It is the faulting guest function's own stack canary,
> loaded two instructions after the prologue from the imported
> `__stack_chk_guard` GOT slot at RVA `0x72293f8`. The actual fault is a NULL
> dereference, `si_addr = 0x28`, from `rbx == NULL` in
> `mov 0x28(%rbx),%rax`. No synthetic `SrObject` backing field carries the
> sentinel at any point in this path.**

What *is* real and still unfaithful is the decision-013 TSD namespace problem on
the **key 2** path — but in the current build the sentinel is not being delivered
through it; the guest's `pthread_getspecific` GOT is bound to **glibc**, not to
our Bionic TSD shim. That binding gap is the reason the shim cannot neutralize
anything on this path, and it is the fix the lead should opt into (§5–§7).

So the answer to "which synthetic SrObject payload/backing field carries it" is:
**none** (§4). The answer to "why does the shim not neutralize it" is: **the
guest never calls the shim's `pthread_getspecific` on this path** (§5).

## 2. Exact provenance of `rax` at `0x31a1360`

`libroblox.dis`, function entry `0x31a1168` (export-relative symbol
`Java_com_roblox_client_startup_MainGameActivity_nativePreloadFlagOverrides+0x2c390f`):

```
31a1168: push %rbp
31a1169: mov %rsp,%rbp
31a116c: push %r15
31a116e: push %r14
31a1170: push %r13
31a1172: push %r12
31a1174: push %rbx
31a1175: sub $0x418,%rsp
31a117c: mov %rdx,%r15
31a117f: mov %rsi,-0x400(%rbp)
31a1186: mov %rdi,%rbx                 ; rbx = arg0
31a1189: mov 0x4088268(%rip),%rax      # 0x72293f8  <-- GOT: &__stack_chk_guard
31a1190: mov (%rax),%rax               ; rax = *__stack_chk_guard = 0xabcdef0123456789
31a1193: mov %rax,-0x30(%rbp)          ; saved stack canary for this frame
...
31a1197: cmpb $0x0,0x45fb992(%rip)     # 0x779cb30   <-- branch to the faulting arm
31a119e: je  31a134f
...
31a134f: mov %r15,-0x3f8(%rbp)
31a1356: movabs $0x7fffffffffffffff,%r12
31a1360: mov 0x28(%rbx),%rax           ; FAULT: rbx == NULL -> si_addr 0x28
31a1364: mov 0x18(%rax),%rax           ; (never reached)
31a1368: mov 0x38(%rbx),%edx
31a136b: mov 0x10(%rax),%rsi
31a1376: call 0x26a01d4
```

`game.log:3722-3745` (the `SOBER_GAME_LOADED_PATH` run) reports exactly:

```
signal=11 si_addr=0x28 rip=...+0x31a1360
rdi=(nil) rsi=0x7ff0b00757c0 rdx=0x7ff0b7ffe5e0 rcx=0x7ff0b0276 rax=0xabcdef0123456789
FP: 0x31a1360 <- 0x320b0ec <- 0x32034d9 <- 0x2ec1661 <- 0x23389fa ...
```

`si_addr = 0x28` and `rdi = (nil)` are decisive: `rbx` (set from `rdi` at
`0x31a1186`) is NULL, so `mov 0x28(%rbx)` faults before writing `rax`; the
reported `rax` is the still-live canary from `0x31a1190`. This is the same
misattribution already corrected in `tests/verification-report-451.md:58-62`
for the `0x2eb4b65` fault ("`rax=0xabcdef0123456789` is incidental register state
… the host compat `__stack_chk_guard`").

The NULL `rbx` itself comes from the caller: `0x320b0ba mov 0x488(%rbx),%rdi`
loads the argument passed to `0x31a1168` (call at `0x320b0e7`). Our opt-in seeds
populate the guest app object at `+0x408` (app delegate,
`src/linux/guest_app_shell.c:739-751`) and `+0x440` (user controller,
`guest_app_shell.c:774-786`) but **not `+0x488`**. That missing guest field, not
the sentinel, is the `0x31a1360` blocker (handed to the game fault owner).

`0x72293f8` is the last 8 bytes of `.got` and is the GOT entry for the imported
`__stack_chk_guard`, as established in `analysis/abort/abort-report.md:88-93`.
The same GOT-indirect canary load pattern recurs in other hot guest functions:
`0x23330f2` (`F_inner`, abort-report.md:63-70), `0x6ca7f80` and `0x6c8f35d`
(the JNI exception-check / FindClass scheduler wrappers in
`analysis/jni/recursion-note.md:12-14,102`). Each of those will leave the
sentinel in `rax` if it faults early — which is why the value "repeatedly
appears" in fault dumps.

## 3. Where the sentinel actually originates (host side)

* `src/compat/android_compat.c:45` — `uintptr_t __stack_chk_guard =
  (uintptr_t)0xABCDEF0123456789ULL;` exported via `src/compat/exports.map:86`
  and `src/compat/android_compat.h:169`. The harness links `sober_compat` with
  `--whole-archive -rdynamic` (`CMakeLists.txt:172-180`), so this is the value
  the guest's GOT `0x72293f8` resolves to.
* `src/compat/bionic_libc.c:453,920-921,1345` — the versioned DSO keeps its own
  `__stack_chk_guard` object (`sober_libc_obj___stack_chk_guard[64]`), copied
  from glibc's symbol and aliased to the exported name. It is a *different*
  object from the compat one above; do not confuse the two.
* Decision 013 §"Root cause" already rules out replacing this value at
  runtime: glibc-compiled code (and the guest's own canary checks, see
  abort-report.md §3) depend on it. This analysis does **not** propose changing
  it.

## 4. SrObject backing-field audit — no field carries the sentinel

Every synthetic object builder allocates and zeroes its storage, then writes
only magics, canonical self-pointers and caller-supplied scalar payloads:

| Field | Written at | Value written |
| --- | --- | --- |
| `SrObject.guest_b` (A) | `src/jni/native_jni.c:211,220-221,231-233,242` | `calloc(1,0x1200)`; `+0=0xc0dedbad`, `+0x10=0`, `+0x20=0`, `+0x68=0`, `+0xf90=D`. Sentinel never written. |
| `SrObject.guest_c` (B) | `native_jni.c:212,222-225,234,240,243` | `+0=0x000bab1e`, table at `+0x10`, capacity 16 at `+0x20`, `+0x68=C`, `+0x120=C`. |
| C / D nodes | `native_jni.c:213-214,226,238-239` | zeroed, C self-pointer, `D+8=0`. |
| `SrObject.payload` | `native_jni.c:1182-1192,1199-1213`; `jni_native_objects.c:137-162`; `startapp_params.c:229-232`; `global_string_seed.c:152-154` | `calloc` + `memcpy(initial)`; initial buffers are locally `memset(0)` then filled with strings/floats/ints (e.g. `guest_app_shell.c:1064-1074`, `jni_native_objects.c:181-191`). |
| `sober_jni_new_shared_param` wrapper | `native_jni.c:1160-1173` | `w+0=vt`, `w+8=1`, `w+0x10=(uintptr_t)back_pointer`, `w+0x18=bytes`, payload `[+0x40…]`. `back_pointer` is a class-name string constant at `guest_app_shell.c:1072-1074`, not the sentinel. |

Additionally, `sr_is_safe_ptr()` (`native_jni.c:271-278`) explicitly rejects
`0xabcdef0123456789ULL` as a handle, and `sr_check()` (`:280-287`) rejects any
handle without the `SR_OBJ_MAGIC`. `javavm_semantics.c:157-160` rejects the same
value as an untrusted `AttachArgs.name`. So even if the guest handed us the
sentinel, our bridge would return NULL rather than embed it in an object.

**Conclusion:** there is no "carrier field" to neutralize. The value is
GOT/register-resident canary state that the guest loads *into `rax`*, not into
an object field that we later expose.

## 5. The real key-2 TSD path, and why the shim misses it

The guest does have a genuine TSD-key-2 consumer; these are the exact RVAs:

```
0x1e47927  allocator thread init
  1e4792b: mov 0x7282000(%rip),%edi     ; key = 2 (Bionic allocator thread-state key)
  1e47936: call 0x6d322e0               ; pthread_getspecific@plt (GOT 0x72298b0)
  1e4793b: test %rax,%rax
  1e4793e: je  0x1e4795c                ; NULL -> initialise thread state
  1e47940: lea 0x721db80(%rip),%rcx     ; static fallback thread state
  1e47947: cmp %rcx,%rax
  1e4794a: je  0x1e4795c                ; fallback -> initialise
  1e4794c: ret                          ; non-NULL, non-fallback -> "already exists"
  1e4795c: call 0x1dbaddd               ; create+setspecific real thread state

0x227aafa  allocator "reallocate" front-end
  227ab07: mov 0x7282000(%rip),%edi     ; key = 2 again
  227ab12: call 0x6d322e0               ; pthread_getspecific@plt
  227ab17: test %rax,%rax
  227ab1a: lea 0x721db80(%rip),%rdi     ; fallback
  227ab21: cmovne %rax,%rdi             ; TSD value used directly as thread_state
  227ab44: -> real allocator (arena table 0x7cd9040); derefs rdi+0xe8 etc.

0x2ea6440  TLS descriptor accessor (key 3, global 0x72a1b08, GOT 0x72298b0)
  (fully decoded in analysis/tls-2a0de60-r70-correction.md §2)
```

The decision-013 fix implements a private 512-slot Bionic TSD array in
`src/compat/bionic_libc.c:469-486` (also emitted by
`src/compat/gen_bionic_libc.py:117-138`), and the trampoline generator skips
forwarding these two symbols (`gen_bionic_libc.py:183-184`). That DSO is built as
`build/ndk-stubs/libc.so` (`CMakeLists.txt:103-113`).

**Why that shim cannot act on the path above:** the guest is a manually relocated
ELF. `src/linux/android_relocs.c:260-278` patches every GLOB_DAT/64 relocation
with

```c
void *value = name ? dlsym(RTLD_DEFAULT, name) : NULL;
```

The guest is dlopen'd `RTLD_NOW | RTLD_LOCAL`
(`src/linux/guest_dlopen_host.c:524`), so `build/ndk-stubs/libc.so` — loaded only
as its local dependency — is **not** in the `RTLD_DEFAULT` global scope; glibc's
`libc.so.6` is. `dlsym(RTLD_DEFAULT, "pthread_getspecific")` therefore yields
glibc's function. A runtime probe confirms it (built and run in `/tmp/opencode`,
no repo files touched):

```
RTLD_DEFAULT getspecific = 0x7f763bb76210
stub LIBC getspecific    = 0x7f763bcc7390
glibc 2.34 getspecific   = 0x7f763bb76210
DEFAULT==glibc? YES   DEFAULT==stub? no
```

Consequently:

1. Guest key 2 (and key 3) are interpreted in **glibc's** key namespace —
   exactly the mismatch decision 013 set out to remove.
2. The opt-in seeders `sober_jni_seed_guest_tls()` (`native_jni.c:1225-1249`)
   and `seed_stub_tsd()` (`guest_app_shell.c:47-75`) call
   `dlvsym(stub, "pthread_setspecific", "LIBC")`, i.e. they fill the shim's
   private array. The guest's GOT-bound `getspecific` reads glibc's array, so
   **the seeds are invisible to the guest on this path**. This also explains why
   the A/B TSD experiments in `analysis/tsd-static-buffer-abort.md` behaved
   inconsistently.
3. Because glibc key 2 is not the Bionic key 2, `0x1e47927`/`0x227aafa` take
   their "thread state already exists" arms on whatever stale value glibc has,
   and any garbage pointer (historically the stack-guard sentinel) is consumed
   as a thread-state pointer. That is the genuine, still-open fidelity gap —
   independent of the (incidental) `rax` at `0x31a1360`.

## 6. Faithful fix proposal (lead owns `src/`; nothing changed here)

Keep the stack guard exactly as-is. The fix is to make the guest actually reach
the Bionic TSD array, plus read-only object-field hardening:

**Primary — rebind the two TSD GOT slots to the versioned stub (root cause).**
In `android_relocs.c:260-278`, special-case the symbol names
`pthread_getspecific` and `pthread_setspecific`: resolve them with
`dlvsym(stub_libc_handle, name, "LIBC")` (the handle already used by
`native_jni.c:1102-1119`) instead of `dlsym(RTLD_DEFAULT, name)`. Equivalently,
post-patch the two GOT words at `0x72298b0` (getspecific) and its setspecific
sibling after relocation. This is ~10 lines, touches no guest byte, no callback,
no marker, and does not touch `__stack_chk_guard`. Expected effect: guest
`pthread_getspecific(2)` returns NULL from the zeroed private array, so
`0x1e47927` takes the `0x1e4795c` init branch and the guest's own
`0x1dbaddd`/`0x1dbab02` allocator creates real thread state; no sentinel can be
consumed as a thread-state pointer. This is the completion of decision 013's
"A compatible pthread TSD array in the versioned libc shim" — the array exists,
but the guest is not wired to it.

**Secondary — guest-shaped pointer/header for JNI objects (defence in depth).**
The synthetic A/B/C/D graph in `sr_new_object()` (`native_jni.c:199-265`) is
already canonical and zero-seeded, and `sober_jni_new_shared_param()` gives
callers a slot-style wrapper (`+0=vptr`, `+8=refcount`, `+0x10=back-pointer`,
payload at `+0x40`). Keep that shape and add an explicit guest-shaped
**back-pointer header** for any object whose payload is handed to a guest
native: a small calloc'd node whose first word is the canonical A pointer and
whose neighbouring pointer fields (`+0x28`, `+0x68`, `+0x120`, `+0xf90`) are
NULL or canonical. This guarantees that if the guest later stores its canary
or a stale code pointer into a "nearby" field, the dereference chain used at
`0x31a1360`/`0x23331ae`/`0x2456079` lands on NULL/canonical values and takes
the existing safe branches rather than on our own JNI metadata. Do **not**
encode the canary value in that header — the header only separates guest data
from bridge metadata.

**Do not:** zero or replace `__stack_chk_guard`, patch guest canary checks
(`0x2333186`, `__stack_chk_fail@plt 0x6d32140`), or widen `sr_is_safe_ptr`
to accept the sentinel. All three would break decision 013 and glibc smoke
canaries.

## 7. Opt-in experiments for the lead

All are default OFF, read-only, and opt-in so the strict decision-013 gate is
untouched.

1. **`SOBER_BIONIC_TSD_GOT=1`** — primary A/B. Apply the §6 primary rebinding
   and log, once, the resolved GOT words:
   `*(void**)0x72298b0` vs `dlsym(RTLD_DEFAULT,"pthread_getspecific")` vs
   `dlvsym(stub,"pthread_getspecific","LIBC")`.
   Also log `pthread_getspecific(2)` from both namespaces and the guest key
   globals `*(u32*)0x7282000` (expect 2) and `*(u32*)0x72a1b08` (expect 3).
   Expected: GOT word equals the stub address, guest key-2 value becomes NULL,
   `0x1e47927` takes `0x1e4795c`, and any TSD-sentinel dereference disappears.
   Capture the next genuine fault RVA (the `0x31a1360` NULL `+0x488` field is a
   separate, already-identified blocker).

2. **`SOBER_AUDIT_JNI_FIELDS=1`** — read-only sentinel audit. On each
   `sober_jni_new_native_object`/`sober_jni_new_shared_param` return and in the
   fault handler, walk every synthetic word (A `0x0..0x1200`, B, C, D, table,
   `SrObject.payload`) and count values equal to `0xabcdef0123456789`.
   Expected count: **0** — this experimentally confirms §4. If a hit occurs,
   print the object kind, field offset and creator so the carrier can be fixed
   precisely.

3. **`SOBER_CANARY_GOT_DIAG=1`** — provenance check for the misleading register
   value. At the fault handler, log `*(void**)0x72293f8`,
   `**(void***)0x72293f8`, and `&__stack_chk_guard`; also disassemble-log the
   first canary load of the faulting function. Expected: the logged value equals
   `rax`, proving it is the frame canary (as in §2) and not an object field.
   Gate with the existing marker-withhold chain per decision 013 phase 7.

## 8. Evidence index

* `game.log:3707-3747` — exact `0x31a1360` fault record: `si_addr=0x28`,
  `rdi=(nil)`, `rax=0xabcdef0123456789`, `tls_key @0x7282000 = 0x2`, FP chain.
* `libroblox.dis`: `0x31a1168/0x31a1189/0x31a1190/0x31a134f/0x31a1360`
  (canary load + NULL deref), `0x320b0ba`/`0x320b0e7` (caller, missing `+0x488`),
  `0x72293f8` GOT slot, `0x1e47927` and `0x227aafa` (key-2 TSD), `0x2ea6440`
  (key-3 TSD), `0x6d322e0` (`pthread_getspecific@plt`, GOT `0x72298b0`),
  `0x23330f2`/`0x6ca7f80`/`0x6c8f35d` (other GOT-indirect canary loads).
* `src/compat/android_compat.c:45`, `exports.map:86`, `android_compat.h:169`;
  `src/compat/bionic_libc.c:453,469-486,920-921,1345`;
  `src/compat/gen_bionic_libc.py:117-138,183-184`.
* `src/linux/android_relocs.c:260-278` (`dlsym(RTLD_DEFAULT,…)`),
  `src/linux/guest_dlopen_host.c:524` (`RTLD_LOCAL`),
  `CMakeLists.txt:103-113,155-186`.
* `src/jni/native_jni.c:199-287,1083-1119,1160-1213,1225-1272,1456-1478`;
  `src/jni/jni_native_objects.c:137-162`; `src/jni/global_string_seed.c:152-154`;
  `src/jni/javavm_semantics.c:157-160`; `src/linux/guest_app_shell.c:47-75,739-786,1064-1074`.
* `analysis/abort/abort-report.md:63-93,108-121` (GOT `0x72293f8` ruling);
  `tests/verification-report-451.md:37-62` (same `rax` misattribution at
  `0x2eb4b65`); `analysis/jni/recursion-note.md` Addendum 2; 
  `analysis/tls-2a0de60-r70-correction.md`; `analysis/tsd-static-buffer-abort.md`;
  `docs/decisions/013-no-weak-app-shell-marker.md`.
* Runtime probe: `/tmp/opencode/tsd_bind_probe.c` (diff `RTLD_DEFAULT` vs
  `dlvsym(stub,"…","LIBC")`); result recorded in §5.
