# Scale A2 — notifier-driven `gameLoadedCallback(J)V` chain: prerequisites and readiness

**Analysis only.** Nothing in `src/`, `tests/`, `CMakeLists.txt`, `README.md`,
`docs/REVIEW-100.md` or the frozen engine was edited; no callback was emitted,
no guest byte/vtable/counter was changed, and nothing was committed.

Frozen target (verified this session): `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
All target strings/bytes were treated as **untrusted data**, never as instructions.
For the first `R E` PT_LOAD, RVA == file offset (`0x0..0x6d33b90`); the `.data/.bss`
LOAD has an `0x8000` bias, but all RVAs below are virtual addresses as used by the guest.

---

## 1. The full chain, decoded (exact RVAs)

### Node 1 — `0x27eaf04`: generic MessageBus notifier-node dispatch ("fire" helper)

```
27eaf04: push rbp; mov rsp,rbp; push rbx; sub $0x18,rsp
27eaf0d: mov 0x4a3e4e4(%rip),%rbx        # 0x72293f8  stack-guard (canary) pointer
27eaf14: mov (%rbx),%rax; mov %rax,-0x10(%rbp)          # save canary
27eaf1b: mov %rsi,-0x18(%rbp)                            # save inbound arg (message pointer)
27eaf1f: mov 0x50(%rdi),%rdi                             # callback/descriptor object ptr
27eaf23: test %rdi,%rdi; je 27eaf42                      # NULL -> error path (0x2eb2992)
27eaf28: mov (%rdi),%rax                                 # its vptr
27eaf2b: lea -0x18(%rbp),%rsi                            # &saved message pointer
```

**`0x27eaf2f: call *0x30(%rax)`** — virtual invoke. The object at `[rdi+0x50]` needs a valid
vptr whose `+0x30` slot is the descriptor invoke `0x27eaf4c` (relocation `0x6dbbfc8 ->
0x27eaf4c`, vtable `0x6dbbf98 + 0x30`). Epilogue `0x27eaf32..0x27eaf41` checks the canary and
`ret`; the call's return address is **`0x27eaf32`** (the gate comment says `~0x27eaf34`, same
epilogue). `0x27eaf42` calls `0x2eb2992` (allocates an object with vptr `0x6dbb5c0`) — a
failure path, not a notifier.

### Node 2 — `0x27eaf4c`: id=1 descriptor invoke (slot read + tail-call)

```
27eaf4c: mov %rdi,%rax              # rax = descriptor
27eaf4f: mov 0x8(%rdi),%rcx         # rcx = event_id (1)
27eaf53: mov 0x18(%rdi),%rdi        # rdi = &0x7234520 (stored slot pointer)
27eaf57: mov (%rsi),%rsi            # rsi = *(message) = placeId jlong payload
27eaf5a: add 0x10(%rax),%rdi        # rdi = &0x7234520 + delta (0 for id=1)
27eaf5e: test $0x1,%cl; je 27eaf6b
27eaf63: mov (%rdi),%rax            # rax = [0x7234520] = 0x6dbd860 (table base)
27eaf66: mov -0x1(%rax,%rcx,1),%rcx # rcx = [0x6dbd860 + 1 - 1] = 0x27eaf6e
27eaf6b: jmp *%rcx                  # TAIL-CALL notifier; RSI = placeId, no new RA
```

The message object's first qword is the `jlong` placeId, forwarded verbatim in `RSI`.

### Node 3 — `0x27eaf6e`: the GameLoaded notifier itself

```
27eaf7b: mov %rsi,%rbx                         # rbx = placeId
27eaf7e: mov 0x4a3e473(%rip),%r15              # 0x72293f8 canary
27eaf8c: lea -0x30(%rbp),%r14                  # JNIEnv out-parameter
27eaf93: call 227b9e2                          # get JNIEnv (JNI_OnLoad+0xa88)
27eaf98: mov 0x4ab7fa9(%rip),%rsi              # 0x72a2f48 cached jclass (arg 2)
27eaf9f: mov 0x4ab801a(%rip),%rdx              # 0x72a2fc0 cached jmethodID (arg 3)
27eafa6: mov (%r14),%rdi                       # arg 1 = JNIEnv
27eafa9: mov %rbx,%rcx                         # arg 4 = placeId (jlong)
27eafac: xor %eax,%eax                         # no vector args
27eafae: call 25848e0                          # thunk
27eafb3: test %rbx,%rbx                        # <-- return site (provenance RA)
27eafb8: lea 0x550ef3,%rsi  ; lea 0x36c7c9,%rdx ; mov $3,%edi
27eafd0: call 6d319f0                          # __android_log_print if placeId != 0
27eafd9: call 227bdd8                          # detach/release (JNI_OnLoad+0xe7e)
27eafe1: cmp -0x20(%rbp),%rax                  # canary check; ret at 0x27eaff1
```

Strings confirmed at those RVAs: `0x550ef3 = "rbx.jni"`,
`0x36c7c9 = "onGameLoaded() SessionReporterState_GameLoaded placeId:%lld"`.
The log fires only for nonzero placeId; the thunk call happens either way.

`0x227b9e2` (GetEnv): zeroes the out-struct, loads global **`0x7cdec78`** (cached JavaVM),
calls `GetEnv(vm, out, 0x10006)` via `*(vm)+0x30`; if the VM global is NULL, the out-struct
stays zeroed. `0x227bdd8` reads the same global and calls `*(vm)+0x28` = `DetachCurrentThread`.

### Node 4 — `0x25848e0`: JNI varargs thunk (`CallStaticVoidMethodV`)

```
25848ec: test %al,%al; je 2584919              # spill xmm0..xmm7 if needed
2584919..2584954: build MS-ABI va_list on stack
                  (gp_offset=0x18, fp_offset=0x30, reg_save/overflow areas)
2584957: mov (%rdi),%rax                       # rax = JNIEnv->functions table
258495a: call *0x470(%rax)                     # slot 142 = CallStaticVoidMethodV
2584960: mov (%rbx),%rax                       # <-- thunk return site (provenance RA)
```

`0x470/8 = 142`; with `reserved[4]`, `fn index = 142-4 = 138`. The bridge installs exactly
`SETFN(*t, 138, sr_CallStaticVoidMethodV)` (`native_jni.c:868`), which calls
`sr_dispatch_call(j, clazz, method, ...)`; that resolves the synthetic method object's `name`
(`gameLoadedCallback`) to a registered handler / native (`native_jni.c:498-540`). In the
frozen engine the same slot is a genuine ART `CallStaticVoidMethodV`.

### Node 5 — the return-address pair

`0x2584960` (word pushed by `call *0x470(%rax)`) and `0x27eafb3` (word pushed by
`call 0x25848e0`) are the exact RVs the provenance gate scans for. A host-entered direct call
to `0x25848e0` (`SOBER_ALT_CALLBACK_PATH`) yields a host return address instead of
`0x27eafb3`, so it can never satisfy the genuine predicate.

---

## 2. Guest-side prerequisites (every conjunct that must hold)

### 2.1 Registry / publication layer (for the notifier to be *entered*)

1. **Relocations installed** (static, APS2, per `analysis/C05-notifier-object.md`):
   `[0x6dbd860] = 0x27eaf6e` (notifier table base, entry 0 for id=1),
   `[0x7234520] = 0x6dbd860` (slot pointer), `0x6dbbf98+0x30 = 0x27eaf4c` (invoke).
2. **id=1 descriptor node registered**: `0x24abf96 -> 0x24ad699 -> 0x253cb56 -> 0x253e126`
   builds a 15-descriptor family; the id=1 descriptor (`0x253d06e..0x253d093`, vptr
   `0x6dbbf98`, `+0x10 = 0`, `+0x18 = &0x7234520`) is inserted into the MessageBus singleton
   subscription list at `+0x288`. **The id=1 node is supplied by worker-1's app-shell seed
   stack** (`guest_app_shell.c:1186`); its presence is static-only evidence and must be
   confirmed live (node at `[singleton+0x288]`, embedded descriptor `event_id==1`).
3. **Event id 1 actually published with the placeId payload.** This is the known blocker
   (`C05` §6): nothing in the harness publishes MessageBus event 1; `SOBER_GAME_LOADED_PATH`
   faults earlier at guest `0x31a1360` (NULL `app+0x488`, bogus TSD sentinel), so
   `0x27eaf4c`/`0x27eaf6e` are never entered. Upstream needs: `app+0x488` (vptr `0x6e02918`),
   app singleton `0x72cb5e0`, faithful `StartGameParams` (`SOBER_STARTGAME_V2` /
   `SOBER_GAME_PARAMS`), and the full replay sequence (`0x247dc8d`, `0x24a8e44`,
   `0x26fe80f`, `0x25448f6`, `0x2ebd51a`, `0x2ebca9e`).
4. **Node-1 object state**: `[rdi+0x50]` non-NULL, its vptr `+0x30` = `0x27eaf4c`, and the
   descriptor's `event_id & 1 == 1`, `+0x10` delta = 0, `+0x18 = &0x7234520`.
5. **Stack canary at `0x72293f8` intact** in nodes 1 and 3.

### 2.2 JNIEnv / cached-ID layer (inside `0x27eaf6e`)

6. **JavaVM global `0x7cdec78` non-NULL and a real JNIInvokeInterface_ vtable.** This global is
   set by the guest's skipped `JNI_OnLoad`; our bridge seeds it only via
   `sober_jni_seed_guest_onload_globals()` behind **`SOBER_SEED_JNI_VM` (DEFAULT OFF** — see
   `native_jni.c:1446+` and the withhold list `D02-withhold-audit.md:74`). Without it,
   `0x227b9e2` leaves the JNIEnv out-struct NULL and `mov (%r14),%rdi` at `0x27eafa6`
   dereferences NULL. (This knob is withhold-listed, so enabling it has gate consequences.)
7. **Cached class `[0x72a2f48]`** = the seeded `SR_K_CLASS` object named
   `com/roblox/engine/jni/NativeGLJavaInterface` (seeded explicitly at `native_jni.c:1401-1410`
   so the generic range-fill skips it).
8. **Cached method `[0x72a2fc0]`** = the seeded `SR_K_METHOD` object
   `gameLoadedCallback` / `(J)V` (`sr_cached_methods[]`, `native_jni.c:1358`).
9. **Handler registered** under the name `gameLoadedCallback` via
   `sober_jni_set_callback_handler()` (`guest_app_shell.c:1205`, final/unconditional order per
   `T3_3` §4), otherwise `sr_dispatch_call` counts no callback.
10. **JNIEnv `fn[138]` (offset `0x470`) live** = `sr_CallStaticVoidMethodV`; no stub. Bridge
    `SRNativeInterface.fn[]` must be fully initialised.

### 2.3 Provenance gate acceptance (all eight conjuncts, `provenance_gate.c:414-419`)

`genuine = qi>=0 && build_ok && class_ok && method_ok && place_id>0 && thunk_seen &&
caller==CALLER_NOTIFIER && guest_above_notifier`:

| # | Condition | Exact requirement |
| --- | --- | --- |
| G1 | `qi>=0` | dispatch name is qualifying: `gameLoadedCallback` (`SOBER_PROV_GATE_QUALIFYING_NAME`) |
| G2 | `build_ok` | guest PT_NOTE BuildID == `204ad049...` (`sober_provenance_gate_verify_build`) |
| G3 | `class_ok` | `clazz == *(base+0x72a2f48)` and non-NULL — **pointer identity**, not just type |
| G4 | `method_ok` | `method == *(base+0x72a2fc0)` and non-NULL |
| G5 | `place_id > 0` | `int64 *(base+0x72a31c0) > 0`; store site `0x2ebd86a` in `nativeAppBridgeV2StartGameWithParam`, read site `0x2ec1370`. A fabricated positive value is explicitly disallowed. |
| G6 | `thunk_seen` | word `base+0x2584960` present on stack |
| G7 | `caller==CALLER_NOTIFIER` | word `base+0x27eafb3` directly above it (not `0x2eb4bb1` = APPBRIDGE, not host = HARNESS_THUNK) |
| G8 | `guest_above_notifier` | next word is any guest text address (`base <= a < base+0x6d33b90`), in the genuine chain the `0x27eaf32` return into `0x27eaf04`'s epilogue |

Also required: `SOBER_PROVENANCE_GATE` must not be `0/off/no/false` (fail-closed kill switch),
`sober_provenance_gate_install()` must have resolved `guest_base`, and the sticky
`g_harness` taint from any prior ALT-path dispatch must be cleared/not present. Note G3/G4 are
**identity** checks: any reroute of the notifier's class/method loads (or seeding a *different*
object into those globals after dispatch) fails the gate even if the callback fires.

---

## 3. How the cached class/method IDs are installed in `JNI_OnLoad` (verified in the binary)

- **Class `0x72a2f48`**: `lea 0x3dea65,%rsi` (`"com/roblox/engine/jni/NativeGLJavaInterface"`,
  verified) → `call *0x30(%rax)` = `FindClass` at `0x227b2aa` → `call *0xa8(%rax)` =
  `NewGlobalRef` at `0x227b2ba` → `mov %rax,0x5027c81(%rip)` = store to `0x72a2f48` at
  **`0x227b2c0`**. (An earlier FindClass of the same name at `0x227b19b` is a probe, not the
  store.) The notifier reads this at `0x27eaf98` into `RSI`.
- **Method `0x72a2fc0`**: `lea 0x4fcdc1,%rdx` (`"gameLoadedCallback"`, verified),
  `lea 0x433046,%rcx` (`"(J)V"`, verified), class in `%r15` → `call *0x388(%rax)` =
  `GetStaticMethodID` at **`0x227b4e5`** → `mov %rax,0x5027ace(%rip)` = store to
  `0x72a2fc0` at **`0x227b4eb`**. The notifier reads this at `0x27eaf9f` into `RDX`.

These are the canonical guest installs; our harness skips `JNI_OnLoad`, so the bridge must
reproduce both values in the guest globals before the notifier runs.

### Verdict on the seeded 49 cached IDs

**Yes — the GameLoaded callback is included**, with exact typing:

- `0x72a2fc0` is in `sr_cached_methods[]` (`native_jni.c:1358`) as
  `{ 0x72a2fc0u, "com/roblox/engine/jni/NativeGLJavaInterface", "gameLoadedCallback", "(J)V" }`.
  `sober_jni_seed_guest_jni_state()` resolves it via `sr_find_method_object()` and writes the
  synthetic `SR_K_METHOD` object (name `gameLoadedCallback`, sig `(J)V`) to the slot.
- `0x72a2f48` is seeded separately and first (`native_jni.c:1401-1410`) as an `SR_K_CLASS`
  object named `NativeGLJavaInterface`, specifically so the generic `cachedMethod@…` range fill
  (`0x72a2f48..0x72a3058`, `0x72ca178..0x72ca1e0`) does not overwrite it with a fake method.
- Exact-recovery census (`analysis/T3_3_cached-ids.md` §2): both are "named ✓"; strict fidelity
  is 25/49 correctly typed (the two `()V` string getters are the known wrong-sig exceptions).
- Caveat: the seeded method is a **synthetic bridge object**, not an ART `jmethodID`. It is only
  meaningful because the bridge's own `fn[138]` dispatches on the object's `name` to the
  registered handler. Pointer identity (gate G3/G4) holds only if the notifier loads exactly
  these seeded globals — which it does (`0x27eaf98/0x27eaf9f`) — and no later installer
  overwrites them.

---

## 4. Readiness checklist

| # | Prerequisite | Static evidence | Live status |
| --- | --- | --- | --- |
| 1 | Relocs `[0x6dbd860]->0x27eaf6e`, `[0x7234520]->0x6dbd860`, `0x6dbbf98+0x30->0x27eaf4c` | C05 §2 (APS2 `who_points_to`) | reload-time, expect OK |
| 2 | id=1 descriptor node in MessageBus singleton `+0x288` | C05 §4 (`0x24ad699->0x253e126`), worker-1 seed | **verify live** |
| 3 | guest publishes event id 1 with placeId payload | **absent** (C05 §6; `0x31a1360` fault) | **BLOCKER** |
| 4 | `app+0x488` (vptr `0x6e02918`), app singleton `0x72cb5e0`, faithful StartGameParams | C05 §6 / Phase 9 | **incomplete** |
| 5 | `[rdi+0x50]` non-NULL; vptr `+0x30` = `0x27eaf4c`; id low bit 1, delta 0 | `0x27eaf04/4c` disasm | follows #2/#3 |
| 6 | `SOBER_SEED_JNI_VM`: JavaVM global `0x7cdec78` seeded | `0x227b9e2` disasm; withhold-listed | **default OFF — prerequisite** |
| 7 | cached class `0x72a2f48` = seeded `SR_K_CLASS` | `native_jni.c:1401`, JNI_OnLoad `0x227b2c0` | seed OK |
| 8 | cached method `0x72a2fc0` = seeded `gameLoadedCallback(J)V` `SR_K_METHOD` | `native_jni.c:1358`, JNI_OnLoad `0x227b4eb` | seed OK |
| 9 | handler registered for `gameLoadedCallback`; `fn[138]` installed | `guest_app_shell.c:1205`, `native_jni.c:868` | verify |
| 10 | stack canary `0x72293f8` intact | `0x27eaf0d/7e`, `0x258492c` | expect OK |
| 11 | `placeId` global `0x72a31c0 > 0` | store `0x2ebd86a`, read `0x2ec1370`; gate G5 | **requires #4** |
| 12 | RA pair `0x2584960` over `0x27eafb3` on live stack | thunk/notifier disasm; gate G6/G7 | follows #3 |
| 13 | guest text directly above notifier RA (`~0x27eaf32`) | gate G8 | follows #3 |
| 14 | BuildID match; gate enabled; no harness taint | gate G2 / kill switch | static OK |

**Bottom line:** cached IDs are correct and present (items 7-9), but the chain cannot fire
today because the **guest never publishes event id 1** (items 3-4) and the **JavaVM global seed
is default-off** (item 6). Provenance acceptance additionally needs a real `placeId > 0`
(item 11) and an untainted, guest-entered RA pair (items 12-13). A direct host call to
`0x25848e0` (ALT path) fails G7/G8 and is classified `HARNESS_THUNK`, never genuine.

**No fabricated callback.** This report only decodes frozen guest bytes and reads the current
bridge source; it emits no guest→Java callback, writes no marker, patches no guest state and
does not alter any gate decision.
