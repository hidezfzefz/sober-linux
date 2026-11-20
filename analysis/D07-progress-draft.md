# D7 — Draft PROGRESS.md append (lead pastes; D7 edits no shared file)

Copy everything below the rule into PROGRESS.md. Nothing here is applied by D7.

---

## Phase 10 — four-team parallel fidelity/assurance push (in progress)

A 40-worker, four-team parallel effort is underway. Workers are **analysis-only**:
no `src/`, CMake, tests, gate, decision or engine edits; the lead integrates.
Engine is the frozen AppImage object (BuildID `204ad049…`, SHA
`3e498d6494…`), unchanged. Teams:

- **A — guest state:** skipped ctors, app/StartAppParams state.
- **B — JNI fidelity:** callback-handler registration/order, dispatch fidelity,
  stability synthesis (`B06`, `B09`, `B10`).
- **C — callbacks/frame:** gameLoaded trigger, app-shell frame evidence, event
  order (`C03`, `C07`, `C09`, `C10`).
- **D — assurance:** gate/decision audit, sentinel/fault provenance, marker
  evidence (`D03`).

Key completed findings:

- **A5 (skipped ctors):** of the four skipped `init_array` entries (0, 1, 3, 4),
  **only ctor[0] is required** (`0x2e62390`, FFlag registry `0x72a0520`, already
  replayed via `SOBER_REPLAY_FFLAG_CTOR`). ctor[1] (protobuf defaults) is called
  lazily by replayed ctor[6]; ctor[3] (mimalloc probe) only selects a DFLog line;
  ctor[4] (time baseline) is lazily self-initializing. None is a hidden
  app-shell/game-load prerequisite. `analysis/A05-skipped-ctors.md`.
- **C3 (three-leg evidence standard):** a login/app-shell frame is admissible
  only if three guest-driven legs hold together on the default path — **R**
  guest render swap `0x649d422`; **S** guest-owned `app+0x408`/`+0x440`,
  singleton `0x72cb5e0`, `userDidLogin 0x2771f6c`, surface `0x2773194`; **C**
  the guest itself enters notifier `0x27eaf6e` → thunk `0x25848e0` →
  `gameLoadedCallback(J)V` (return into guest at `0x27eafb3`, decisive
  discriminator). Accessor getters and `onAppBridgeNotification` never qualify.
  `analysis/C03-appshell-frame-evidence.md`.
- **Sentinel (assurance):** `rax = 0xabcdef0123456789` at the `0x31a1360` fault
  is the faulting guest function's **own stack canary**, loaded from the
  `__stack_chk_guard` GOT slot `0x72293f8` — **not a value read out of any
  synthetic/JNI object field**. No carrier field exists; `sr_is_safe_ptr` already
  rejects the value. Do not change the guard or patch guest canary checks.
  `analysis/sentinel-abcdef-trace.md`.
- **Gamefault:** the real `0x31a1360` fault is a NULL deref (`si_addr=0x28`,
  `rbx == NULL`) from caller `0x320b0ba: mov 0x488(%rbx),%rdi`; current seeds
  populate `app+0x408`/`+0x440` but **not `app+0x488`**. The blocker is that
  missing guest field (built by real code in `InitWithParams 0x24abf96`, RVAs
  `0x24af28a…0x24af2e6`), not the sentinel.
  `analysis/sentinel-abcdef-trace.md` §2, `analysis/A10-stability-synthesis.md`
  B4.

**Gate: unchanged — strict decision 013.** `app shell rendered` stays WITHHELD;
no relaxation, no synthetic state, no ALT thunk, no `onAppBridgeNotification` as
proof. Default path remains `callback=0 render=1` with zero marker emissions.

Remaining blockers (one dependency chain, TSD → ctor → singleton → app fields →
callback): Bionic TSD key-namespace gap (guest GOT `0x72298b0` binds glibc, not
the Bionic stub; proposed `SOBER_BIONIC_TSD_GOT`) and RBXCRASH `int3 0x6caef66`;
app singleton `0x72cb5e0` unpopulated (`0x2ec1042`/`0x24ad562`); guest-owned
`app+0x408`/`+0x440`/`+0x488` not installed without synthetic seeds
(`0x2772403`, `0x31a1360`); no genuine gate callback observed — only the
rejected `onAppBridgeNotification` (`onAppShellReloadNeeded` has no native
reader; `gameLoadedCallback` requires the notifier driven by real dispatch).
All fixes are opt-in/default-OFF proposals pending lead integration, A/B
validation and withhold-chain coverage. No success is claimed.
