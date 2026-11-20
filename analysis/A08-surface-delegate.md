# A08 — Surface the genuine `app+0x408` delegate (worker A8, Team A)

Status: **FINAL ANALYSIS — no code changes.** Scope: this file only. `src/` shared
files, `CMakeLists.txt`, the gate/test scripts and the engine binary were READ
ONLY.

Engine: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` (verified
unchanged before and after this analysis).

Evidence classes: **OBSERVED** = bytes read from the frozen ELF with
`objdump -d --start-address/--stop-address`, or APS2 relocation decode via
`python3 /tmp/opencode/reloc.py dump`, or harness source; **DERIVED** = call-graph
/ role inference; **RUNTIME** = quoted from existing lead logs with source named.
No runtime execution was performed by this worker. Every RVA is AppImage-build
only.

---

## 0. Executive answer

1. The two writers named in the task are **confirmed genuine**. At
   `0x25d7e75` and `0x25f7e7e` the guest stores into `app+0x408` the result of
   its own lazy-singleton helpers `0x1dd55f2` and `0x25f7ff2`. **No synthetic
   object and no host vtable is needed** — the guest creates these objects
   itself (OBSERVED, §2.1/§2.2).
2. **Correction to the lead's premise:** those two objects do **not** carry
   vtable `0x6e047b0`. Their vtables are **`0x7220868`** (site A) and
   **`0x6dc3840`** (site B), both confirmed genuine APS2-relocated guest
   vtables (OBSERVED, §3). `0x6e047b0` is a real guest vtable too, but in the
   `InitWithParams` path it is used as a *type/factory tag*; the actual
   delegate object produced there has vptr **`0x6e04ec0`** (OBSERVED, §2.3).
   All four vtables are guest-owned; any of them is acceptable as the "genuine
   RVA", but the report should name the right one for each path.
3. `app+0x408` is a **pointer** to a heap/static guest object, not an inline
   object — consumers do `mov 0x408(%rdi),%rdi; mov (%rdi),%rax; call
   *0x90(%rax)` (OBSERVED, §4). The current `SOBER_SEED_APP_DELEGATE` fake
   (`src/linux/guest_app_shell.c:729-754`) allocates a 0x100-byte **host
   no-op vtable** in guest memory and points a 0x100-byte zero object at it;
   that is exactly the "fake host vtable" decision 013 forbids and must be
   deleted, not extended (§1).
4. **Faithful delegate:** the strictly best construction is to *call the
   guest's own helpers* and let them return their genuine singleton objects —
   zero fabrication, zero host pointers, zero byte patches (§5.1, §6). A
   fallback guest-shaped fresh object using the guest allocator `0x1dbab02`,
   size `0x50`, vptr `guest_base+0x6e04ec0` and the constructor's exact field
   initialisation is specified for the case where the singletons cannot be
   reached early (§5.2).
5. This is a **diagnostic experiment only**. Under decision 013 the new opt-in
   (`SOBER_FAITHFUL_APP_DELEGATE`) must be added to the marker withhold list and
   must never emit `app shell rendered` (§7).

---

## 1. The fake that must be removed (current harness state)

`src/linux/guest_app_shell.c`:

```
:726-728  comment "No-op vtable target for the synthetic delegate below"
:729-733  static long sober_noop_virtual(long a,long b,long c,long d){return 0;}
:735-754  static int seed_app_delegate(void *app, uintptr_t base)
:739      slot = (unsigned char **)((char *)app + 0x408); if (*slot) return 0;
:741-742  vt = guest_malloc(0x100); obj = guest_malloc(0x100);   // base+0x1dbab02
:747      for (i=0;i<32;i++) ((void**)vt)[i] = (void*)sober_noop_virtual;
:748      *(void**)obj = vt;  *slot = (unsigned char*)obj;
:814-815  if (getenv("SOBER_SEED_APP_DELEGATE")) seed_app_delegate(app, base);
:1338     SOBER_SEED_APP_DELEGATE is in the seeded_state withhold OR-chain
```

Why it is unacceptable:

* **Every vtable slot is a host function** (`sober_noop_virtual`). A guest
  indirect call therefore enters harness code, so any guest work performed
  through this delegate is harness-driven, not guest-driven — precisely the
  decision-013 failure mode.
* The object is 0x100 bytes of zeroes plus a pointer; it does **not** reproduce
  the guest's 0x50-byte delegate layout (§4), so slot methods that read fields
  (e.g. `0x321a85e: mov 0x20(%rdi),%eax`) silently return 0 instead of real
  guest state.
* RUNTIME confirmation it is live in the synthetic path:
  `/tmp/opencode/lua.log:3712`, `full.log:3709`, `seedctl3.log:3696` etc. all
  show `[app-shell] app-delegate seed: installed obj=… vt=… at app+0x408`.
* The sibling `sober_native_delegate()` in
  `src/jni/jni_native_objects.c:243-256` is already honest about the vptr
  (`guest_base+0x6e047b0`, no host stub) but still fabricates a bare 0x100-byte
  object and "**does NOT install**" it. It is a layout publisher, not a
  solution; §5 explains how to replace the seed.

---

## 2. The three genuine writers of `app+0x408` (byte evidence)

### 2.1 Site A — `nativeAppBridgeStartLuaAppDM`, `0x25d7e75`

OBSERVED (`objdump 0x25d7d80-0x25d7f20`):

```
25d7e5f  call   1dd55f2                 ; genuine guest lazy-singleton getter
25d7e64  xorps  %xmm0,%xmm0
25d7e67  movups %xmm0,0x3f0(%rbx)
25d7e6e  mov    %r14,0x400(%rbx)        ; r14 = 0
25d7e75  mov    %rax,0x408(%rbx)        ; <-- delegate installed
25d7e7c  mov    %r14b,0x410(%rbx)
25d7e83  mov    %r14,0x438(%rbx)
25d7e8a  mov    %r14b,0x440(%rbx)
25d7e91  movups %xmm0,0x418(%rbx) ; ... 0x421, 0x444 zeroed
25d7ea6  movl   $0,0x454(%rbx)
```

`rbx` is the ~`0x458`-byte app object. Helper `0x1dd55f2` is a one-shot lazy
singleton (OBSERVED, full body at `0x1dd55f2-0x1dd5641`):

```
1dd55f2  mov 0x7d9d0f8(%rip),%al ; "initialised?" byte
1dd55f8  test %al,%al
1dd55fa  je   1dd5604
1dd55fc  mov 0x7d9d0f0(%rip),%rax ; singleton object pointer
1dd5603  ret
1dd5604  ... call 2a0d6c0 (init guard on 0x7d9d0f8)
1dd5618  lea  0x7220868(%rip),%rax ; GENUINE vtable
1dd561f  mov  %rax,0x7d9d100        ; object's vptr slot
1dd5626  lea  0x7d9d100(%rip),%rax
1dd562d  mov  %rax,0x7d9d0f0        ; publish singleton
1dd563b  call 2a0d7d0               ; mark initialised
1dd5641  jmp  1dd55fc               ; return singleton
```

So site A's delegate is the static guest object at **`guest_base+0x7d9d100`**,
whose `+0` word is **`guest_base+0x7220868`**. It is guest-allocated (static)
and guest-vptr'd.

### 2.2 Site B — `nativeAppBridgeStartLuaAppDM`, `0x25f7e7e`

OBSERVED (`objdump 0x25f7dc0-0x25f7f20`):

```
25f7e6e  lea   -0x48(%rbp),%rdi        ; 16-byte shared-ref output
25f7e72  call  25f7ff2                 ; genuine guest singleton getter
25f7e77  lea   -0x48(%rbp),%rdi
25f7e7b  mov   (%rdi),%rax             ; object pointer
25f7e7e  mov   %rax,0x408(%rbx)        ; <-- delegate installed
25f7e8c  call  1dc1208                 ; log/notification with string 0x4395a5
25f7e98  call  24bb532                 ; (app, -0x48) consume the shared ref
...
25f7eed  mov   0x408(%rbx),%rdi        ; error cleanup path
25f7ef9  mov   (%rdi),%rax
25f7efc  call  *0x10(%rax)             ; vtable slot +0x10 of the delegate
```

Helper `0x25f7ff2` (OBSERVED, `0x25f7ff2-0x25f806a`) is the same lazy-singleton
pattern, with an intrusive refcount:

```
25f7ffb  mov 0x72a5ab0(%rip),%al       ; "initialised?" byte
25f8005  lea 0x72a5a88(%rip),%rdi
25f800c  call 25f806a                  ; incref (lock xadd +0x20), returns this
25f8011  mov %rax,(%rbx)               ; *out = object
25f8014  mov %rbx,%rax ; ret
25f8025  ... call 2a0d6c0 (guard on 0x72a5ab0)
25f8031  movups %xmm0,0x72a5aa0 ; movups %xmm0,0x72a5a90
25f8046  lea 0x6dc3840(%rip),%rcx      ; GENUINE vtable
25f804d  mov %rcx,0x72a5a88            ; object's vptr slot
25f8054  mov %rax,0x72a5ab8            ; publish
25f8062  call 2a0d7d0 ; jmp 25f8005
```

Site B's delegate is the static guest object at **`guest_base+0x72a5a88`** with
`+0` word **`guest_base+0x6dc3840`**. Its first vtable slot is itself the
refcount helper `0x25f806a`, confirming it is an intrusive-refcounted guest
object.

### 2.3 Third writer (for completeness) — `InitWithParams`, `0x24aed3e`

OBSERVED (`0x24aec0a-0x24aed45`):

```
24aec0a  cmpq $0,0x408(%rbx) ; if already installed, use it
24aec69  mov 0x408(%rbx),%rdi ; mov (%rdi),%rax; call *0x10(%rax)  (slot +0x10)
24aecd4  mov 0x408(%rbx),%rdi ; mov (%rdi),%rax; call *0x38(%rax)  (slot +0x38)
24aed01  mov 0x72cb648,%cl ; test; jne 24aed99   ; FFlag EnableAppBridgeComponents
24aed0f  lea 0x6e047b0(%rip),%rax
24aed16  lea -0xa0(%rbp),%rsi ; mov %rax,(%rsi)  ; tag struct {0x6e047b0}
24aed20  lea -0x70(%rbp),%r14 ; mov %r14,%rdi    ; 16-byte out {obj,ctrl}
24aed27  call 2520892                            ; guest delegate factory
24aed2c  movaps (%r14),%xmm0 ; xorps %xmm1,%xmm1; movaps %xmm1,(%r14)
24aed3e  movups %xmm0,0x408(%rbx)                ; app+0x408=obj, +0x410=ctrl
```

The factory `0x2520892` (OBSERVED):

```
2520907  mov $0x50,%edi
252090c  call 1dbab02                 ; GUEST allocator: malloc(0x50)
2520911  mov %rax,%r14
2520917  call 252095e                 ; constructor
252091f  mov %r14,%rsi
2520922  call 253bd94                 ; wrap into intrusive ptr pair
2520930  ret                          ; RAX = out pointer (rbx)
```

The constructor `0x252095e` (OBSERVED, the authoritative object layout):

```
252097f  xorps %xmm0,%xmm0
2520982  movups %xmm0,0x8(%rdi)       ; zero [+0x08..+0x17]
2520986  movq   $0,0x18(%rdi)         ; zero [+0x18..+0x1f]
252098e  lea    0x6e04ec0(%rip),%rax
2520995  mov    %rax,(%rdi)           ; OBJECT VPTR = 0x6e04ec0, not 0x6e047b0
2520998  movl   $0,0x20(%rdi)         ; int field = 0
25209a3  movups %xmm0,0x28(%rdi)      ; zero subobject [+0x28..+0x37]
25209a7  movups %xmm0,0x38(%rdi)      ; zero subobject [+0x38..+0x47]
25209ab  movl   $0x3f800000,0x48(%rdi); float 1.0f at +0x48
25209b8/…cc/…e6  build 3 subobjects at obj+0x28 via 2520b3d/252117a/25215b4
                 (indices 0,1,2; optional index 3 if byte 0x77892a8 != 0)
```

**Important reconciliation:** `0x6e047b0` (stored at `-0xa0` and passed as
`rsi` to the factory) is **DERIVED** to be a type/factory descriptor: its own
vtable slots `+0x10` and `+0x18` are the factory functions `0x2520892` and
`0x2520c1c` (relocation dump, §3). It is not the vptr stored in the object.
The object's vptr is `0x6e04ec0`. This matters: a delegate seeded with vptr
`0x6e047b0` would dispatch `slot +0x10` to `0x2520892` (a factory expecting
different arguments) rather than to the real getter `0x321a85e`.

### 2.4 Summary of writers

| Path | RVA of write | Object origin | Object address | Object vptr | Allocator |
| - | - | - | - | - | - |
| InitWithParams | `0x24aed3e` | factory `0x2520892` + ctor `0x252095e`, type tag `0x6e047b0` | heap, 0x50 bytes | **`0x6e04ec0`** | guest `0x1dbab02` |
| StartLuaAppDM site A | `0x25d7e75` | lazy singleton `0x1dd55f2` | `0x7d9d100` (static) | **`0x7220868`** | guest static |
| StartLuaAppDM site B | `0x25f7e7e` | lazy singleton `0x25f7ff2` (+ incref `0x25f806a`) | `0x72a5a88` (static) | **`0x6dc3840`** | guest static |
| Harness fake (remove) | `seed_app_delegate:754` | `guest_malloc` + host no-op vtable | heap, 0x100 bytes | **host `sober_noop_virtual`** | guest `0x1dbab02` for mem only |

---

## 3. Vtable verification (all OBSERVED via APS2 type-8 relocation decode)

`python3 /tmp/opencode/reloc.py dump <vtable> 24` — every entry below is a type-8
relocation into guest `.text`/vtable data; **no host address appears in any of
them**.

`0x6e047b0` (the task's cited RVA — genuine, but a type/factory table):

```
+0x00 -> 0x1dd72a0   +0x08 -> 0x32197c6   +0x10 -> 0x2520892 (factory)
+0x18 -> 0x2520c1c (factory)              +0x20 -> 0x6db7968
+0x28 -> 0x671c1e    +0x30 -> 0x6db79b8   +0x38 -> 0x671c04
+0x40 -> 0x6e047d0   +0x48 -> 0 (raw)     +0x50 -> 0x6e04830
```

`0x6e04ec0` (actual delegate object vptr from the InitWithParams factory):

```
+0x00 -> 0x321a38e   +0x08 -> 0x321a44e   +0x10 -> 0x321a85e
+0x18 -> 0x26ffbf6   +0x20 -> 0x321a862   +0x28 -> 0x253bf06
+0x30 -> 0x2544d06   +0x38 -> 0x321a98c   +0x40 -> 0x321aa88
+0x48 -> 0x2700310   +0x50 -> 0x321ab0c   +0x58 -> 0x321ace2
+0x60 -> 0x321b07e   +0x68 -> 0x276e804   +0x70 -> 0x321b202
+0x78 -> 0x321b530   +0x80 -> 0x321b668   +0x88 -> 0x321b794
+0x90 -> 0x276fd08   +0x98 -> 0x6db79b8   +0xa0 -> 0x672425
+0xa8 -> 0x6e02048
```

`0x7220868` (site A singleton vptr):

```
+0x00 -> 0x1dd72a0   +0x08 -> 0x6d2882c   +0x10 -> 0x1dd56f2
+0x18 -> 0x1dd686a   +0x20 -> 0x31acc6e   +0x28 -> 0x6d28840
+0x30 -> 0x1df6700   +0x38 -> 0x6d28864   +0x40 -> 0x6db79b8
+0x48 -> 0xd8c5af    +0x50 -> 0x7220830   +0x58 -> 0 (raw)
+0x60 -> 0x7220910   +0x68 -> 0x6d28b24   +0x70 -> 0x6d28bc6
+0x78 -> 0x1dd56f2   +0x80 -> 0x1dd686a   +0x88 -> 0x31acc6e
+0x90 -> 0x3281c9e   +0x98 -> 0x1df6700
```

`0x6dc3840` (site B singleton vptr):

```
+0x00 -> 0x25f806a (refcount helper)     +0x08 -> 0x2315912
+0x10 -> 0x2f26936   +0x18 -> 0x1dd72a0   +0x20 -> 0x2eb4722
+0x28 -> 0x6db79b8   +0x30 -> 0x61bb0b    +0x38 -> 0x6dbb628
+0x40 -> 0x1dd72a0   +0x48 -> 0x1dd72a0   +0x50 -> 0 (raw)
+0x58 -> 0x6dc38e8   +0x60 -> 0x1dd72a0   +0x68 -> 0x2f2839e
+0x70 -> 0x2f283b2   +0x78 -> 0x2f283ee   +0x80 -> 0x1dd72a0
+0x88 -> 0x2f28412   +0x90 -> 0x2f28426   +0x98 -> 0x6dc38e8
```

Consumer slots verified as real guest code (OBSERVED, disassembly):

* `0x6e04ec0+0x10 = 0x321a85e`: `mov 0x20(%rdi),%eax; ret` — a genuine int
  getter matching the ctor's `+0x20` field.
* `0x6e04ec0+0x90 = 0x276fd08`: real method that reads `cmpl $0x1,0x20(%rdi)`
  and follows `obj+0x38`; this is what `0x3205822` calls with an output pointer.
* `0x7220868+0x90 = 0x3281c9e`: `mov 0x8(%rdi),%rdi; mov (%rdi),%rax;
  jmp *0x28(%rax)` — a forwarding method.
* `0x6dc3840+0x90 = 0x2f28426`: reads `+0x8/+0x10/+0x18/+0x20` from the object.
* `0x7220868+0x10 = 0x1dd56f2`, `0x6dc3840+0x10 = 0x2f26936`: real functions
  (the latter is a `lock xadd` refcount op).

---

## 4. Object layout and consumer contract

* `app+0x408` holds a **pointer**; `app+0x410` holds the intrusive control
  pointer on the factory path (`movups` of the 16-byte pair at `0x24aed3e`).
  Consumers: `0x3205814` (`mov 0x408(%rdi),%rdi; mov (%rdi),%rax; call
  *0x90(%rax)`), `0x24aec73` (`call *0x10(%rax)`), `0x24aecde`
  (`call *0x38(%rax)`), `0x25f7efc` (`call *0x10(%rax)`).
* Therefore a delegate must be: a valid guest pointer at `app+0x408`, whose
  `*(void**)obj` is a genuine guest vtable whose slots `+0x10`, `+0x38` and
  `+0x90` are callable guest code, and whose fields match the constructor
  (`0x252095e`) so those methods read sane values.
* Guest allocator: `0x1dbab02(size_t)` — called as
  `mov $0x50,%edi; call 1dbab02` at `0x2520907-0x252090c`; size in RDI, pointer
  in RAX. This is the same allocator the existing seed code uses; keep it, but
  allocate the **guest's 0x50 bytes**, not 0x100, when constructing fresh.
* Required 0x50-byte shape (from `0x252095e`):

| Offset | Value | Notes |
| - | - | - |
| `+0x00` | vptr = genuine guest RVA | `0x6e04ec0` for the factory object |
| `+0x08..0x17` | 0 | 16 zero bytes |
| `+0x18` | 0 | 8 bytes |
| `+0x20` | `int 0` | read by `0x321a85e` (slot `+0x10`) and `0x276fd08` (slot `+0x90`) |
| `+0x28..0x47` | 0 | two subobject slots; the guest fills 3–4 entries via `0x252117a`/`0x25215b4` |
| `+0x48` | `float 1.0f` (`0x3f800000`) | set by `0x25209ab` |

---

## 5. Faithful delegate construction — two sanctioned variants

### 5.1 Preferred: obtain the guest's own object (zero fabrication)

The two StartLuaAppDM sites do not create a delegate at all — they fetch an
object the guest already built. Reproduce that by **calling the helpers** with
the exact ABI the guest uses:

```c
/* Site A: no arguments, returns object pointer directly. */
typedef void *(*lua_delegate_get_a_fn)(void);
void *obj_a = ((lua_delegate_get_a_fn)(base + 0x1dd55fu))();
/* obj_a == base+0x7d9d100 ; *(void**)obj_a == base+0x7220868 */

/* Site B: one out-parameter; returns the out pointer, writes object into *out. */
typedef void *(*lua_delegate_get_b_fn)(void *out);
void *out = NULL;                 /* 8 bytes is enough for the returned pointer */
void *ret = ((lua_delegate_get_b_fn)(base + 0x25f7ff2u))(&out);
void *obj_b = out;                /* == base+0x72a5a88 ; *(void**)obj_b == base+0x6dc3840 */
```

Properties: guest-owned static allocation, genuine guest vptr, genuine
initialisation already performed by the guest, no host function in any slot,
no engine bytes touched. The helpers are idempotent one-shot singletons (they
check an init byte and return the published pointer on subsequent calls), so
calling them before or after the real StartLuaAppDM path is safe; the guest
will get the same object.

### 5.2 Fallback: guest-shaped fresh object (only if the singletons cannot be obtained)

If reaching `0x1dd55f2`/`0x25f7ff2` early is impossible, build the object the
guest's own factory builds — with the guest allocator and a genuine vptr, not
a host table:

```c
void *(*guest_malloc)(size_t) = (void *(*)(size_t))(base + 0x1dbab02u);
uintptr_t *o = guest_malloc(0x50);          /* exact size from 0x2520907 */
if (!o) return -1;
memset(o, 0, 0x50);
o[0]      = base + 0x6e04ec0u;              /* actual factory object vptr */
*(float *)&o[9] = 1.0f;                     /* +0x48 (o[9]) = 0x3f800000 */
/* +0x20 (o[4]) already zero -> slot 0x10 getter returns 0, matching factory */
*(void **)((char *)app + 0x408) = o;        /* single 8-byte store, as guest does */
```

Then, **and only then**, call the genuine slot to prove the delegate is live:

```c
uintptr_t vt = *(uintptr_t *)o;
int (*get_state)(void *) = (int (*)(void *))(vt + 0x10);   /* 0x321a85e */
(void)get_state(o);
```

Caveats (DERIVED, important for honesty): this reproduces the header and scalar
fields but **not** the 3–4 subobjects the ctor builds at `+0x28` via
`0x252117a`/`0x25215b4`; slot `+0x90` (`0x276fd08`) reads `obj+0x38`, so with
the fallback it will see 0 and take its early-out branch. Prefer §5.1. Use
`0x6dc3840` or `0x7220868` as the vptr only when emulation targets the
corresponding singleton path; **never** use `0x6e047b0` as the object vptr
(reasons in §2.3). Do **not** fabricate a control block at `app+0x410` with
host pointers; leave it 0 or use the value returned by the guest helper path.

### 5.3 Prohibitions (unchanged from the task)

* No host function in any vtable slot — delete `sober_noop_virtual` usage for
  this object.
* No byte patches to the engine; no `mprotect`/trampolines at delegate sites.
* No fabricated callbacks and no marker emission; decision 013 stays strict.

---

## 6. Exact construction sequence and opt-in experiment

Proposed new knob: **`SOBER_FAITHFUL_APP_DELEGATE=1`** (default OFF), owned by
the lead to wire into `src/linux/guest_app_shell.c` (this worker edits no
source). It should **replace**, not sit beside, the `SOBER_SEED_APP_DELEGATE`
install when set.

1. **Resolve base/app.** Use the existing path: `wrapper = ((void *(*)())
   (base+0x24ad6fcu))(); app = *(void **)(wrapper+0x20);` (as in
   `seed_user_controller`, `guest_app_shell.c:781-793`). Fail with a log if
   either is NULL.
2. **Guard against clobbering a genuine delegate.** Read
   `cur = *(void **)(app+0x408)`. If `cur != NULL`, log
   `faithful-delegate: app+0x408 already non-NULL (<cur>) — skip` and do
   nothing. (The guest may have installed one via InitWithParams before we
   arrive.)
3. **Obtain the genuine object** via §5.1 site A helper `0x1dd55f2` (preferred;
   simplest ABI). Site B `0x25f7ff2` may be selected by a second knob for A/B
   comparison.
4. **Validate before publishing.** Check `obj != NULL`, `vt = *(uintptr_t *)obj`
   equals `base+0x7220868` (site A) or `base+0x6dc3840` (site B), and that the
   three function pointers `vt+0x10`, `vt+0x38`, `vt+0x90` lie inside the
   mapped guest text segment. Log each value; on mismatch, abort the install
   (do not fall back silently).
5. **Install with one 8-byte store:** `*(void **)((char *)app+0x408) = obj;`
   This matches the guest's own `mov %rax,0x408(%rbx)`. Do not zero adjacent
   app fields — the fresh app object already has them at 0, and the guest's
   own site-A code writes the same zeros.
6. **Call the genuine slot once** (`vt+0x10`) with `rdi=obj` to confirm the vptr
   dispatches into guest code; log the returned int. No guest bytes are
   patched; this is an ordinary virtual call the guest itself makes at
   `0x24aec73`/`0x25f7efc`.
7. **Continue the real sequence unchanged** (with all other opt-ins at their
   current settings): `AppStart 0x247dc8d` → `StartAppWithParams 0x26fe80f` →
   `nativeAppBridgeStartLuaAppDM 0x25448f6` →
   `UpdateSurfaceAppWithPlatformParams 0x2773194`. Use `SOBER_APP_SLOT_DIAG`
   (`log_app_slots`, `guest_app_shell.c:833-837`) at every step.
8. **Marker:** add `getenv("SOBER_FAITHFUL_APP_DELEGATE")` to the `seeded_state`
   OR-chain at `guest_app_shell.c:1336-1340`. The gate must still withhold
   `app shell rendered` (`marker withheld: diagnostic guest-state seed active`).

Expected log lines (diagnostic only):

```
[app-shell] faithful-delegate: obj base+0x7d9d100 vt base+0x7220868 slots +0x10=...,+0x38=...,+0x90=...
[app-shell] faithful-delegate: installed genuine guest object at app+0x408 (helper 0x1dd55f2)
[app-shell] faithful-delegate: slot+0x10 getter -> <int>
```

---

## 7. Observations, success criteria and falsifiers

Baseline (RUNTIME, from lead logs / PROGRESS.md):

* `PROGRESS.md:173-174`: with the flag at 0, calling StartLuaAppDM faults at
  `0x320581b` on a NULL `app+0x408`; that is the exact consumer this
  experiment targets.
* `/tmp/opencode/lua.log:3712-3726`: with the current fake delegate seeded, the
  sequence advances past `0x320581b` (StartApp and UpdateSurface ran) but ends
  in `RBXCRASH: Can't initialize the TaskScheduler before flags have been
  loaded`, faulting RVA `0x6caef67` — a separate flag-state blocker, **not**
  delegate evidence.
* `PROGRESS.md:132-133`: a NULL `app+0x408` is one of the two next genuine
  faults after the controller seed.

Success criteria **for this task only**:

1. `app+0x408` becomes non-NULL via the **guest helper's** returned pointer,
   and its vptr equals the expected genuine RVA (§6 step 4).
2. The `vt+0x10` virtual call returns without entering harness code (verify the
   call target address is inside the guest mapping).
3. The previous `0x320581b` fault is cleared; any new fault is at a different
   RVA and is reported honestly as a new blocker.
4. **Zero** `app shell rendered` emissions and an explicit withhold line —
   decision 013 is untouched.

Falsifiers / what would disprove the hypothesis:

* Site A/B helper returns NULL or a vptr other than `0x7220868`/`0x6dc3840`
  under our JNI model (means the lazy-init guard did not complete; investigate
  `0x2a0d6c0`/`0x2a0d7d0`, not the delegate).
* The `vt+0x10`/`vt+0x90` calls fault inside guest code on fields our bridge
  never populates — this confirms the delegate was never the real blocker and
  the remaining work is JNI/autovalue fidelity (as `PROGRESS.md:174-175`
  already concludes).
* A genuine `onAppShellReloadNeeded()V` / `gameLoadedCallback(J)V` appearing
  under this knob still does **not** satisfy decision 013 while the knob is
  set; it must be re-tested with every opt-in unset.

---

## 8. Risks, caveats, honesty

* The two site helpers initialise **process-global singletons**. Calling them
  earlier than the guest would changes initialisation order (DERIVED); this is
  a diagnostic behaviour change, so it must stay opt-in and force-withhold the
  marker.
* Site B's object is intrusive-refcounted (`0x25f806a`, `lock xadd +0x20`).
  Publishing it into `app+0x408` without also delivering the matching shared
  ref (`-0x48(%rbp)` in site B) may lead to an early release; prefer site A for
  the first experiment.
* The fallback in §5.2 is explicitly **not** a full reconstruction of the
  0x50-byte object (missing `+0x28` subobjects). It is better than the host
  no-op vtable but is not evidence of a real app-shell frame.
* `0x6e047b0` is cited in the task as "real vtable"; it is genuine, but §2.3/§3
  show it is not the object vptr on any of the three guest write paths. Using
  it as `*(void**)obj` would misdispatch slot `+0x10` to a factory function.
  This correction is the main new finding of this worker.
* No engine bytes were modified; SHA-256/BuildID re-verified unchanged.

---

## 9. Evidence references

* Frozen engine disassembly via `objdump -d --start-address/--stop-address`:
  `0x25d7d80-0x25d7f20`, `0x25f7dc0-0x25f7f20`, `0x25f7ff2-0x25f80d0`,
  `0x1dd55f2-0x1dd5641`, `0x24aec00-0x24aed50`, `0x2520892-0x2520b30`,
  `0x252095e-0x2520b3d`, `0x3205800-0x3205850`, `0x32086aa-0x32086e0`,
  `0x321a85e`, `0x276fd08`, `0x3281c9e`, `0x2f26936`, `0x2f28426`.
* APS2 relocation decode: `python3 /tmp/opencode/reloc.py dump 0x6e047b0 24`,
  `... 0x6e04ec0 24`, `... 0x7220868 24`, `... 0x6dc3840 24`;
  `python3 /tmp/opencode/reloc.py slots 0x2520892 0x2520c1c 0x1dd72a0`.
* Harness source (READ ONLY): `src/linux/guest_app_shell.c:690,
  729-754, 779-815, 833-837, 1336-1340, 1378`;
  `src/jni/jni_native_objects.c:243-256`; `src/jni/jni_native_objects.h:53-57`.
* Prior analysis: `analysis/appstate_fields.md` §2/§3,
  `analysis/lua-app-start.md` §3.2/§3.3,
  `analysis/native-object-layouts.md` §1/§2,
  `analysis/decision-memo-alt-thunk-vs-013.md` §1/§4,
  `docs/decisions/013-no-weak-app-shell-marker.md`.
* Lead logs: `/tmp/opencode/lua.log:3712-3726`, `full.log:3709`,
  `seedctl3.log:3696`, `gseed-all.log:3679`; `PROGRESS.md:132-133, 160-175,
  319-336`.

---

## 10. Addendum — wider disassembly of the two sites (OBSERVED)

A full-radius read of both sites (initial `dis.sh` run, later completed)
confirms every byte quoted in §2 and adds two relevant facts:

1. **Site A re-fetches the same singleton and calls slot `+0x10` with real
   arguments** at `0x25d7f1b-0x25d7f76`:

   ```
   25d7f0e  cmpb $0x0,0x7cdd149(%rip)     ; assert flag (NULL in our lua.log)
   25d7f15  je   25d7fa8                  ; branch skipped in current harness
   25d7f1b  call 1dd55f2                  ; SAME genuine singleton
   25d7f35  mov (%rax),%rax               ; vptr = 0x7220868
   25d7f38  mov $0x198,%esi
   25d7f3d  mov $0x8,%edx
   25d7f42  mov %r12,%rdi
   25d7f45  call *0x10(%rax)              ; -> 0x1dd56f2 (real method)
   25d7f48  mov %rax,%r15                 ; return used as an object pointer
   25d7f55  movups %xmm0,0x8(%rax)        ; WRITES through the returned pointer
   25d7f59  lea 0x6fae0f0(%rip),%rax
   25d7f60  mov %rax,(%r15)               ; install vptr 0x6fae0f0 in new object
   25d7f63  mov %r12,0x18(%r15)           ; back-pointer to the delegate
   ```

   This is concrete proof that slot `+0x10` is not a void no-op: it must return
   a valid guest object pointer (`0x1dd56f2`), which the caller immediately
   writes through. The present `sober_noop_virtual` returns 0, so if
   `0x7cdd149` were ever set this path would fault at `0x25d7f55`. A genuine
   delegate is therefore a correctness requirement, not just an honesty one.

2. **Site B's install is immediately followed by a consume/release pair**:
   `0x25f7e8c call 1dc1208` (log with string `0x4395a5`) and
   `0x25f7e98 call 24bb532(app, -0x48)` (takes the same 16-byte shared ref
   returned by helper `0x25f7ff2`). The error path at
   `0x25f7eed-0x25f7efc` again calls `app+0x408 -> vptr -> slot +0x10`.
   This reinforces §8's warning that site B publishes an intrusive ref: use
   site A (`0x1dd55f2`) for the first experiment.

No change to the §5/§6 recommendation: the guest helpers are the faithful
source, and the host-no-op delegate remains disqualified.
