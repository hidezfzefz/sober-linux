# `realloc(): invalid pointer` abort with TSD seed — the harness seeded a **static buffer** as the guest's TLS backing array (task #464 follow-up)

Owner: **flagbridge**. Analysis only — no `src/`, CMake, gate or `tests/` edits;
no decision-013 marker touched. Follow-up to
`analysis/tls-2a0de60-r70-correction.md`. Date 2026-11-20.

Evidence: `/tmp/opencode/tsd.log`, `/tmp/opencode/tsd-min.log`,
`/tmp/opencode/libroblox.dis`, `reloc.py`, `src/compat/bionic_bootstrap/bionic_bootstrap.c`,
`src/jni/native_jni.c`, `src/linux/guest_app_shell.c`, `src/compat/bionic_libc.c`.

## 1. Verdict

The SIGABRT in `nativeGameGlobalInit` is **not a guest invariant failure** and not
a fault in `0x2eb6885`/`0x2e542b5`. It is a **host glibc heap abort** caused by the
harness itself:

> `SOBER_STUB_TSD_SEED=1` installs a **static, non-heap** buffer
> (`static unsigned char state[65536]` in `src/linux/guest_app_shell.c:64`) into
> the guest's TSD slot for key 3. The guest's TLS accessor later needs to grow
> that array and calls its allocator bridge `0x227aafa` → `host_reallocate()` →
> **glibc `realloc()`** on the static address. glibc prints
> `realloc(): invalid pointer` and calls `abort()` (SIGABRT).

`0x2eb6885` and `0x2e542b5` are simply the return addresses of the call chain
that happened to be executing when the abort fired. The TSD route itself is
sound; the *seed buffer lifetime/allocator* is wrong.

## 2. Exact chain (all RVAs from the frozen object)

```
0x233764a  harness -> JNI nativeGameGlobalInit
  -> 0x2eb67d0  reads once-guard global 0x72a30c8; if != -1 -> call_once
  -> 0x2e54220  call_once(flag=0x72a30c8, arg, callback=0x2eb687c)
       0x2e542b2: call *%r14          ; invokes callback
       0x2e542b5: <-- FP #14 (after callback returns)
  -> 0x2eb687c  thunk:
       0x2eb6880: call 0x23376d6      ; real nativeGameGlobalInit core
       0x2eb6885: mov %rax,0x72a30c0  ; <-- FP #13
  -> 0x233776a  inside core; calls scheduler query 0x2a13390
  -> 0x2a13390  TLS accessor user:
       0x2a13394: call 0x2a0de60
       0x2a13399: call 0x2a0de60      ; <-- FP #11
       0x2a1339e: mov (%rax),%rax ; cmpq $0,0x28(%rax)
  -> 0x2a0de70  inside 0x2a0de60, after call 0x2ea6440(0x722e560)  <-- FP #10
  -> 0x2ea6490  inside 0x2ea6440: call 0x227aafa  <-- FP #9 (grow/realloc)
  -> 0x227aafa  guest allocator "reallocate" (arena table 0x7cd9040,
                thread-state fallback 0x721db80, TSD key global 0x7282000=2)
  -> host_reallocate() in bionic_bootstrap.c:332-338
       0x...: void *q = realloc(p, n);   <-- glibc realloc(STATIC BUFFER)
  -> glibc: "realloc(): invalid pointer" -> abort() -> signal 6
```

`tsd.log` report line:
`realloc(): invalid pointer` immediately before the GUEST FAULT block, with
frames `#8 ./build/guest-init-host+0x15731` (host bridge), `#9 guest+0x2ea6495`,
`#10 guest+0x2a0de70`, `#11 guest+0x2a13399`, `#12 guest+0x233776a`,
`#13 guest+0x2eb6885`, `#14 guest+0x2e542b5`, `#15 guest+0x2eb67d0`,
`#16 guest+0x233764a`. `tsd-min.log` is identical with only TLS+TSD seed and no
other opt-ins, proving the seed itself is the cause.

## 3. The two conflicting seeds (why the good one was lost)

Both knobs write the **same** stub TSD slot for key 3 (key read from guest global
`0x72a1b08`), and the log order shows the static one wins:

```
[sober-jni] seeded STUB TSD key 3 (rva 0x72a1b08) -> 0x... (4096 bytes)   ; calloc, VALID
[app-shell] TLS seed (key rva 0x72a1b08, 4096 bytes) -> rc=0
[app-shell] stub-TSD seed: key=3 state=0x... rc=0                         ; static, INVALID
```

| Knob | Implementation | Buffer | glibc `realloc()` safe? |
| --- | --- | --- | --- |
| `SOBER_TLS_SEED` | `src/jni/native_jni.c:1226-1249` `sober_jni_seed_stub_tsd()` | `calloc(1, 4096)` | **yes** (heap pointer) |
| `SOBER_STUB_TSD_SEED` | `src/linux/guest_app_shell.c:49-70` `seed_stub_tsd()` | `static unsigned char state[65536]` | **no** (BSS address) |

`guest_app_shell.c` runs later (line 3684) and overwrites the valid heap pointer
with the static one. That single overwrite turns a legal realloc into SIGABRT.

## 4. The guest's TLS backing-array format (and why growth is forced)

`0x2ea6440(key_desc)` uses the stub TSD value for key 3 as a **growable array**:

```
 2ea6469: r14 = pthread_getspecific(key 3)        ; the seeded buffer
 2ea646c: r15 = *(r14 + 8)                        ; recorded capacity
 2ea6470: cmp r13,r15 ; jae 2ea6573               ; r13 = key-desc index (+0x10)
 2ea6479..2ea6490: call 0x227aafa(r14, new_size)  ; GROW -> realloc -> abort
 2ea655a: *(r14) = 1                              ; slot[0] = "initialised"
 2ea6561: *(r14+8) = capacity                     ; slot[8] = capacity
 2ea6573: r15 = *(r14 + 8 + index*8)              ; per-index block
 2ea65de: store newly allocated per-index block
```

A zero-filled seed has `capacity == 0`, so any nonzero descriptor index takes
the grow path. (Key descriptors are globals `0x722e540`/`0x722e560`; their
indices are their `+0x10` fields — note the old scheduler "neutralize"
`memset(p+0x28,0,16)` also overwrote the second descriptor's index at
`0x722e570`; with `SOBER_NO_SCHED_NEUTRAL=1` that is no longer happening.)

## 5. Next faithful opt-in steps (lead/worker own `src/`; I edit nothing)

### Step 1 — minimal A/B: drop the static seed, keep the heap seed
Run with **`SOBER_TLS_SEED=1` + `SOBER_NO_SCHED_NEUTRAL=1`**, with
**`SOBER_STUB_TSD_SEED` OFF**. The guest will `realloc()` the `calloc(4096)`
buffer — a perfectly legal glibc realloc — complete `0x2ea6440`, and the
`0x2a13390`/`0x2a132c0` nesting logic will run on persistent TLS state. Expected:
the `realloc(): invalid pointer` line and SIGABRT disappear; capture the next
genuine fault RVA or callback. No guest bytes patched, no callbacks, no marker.

### Step 2 — fully faithful seed: guest-compatible buffer + guest-format header
If step 1 still shows instability, replace the static seed with:

1. a buffer allocated from the **same allocator family** as `host_reallocate()`
   (host glibc `calloc`/`malloc`, or better the guest's own allocator via
   `guest_base+0x1dbab02`, which is what `gseed` already uses), sized generously
   (e.g. `0x10000`);
2. pre-initialise the header exactly as `0x2ea655a/0x2ea6561` do:
   `*(uint64_t*)(buf+0) = 1`, `*(uint64_t*)(buf+8) = capacity`, with
   `capacity >= max(index(0x722e540), index(0x722e560)) + 1` (use 512 to be safe).

Then `cmp %r13,%r15; jae` at `0x2ea6470` skips the grow entirely, and the guest
allocates each per-index block itself at `0x2ea65de` with its own allocator.
This is guest state, not a byte patch or a synthetic callback. Add a read-only
diagnostic that logs the two descriptor indices and the buffer capacity.

### Step 3 — about the earlier "call `0x2a132c0(edi=1)` on the worker" idea
That option is **not an alternative to fixing TSD** — `0x2a132c0` itself calls
`0x2a0de60` → `0x2ea6440` at `0x2a132cd`/`0x2a132d2`, so with a static/invalid
TSD backing it would hit exactly the same realloc abort. It is only useful
*after* step 1/2, if a residual nesting underflow appears.

If needed, target the real worker without fabricating anything: the guest itself
calls `JavaVM->AttachCurrentThread(name="FunctionMarshaller")` (observed in the
faithful-JavaVM log; other guest workers attach as `" RBX Worker B"`). The
harness already sees that genuine guest JNI call, so on that same thread, right
after the attach returns, invoke `guest_base + 0x2a132c0` with `edi = 1` — the
guest's own "enter/increment" path at `0x2a13332..0x2a1333c` writes a valid
nesting count into `obj+0x70`. This is a real guest-code call on the correct
thread, uses no byte writes, emits no callback, and sets no decision-013 marker.

## 6. Evidence index

* `tsd.log:3701-3721`, `tsd-min.log:3695-3714` — `realloc(): invalid pointer`,
  SIGABRT, FP chain `0x2ea6495 → 0x2a0de70 → 0x2a13399 → 0x233776a →
  0x2eb6885 → 0x2e542b5 → 0x2eb67d0 → 0x233764a`.
* `libroblox.dis`: `0x2ea6440` (TSD array grow, `0x2ea6490` call `0x227aafa`),
  `0x2a0de60`, `0x2a13390`, `0x2a132c0`, `0x2eb687c`/`0x2eb6885`,
  `0x2e54220`/`0x2e542b5`, `0x23376d6`/`0x233776a`, `0x227aafa` (guest realloc).
* `src/compat/bionic_bootstrap/bionic_bootstrap.c:332-338,592-605` —
  `host_reallocate()` calls glibc `realloc`; bridge installed at `0x227aafa`.
* `src/jni/native_jni.c:1226-1249` (heap seed), `src/linux/guest_app_shell.c:49-70,887`
  (static seed), `src/compat/bionic_libc.c:470-490` (stub 512-slot TSD).
* `analysis/tls-2a0de60-r70-correction.md`, `analysis/jni/recursion-note.md`
  Addendum 2 (key namespacing background).
