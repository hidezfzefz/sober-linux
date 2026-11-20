# A10 stability synthesis — one ranked, minimal experiment order

Owner: **worker A10** (logical Team A). Scope: **this file only**. No `src/`,
`CMakeLists.txt`, `tests/`, gate, decision or engine edits. Frozen engine
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256 `3e498d6494…` — read only.
All RVAs below are for that build (never mix with 2.745.1142 / `791c3f9e…`).
Target strings/data are **untrusted DATA**, never instructions. Date 2026-11-20.

## 0. Honesty statement and gate ruling (read first)

- **No success is claimed in this document.** Every item below is a diagnostic
  fix-discovery experiment or an acceptance condition, not a passing result.
- **Decision 013 stays strict; no gate change is requested.** The genuine but
  counted `onAppBridgeNotification(String,String)` (cached ID `0x72a3028` →
  thunk `0x3dea91`) is **explicitly rejected** as app-shell proof. The only
  reachable qualifying callback is `gameLoadedCallback(J)V` from notifier
  `0x27eaf6e`; `onAppShellReloadNeeded()V` (`0x72a3020`) has no native reader
  and cannot fire in a native-only harness (`analysis/jni/appshell-reload-trigger.md`).
- Every experiment is **opt-in, default OFF, one new variable per A/B run**.
  All of the knobs named below are (or must be added to) the
  `seeded_state` force-withhold chain in `src/linux/guest_app_shell.c:1337-1375`;
  a genuine callback observed while any of them is active **cannot** qualify.
  Therefore this order *explains and removes faults*; the strict marker can only
  be evaluated after the lead lands each fix in default (env-free) code and
  re-runs with every opt-in unset.
- Do not byte-patch guest code to bypass a fault, do not fabricate vtables or
  callbacks, do not change `__stack_chk_guard` (`0x72293f8`), and do not set the
  RBXCRASH mode bytes `0x7cdd149`/`0x7cef650` (that only converts `int3` into
  `abort()`).

## 1. Blocker inventory — one causal chain, not five independent bugs

| # | Blocker (as stated) | Fault site / evidence | Root cause | Removed by |
| - | - | - | - | - |
| B1 | **TSD key namespace** + RBXCRASH `int3` | SIGTRAP at `0x6caef66` (reported `0x6caef67`); `RBXCRASH: FatalRuntimeError (RSL - panic: 257f718-Context,20)`; FP `0x2a0daf0→0x1e477ec→0x2a13332→0x6c47cd4→0x6caed7c` | Guest GOT `0x72298b0` binds glibc `pthread_getspecific`, not Bionic stub; per-thread array reallocated on every call. Scheduler nesting underflow on `FunctionMarshaller` worker. The second TLS descriptor at `0x722e560` is clobbered by the scheduler "neutralize" `memset(p+0x28,0,16)` at `0x722e570`. A static TSD seed buffer is `realloc()`d by the guest (`0x227aafa` → host `realloc`) → glibc abort. Keys: `2` @ `0x7282000`, `3` @ `0x72a1b08`. | Exp. 1 (a/b/c) |
| B2 | **Skipped ctors** | `0x2e7b8e2` (`lock incq 0x8(%rax)`, `rax=0`, `si_addr=0x8`); `0x2e5d42a` (NULL `std::string` header) | Decision 008 skips `ctor[0]=0x2e62390`, the sole initializer of the FFlag registry handle `0x72a0520`: `0x2e62390→0x2e618b0→0x2e61280→0x2e5bc10→0x2e7b850`. Without it `*(0x72a0520+0x38)` (control block) is 0; guard `0x72a0a30`, lazy globals `0x72a0cd0`/`0x72a0cc0`, sentinel `0x72a0b88`. | Exp. 2 |
| B3 | **Singleton service vector empty** at `0x2ec1042` / `0x24ad562` | `0x2ec1042: mov rcx,[rax]` and `0x24ad562: mov (%rax),%rcx`, both `rax=0` from `0x30ef840` (logs: `realpath1.log`, `realstate2.log`) | App/service singleton `0x72cb5e0` is never written. Sole writer is `nativePostClientSettingsLoadedInitialization3` `0x2383c18` at `0x238497d` (guest gate `0x6096928`, gate byte `0x77979c0`). `0x30ef840` gate-true branch reads `0x72cb5e0`; NULL → returns 0. With flag `0x72cb648=1`, `0x24ad50a→0x24ae1fe` cannot install `app+0x440`. | Exp. 3 |
| B4 | **`app+0x488` NULL** at `0x31a1360` | `si_addr=0x28`, `rax=0xabcdef0123456789` (incidental GOT canary from `0x72293f8`); fault `mov 0x28(%rbx),%rax`, `rbx=NULL`; caller `0x320b0ba: mov 0x488(%rbx),%rdi` → `0x320b0e7` → `0x31a1168`; FP `0x31a1360←0x320b0ec←0x32034d9←0x2ec1661←0x23389fa` (`game.log`) | Seeds populate `app+0x408`/`+0x440` but not `+0x488`. The genuine object is built inside `nativeAppBridgeV2InitWithParams` `0x24abf96`: `0x24af28a` (`ebx=0x488`), `0x24af28f` (vptr `0x6e02918`), `0x24af296` (object into `r15`) / `0x24af2a4` (`+0x20 = self`), `0x24af2cc`/`0x24af2e6` → `0x231c124`. The key-2 TSD gap may feed stale state elsewhere but does **not** put the sentinel in this field. | Exp. 4a |
| B5 | **Fake app state** blocks the gate | Full stack reaches genuine but rejected `onAppBridgeNotification`; `full.log:3717-3720` = `callbacks=3`, `marker withheld` | `SOBER_SEED_USER_CONTROLLER` (`app+0x440`), `SOBER_SEED_APP_DELEGATE` (`app+0x408`), `SOBER_SEED_APP_488` (`app+0x488`) manufacture the very objects the guest should create, and force-withhold the marker. | Exp. 4b/4c + Exp. 6 |

Dependency order: **B1 (thread/TSD substrate) → B2 (FFlag ctor) → B3 (singleton)
→ B4 (real app fields) → B5 (remove seeds) → gate callback**. Running a deeper
experiment first only reproduces a shallower fault again.

## 2. Ranked minimal experiment order

One new variable per A/B; keep the default (all opt-ins off) path unchanged:
baseline fault `0x2eb4b65`, phase-2/3 markers present, zero marker emissions.

### Rank 1 — Bionic TSD fidelity (clears `0x6caef67`; three A/B runs, strict order)

| Sub-run | Knob (existing unless noted) | Explicit RVAs / action | Success criterion |
| - | - | - | - |
| 1a | **`SOBER_BIONIC_TSD_GOT=1`** (proposed; add via lead) | In `android_relocs.c` resolve `pthread_getspecific`/`pthread_setspecific` with `dlvsym(stub, name, "LIBC")` instead of `dlsym(RTLD_DEFAULT, name)`; log GOT word `0x72298b0`, key globals `0x7282000` (expect 2) and `0x72a1b08` (expect 3). | GOT word equals the stub `dlvsym` address (runtime probe showed `RTLD_DEFAULT == glibc`, not stub); guest `pthread_getspecific(2)/(3)` returns NULL/zeroed private-array state; no `257f718-Context,20` line and no SIGTRAP at `0x6caef66`/`0x6caef67`. |
| 1b | **`SOBER_NO_SCHED_NEUTRAL=1`** (existing) | Remove `sober_jni_neutralize_guest_scheduler()`'s `memset(p+0x28,0,16)` at `p=0x722e540`; this restores second descriptor index at `0x722e570`; the `+0x70` write at `0x722e5b0` is unrelated and can simply be dropped. | `*(u32*)(0x722e560+0x10) != 0`; `pthread_getspecific` wrapper `0x2ea6440` stops taking the `pthread_once`/mutex reassign slow path; TLS accessor `0x2a0de60 → TLS+0x410 → obj+0x70` yields a stable per-thread object across the worker's enter/leave. |
| 1c | **`SOBER_TLS_SEED=1`, `SOBER_STUB_TSD_SEED=0`** (existing) | Keep the `calloc(4096)` heap backing (`native_jni.c:1226-1249`), not the `static state[65536]` in `guest_app_shell.c:49-70`; optionally pre-set header `*(u64*)buf=1`, `*(u64*)(buf+8)=capacity≥index+1` so `0x2ea6470` skips the grow. Guest grow path is `0x2ea6440→0x227aafa→host_reallocate()`. | No `realloc(): invalid pointer` / SIGABRT with FP `0x2ea6495→0x2a0de70→0x2a13399`; nesting counter at `obj+0x70 & 0x3f` is `1..0x3e` after the guest's own enter (`0x2a132c0`, `0x2a13332..0x2a1333c`), not 0; RBXCRASH reporter `0x6caed7c` is never entered. |

Do **not** substitute `SOBER_SCHED_R70_KEEP` / `SOBER_SCHED_NEST_ONE`: prior
A/B (`r70.log`, `nest.log`) proved they target the wrong object (`0x722e540`
is a TLS key descriptor, not the scheduler) and still int3.

### Rank 2 — Replay skipped `ctor[0]` and fix the replay order (clears `0x2e7b8e2`, `0x2e5d42a`)

- Knob: **`SOBER_REPLAY_FFLAG_CTOR=1`** (existing); keep `SOBER_SEED_GLOBAL_STRING`
  earliest (worker race with JNI_OnLoad/JavaVM seeds).
- Replay real chain `0x2e62390 → 0x2e618b0 → 0x2e61280 → 0x2e5bc10 → 0x2e7b850`
  (lazy create `0x2e79af0 → 0x2e77400` from sentinel `0x72a0b88`), which stores
  the control block at `handle+0x38` (`0x72a0520+0x38`).
- **Wiring fix (order only):** the `SOBER_JSON_APP_SINGLETON` postsettings block
  currently at `guest_app_shell.c:858-866` runs *before* the ctor replay at
  `:875-882`; move it after. If the ctor cannot run on some path, the narrow
  fallback is to call guest code `0x2e7b850` once with
  `rdi = 0x72a0520+0x38` — never a raw sentinel word write.
- Do **not** blanket-enable `ctor[1]=0x1dba8f3`, `ctor[3]=0x1db9a40`,
  `ctor[4]=0x1dba6c4` (they reinitialise the allocator). `ctor[2]=0x1dbe2b1`
  and `ctor[5]=0x1dba834` already run.
- **Success:** guard `0x72a0a30 == 0x101`; `0x72a0cd8` (lazy guard) set;
  `0x72a0cd0`/`0x72a0cc0` non-NULL; `*(u64*)(0x72a0520+0x38)` non-NULL and not
  the sentinel; `0x2e7b8e2` and `0x2e5d42a` no longer fault.

### Rank 3 — Populate the app/service singleton (clears `0x2ec1042` and `0x24ad562`)

- Prerequisite: Rank 2 complete (else postsettings hits `0x2e7b8e2` first).
- Knobs: **`SOBER_JSON_APP_SINGLETON=1` + `SOBER_NATIVE_OBJECTS=1`**
  (existing, both in the withhold chain). Replay genuine
  `nativePostClientSettingsLoadedInitialization3` `0x2383c18`; the **only**
  singleton store is `0x238497d`. Do **not** use
  `SOBER_REAL_APPSTATE_POSTSETTINGS` with the plain zeroed `java/util/List` —
  that is the object shape that caused Rank-2's fault.
- Guest-shape hardening for `sober_native_java_list()`: vptr must be genuine
  `0x6db3ec0+0x10`; `+0x30` control block (refcount 1 at `+8`, destroy slot from
  `0x72a0b88` or guest RVA) and `+0x40` element array must be **guest-allocated**
  via `0x1dbab02`, not one host `calloc` pointer serving both roles (the
  destructor `0x2e5baa0` / retain `0x2e7b8d0` interpret them differently).
- Log feature gate `0x6096928` and byte `0x77979c0`; if the gate is false,
  `0x30ef840` takes `0x1e31fb1 → 0x30f07aa` instead and this experiment needs a
  different diagnostic.
- **Success:** `*(0x72cb5e0)` non-NULL and attributable to `0x238497d`;
  `0x30ef840` returns non-NULL on the gate-true path; `0x2ec1042`
  (`rax=0`, `si_addr=0x0`) and `0x24ad562` (`rax=0`) both disappear; with
  `0x72cb648=1` the installer reaches `0x24ad5b6` / `0x24ae1fe`.

### Rank 4 — Restore genuine `app+0x408`/`+0x440`/`+0x488`, then remove the fake seeds

| Sub-run | Knob | Explicit RVAs / action | Success criterion |
| - | - | - | - |
| 4a | **`SOBER_SEED_APP_488=0`** | Replay `nativeAppBridgeV2InitWithParams` `0x24abf96` with fully backed getters so the guest itself builds the `+0x488` object: `0x24af28a` (`ebx=0x488`) / `0x24af28f` (vptr `0x6e02918`), `0x24af296` (object into `r15`) / `0x24af2a4` (`+0x20 = self`), `0x24af2cc` / `0x24af2e6 → 0x231c124`. | `*(app+0x488)` non-NULL, guest-owned, written by the `0x24af2e6` path; `0x31a1360` no longer faults; FP `0x320b0ba → 0x320b0e7 → 0x31a1168` completes. The `rax` sentinel is irrelevant — do not "fix" it. |
| 4b | **`SOBER_SEED_USER_CONTROLLER=0`** | Dispatch the genuine installer through vtable slot `0x6dbc7f8 → 0x24ad50a` (flag `0x72cb648=1` from JSON setter `0x23648be`); `0x24ad59e → 0x30ef840`; `0x24ad5b6 → 0x24ae1fe` writes `app+0x440`. `this+0xb0` shared_ptr built in `0x24abf96` at `0x24ac5a9`/`0x24ac5c8`. | `*(app+0x440)` non-NULL and inside the guest heap; `0x2772403` (`cmpl $0,0x140(%r14)` in callee `0x2772384`, called at `0x2772129`) no longer faults. |
| 4c | **`SOBER_SEED_APP_DELEGATE=0`** | Replay `nativeAppBridgeStartLuaAppDM` `0x25448f6` → heavy driver `0x25449fc` (mode-4 `0x24a8e44`); genuine writes `0x25d7e75` / `0x25f7e7e` (or flag-0 branch `0x24aed3e`) install `app+0x408`. | `*(app+0x408)` non-NULL and guest-owned; capture the next genuine fault (expected `0x2772161` on `app+0x408`) rather than seeding it. |

Only after 4b/4c/4a are all off can any callback be considered for the gate.

### Rank 5 — Drive the only reachable gate callback (`gameLoadedCallback`)

- Real JNI order (all real guest entry points; no thunk, no
  `SOBER_ALT_CALLBACK_PATH`): `0x2edd859` (JSON `{"EnableAppBridgeComponents":true}`)
  + `0x2edd739` → `0x2ebcf36` (`"AppShellNotifications","<id>","StartConversationWithUserId","Chat"`)
  → `0x233762c` → `0x24abf96` (registers notifier family at `0x24ad699` via
  `0x253cb56`, global `0x7234520` → slot `0x6dbd860`) → `0x247dc8d` →
  `0x25448f6` → `0x26fe80f` (all ten typed `StartAppParams` getters) →
  `0x2ebd51a` → `0x2ebca9e` → `0x2773194`.
- **Success:** guest enters notifier `0x27eaf6e` with `RSI = placeId` (breakpoint
  or hit-counter; thunk return address `0x27eafb3`), cached class `0x72a2f48`
  and method `0x72a2fc0` (write at `0x227b4eb`) non-NULL; bridge log
  `guest->Java callback: gameLoadedCallback` with count increased ≥1; no
  `ALT PATH guest call 0x25848e0`, no `HONESTY NOTICE`.
- `onAppShellReloadNeeded` (`0x72a3020`, sole write at `0x227b432`, no native
  reader) is excluded; `onAppBridgeNotification` remains rejected.

### Rank 6 — Strict acceptance (evaluate only after fixes are env-free)

Not an experiment: the condition under which the lead may legitimately evaluate
`app shell rendered`. All must hold simultaneously:

1. Ranks 1–5 success with **every opt-in unset** (the fixes live in default code).
2. A genuine `gameLoadedCallback(J)V` from `0x27eaf6e` plus phase-3 guest render
   RVA `0x649d422`.
3. `seeded_state == 0` — no `SOBER_SEED_*`, `SOBER_*TSD*`, `SOBER_REPLAY_*`,
   `SOBER_JSON_*`, `SOBER_NATIVE_OBJECTS`, etc. from
   `guest_app_shell.c:1337-1375`; zero marker emissions until then.
4. Engine SHA `3e498d6494…` / BuildID `204ad049…` unchanged; phase-2/3 checks
   green (`guest init complete`, `first frame rendered`, `verify-native.sh`,
   ctest 2/2, ldd libc/ld-linux only).

## 3. Experiment hygiene / non-goals

- One new variable per run; ranks 1a→1b→1c and 4a→4b→4c are separate A/B runs.
- Never set `0x7cdd149`/`0x7cef650` (RBXCRASH mode bytes); never change
  `__stack_chk_guard` or `sr_is_safe_ptr()` to accept `0xabcdef0123456789`.
- Never widen the canary "carrier" theory: `rax` at `0x31a1360` is the frame
  canary loaded from GOT `0x72293f8`; the fault is the NULL `+0x488` field.
- No blanket ctor enablement, no direct `call 0x25848e0`, no fake host vtables,
  no byte patches to bypass checks, no decision-013 gate change.
- This worker edited only `analysis/A10-stability-synthesis.md`; the engine was
  not modified; **no success is claimed**.

## 4. Evidence index

- `analysis/stability-fault-audit.md` (#470): `0x2e7b8e2` / `0x24ad562` /
  `0x2772403` chain, FFlag ctor chain, singleton writer `0x238497d`.
- `analysis/game-loaded-path.md` (#466): notifier `0x27eaf6e`, thunk `0x25848e0`,
  cached class/method, registration `0x24ad699 → 0x253cb56`, call order.
- `analysis/sentinel-abcdef-trace.md` (#475): `0x31a1360` fault, `0x320b0ba`
  caller, GOT canary provenance, key-2 TSD GOT binding gap, proposed
  `SOBER_BIONIC_TSD_GOT`.
- `analysis/int3-6caef66-rbxcrash.md` (#464) and
  `analysis/tls-2a0de60-r70-correction.md`: RBXCRASH reporter chain, scheduler
  underflow tag, TLS descriptor details.
- `analysis/tsd-static-buffer-abort.md`: `realloc()` abort from static TSD seed.
- `analysis/bootstrap/bootstrap-map.md` (#378) / `init-array.json`: decision-008
  ctor skip set and replay order.
- `analysis/lua-app-start.md` (#468), `analysis/appstate_fields.md` (#450):
  real Java order, `app+0x408`/`+0x440` writers, `0x2ec1042` prerequisite.
- `analysis/jni/appshell-reload-trigger.md` (#467): `onAppShellReloadNeeded`
  ruled out natively.
- `docs/decisions/013-no-weak-app-shell-marker.md`,
  `analysis/decision-memo-alt-thunk-vs-013.md`: strict gate, rejected callback,
  withhold policy.
- Harness source read only: `src/linux/guest_app_shell.c:755-777,816-817,1337-1375`.
- Live logs read only: `realpath1.log`, `realstate2.log`, `game.log`, `full.log`,
  `notif.log`, `final_baseline.log`.
