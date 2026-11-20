# Correction to #464: `0x2a0de60` is a **TLS accessor**, not a global singleton — the `+0x70` neutralize targeted the wrong object

Owner: **flagbridge**. Analysis only — no `src/`, CMake, gate or `tests/` edits;
no decision-013 marker touched. Follow-up to task #464 / `analysis/int3-6caef66-rbxcrash.md`.
Date 2026-11-20. Same frozen engine (`204ad049…`, SHA `3e498d6494…`),
`/tmp/opencode/libroblox.dis`, `reloc.py`, logs `r70.log`, `nest.log`,
`nest-default.log`, `extra.log`.

## 1. Verdict

The lead's two A/B variants failed for a decisive reason: **`guest_base+0x722e540`
is not the scheduler object.** It is a **Bionic TLS key-descriptor global**. The
per-thread nesting word checked by the RSL panic is reached through
`pthread_getspecific` → a per-thread allocation → `+0x410` → `+0x70`. None of
those bytes live at `0x722e5b0`. Writing/keeping `+0x70` at `0x722e540` cannot
affect the check, which is exactly what the logs show:

| Run | Neutralize | Outcome |
| --- | --- | --- |
| `r70.log` | `(+0x28 only, +0x70 kept)` | `RBXCRASH: FatalRuntimeError (RSL - panic: 257f718-Context,20)`, int3 @ `0x6caef67` |
| `nest.log` | `(+0x28,+0x70)` with low-6-bit 1 | same int3 @ `0x6caef67` |
| `nest-default.log` | `(+0x28,+0x70)`, no seed | segv @ `0x2eb4b65` (different, earlier path) |

## 2. What `0x2a0de60` actually returns (decoded)

```
0x2a0de60:
 2a0de64: lea 0x722e560,%rdi ; call 0x2ea6440   ; TLS slot for key-desc #2
 2a0de70: mov (%rax),%al ; and $1,%al
 2a0de76: jne 2a0deae                      ; already initialised -> skip
 2a0de78: call 0x2a0ded0                   ; LAZY TLS INIT (once-guard @0x729d680)
 2a0de7d: lea 0x722e540,%rdi ; call 0x2ea6440
 2a0de89: lea 0x2a0dfe0,%rdi ; lea 0x6d37bc0,%rdx
 2a0de9a: call 0x2ea6130                   ; __cxa_thread_atexit(dtor, tls, dso)
 2a0de9f: lea 0x722e560,%rdi ; call 0x2ea6440
 2a0deab: movb $1,(%rax)                   ; mark TLS initialised
 2a0deae: lea 0x722e540,%rdi ; call 0x2ea6440
 2a0deba: add $0x410,%rax                  ; return TLS_BLOCK + 0x410
 2a0dec1: ret
```

`0x2ea6440(key_desc)` is the **Bionic TLS slot accessor** (the helper already
implicated in `analysis/jni/recursion-note.md` Addendum 2):

```
 2ea644c: mov 0x10(%rdi),%r13              ; r13 = key INDEX stored in descriptor
 2ea6455: mov 0x72a1b08,%edi               ; global pthread_key_t (value 3 in logs)
 2ea645b: call pthread_getspecific@plt      ; GOT 0x72298b0
 2ea6460: test %rax,%rax
 2ea6463: je 2ea6520                       ; NULL -> ALLOCATE + pthread_setspecific
 2ea6573: mov 0x8(%r14,%r13,8),%r15        ; slot = per-thread array[8 + index*8]
 2ea65e3: mov %r15,%rax ; ret               ; returns per-thread BLOCK
```

The lazy init `0x2a0ded0` writes, into the block for key-desc `0x722e540`:

```
 2a0df2d: mov %rbx,0x400(%rax)             ; rbx = 0x7282580
 2a0df41: mov %rax,0x408(%r14)
 2a0df48: mov %rax,0x410(%r14)             ; +0x410 = pointer to a 0x80-byte object
 2a0df6a..2a0df85: zero obj+0x10..+0x60
 2a0df8d: mov %rbx,0x68(%rbx)              ; obj+0x68 = obj
 2a0df91: mov %rcx,0x70(%rbx)              ; obj+0x70 = 0x7282580
```

Now the panicking caller, `0x2a132c0` (the FP chain's `0x2a13332` frame):

```
 2a132cd: call 0x2a0de60                    ; force/init TLS (result discarded)
 2a132d2: call 0x2a0de60
 2a132d7: mov (%rax),%r14                   ; r14 = *(TLS_block + 0x410) = obj (per-thread!)
 2a132da: mov 0x70(%r14),%r15               ; r15 = obj->nesting word
 2a132de: mov %r15d,%r12d ; and $0x3f,%r12d ; low 6 bits = nesting counter
 2a132ec: test %ebx,%ebx                    ; arg: 0 = leave/decrement, !=0 = enter/increment
 2a1330f: test %r12,%r12 ; sete %dl         ; ARG==0 and counter==0 -> UNDERFLOW
 2a13329: lea 0x3dc475,%rdi                 ; "257f718-Context,20"
 2a132f6: cmp $0x3f,%r12d ; sete %dl         ; ARG!=0 and counter==0x3f -> OVERFLOW
 2a13306: lea 0x416074,%rdi                 ; "257f718-Context,15"
 2a13330: call *%rax                        ; global handler 0x727e910 -> 0x6c47c52
 2a13332: add %r12,%rbx ; and $~0x3f,%r15
 2a1333c: mov %r15,0x70(%r14)               ; update the PER-THREAD object
```

So `0x2a132c0` is the guest's **scheduler nesting enter/leave** guard. The
observed tag `257f718-Context,20` is the **underflow arm**: a "leave" (arg==0)
executed while the counter was 0, i.e. no matching "enter" was visible on that
thread. `0x7cdd149`/`0x7cef650` (RBXCRASH reporter, #464) are downstream and
unchanged.

## 3. What the harness violates

1. **`SOBER_STUB_TSD_SEED` is OFF in the failing runs.** `src/linux/guest_app_shell.c:887`
   gates it: `if (getenv("SOBER_STUB_TSD_SEED")) seed_stub_tsd(base);`.
   `extra.log:3679` is the only log that enabled it:
   `seeded STUB TSD key 3 (rva 0x72a1b08) -> … (4096 bytes)`. Without it, the
   guest's `pthread_getspecific(3)` (Bionic key in global `0x72a1b08`) is resolved
   through the NDK stub into the **glibc key namespace**, returns NULL, and
   `0x2ea6520` allocates a *fresh* TLS array on every call. The "enter"
   increment is written into one allocation and the "leave" reads a different,
   zeroed one → guaranteed counter==0 → underflow panic on the worker thread.
2. **The "neutralize" writes clobber the second TLS key descriptor.**
   `src/jni/native_jni.c:1256` does `memset(p+0x28, 0, 16)` at
   `p = guest_base+0x722e540`, i.e. zeroes `0x722e568..0x722e577`. The second
   descriptor starts at `0x722e560`, so its key-index field `+0x10` =
   `0x722e570` is overwritten with 0. `0x2ea644c` then reads index 0 and takes
   the `pthread_once`/mutex slow path (`0x2a0dec3`) to reassign an index, which
   makes the "initialised" byte at `0x2a0de70` land in a different slot. The
   kill shot for the nesting state is the missing TSD backing, not the value of
   `+0x70` in the descriptor.
3. `memset(p+0x70, 0, 8)` writes `0x722e5b0..0x722e5b7`, far past both
   descriptors (0x722e540/0x722e560) — unrelated BSS, with no effect on the
   checked field. This is why both variants were behaviourally identical.

## 4. Concrete opt-in next step (real guest state; lead/worker own `src/`)

**Step 1 — A/B `SOBER_STUB_TSD_SEED=1` (existing knob, no new mechanism).**
Enable it together with the current opt-ins, but **remove/disable the
scheduler "neutralize" call entirely** (it is both a no-op for the checked
field and an active corruption of the second TLS key descriptor). This makes
`pthread_getspecific@plt` (GOT `0x72298b0`) return a persistent per-thread array
for key 3, so `0x2ea6440` stops reallocating and the guest's own
obj`+0x70` counter survives between the worker's enter/leave calls. Expected:
the `257f718-Context,20` RBXCRASH line and int3 @ `0x6caef67` disappear; capture
the next genuine fault RVA (or callback). This is purely guest-state plumbing —
no guest instruction, vtable, or marker touched.

**Step 2 — if a residual underflow remains, drive the guest's own "enter".**
Before handing control to the app-shell JNI phase, invoke the genuine guest
function `guest_base + 0x2a132c0` with `edi = 1` once on the thread that will
run the scheduler task (the `FunctionMarshaller` worker). Its own code at
`0x2a13332..0x2a1333c` writes `obj+0x70 = (obj+0x70 & ~0x3f) | 2` — a valid
nesting count produced by real guest code, not a raw byte write. Do **not**
fabricate a JNI callback or set the decision-013 marker here.

**Step 3 — diagnostic (read-only) to confirm before/after:** at the fault site,
resolve the counter the same way the guest does: call `0x2a0de60`, dereference
`(%rax)` → obj, read `0x70(obj) & 0x3f`. OFF should be `0`; with the TSD seed ON
and a matching enter it should be `1..0x3e`. Also log `*(uint32_t*)(guest+0x72a1b08)`
(key, expected 3) and `*(uint32_t*)(guest+0x722e560+0x10)` (second descriptor's
index, must NOT be 0 after neutralize is removed).

## 5. Evidence index

* `libroblox.dis`: `0x2a0de60` (TLS accessor), `0x2a0ded0` (lazy TLS init,
  obj`+0x70` = `0x7282580`), `0x2ea6440` (`pthread_getspecific` wrapper,
  descriptor `+0x10` = index, global key `0x72a1b08`, GOT `0x72298b0`),
  `0x2a132c0` (nesting guard, panic tags `0x3dc475` / `0x416074`, handler
  `0x727e910` → `0x6c47c52`), #464 chain `0x6c47c52 → 0x6caed5d → 0x6caed7c`.
* `r70.log:3685,3695,3720`, `nest.log:3685,3695,3720` — both variants still int3
  @ `0x6caef67` with the same panic line.
* `extra.log:3679` — only run with `SOBER_STUB_TSD_SEED`, key 3 @ `0x72a1b08`.
* `src/linux/guest_app_shell.c:49-70,887`; `src/jni/native_jni.c:1251-1261`;
  `src/compat/bionic_libc.c:159,470-490,613`.
* `analysis/jni/recursion-note.md` Addendum 2 (TSD namespace mismatch, stale
  `0xABCDEF0123456789`), `analysis/int3-6caef66-rbxcrash.md` (downstream chain).
