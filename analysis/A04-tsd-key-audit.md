# A4 — Guest TSD key audit and `SOBER_BIONIC_TSD_GOT` rebind review

Owner: **worker A4** (logical Team A). Analysis only — no `src/`, CMake, gate,
test, marker or engine edits. Frozen engine
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` (BuildID `204ad049…`)
was read but not modified. Rebinding code reviewed as currently uncommitted in
`src/linux/android_relocs.c:292-309`. All RVAs are from `/tmp/opencode/libroblox.dis`;
GOT/symbol mapping from the ELF `DT_JMPREL` table via `/tmp/opencode/a4_plt.py`.
Date 2026-11-20. Coordinates with decision 013,
`analysis/sentinel-abcdef-trace.md`, `analysis/tsd-static-buffer-abort.md`,
`analysis/tls-2a0de60-r70-correction.md`, `analysis/jni/recursion-note.md`
Addendum 2.

## 1. Verdict

1. **The existing two-slot rebind is the right primary fix and is mechanically
   correct.** `0x72298b0` (getspecific) and `0x72295c8` (setspecific) are
   genuine `R_X86_64_JUMP_SLOT` GOT words, inside the RELRO window, and the
   opt-in writes them **before** `mprotect(PROT_READ)` is restored
   (`android_relocs.c:296-309` vs `:311-313`). It does not touch
   `__stack_chk_guard` (`0x72293f8`) and does not write guest code. Logs prove
   it fires (`tsdgot.log:8`, `tsdgot2.log:8`, `final-ab.log:8`,
   `final-ab2.log:8`).
2. **Persistent Bionic TSD can be made fully faithful with no heap or
   static-buffer seed at all — by removing the seeds, not adding better ones.**
   Once get/set are rebound, the guest's own NULL paths allocate and register
   its TSD backing:
   * key 2: `0x1e47927 → 0x1e4795c → 0x1dbaddd` (guest allocator creates and
     `pthread_setspecific`s real thread state);
   * key 3: `0x2ea6440 → 0x2ea6520 → 0x1dbad62` (guest allocator creates the
     growable array, writes its header, `pthread_setspecific`s it at
     `0x2ea656e`).
   The array that backs this is the stub's genuine `_Thread_local void *[512]`
   (`bionic_libc.c:470-486`) — real per-thread TLS, persistent by construction.
   Both harness seeders (`SOBER_STUB_TSD_SEED` static BSS and `SOBER_TLS_SEED`
   `calloc`) should be **OFF** when the rebind is ON. The static one is what
   caused the `realloc(): invalid pointer` abort
   (`analysis/tsd-static-buffer-abort.md`); the heap one is redundant and
   suppresses the guest's own header/index initialisation at `0x2ea64c3`/
   `0x2ea655a`/`0x2ea6561`.
3. **The rebind is a global switch over the whole guest, not just keys 2/3.**
   There are **269** `pthread_getspecific@plt` call sites and **54**
   `pthread_setspecific@plt` call sites (§2.3). Other GOT slots in the TSD
   family are *not* rebound; see §5 for the exact list and the recommendation
   not to rebind `pthread_once`.
4. **No direct safety risk to glibc.** The rebind changes only guest PLT GOT
   words; the stub's native get/set touch only its own TLS array; host-side
   glibc TSD users call glibc directly. The realistic risks are guest-side
   (key namespace split, destructor key encoding, 512-slot ceiling) and are
   enumerated in §6 with guard rails.

## 2. Complete TSD key-usage audit

### 2.1 Key 2 — global `0x7282000` (allocator thread state)

Created once by `0x1dbc2e6` (`lea 0x7282000,%rdi`) →
`0x1dbc2f4 call pthread_key_create@plt (0x6d320c0)` with destructor
`0x235c4f6`; after creation `0x1dbc2f9` reads the key, compares to `-1`, and
`0x1dbc30c` jumps to `pthread_setspecific@plt (0x6d31d10)` with the static
fallback `0x7280c00`. Guard flag `0x7cd9070`.

All **21** reads of `0x7282000` (every one immediately followed by
`getspecific/setspecific@plt`):

```
1dbad6b  1dbae02  1dbae50  1dbb097  1dbc20b  1dbc2f9  1dd60f2  1e4792b
1e4bce8  1f545bd  227ab07  235c57c  235c5f1  235c611  253226b  2a0d57b
6c7f530  6c7f736  6c7fcea  6c80bbe
```

Principal consumers:

* `0x1e47927` allocator thread init — decode at
  `analysis/sentinel-abcdef-trace.md §5`; NULL or fallback `0x721db80` →
  `0x1e4795c call 0x1dbaddd` (create + setspecific real state).
* `0x227aafa` reallocate front-end: `0x227ab07` loads key, `0x227ab12`
  getspecific, `0x227ab1a` fallback `0x721db80`, `0x227ab44` derefs
  `thread_state+0xe8` into arena table `0x7cd9040`. This is the path that
  called `host_reallocate()` on the harness's static seed.
* `0x235c57c..0x235c58a` key-2 setspecific on thread-state install.
* `0x6c7f530/0x6c7f736/0x6c7fcea/0x6c80bbe` — JNI exception/scheduler
  wrappers (the `0x6ca7f95` recursion family in `recursion-note.md`): each
  loads key 2, getspecific, compares result to `0x721db80`. Before the rebind
  these read glibc's slot 2 and consumed the stale canary sentinel.

Fault-log confirmation after the rebind: `tsdgot.log` and both `final-ab*.log`
still report `tls_key @0x7282000 = 0x2`, `thread_state @0x721db80 = <real
guest pointer>`; no sentinel dereference and no `realloc(): invalid pointer`
in any rebind log.

### 2.2 Key 3 — global `0x72a1b08` (Bionic TLS descriptor array)

Created once by the `pthread_once` callback `0x2ea6640`:
`0x2ea6641 lea 0x72a1b08,%rdi`, `0x2ea6648 lea 0x2ea6670,%rsi` (destructor),
`0x2ea664f call pthread_key_create@plt`; on success flag `0x72a1b04 = 1`, on
failure `abort@plt`. Teardown `0x2ea6610` calls
`0x2ea6620 pthread_key_delete@plt (0x6d33710)` when that flag is set.

All **6** references to `0x72a1b08`, all inside this one subsystem:

```
2ea6455  getspecific for descriptor slot lookup        (0x2ea6440)
2ea650c  getspecific re-check after once/mutex path    (0x2ea6440)
2ea6565  setspecific of freshly allocated array        (0x2ea6440)
2ea661a  key passed to pthread_key_delete              (0x2ea6610)
2ea6641  &key passed to pthread_key_create             (0x2ea6640)
2ea6685  destructor re-arms setspecific(key-1, ...)     (0x2ea6670)
```

Accessor `0x2ea6440` (full decode in `tls-2a0de60-r70-correction.md §2` and
`tsd-static-buffer-abort.md §4`):

* `0x2ea645b getspecific(3)`; NULL → `0x2ea6520` allocate via `0x1dbad62`,
  `0x2ea655a *(buf)=1`, `0x2ea6561 *(buf+8)=capacity`,
  `0x2ea656e setspecific(3, buf)`.
* Non-NULL with `capacity < index` → `0x2ea6490 call 0x227aafa` (guest
  realloc) — legal only if the pointer came from `0x1dbad62`/`0x227aafa`.
* Descriptor index 0 path `0x2ea64c3`: `pthread_once@plt (0x6d31b90)` on flag
  `0x72a1b18` with init `0x2ea6640`, `pthread_mutex_lock@plt (0x6d31d30)` on
  `0x72a1b1c`, then index assigned from monotonic counter `0x72a1b10` into
  `descriptor+0x10` at `0x2ea64fc`. **This is exactly the state the harness's
  static/heap seeds bypass** — and why `SOBER_NO_SCHED_NEUTRAL=1` matters (the
  old `memset(p+0x28,0,16)` at `native_jni.c:1256` zeroed the second
  descriptor's index at `0x722e570`).
* Destructor `0x2ea6670` decrements the stored key (`dec %rax` at
  `0x2ea667f`) before `0x2ea6695 jmp pthread_setspecific@plt` — a Bionic-style
  key encoding. See §6.3 for why mixing this with glibc `key_create` is a
  fidelity caveat.

### 2.3 A third guest key and the global call-site census

`0x2ea6230` creates another key into global `0x72a1b00` with destructor
`0x2ea62c0` (the `__cxa_thread_atexit_impl`-style key). Therefore the guest
creates **at least three** keys in this one subsystem, plus the other
`pthread_key_create@plt` sites at `0x1dbc2f4`, `0x2315ae4`, `0x2a27c6b`,
`0x2c44394`, `0x2e5b60b`, `0x5dae8a7`, `0x6be0df6`.

Global census from the full disassembly (`/tmp/opencode/a4_grep.out`):

| Import | PLT | GOT | Call sites |
| --- | --- | --- | ---: |
| `pthread_getspecific@LIBC` | `0x6d322e0` | `0x72298b0` | **269** |
| `pthread_setspecific@LIBC` | `0x6d31d10` | `0x72295c8` | **54** |
| `pthread_key_create@LIBC` | `0x6d320c0` | `0x72297a0` | 9 call + 1 jmp |
| `pthread_key_delete@LIBC` | `0x6d33710` | `0x722a2c8` | 2 |
| `pthread_once@LIBC` | `0x6d31b90` | `0x7229508` | 6 |
| `__cxa_thread_atexit_impl@LIBC` | (weak) | `0x722a4f8` | — |

Representative getspecific sites outside the key-2/3 subsystem (evidence that
the rebind is global): `0x1dbad76`, `0x1dbae11`, `0x1dbc216`, `0x1dd60fd`,
`0x1e4bcf3`, `0x1f545c8`, `0x235c600`, `0x23daf85`, `0x253227a`, `0x2a0d58a`,
`0x2a27806`, `0x2c43a5b`, `0x5b4f8a7…0x6c80bcd` (the bulk, ~240 sites in
statically linked mid/upper code). Representative setspecific sites:
`0x1dbae66`, `0x1dbb0a5`, `0x2315ce7`, `0x2338766`, `0x23495ea`,
`0x235c58a`, `0x235c623`, `0x2a2795e`, `0x2c43bf4`, `0x2ea61b6`, `0x2ea656e`.

## 3. Review of the current opt-in (`android_relocs.c:292-309`)

Correct:

* Resolves by **versioned** `dlvsym(stub, "…", "LIBC")`, the same namespace the
  seeders use, so stub seeds stay visible.
* Applies after the APS2 relocation loop, so the loop's
  `dlsym(RTLD_DEFAULT, …)` values are overwritten deterministically.
* Applies before the RELRO `mprotect(PROT_READ)` restore. RELRO is
  `0x6d37bc0/0x4f3440` → `[0x6d37bc0, 0x722b000)`, which **contains** every
  PLT GOT word listed above, so ordering is essential.
* Logs the resolved pointers, and the log lines show non-NULL stub addresses
  (e.g. `get=0x7f61fb39c390 set=0x7f61fb39c350`).

Incomplete / fragile:

* It logs only the *new* pointers, not the pre-rebind GOT words (the A/B
  diagnostic requested in `sentinel-abcdef-trace.md §7.1`).
* Silent no-op if `dlopen("libc.so", RTLD_NOLOAD|RTLD_LAZY)` fails or either
  `dlvsym` returns NULL — no log in that case. Add an `else` with the failure
  reason.
* Hard-coded slot RVAs are correct for this frozen BuildID only (fine under
  the freeze, but worth an assertion that the relocation-decoded symbol name
  at each slot really is the intended one).
* It rebinds only 2 of the 5 TSD-family slots. See §5.

## 4. Can persistent Bionic TSD be fully faithful without heap/static-buffer hacks?

**Yes for the guest-observed TSD *values*; the guest already contains all the
code needed to create them.** Minimal faithful configuration:

```
SOBER_BIONIC_TSD_GOT=1          # guest reaches the private Bionic array
# SOBER_TLS_SEED        -> OFF  (remove heap seed)
# SOBER_STUB_TSD_SEED   -> OFF  (remove static BSS seed)
SOBER_NO_SCHED_NEUTRAL=1        # keep the two key descriptors intact
```

With that, on every worker thread:

1. key 2: `getspecific(2)` returns NULL from the zeroed private array →
   `0x1e4795c → 0x1dbaddd` allocates real thread state with the guest
   allocator and registers it via the rebound `setspecific(2, …)`.
2. key 3: `getspecific(3)` returns NULL → `0x2ea6520 → 0x1dbad62` allocates
   the growable array, writes `*(buf)=1` and `*(buf+8)=capacity`, registers via
   `0x2ea656e`. Growth at `0x2ea6490 → 0x227aafa` is then a legal realloc of a
   guest-allocated pointer.
3. `0x2ea64c3`/`0x2ea64fc` assign descriptor indices from `0x72a1b10` once;
   `0x2a0de60 → 0x2ea6440 → +0x410` yields the per-thread scheduler object
   whose `+0x70` nesting word survives between enter/leave.

Observed evidence that this works: `tsdgot2.log` (rebind present, no guest
fault) reaches `guest init complete`, phase 3, and `first frame rendered`;
the only abort there is an X-server teardown error
(`XIO: fatal IO error 2`), not a guest fault. `final-ab*.log` fault at new
RVAs `0x2772161` (`nativeAppBridgeV2StartAppWithParams+0x73952`, `mov
(%rdi),%rax` on a bogus app object) and `0x3281ca2` (bogus JNI call-protocol
vtable), i.e. **past** the TSD/sentinel family. `tsdgot.log` with the full
seed stack still reaches the already-known `0x6caef67` nesting underflow —
consistent with `tls-2a0de60-r70-correction.md`, not a TSD regression.

What is *not* fully faithful yet (does not block the above, but should be
recorded):

* `pthread_key_create`/`pthread_key_delete` still resolve to glibc; the stub
  only forwards them (`bionic_libc.c` sets `sober_libc_fn_pthread_setspecific`
  and `…_getspecific` as native slots but has no native key_create/delete;
  `gen_bionic_libc.py:183-184` skips only get/set). Key *allocation* is
  therefore glibc's, not Bionic's.
* The private array has 512 slots (`SOBER_BIONIC_TSD_SLOTS 512`). Key values
  ≥ 512 make the stub's setspecific return `EINVAL` and getspecific return
  NULL. With ~10 key_create sites this is unlikely, but should be asserted.
* The `0x2ea6670` destructor decrements the key before re-arming setspecific.
  With Bionic-style key encoding this is meaningful; with glibc-returned keys
  it can write `slot[key-1]`. This is a latent cross-key corruption risk, not
  an observed fault.

## 5. Which other GOT slots need rebinding

| Slot | Symbol | Rebind? | Reason |
| --- | --- | --- | --- |
| `0x72298b0` | `pthread_getspecific` | **yes (already)** | root cause; all 269 guest get sites |
| `0x72295c8` | `pthread_setspecific` | **yes (already)** | pairs with get; all 54 guest set sites |
| `0x72297a0` | `pthread_key_create` | **only after stub work** | stub currently forwards to glibc; rebinding alone is a no-op. Needed for true Bionic key allocation/destructor encoding (worker-2 owns `bionic_libc.c`/gen script). |
| `0x722a2c8` | `pthread_key_delete` | **only after stub work** | paired with key_create; guest calls it at `0x2ea6620`. |
| `0x7229508` | `pthread_once` | **no** | correctness primitive, not TSD storage. Guest key-3 creation depends on it at `0x2ea64d1`; glibc's implementation works and a forwarder adds nothing. Rebinding risks hangs with no benefit. |
| `0x722a4f8` | `__cxa_thread_atexit_impl` | **no** | thread-local C++ destructor registration; guest's custom TSD destructors are installed through `pthread_key_create`, not this slot. |
| `0x72293f8` | `__stack_chk_guard` | **NEVER** | decision 013 / strict gate; glibc-compiled code and guest canary checks depend on it. |

So for the current opt-in the answer is: **no additional slot is required to
fix the observed TSD faults**; two slots are sufficient. If the lead wants
*namespace-complete* Bionic TSD, the two extra slots are `0x72297a0` and
`0x722a2c8`, but they must be preceded by native implementations in the stub.

## 6. Safety risk to glibc and to the guest

### 6.1 glibc — none directly

* No glibc code, data or PLT is modified; the change is confined to guest GOT
  words in the mapped engine image.
* The stub's `pthread_getspecific`/`setspecific` are pure accesses to its own
  `_Thread_local` array (`bionic_libc.c:474-486`); they make no glibc calls.
* Host-side TSD users (`src/jni/javavm_semantics.c:145,206,235`,
  g_tls created with glibc `pthread_key_create`) call glibc directly and never
  traverse the guest GOT.
* `__stack_chk_guard` GOT `0x72293f8` and the host compat symbol stay exactly
  as-is; the `0xABCDEF0123456789` value remains available to every glibc frame.

### 6.2 Guest-side global effect (the real risk to manage)

Because the two GOT slots are shared, the rebind redirects **all** guest TSD
access, not just keys 2/3. Consequences:

* Other subsystems that previously read glibc TSD now read the Bionic array.
  They are internally consistent only because their key integers (from glibc
  key_create) are reused as Bionic indices. Add a read-only assertion that no
  key passed to the rebound functions is ≥ 512.
* Any host code that seeded guest keys via **glibc** `pthread_setspecific`
  would become invisible. The known seeders already use
  `dlvsym(stub,…,"LIBC")`, so they remain visible — keep that invariant in any
  new seeding code.
* Destructor semantics may differ for keys created by glibc (§4 caveat 3).
  Watch `0x2ea6670`/`0x2ea62c0` and the other seven key_create sites.

### 6.3 Mechanics to protect

* RELRO covers `0x6d37bc0..0x722b000`; the rebind write must stay before the
  `mprotect(PROT_READ)` at `android_relocs.c:311-313`. If lead moves the block,
  it will SIGSEGV on the next run.
* `_Thread_local` in a `RTLD_LOCAL` dlopen'd DSO uses dynamic TLS; it is valid
  for the harness's glibc-created threads (JNI `AttachCurrentThread`), but a
  guest thread created by raw `clone` without glibc TLS setup would need
  verification. Keep the opt-in gated until such a thread is exercised.

## 7. Opt-in experiment order for lead

All steps are default-OFF, opt-in, read-only diagnostics; no guest bytes, no
callbacks, no decision-013 marker.

1. **A/B the rebind with no seeds (highest priority).**
   `SOBER_BIONIC_TSD_GOT=1 SOBER_NO_SCHED_NEUTRAL=1`, both
   `SOBER_TLS_SEED` and `SOBER_STUB_TSD_SEED` **OFF**. Keep the current
   app-shell/render stack otherwise identical. Expected: no
   `realloc(): invalid pointer`, no canary-sentinel dereference; guest
   self-initialises keys 2/3 at `0x1dbaddd`/`0x1dbad62`. Capture the next
   genuine fault RVA (baseline after rebind: `0x2772161`, `0x3281ca2`, or the
   `0x6caef67` underflow).
2. **Instrument the rebind (read-only), then repeat step 1.** In
   `android_relocs.c`, before overwriting each slot log
   `*(void**)0x72298b0` / `*(void**)0x72295c8`, `dlsym(RTLD_DEFAULT,…)` and
   `dlvsym(stub,…,"LIBC")`; also log `*(u32*)0x7282000` (expect 2),
   `*(u32*)0x72a1b08` (expect 3), `*(u32*)0x72a1b00`, and one
   `pthread_getspecific(2/3)` result from each namespace. Expected: pre-rebind
   words equal glibc, post-rebind words equal stub, guest-side values NULL on
   a fresh thread before `0x1e4795c`/`0x2ea6520` and valid afterwards. Add the
   missing `else` log for dlopen/dlvsym failure.
3. **Only if a residual `257f718-Context,20` underflow remains**, follow
   `tls-2a0de60-r70-correction.md §4 step 2`: call genuine guest
   `0x2a132c0(edi=1)` once on the real `FunctionMarshaller` worker immediately
   after its `AttachCurrentThread`, so the guest itself writes
   `obj+0x70 = (obj+0x70 & ~0x3f) | 2`. No seed, no byte write, no callback.
4. **Namespace completeness (needs worker-2, not A4).** If step 1 shows key
   value/encoding problems, have worker-2 add native
   `pthread_key_create`/`pthread_key_delete` with Bionic index semantics to
   `bionic_libc.c` + `gen_bionic_libc.py`, then opt-in rebind
   `0x72297a0`/`0x722a2c8` as a second A/B. Keep `pthread_once`,
   `__cxa_thread_atexit_impl` and `__stack_chk_guard` untouched.
5. **Safety smoke test.** With the rebind ON, run a plain glibc-only host
   binary / existing smoke tests to confirm canaries and host TSD still work;
   assert no key ≥ 512 is observed at the rebound functions. Do **not** fold
   any of these into the marker gate — decision 013 stays strict.

## 8. Evidence index

* ELF `DT_JMPREL` map (`a4_plt.py`): `0x7229508 once`, `0x72295c8 setspecific`,
  `0x72297a0 key_create`, `0x72298b0 getspecific`, `0x722a2c8 key_delete`,
  `0x722a4f8 __cxa_thread_atexit_impl`; PLTs `0x6d31b90/0x6d31d10/0x6d320c0/
  0x6d322e0/0x6d33710`.
* `libroblox.dis`: `0x1dbc2e6/0x1dbc2f4` (key 2 create), `0x1e47927/
  0x1e4795c/0x1dbaddd` (key 2 init), `0x227aafa/0x227ab07/0x227ab12`
  (allocator), `0x235c57c/0x235c58a`, `0x6c7f530/0x6c7f736/0x6c7fcea/
  0x6c80bbe` (JNI wrappers); `0x2ea6640/0x2ea664f/0x2ea6610/0x2ea6620/
  0x2ea6670/0x2ea6685` (key 3 create/delete/dtor), `0x2ea6230/0x2ea6242`
  (third key), `0x2ea6440/0x2ea64c3/0x2ea6520/0x2ea6490/0x2ea655a/
  0x2ea6561/0x2ea656e` (key 3 accessor), `0x2a0de60/0x2a0ded0/0x2a132c0`
  (TLS/scheduler), `0x2338766/0x233876e/0x2772161/0x3281ca2` (post-rebind
  faults); `0x72293f8` (canary GOT, untouched).
* `/tmp/opencode/a4_grep.out` — full census: 269 getspecific and 54
  setspecific call sites, 21 key-2 global reads, 6 key-3 global refs,
  9 key_create sites, 6 once sites.
* Logs: `tsdgot.log:8,3699-3739` (rebind fires; still `0x6caef67` underflow);
  `tsdgot2.log:8,3669-3679` (rebind; `guest init complete`, `first frame
  rendered`, only XIO teardown error); `final-ab.log:8,3713-3747` and
  `final-ab2.log:8,3714-3749` (rebind; new downstream faults `0x2772161`/
  `0x3281ca2`, `tls_key @0x7282000 = 0x2`, valid thread_state).
* `src/linux/android_relocs.c:260-278,292-309` (dlsym default path and opt-in);
  `src/compat/bionic_libc.c:470-486,536,613` and
  `src/compat/gen_bionic_libc.py:117-138,183-184` (stub TSD implementation);
  `docs/decisions/013-no-weak-app-shell-marker.md` (root cause, canary ruling);
  `analysis/sentinel-abcdef-trace.md §5-7`, `analysis/tsd-static-buffer-abort.md
  §2-5`, `analysis/tls-2a0de60-r70-correction.md §2-4`,
  `analysis/jni/recursion-note.md Addendum 2`.
