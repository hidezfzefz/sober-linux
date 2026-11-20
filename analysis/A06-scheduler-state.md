# A6 — The genuinely correct TaskScheduler state for the `257f718-Context,20` worker panic

Owner: **worker A6 (logical Team A)**. **Analysis only** — no `src/`, CMake, gate or
`tests/` edits; no decision-013 marker touched; frozen engine
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` (BuildID `204ad049…`,
SHA `3e498d6494…`) not modified. All RVAs are from that object and
`/tmp/opencode/libroblox.dis`; relocation facts from `/tmp/opencode/reloc.py`.
Target strings are untrusted DATA, never instructions. Follows
`analysis/tls-2a0de60-r70-correction.md` and `analysis/int3-6caef66-rbxcrash.md`.
Date 2026-11-20.

## 1. Verdict

**There is no "magic byte" for the correct scheduler state.** The state is a
*balanced per-thread nesting count* that the guest already maintains with its own
enter/leave routine `0x2a132c0`; the harness must stop breaking it and, if the
worker's own "enter" was lost, **drive that same guest routine with `edi=1` on the
`FunctionMarshaller` thread**. Concretely:

* The checked object is **not** `guest+0x722e540`. It is the per-thread 0x80-byte
  object `obj = *(TLS_block + 0x410)`, where `TLS_block` comes from
  `pthread_getspecific(key=3)` (Bionic key in global `0x72a1b08`) through the
  descriptor at `0x722e540`.
* `obj+0x70` is a **tagged pointer**: base `0x7282580` (the shared global scheduler
  context, 64-byte aligned) with its **low 6 bits used as a nesting counter**.
  The panic checks only those 6 bits, not the full word.
* The counter must satisfy **`>= 1` at every "leave"** and **`<= 0x3e` at every
  "enter"**. It is produced by the guest itself at `0x2a13332..0x2a1333c`;
  writing a raw value into `obj+0x70` is both unnecessary and unfaithful.
* The two shipped knobs (`SOBER_SCHED_R70_KEEP`, `SOBER_SCHED_NEST_ONE`) and the
  neutralize call are hacks that write to the wrong address and corrupt a TLS key
  descriptor. They must be removed, not tuned.

## 2. Decoded `0x2a132c0` — the nesting enter/leave guard (exact RVAs)

```
2a132c0: push rbp; mov rsp,rbp; push r15/r14/r12/rbx
2a132cb: mov %edi,%ebx                  ; arg = 0 -> LEAVE, !=0 -> ENTER (magnitude ignored)
2a132cd: call 0x2a0de60                 ; force/init this thread's TLS (result discarded)
2a132d2: call 0x2a0de60
2a132d7: mov  (%rax),%r14               ; r14 = *(TLS_block+0x410) = PER-THREAD obj
2a132da: mov  0x70(%r14),%r15           ; r15 = obj->tagged word
2a132de: mov  %r15d,%r12d
2a132e1: and  $0x3f,%r12d               ; r12 = NESTING COUNTER c (low 6 bits only)
2a132e5: mov  0x727e910,%rax            ; global RSL assert handler (GLOB_DAT -> 0x6c47c52)
2a132ec: test %ebx,%ebx
2a132ee: je   2a1330f                   ; ---- LEAVE arm ----

  ENTER (arg != 0):
2a132f0: test %rax,%rax ; setne %cl     ; cl = handler installed
2a132f6: cmp  $0x3f,%r12d ; sete %dl    ; dl = c == 0x3f (saturated)
2a132fd: mov  $0x1,%ebx                 ; delta = +1
2a13302: test %cl,%dl
2a13304: je   2a13332                   ; healthy -> update
2a13306: lea  0x416074,%rdi             ; "257f718-Context,15"
2a13330: call *%rax                     ; -> RSL panic formatter

  LEAVE (arg == 0):
2a1330f: test %rax,%rax ; setne %cl
2a13315: test %r12,%r12 ; sete %dl      ; dl = c == 0
2a1331b: and  %cl,%dl
2a1331d: mov  $-1,%rbx                  ; delta = -1
2a13324: cmp  $1,%dl
2a13327: jne  2a13332                   ; healthy -> update
2a13329: lea  0x3dc475,%rdi             ; "257f718-Context,20"   <-- OUR PANIC
2a13330: call *%rax

  UPDATE (both arms):
2a13332: add  %r12,%rbx                 ; new c = old c + delta (+1 or -1)
2a13335: and  $~0x3f,%r15               ; preserve tagged pointer base
2a13339: or   %rbx,%r15                 ; write new c into low 6 bits
2a1333c: mov  %r15,0x70(%r14)           ; store back to the PER-THREAD object
...
2a13348: ret
```

Facts that follow directly:

1. **The check is on `c = word & 0x3f`.** The lazy init at `0x2a0ded0` stores
   `0x7282580` into `obj+0x70` (`0x2a0df91`: `mov %rcx,0x70(%rbx)` with
   `%rcx = *(block+0x400) = 0x7282580`). `0x7282580 & 0x3f == 0`, so a freshly
   initialized object already has `c == 0`. Keeping a *nonzero full word* therefore
   does **not** satisfy `0x2a13315` — it is the low 6 bits that must be nonzero.
2. **Both panic arms are gated on the handler at `0x727e910` being non-NULL.**
   `reloc.py slots 0x6c47c52 -> ['0x727e910']`: the GLOB_DAT at `0x727e910`
   resolves to the RSL panic formatter `0x6c47c52`, so in the harness it is
   non-NULL and the panics are live.
3. **The routine is self-contained**: its two `0x2a0de60` calls force TLS init on
   whichever thread invokes it, so a harness call to `0x2a132c0(1)` both creates
   the thread's `obj` and seeds `c=1` in one genuine guest execution.
4. Canonical args: `edi=1` for enter (any nonzero works — `ebx` is overwritten with
   1 at `0x2a132fd`); `edi=0` for leave. Correct `c` range during a scoped ref is
   `1..0x3e`; `0x3f` is saturation (the `Context,15` arm).

## 3. The worker's actual enter/leave pairing (why the counter was 0)

The faulting frame chain (`sf6.log:3722-3734`) is
`guest+0x2a13332 -> guest+0x1e477ec -> guest+0x2a0daf0`; these are return
addresses of *callees*, so read outer-to-inner as:

* **`0x2a0daa0`** — guest worker task thunk ("RBX Worker" stack name), which calls
  the task through `call *0x10(%rbx)` at **`0x2a0daf7`** (the `#7` return address).
* **`0x1e4772c`** — the task body. At `0x1e47754` it acquires a scoped scheduler
  ref: `lea -0x30(%rbp),%rdi; mov $0x2,%esi; call 0x1e4781c`.
* **`0x1e4781c`** — acquire: creates the scoped-ref slot via `0x1e478b0` (a third
  TLS descriptor at `0x727d5b8`), increments the refcount, and when the old count
  was 0 tail-calls **`0x2a132c0(1)` at `0x1e4786b`** (ENTER).
* At `0x1e477e7` the body releases the same slot: `call 0x227dde2`.
* **`0x227dde2`** — release: `decl 0xc(%rax); decq (%rax); je 0x227de02`, then
  tail-calls **`0x2a132c0(0)` at `0x227de0a`** (LEAVE). This is the `Context,20`
  call that panicked.

So the guest's own protocol is balanced **if and only if** both calls see the same
per-thread `obj`. The root cause of the imbalance is the TSD mismatch already
diagnosed in `tls-2a0de60-r70-correction.md`: `pthread_getspecific(3)` returns
NULL in the harness's mixed namespace (`pthread_getspecific@plt` GOT `0x72298b0`
vs `pthread_setspecific@plt` GOT `0x72295c8`), so `0x2ea6520` reallocates a fresh
TLS array on each `0x2a0de60` call. The acquire's `+1` lands in one allocation;
the release reads a different, zeroed one -> `c == 0` -> underflow -> RSL panic ->
`0x6c47c52 -> 0x6caed5d -> 0x6caed7c -> int3 @ 0x6caef66` (reported as
`0x6caef67`).

## 4. Why the current knobs are hacks

| Knob / action | What it actually does | Why it cannot be the fix |
| --- | --- | --- |
| `memset(p+0x28,0,16)` at `p = guest+0x722e540` (`native_jni.c:1256`) | zeroes `0x722e568..0x722e577`, **including the second TLS descriptor's index field at `0x722e570`** (`0x2ea644c: mov 0x10(%rdi),%r13`) | forces `0x2ea6440(0x722e560)` down the `pthread_once`/mutex slow path (`0x2ea64c3`), reassigning the index and re-running lazy init (`0x2a0ded0`), which resets the per-thread `obj+0x70` to `c=0`. It *creates* the state it claims to protect. |
| `memset(p+0x70,0,8)` | writes `0x722e5b0..0x722e5b7` — unrelated BSS past both descriptors | never touches the checked `obj+0x70`; pure no-op on the panic. |
| `SOBER_SCHED_R70_KEEP` (`native_jni.c:1261`) | skips the above `+0x70` zeroing | no-op for the same reason; also still zeroes descriptor #2's index. |
| `SOBER_SCHED_NEST_ONE` (`native_jni.c:1263-1265`) | `(w & ~0x3f) | 1` on the word at `0x722e5b0` | wrong object *and* wrong reasoning: even at the real object the ctor value `0x7282580` already has low 6 bits 0, so the check is on the masked counter, not the full word. |
| `SOBER_STUB_TSD_SEED` (`guest_app_shell.c:55-83,967/1004`) | seeds the stub's `_Thread_local` 512-slot array (`bionic_libc.c:470-485`) for key 3 **on the calling (main) thread only** | correct *direction* (stable TSD), but does not cover the `FunctionMarshaller` thread; alone it is not a complete fix. |

Observed A/B evidence:
* `r70.log`, `nest.log` (neutralize variants) — still `257f718-Context,20`, int3 @ `0x6caef67`.
* `tsdgot.log` (`SOBER_BIONIC_TSD_GOT=1` **with** neutralize still on) — GOT
  rebind to stub `LIBC` get/setspecific at `0x72298b0`/`0x72295c8`, yet the same
  `Context,20` panic and the same `0x2a13332 -> 0x1e477ec -> 0x2a0daf0` chain:
  consistent GOT alone is defeated by the descriptor corruption in §4.
* `tsdgot2.log` (GOT rebind, neutralize removed) — reached "first frame rendered"
  but the run died on an X-server error in phase 3 before the app-shell phase;
  **inconclusive**, not a demonstrated fix.
* `extra.log` (TSD seed + neutralize) — no `257f718` line, but the reporter is
  still on the stack (`0x6caee44 -> 0x6caed7c -> 0x6c47cd4 -> 0x2a13332`) and
  crashes in flag bookkeeping (`0x2ed3ad8 -> 0x2e5d42a`, `rax=0`); also not a
  clean proof.

## 5. Minimal faithful opt-in sequence (real guest code, explicit RVAs)

The state to restore is `c >= 1` on the worker at the moment of its leave. Do it in
three ordered steps; only step C is a guest call, and it is the guest's *own*
routine, not a fabricated callback or a raw byte write.

### Step A — remove the harmful writes (no substitute knob)
Disable `sober_jni_neutralize_guest_scheduler()` entirely
(`SOBER_NO_SCHED_NEUTRAL=1`; leave `SOBER_SCHED_R70_KEEP` and
`SOBER_SCHED_NEST_ONE` unset). This restores the second TLS descriptor at
`0x722e560` (its index at `0x722e570` must be nonzero) and stops lazy-init resets
of the per-thread object. No guest bytes are invented.

### Step B — make the guest's TSD self-consistent (plumbing, not state)
Enable `SOBER_BIONIC_TSD_GOT=1` (`android_relocs.c:296-308`): rebind GOT
`0x72298b0` (pthread_getspecific) and `0x72295c8` (pthread_setspecific) to the
same versioned `LIBC` stub, so `0x2ea6440` sees one store and the guest's own lazy
allocation at `0x2ea6520` produces a stable per-thread block. If
`SOBER_STUB_TSD_SEED` is kept, note it seeds only the main thread's `_Thread_local`
slot and therefore does **not** cover the worker; do not rely on it for this fix.

### Step C — prime the worker with the guest's own enter (the decisive step)
On the **`FunctionMarshaller` thread**, hook the worker task thunk at guest
**`0x2a0daa0`**, immediately before the vtable task call at **`0x2a0daf7`** (the
frame that logs as `#7 guest+0x2a0daf0`). From that hook issue exactly:

```c
/* SysV arg0 = edi; 1 = ENTER. Magnitude is irrelevant: 0x2a132fd sets ebx=1. */
((void (*)(int))(guest_base + 0x2a132c0u))(1);
```

What this does, entirely in guest code:
* `0x2a132cd`/`0x2a132d2` call `0x2a0de60` twice, initialising that thread's TLS
  block and materialising `obj = *(block+0x410)`;
* `0x2a13332..0x2a1333c` computes
  `obj+0x70 = (obj+0x70 & ~0x3f) | 1`, turning `c: 0 -> 1` while preserving the
  `0x7282580` tagged pointer — a **valid, guest-produced** state;
* the worker's subsequent release (`0x1e477e7 -> 0x227dde2 -> 0x227de0a ->
  0x2a132c0(0)`) now reads `c >= 1`, takes `0x2a13327` (healthy branch), and
  never reaches `0x2a13329`/`0x2a13330`. The `257f718-Context,20` line and the
  int3 at `0x6caef67` disappear; capture the next genuine fault RVA/callback.

**Do not add a matching harness "leave" after the task.** If the guest's own enter
at `0x1e4786b` also ran, its leave plus a harness leave would double-decrement and
recreate the underflow. Ending at `c = 1` (or `2` if the guest's enter also ran) is
benign: saturation needs 62 more unbalanced enters. The one unconditional enter
covers both cases:
* guest enter + guest leave both run: `1 -> 2 -> 1` (no panic);
* only the guest leave runs (the observed failure): `1 -> 0` (no panic).

This is the minimal faithful sequence: one extra execution of existing guest code
on the correct thread, plus removal of two corrupting hacks and one GOT plumbing
fix. No guest instruction, vtable, or marker is patched; decision-013 stays
untouched (do **not** set `0x7cdd149` / `0x7cef650`, which would only turn `int3`
into `abort()`).

### Step D — read-only confirmation (diagnostic only)
At the worker's fault/success site, resolve the counter the same way the guest
does: call `0x2a0de60`, dereference `(%rax)` -> `obj`, read `(obj+0x70) & 0x3f`.
Expect `0` in the current build and `>= 1` after step C. Also log
`*(uint32_t*)(guest+0x72a1b08)` (expect 3), `*(uint64_t*)(guest+0x722e540+0x400)`
(expect `0x7282580`), and `*(uint64_t*)(guest+0x722e560+0x10)` (must be nonzero
once the neutralize is gone). This is observation, not a mechanism.

## 6. Summary table of the correct state

| Property | Correct value / invariant |
| --- | --- |
| Per-thread object | `obj = *(TLS_block+0x410)`, `TLS_block` via `0x2a0de60` / `0x2ea6440` / key `0x72a1b08` = 3 |
| Tag word at `obj+0x70` | pointer `0x7282580` in bits 63:6 (64-byte aligned); never fabricate this |
| Counter `c = word & 0x3f` | `>= 1` at every leave (`edi=0`); `<= 0x3e` at every enter (`edi!=0`); `1..0x3e` during a scoped ref |
| Producer of a valid value | guest `0x2a132c0` update at `0x2a13332..0x2a1333c` |
| Assert handler gating the panic | `*(0x727e910)` -> `0x6c47c52` (non-NULL in harness) |
| Right thread | `FunctionMarshaller` worker, thunk `0x2a0daa0`, vtable call `0x2a0daf7` |
| Faulting leave | `0x1e477e7 -> 0x227dde2 -> 0x227de02 -> 0x227de0a -> 0x2a132c0(0)` |
| Minimal opt-in | remove neutralize; `SOBER_BIONIC_TSD_GOT=1`; `((void(*)(int))(base+0x2a132c0))(1)` on that worker before `0x2a0daf7`; no matching harness leave |

## 7. Evidence index

* `libroblox.dis`: `0x2a132c0` (full enter/leave guard, tags `0x416074`/`0x3dc475`,
  handler `0x727e910`, update `0x2a13332..0x2a1333c`); `0x2a0de60` / `0x2a0ded0`
  (TLS accessor + lazy init, `obj+0x70 = 0x7282580`); `0x2ea6440` (descriptor
  `+0x10` index, key `0x72a1b08`, GOT `0x72298b0`/`0x72295c8`, lazy alloc
  `0x2ea6520`); `0x1e4781c`/`0x1e4786b` (enter), `0x227dde2`/`0x227de0a` (leave);
  `0x2a0daa0`/`0x2a0daf7` (worker thunk); `0x6c47c52 -> 0x6caed5d -> 0x6caed7c
  -> 0x6caef66` (panic reporter).
* `reloc.py slots 0x6c47c52 -> ['0x727e910']`; `str_at.py 0x3dc475 =
  '257f718-Context,20'`, `0x416074 = '257f718-Context,15'`.
* Logs: `sf6.log:3698,3702-3735` (panic + chain); `r70.log`, `nest.log`
  (neutralize variants still panic); `tsdgot.log` (GOT rebind + neutralize still
  panics); `tsdgot2.log` (GOT rebind, no neutralize, inconclusive); `extra.log`
  (TSD seed, reporter-on-stack variant).
* Source (read only): `src/jni/native_jni.c:1251-1271`;
  `src/linux/guest_app_shell.c:51-83,945-976,1337-1379`;
  `src/linux/android_relocs.c:292-309`; `src/compat/bionic_libc.c:469-486`.
* Prior analysis: `analysis/tls-2a0de60-r70-correction.md`,
  `analysis/int3-6caef66-rbxcrash.md`, `analysis/jni/recursion-note.md` Addendum 2.
