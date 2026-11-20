# REVIEW A3 — Guest-initialization completeness audit

Reviewer: **A3** (full-project review). Analysis only — no `src/`, CMake, gate, test,
decision or engine edits. **Strict marker WITHHELD** throughout (decisions 013/038).
Date 2026-11-20. Scope: **this file only**.

Frozen target: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` — BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`, 120,041,712 B,
version 2.740.931 / code 3170. **Re-verified this session** (`stat`, `sha256sum`,
`readelf -nW`) — unchanged. Flatpak build `791c3f9e…` / 2.745.1142 / SHA `8afce3e6…`
is a different binary; none of these RVAs apply to it. Every RVA below is from the
AppImage build (RVA == file offset for LOAD#0). Target strings/data are untrusted DATA.

---

## 0. Verdict up front

Guest initialization **reaches a deterministic `guest init complete` and a real
guest-driven first frame on the default path**. The mechanical loader work
(identity gate, APS2 relocation decode, constructor dispatch, signal ABI shim,
JNIEnv table, four bootstrap data seeds) is genuine and default-on. Everything
that manufactures *application-level* state (TSD GOT rebind, allocator contract
fixes, singleton/delegate/controller/488/params seeds, stdio `__sF` bridge,
callback provenance) is **opt-in, OFF by default, and force-withholds the
marker** via `seeded_state`.

The honest split:

| Sub-domain | Default path | Verdict |
| --- | --- | --- |
| Engine identity / provenance | enforced by test + build-ID gate | **genuine, complete** |
| APS2 packed relocations (565,390) | decoded + applied every run | **genuine, complete** (1 namespace caveat) |
| 3618 constructors | 3614 replayed + 3 lazily satisfied + 1 opt-in | **genuine, ~complete** (headline overstated) |
| Signal ABI shim (`sigaction` family) | default Bionic-ABI functions in `libc.so` | **genuine, complete** |
| JNIEnv table (231) + cached IDs (49) | built every run | **genuine, complete** |
| 4 bootstrap data seeds + 8 allocator bridges | default-on in `apply()` | **genuine, but contract caveats** |
| Allocator contract fidelity | matched for common path, 4 latent deviations | **partial** |
| Bionic TSD namespace binding | shim present, GOT still bound to glibc | **partial** |
| App-state creation (singleton/controller/delegate/488) | opt-in modules only | **not on default path** |
| Genuine app-shell callback / game load | never entered notifier `0x27eaf6e` | **absent** (marker withheld) |

**Domain estimate: ~88 % overall for "guest init"** (see §10 for per-sub-domain
percentages). The residual ~12 % is almost entirely the app-state/callback tail,
not the loader/init mechanics.

---

## 1. Engine identity — genuine, default-path, complete (100 %)

- Enforced three ways: (a) `tests/guest-dlopen.sh:0`, `first-frame.sh:0`,
  `app-shell.sh:0` re-read BuildID `204ad049…` and abort on mismatch; (b) the
  bootstrap refuses to patch unless NT_GNU_BUILD_ID matches
  `SOBER_BOOTSTRAP_BUILD_ID_BYTES` (`bootstrap_profile.h`, `bionic_bootstrap.c:373-426,
  422`); (c) this audit re-hashed the file (SHA `3e498d6494…`, 120,041,712 B).
- The on-disk engine is never modified: the harness uses a byte-identical memfd
  copy with `DT_INIT_ARRAYSZ` neutralised; all tests re-check SHA before/after.
- RVA anchors fixed by the identity: `DT_INIT_ARRAY` VMA `0x7220e58`,
  `DT_ANDROID_RELA` `0x15e00`, RELRO `0x6d37bc0/0x4f3440` → `[0x6d37bc0,0x722b000)`,
  GOT window `0x7229400..0x7229a00`. **All identity checks pass.**

---

## 2. APS2 packed relocations — genuine, default-path, complete (100 % applied)

`src/linux/android_relocs.c` decodes `DT_ANDROID_RELA (0x60000011)` /
`DT_ANDROID_RELASZ (0x60000012)` as an `APS2` SLEB128 delta stream. Verified
across 81 harness logs (this session):

```
[relocs] applied=565390 (relative=565308 symbolic=82 unknown=0 unresolved=0)
```

- Count matches the target exactly: 565,390 relocations, all accounted for;
  565,308 `R_X86_64_RELATIVE`, 82 symbolic, **0 unknown, 0 unresolved**.
- RELRO is temporarily opened RW around the pass and restored to `PROT_READ`
  (`android_relocs.c:206-213, 309-311`); the `SOBER_BIONIC_TSD_GOT` rebind must run
  before that restore (see §4).
- **Caveat (namespace, not correctness):** the 82 symbolic relocations resolve via
  `dlsym(RTLD_DEFAULT, name)` (`android_relocs.c:250`). This binds the guest's
  `pthread_getspecific`/`setspecific` PLT GOTs to **glibc**, not the versioned
  Bionic stub — the root cause addressed by the opt-in in §4. No relocation is
  missing or skipped; the only fidelity question is *which* namespace supplies 82
  symbols.

---

## 3. 3618 constructors — genuine mechanics, but the "3618/3618" headline needs qualification

### 3.1 What actually runs by default

`guest_dlopen_host.c:428-461` `drive_constructors()` uses, with no env set,
`std_ranges = { {2,3}, {5,3618} }` — i.e. **3614** `init_array` entries (index 2,
then 5..3617). `bootstrap_profile.h:154-161` declares the same canonical ranges.
In addition, `bionic_bootstrap.c:833-892` explicitly invokes:
- arena init `0x1dbb178` (self-guarded via `cmpxchg` on `0x7cd8fb0`), and
- allocator thread init `0x1e47927` (reads TLS key `0x7282000`),

both after ctor[2] and before the `[5,3618)` range.

Logs confirm completion: `/tmp/opencode/default.log:3649-3667` reaches
`ctor[3617] rva=0x227a8ba` then `guest init complete`; `ctor1.log:3667` likewise.
So **init completion is genuine and default-on.**

### 3.2 The four skipped entries are benign (A05, re-verified)

| idx | entry RVA | Role | Why skipping is safe |
| --- | --- | --- | --- |
| 0 | `0x2e62390` | FFlag provider registry; guard `0x72a0a30` → `0x2e618b0` → `0x2e61280`; global `0x72a0520` | **Required**; replayed only via opt-in `SOBER_REPLAY_FFLAG_CTOR` (`guest_app_shell.c:995`) or `SOBER_CTOR0`/`SOBER_CTOR_RANGES=all`. Without it the guest faults at `0x2e5d42a` / `0x2e7b8e2`. |
| 1 | `0x1dba8f3` → `0x1dba8f8` | protobuf defaults/descriptor pool `0x783da90/aa0/aa8`, guard `0x783dab0`, real init `0x6b90794` | Lazily called from replayed ctor[6] at `0x1dbef4a`; empirical `0x783daa8=1` at fault time. |
| 3 | `0x1db9a40` | mimalloc usable-size probe `0x7cdedd8/0x7cdede0` | Sole reader `0x239eb14` selects a DFLog string; no functional effect. |
| 4 | `0x1dba6c4` → `0x1dbe828` → `0x1dbe894` | monotonic-time baseline `0x7d9ce38`/`0x7d9ce30`, `clock_gettime@plt 0x6d31c40` | Lazily self-installed by time helper `0x1ea4370`. |

### 3.3 Verdict

- Default path = 3614 replayed + 2 explicit init calls + 3 skipped-but-lazily-satisfied
  (1/3/4). ctor[0] is the only genuinely **required** ctor not on the default path,
  and it is behind `SOBER_REPLAY_FFLAG_CTOR` (a withhold entry). The README's
  "3618/3618 100 %" is true only under `SOBER_CTOR_RANGES=all`; the honest default is
  "init completes with the required set, 3614 dispatch + 1 opt-in + 3 lazy".
- All ctor replays call **genuine guest entry points** (no byte patches, no fake
  vtables). `SOBER_CTOR_RANGE0/1` overrides and `SOBER_CTOR0/1` injections are
  diagnostic/withhold knobs.

---

## 4. Bionic TSD shim + GOT rebinding — shim genuine, binding partial, rebind opt-in

**Default-path genuine:** `libc.so` (`ndk_bionic_libc`) exports native
`pthread_getspecific`/`setspecific` (`bionic_libc.c`, TSD array in `bionic_libc.c:474-486`)
as versioned `LIBC` symbols; the guest's own key-2 init (`0x1e4795c → 0x1dbaddd`) and
key-3 init (`0x2ea6520 → 0x1dbad62`) can allocate real guest thread state.

**Not default-path:** the guest GOT words still point to glibc because of §2's
`dlsym(RTLD_DEFAULT)`. `SOBER_BIONIC_TSD_GOT` (`android_relocs.c:292-309`, withhold
entry) overwrites exactly two slots, with `dlvsym(stub,"…","LIBC")`:

| Slot | Symbol | PLT | Calls | Rebind |
| --- | --- | --- | ---: | --- |
| `0x72298b0` | `pthread_getspecific` | `0x6d322e0` | 269 | yes (opt-in) |
| `0x72295c8` | `pthread_setspecific` | `0x6d31d10` | 54 | yes (opt-in) |
| `0x72297a0` | `pthread_key_create` | `0x6d320c0` | 9+1 | only after stub work (stub forwards to glibc) |
| `0x722a2c8` | `pthread_key_delete` | `0x6d33710` | 2 | only after stub work |
| `0x7229508` | `pthread_once` | `0x6d31b90` | 6 | no (primitive, correct in glibc) |
| `0x722a4f8` | `__cxa_thread_atexit_impl` | — | — | no |
| `0x72293f8` | `__stack_chk_guard` | — | — | **NEVER** (canary; decision 013) |

- Guest keys: key 2 global `0x7282000`; key 3 global `0x72a1b08` (created by
  `pthread_once` cb `0x2ea6640`); third key global `0x72a1b00`; accessor `0x2ea6440`.
  Rebind was proven to clear the `0xABCDEF0123456789` canary-sentinel / `realloc():
  invalid pointer` family; downstream faults move to `0x2772161`/`0x3281ca2` or the
  `0x6caef67` nesting underflow (A04).
- **Residual fidelity gaps:** key_create/key_delete still glibc (Bionic key encoding
  not native); private TSD array is 512 slots (no key≥512 assertion); the
  `0x2ea6670` destructor decrements the key before re-arming (cross-key corruption
  risk with glibc keys). Rebind writes must precede the RELRO `mprotect(PROT_READ)`.
- **Caveat found this review:** the opt-in `SOBER_SEED_JNI_SINGLETON`
  (`bionic_bootstrap.c:952-976`) writes a JNI handle into GOT `0x72293f8` — the very
  slot A4 identifies as `__stack_chk_guard`. This is an invasive, withheld opt-in;
  keep OFF and treat the slot as canary-owned on the default path.

**Verdict:** the *shim* is a genuine default-path component; the *GOT rebinding* is a
diagnostic opt-in and a withhold entry. A truly clean Bionic TSD namespace is not
the default.

---

## 5. Allocator bridges / contracts — 8 default bridges, 4 latent deviations

Default-on in `bionic_bootstrap.c:588-635` (`install_bridges`) unless
`SOBER_BOOTSTRAP_NO_BRIDGES`: eight 12-byte `movabs/jmp` trampolines at
`0x1dbad62` (small alloc), `0x1dbdfba` (usable_size), `0x227aafa` (reallocate),
`0x1dbab02` (allocate), `0x1dd60e5` (aligned), `0x1dbe559` (free), `0x253225e`
(7th calloc), `0x231f0e2` (runtime calloc); plus the calloc slot patch at
`0x726c0c0` (`patch_calloc_slot`). Each prologue fingerprint is verified before
patching; pages restored RX. Four data seeds run in the same default `apply()`.

The guest has **two allocation contracts**: Contract A (plain pointer) and Contract
B (`n+0x10` block, 16-byte 0/handle header, return `base+0x10`). The guest rebinds
its slot table at `0x231e88c`/`0x231e8f9`: `0x726c0c0→0x231f0e2`,
`0x726c0c8→0x231f13a`, **`0x726c0b8→0x231f53a`** (calloc-free thunk, does
`-0x10`), `0x726c0b0→0x234f716`. Our trampoline at `0x231f0e2` still catches the
rebound slot. Latent deviations (A07, re-affirmed):

| ID | Deviation | RVA / line | Status |
| --- | --- | --- | --- |
| M1 | `host_calloc` writes `total` into the 16-byte header; guest writes `0` (`xor r14d,r14d` `0x231f1ac`, `mov %r14,(%r12)` `0x231f243`) | `bionic_bootstrap.c:299` | latent SIGSEGV at `0x6a28c22` if flags `0x7cdd149` & `0x7839dc8` both set |
| M2 | 7th bridge `0x253225e` is Contract-A plain-calloc but redirected to `base+0x10` `host_calloc` | `0x25322ea` `jmp memset` | dormant (zero hits after rebind); secondary install `0x6a35e30` could revive |
| M3 | `host_free`/`host_reallocate`/`host_usable_size` lack `-0x10` → interior glibc pointer for Contract-B pointers | `:329/:338/:367` | same failure family as the observed `realloc(): invalid pointer` |
| M4 | plain `host_allocate` pointers freed via rebound `0x231f53a` become `p-0x10` | `0x231f542` | mechanism proven, occurrence unproven |

The common 0x1100-handle helper-cluster pair (`0x2333ac5` calloc → `0x2333b91` free)
is structurally matched, so these are **not** the observed fault cause. Fixes
(`SOBER_ALLOC_FIX_*`, `SOBER_ALLOC_CONTRACT_FIX`, `SOBER_ALLOC_SITE_FIX`) are all
opt-in and withhold-listed. **Verdict: bridges installed and common path works;
contract fidelity partial.**

---

## 6. Signal shim — genuine, default-path, complete (100 %)

`src/compat/bionic_signal.c` is compiled directly into `libc.so` (`CMakeLists.txt:103-114`)
and exports **Bionic-ABI** `sigaction`, `sigemptyset`, `sigfillset`,
`pthread_sigmask` via `__asm__("sigaction")` aliases — not trampolines
(`gen_bionic_libc.py:185-186` skips them). The 32-byte Bionic `sigaction` is
statically asserted; masks are translated 8-byte Bionic ↔ 128-byte glibc. This
fixes the old 152-byte glibc write into the guest's 32-byte stack slot (the
`+0x68`/stack-smashing family). This is a **default-path genuine** shim.

- Host crash instrumentation (`guest_dlopen_host.c:282-311`) installs SIGSEGV,
  SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP. `SOBER_TRACE_NO_SIGTRAP_HANDLER`
  (`:292-300`) is a benign host diagnostic that leaves SIGTRAP at default — not a
  guest-state seed (correctly absent from `seeded_state`).
- `bionic_sf_full.c` (stdio `__sF` bridge: guest `__sF+0x98`→stdout, `+0x130`→stderr)
  is **opt-in** via `SOBER_BIONIC_SF_FULL` and is withhold-listed. **Verdict: signal
  ABI complete on default path; stdio bridge opt-in.**

---

## 7. JNI singleton / data seeds — bootstrap seeds genuine, app-state seeds opt-in

### 7.1 Default-on bootstrap seeds (`bionic_bootstrap.c:704-740`, `do_apply`)

| # | Slot RVA | Init RVA | Size | Notes |
| --- | --- | --- | --- | --- |
| 0 | `0x7834570` | `0x2251b2a` | `0x80` | allocator object (inside `0x7834554` block) |
| 1 | `0x7d9cf40` | `0x1dba834` (= ctor[5]) | 1024 | JNI singleton: zeroed here, non-idempotent xchg done by replayed ctor[5] |
| 2 | `0x79413b8` | `0x225dd2a` | `0x40` | empty string; default path uses direct field writes at `0x7941360/3c0/3c8/3b8/380/3b0` + `str_ctor 0x1dc1208`/literal `0x49b356` |
| 3 | `0x7cdd118` | `0x227be14` | 8 | pre-JNI registry (single pointer) |

Plus page-size force `0x7281fe0/0x7281fe8 = 0x1000`. These are skip-able only by
`SOBER_BOOTSTRAP_NO_SEEDS` / `SOBER_SEED_MASK` (both withhold entries). No packed
relocation targets these slots (verified by worker-2), so zeroing is safe.

JNIEnv function table: 231 `fn` slots (`native_jni.c:772-878`, canonical offsets
`0x20..0x750`) and 49 cached jmethodIDs are built every run — genuine and complete.
Cached class `0x72a2f48` / method `0x72a2fc0` are the candidates for the genuine
callback, but are only populated on the opt-in JSON-singleton path.

### 7.2 Opt-in app-state seeds — NOT on the default path

All are withhold entries: `SOBER_SEED_JNI_SINGLETON` (writes `0x72293f8`, §4),
`SOBER_JSON_APP_SINGLETON`, `SOBER_JSON_FLAG_BRIDGE`, `SOBER_SEED_GLOBAL_STRING`,
`SOBER_SEED_USER_CONTROLLER`, `SOBER_SEED_APP_DELEGATE`, `SOBER_FAITHFUL_APP_DELEGATE`,
`SOBER_SEED_APP_488`, `SOBER_APP488_FIX`, `SOBER_SEED_JNI_VM`,
`SOBER_FAITHFUL_JAVAVM`, `SOBER_NATIVE_OBJECTS`, `SOBER_APP_SINGLETON_BRIDGE`
(strategy `slice`/`replay`, release-old), `SOBER_GAME_PARAMS` / `SOBER_STARTAPP_PARAMS`
/ `SOBER_STARTGAME_V2` / `SOBER_STARTAPP_V2` (and 44 value overrides),
`SOBER_PLATFORM_PARAMS_FIELDS`, `SOBER_SINGLETON_FIX*`, `SOBER_SINGLETON_SERVICES*`,
`SOBER_LUA_APP_START`, `SOBER_REAL_APPSTATE_*`. The app singleton itself
(`0x72cb5e0`) has a single writer in the whole engine — `mov %r14,0x72cb5e0` at
`0x238497d` inside `nativePostClientSettingsLoadedInitialization3` (`0x2383c18`) —
which is gated by flag `0x72cb648` and GL predicate `0x6096928`; no default run
enables that replay (A13). **Verdict: bootstrap data seeds genuine; application
state is manufactured only by opt-in modules.**

---

## 8. Opt-in module inventory (src/compat + src/linux)

All OFF by default; every name below is in the `seeded_state` withhold expression
(`guest_app_shell.c:1449-1545`) unless noted as a benign diagnostic.

| Area | Module / knob | Classification |
| --- | --- | --- |
| TSD | `SOBER_BIONIC_TSD_GOT` (`android_relocs.c:296`) | opt-in diagnostic (GOT write `0x72298b0/0x72295c8`) |
| TSD | `SOBER_TLS_SEED`, `SOBER_STUB_TSD_SEED`, `SOBER_NO_SCHED_NEUTRAL`, `SOBER_TLS_BYTES`, `SOBER_SCHED_RVA/R70_KEEP/NEST_ONE` (`guest_app_shell.c:1058-1068`) | opt-in diagnostics (TSD/scheduler seeds/neutering) |
| Stdio | `SOBER_BIONIC_SF_FULL` (`bionic_sf_full.c:56`) | opt-in diagnostic |
| Allocator | `SOBER_BOOTSTRAP_NO_BRIDGES/NO_SEEDS`, `SOBER_SEED_MASK`, `SOBER_NO_DIRECT_SEED_EMPTY`, `SOBER_ALLOC_CONTRACT_FIX`, `SOBER_ALLOC_SITE_FIX`, `SOBER_TRACE_CALLOC/USABLE/FREE_SLOT`, `SOBER_HANDLE_SANITISER`/`NO_…` | opt-in diagnostics / withhold |
| Bootstrap patches | `SOBER_CALLER_PTR_FIX/FORCE`, `SOBER_HELPER_REPAIR(_V2)`, `SOBER_HELPER_LOG`, `SOBER_FINNER_LOG/BP`, `SOBER_CURL_HOLDER_FIX`, `SOBER_BOOTSTRAP_INTERPOSE`, `SOBER_VALIDATOR_PATCH`, `SOBER_FINAL_PATCH`, `SOBER_FIELD_NORMALISER`, `SOBER_OBJECT_FIX`, `SOBER_APP_SHELL_FIX`, `SOBER_CALL_SITE_FIX` (guest code writes at `0x23331ae`, `0x23330e7`, `0x6a26e78`, `0x245771e`, `0x234ddc0`) | opt-in diagnostics / withhold |
| Constructors | `SOBER_REPLAY_FFLAG_CTOR`, `SOBER_CTOR0/1`, `SOBER_CTOR_RANGES` | opt-in diagnostics / withhold |
| JNI/state | `SOBER_SEED_*`, `SOBER_FAITHFUL_*`, `SOBER_*_FIX`, `SOBER_*_V2`, `SOBER_APP488_*`, `SOBER_GAME_PARAMS`/`SOBER_STARTAPP_*`/`SOBER_SGV2_*`, `SOBER_APP_SINGLETON_*`, `SOBER_JSON_*`, `SOBER_LUA_APP_START`, `SOBER_REAL_APPSTATE_*` | opt-in diagnostics / withhold |
| Callback | `SOBER_ALT_CALLBACK_PATH` (harness thunk `0x25848e0`), `SOBER_CALLBACK_PROVENANCE`, `SOBER_NOTIFIER_PROBE`, `SOBER_START_APP`, `SOBER_GAME_LOADED_PATH`, `SOBER_LOG_APP_BRIDGE_NOTIFICATION` | opt-in diagnostics / withhold |
| Render | `SOBER_GUEST_ROUTE_A` (guest EGL `0x649c21e`), `SOBER_RENDER`/`SOBER_APP_SHELL` | `ROUTE_A` opt-in/withhold; `RENDER`/`APP_SHELL` are **legitimate phase gates**, correctly NOT withheld (D02 §5.6) |
| Benign diagnostics | `SOBER_JNI_VERBOSE`, `SOBER_APP_SHELL_DIAGNOSTIC/_WAIT_MS`, `SOBER_APP_SLOT_DIAG`, `SOBER_TRACE_*`, `SOBER_DUMP_HANDLE`, `SOBER_WATCHDOG`, `SOBER_GDB_PAUSE_MS`, `SOBER_WINDOW_HOLD_MS`, marker paths | log/timing/config only, no withhold |

Withhold-chain health: D02 (snapshot 21:44Z) found **71 names** in `seeded_state`,
all 23 Tier-1 guest-mutating knobs closed, but **47 Tier-2 value modifiers** plus
10 Tier-3 config values and the dynamic `SOBER_PROP_<name>` family still outside
the chain — a false-green surface if those modifiers are set with a module alias.
The README's "22 opt-in modules" is a module count, not a knob count.

---

## 9. What remains for a clean default path

1. **A genuine app-shell callback (blocker R1, critical).** No default run ever
   enters notifier `0x27eaf6e` → thunk `0x25848e0` (`call *0x470(%rax)` at
   `0x258495a`, return `0x27eafb3`) → `gameLoadedCallback(J)V`. The only observed
   callbacks are the harness-triggered ALT thunk (`SOBER_ALT_CALLBACK_PATH`) and
   `onAppBridgeNotification`, both rejected by decisions 013/038. Marker stays
   WITHHELD.
2. **Five open guest-state faults** (D04): `0x2eb4b65`, `0x24ad562` (controller
   installer `0x24ad50a` dereferences NULL singleton `0x72cb5e0`), `0x2772403`,
   `0x2ec1042`, `0x31a1360`. The genuine store to `app+0x440` at `0x24aea9c` is
   never reached; dispatch is `call *0x30(%rax)` at `0x23389f7`.
3. **Default-path app state.** `SOBER_REPLAY_FFLAG_CTOR` (ctor[0] `0x2e62390`) is
   required yet opt-in; singleton `0x72cb5e0` / controller / delegate / 488 exist
   only as opt-in modules. A clean path needs the guest's own replay
   (`0x2383c18` → `0x608f85a` → `0x238497d`) reached with all opt-ins unset.
4. **Namespace fidelity:** bind TSD GOTs to the Bionic stub by default (or prove
   glibc TSD is observationally equivalent), and implement native
   `pthread_key_create`/`delete` for `0x72297a0`/`0x722a2c8`.
5. **Allocator contracts:** resolve M1–M4 (header `0` at `bionic_bootstrap.c:299`;
   plain-pointer 7th bridge; header-aware generic bridges) without opt-ins.
6. **Withhold-chain hygiene:** close the 47 Tier-2 + 10 Tier-3 + `SOBER_PROP_*`
   gaps and keep `SOBER_STARTAPP_V2`/`SOBER_STARTAPP_PARAMS` aliases paired.

None of these justify emitting the marker; the gate stays strict until all
three legs (notifier `0x27eaf6e`, render `0x649d422`, no seeded_state) hold.

---

## 10. Domain % estimate

| Sub-domain | Estimate | Basis |
| --- | ---: | --- |
| Engine identity / provenance | **100 %** | BuildID + SHA + build-ID guard re-verified |
| APS2 relocations applied | **100 %** | 565,390/565,390; 0 unknown/unresolved |
| Constructor dispatch to init-complete | **95 %** | 3614 default + 3 lazy + 1 opt-in; full 3618 only opt-in |
| Signal ABI shim | **100 %** | Bionic 32-byte `sigaction` default in `libc.so` |
| JNIEnv table + cached IDs | **100 %** | 231/231 slots, 49/49 IDs (build-time verified) |
| Bootstrap data seeds (4) | **100 %** | default-on, zero-relocation slots verified |
| Allocator bridge install + common contract | **80 %** | 8 bridges default; M1–M4 latent |
| Bionic TSD namespace binding | **70 %** | shim genuine; GOT rebind opt-in; key_create/delete glibc |
| App-state creation (singleton/controller/delegate/488) | **35 %** | opt-in modules only, no default-path creation |
| Genuine app-shell callback / game load | **≈5 %** | notifier `0x27eaf6e` never entered; marker withheld |
| Withhold-chain coverage | **≈85 %** | Tier-1 closed; Tier-2/3 + `SOBER_PROP_*` open |

**Composite guest-init domain ≈ 88 %.** Reading it as two layers: the *loader/init
mechanics* layer (identity, relocations, constructors, signals, JNI table, seeds)
is ~95–100 % and genuinely default-path; the *application-state/callback* layer
is ~20–35 % and almost entirely opt-in diagnostics. A "clean default path" that
earns the marker additionally requires items §9.1–9.3 — until then no success is
claimed and the strict marker remains **WITHHELD**.

---

## 11. Evidence index (explicit RVAs)

- Identity: BuildID `204ad049…`, SHA `3e498d6494…`, 120,041,712 B (re-verified this
  session); `bootstrap_profile.h` build-ID bytes; test scripts `guest-dlopen.sh`,
  `first-frame.sh`, `app-shell.sh` §0.
- APS2: `DT_ANDROID_RELA=0x60000011`, `DT_ANDROID_RELASZ=0x60000012`, stream
  `0x15e00`/`0x1eb1f2`; 565,390 = 565,308 relative + 82 symbolic; RELRO `0x6d37bc0/0x4f3440`.
- Constructors: `DT_INIT_ARRAY 0x7220e58` (file `0x721ce58`), 3618×8; ranges
  `[2,3)+[5,3618)`; ctors `0x2e62390`, `0x1dba8f3`, `0x1dbe2b1`, `0x1db9a40`,
  `0x1dba6c4`, `0x1dba834`; arena `0x1dbb178`, thread init `0x1e47927`;
  FFlag guard `0x72a0a30`→`0x2e618b0`→`0x2e61280`, global `0x72a0520`.
- TSD: GOT `0x72298b0/0x72295c8/0x72297a0/0x722a2c8/0x7229508/0x722a4f8`,
  PLTs `0x6d322e0/0x6d31d10/0x6d320c0/0x6d33710/0x6d31b90`, canary `0x72293f8`;
  keys `0x7282000/0x72a1b08/0x72a1b00`; faults `0x6caef67`, `0x2772161`, `0x3281ca2`.
- Allocator: bridges `0x1dbad62/0x1dbdfba/0x227aafa/0x1dbab02/0x1dd60e5/0x1dbe559/
  0x253225e/0x231f0e2`; slots `0x726c0b8/0x726c0c0/0x726c0c8`; rebind `0x231e88c/
  0x231e8f9`; contract B `0x231f0e2/0x231f13a/0x231f1ac/0x231f243/0x231f53a/
  0x231f542`; flags `0x7cdd149/0x7839dc8`; special free `0x6a28bf4/0x6a28c22`.
- Seeds: `0x7834570/0x2251b2a`, `0x7d9cf40/0x1dba834`, `0x79413b8/0x225dd2a`,
  `0x7cdd118/0x227be14`; page size `0x7281fe0/0x7281fe8`.
- JNI/callback: table `0x20..0x750` (231 slots), cached `0x72a2f48/0x72a2fc0`;
  singleton `0x72cb5e0` writer `0x238497d`; controller `0x24ad50a`, `app+0x440`
  store `0x24aea9c`, dispatch `0x23389f7`; notifier `0x27eaf6e`/call `0x27eafae`/
  return `0x27eafb3`, thunk `0x25848e0`, `call *0x470(%rax)` `0x258495a`;
  render `0x649d422`; open faults `0x2eb4b65/0x24ad562/0x2772403/0x2ec1042/0x31a1360`.
- Source: `src/linux/android_relocs.c:206-311`, `src/linux/guest_dlopen_host.c:282-311,
  428-461, 543, 617`, `src/linux/guest_app_shell.c:995, 1449-1545`,
  `src/compat/bionic_bootstrap/bionic_bootstrap.c:271-368, 588-740, 833-892, 952-976,
  1314-1364`, `bootstrap_profile.h:62-161`, `src/compat/bionic_signal.c`,
  `src/compat/bionic_sf_full.c`, `src/compat/gen_bionic_libc.py:175-215`.
- Peer reports: A03/A04/A05/A07/A13, B05/B06, C01, D02/D10/D11, decisions 007/008/
  013/019/028/038; logs `default.log`, `ctor1.log`, `tsdgot2.log`, `final-ab*.log`,
  and 81 `applied=565390` log hits (all read only).
