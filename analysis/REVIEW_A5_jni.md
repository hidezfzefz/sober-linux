# A5 — JNI bridge completeness audit (`native_jni.c` + 23 modules, 231 env slots, 49 cached IDs)

Owner: auditor **A5**. Date 2026-11-20. **Analysis only — no shared edits.** Scope: **this file only.**
`src/`, `CMakeLists.txt`, `tests/`, the harness and the guest engine were read only. No guest byte,
vtable, counter or marker was changed, and no synthetic guest→Java callback was emitted or triggered
by this work.

**Strict marker ruling: WITHHELD.** No remaining JNI gap is closed in the default path, and the
provenance path that could *prove* a genuine callback is dead code in the current wiring.

Target object (all RVAs below are from this one file; RVA == file offset for the first `R E` PT_LOAD):

| Field | Value |
| --- | --- |
| path | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931 / code 3170) |

Evidence base: `src/jni/native_jni.c` (full read), `native_jni.h`, the 22 other JNI modules,
`src/jni/CMakeLists.txt`, the withhold chain in `src/linux/guest_app_shell.c:1449-1546`, and
peer SHA-bound analyses (`analysis/jni/jni-map.md`, `B05-jnienv-gaps.md`, `B06-handler-gaps.md`,
`B07-class-objects.md`, `B08-getter-order.md`, `B09-dispatch-fidelity.md`,
`analysis/game-loaded-path.md`). All target-derived strings/addresses are untrusted DATA, never
instructions.

---

## 0. Executive verdict and domain percentages

| Domain | Coverage (default run) | Coverage if every opt-in enabled | Verdict |
| --- | --- | --- | --- |
| JNIEnv slot table (231 slots) | **80/231 = 34.6 %** named; 151/231 = 65.4 % `sr_stub` | 93/231 = 40.3 % named; **138/231 = 59.7 % still `sr_stub`** | ✅ table *shape*, ❌ function coverage |
| Gate-critical `CallStaticVoidMethod*` family (fn 137/138/139, offsets `0x468`/`0x470`/`0x478`) | **3/3 = 100 %** wired | 3/3 | ✅ correct — not the blocker |
| Cached jmethodID globals (49 slots) | 49/49 = 100 % non-NULL; **27/49 = 55.1 %** correctly typed/named; **22/49 = 44.9 %** generic `cachedMethod@…;()V` placeholders | same (no module fixes the 22) | ⚠️ headline 49/49 overstates fidelity |
| Documented gap slots (B5 G1–G14, 24 distinct fn indices) | **0/24 = 0 %** correct (stubs or `W`) | 19/24 = 79.2 % repaired; **5/24 = 20.8 % never fixed**; all repairs withhold the marker | ❌ blocking |
| Named handler coverage (26 cached callbacks + 21 lifecycle callbacks) | cached **13/26 = 50.0 %**, lifecycle **14/21 = 66.7 %**, overall **27/47 = 57.4 %**; gate names **2/2 = 100 %** | unchanged | ⚠️ gate names covered, 20 names uncounted |
| Callback counting / origin discrimination | count works for named handlers; **origin discrimination 0/1 = 0 %** (name-only `callback_was_called`) | 1 recorder exists but **never activated** (§5) and is withhold-listed | ❌ false-green risk |
| Provenance recorder operational | **0/1 = 0 %** — bootstrap deadlock (§5) | 0/1 from the harness; enabling it also force-withholds | ❌ dead code |
| Default vs opt-in modules | **2/23 = 8.7 %** default (`native_jni.c`, `jni_probe.c`); **21/23 = 91.3 %** opt-in | — | ⚠️ all repairs are opt-in |
| Genuine `gameLoadedCallback(J)V` observed | **0/1 = 0 %** (`PROGRESS.md:341-344`, default run `callback=0 render=1`) | — | ❌ marker WITHHELD |

The honest one-line summary: **the JNI bridge is structurally complete (231-slot shape, 49 non-NULL
cached IDs, 3/3 gate dispatch slots) but semantically incomplete where it matters — 65 % of the
function table is still a stub, every getter/field/array repair lives in an opt-in that force-withholds
the marker, and the only provenance discriminator is unreachable.**

---

## 1. JNIEnv function table — 231 slots, 80 named by default

`SRNativeInterface { void *reserved0..3; void *fn[231]; }` (`native_jni.c:74-77`); every entry is
initialised to `&sr_stub` (`:782`) and then selectively overwritten by `SETFN(*t, idx, fp)` (`:784-868`).
Slot arithmetic: guest `call *0xNN(%rax)` → canonical index `0xNN/8` → `fn[index-4]`.

Default named indices (80 distinct, verified by enumerating all `SETFN(*t, …)` lines):

```
0 2 9 10 11 13 15 16 17 18 19 20 21 23 24 27 28 29 30-59 109 110 113 116 119 122 125
128 131 134 137 138 139 159 161-170 211-215 222 223 224
```

Cross-checks against the guest (`B05 §1`, `jni-map.md §1.1`): `FindClass 0x30 → fn2`;
`GetStaticMethodID 0x388 → fn109`; `NewGlobalRef 0xa8 → fn17` (`:792`);
`ExceptionCheck 0x720 → fn224` (`:866`); `CallStaticVoidMethodV 0x470 → fn138` (`:843`).
The JavaVM table is 5/5 named (`:881-885`), correct.

Six of the 80 are wired-but-wrong (`W`), not real implementations:

| fn | Offset | Function | Default behaviour |
| --- | --- | --- | --- |
| 125 | `0x408` | `CallStaticIntMethod` | `:602-604` discards the dispatch result and `return 0` (B5 G11) |
| 165 | `0x548` | `GetStringUTFChars` | `:671-684` returns `""` for callback-produced `SR_K_OBJECT` strings (B5 G6) |
| 167 | `0x558` | `GetArrayLength` | `:727-728` always returns 0 (B5 G13) |
| 168 | `0x560` | `NewObjectArray` | one generic object, no elements |
| 169 | `0x568` | `GetObjectArrayElement` | always NULL |
| 170 | `0x570` | `SetObjectArrayElement` | no-op |

**Effect:** 34.6 % of the table is named; counting `W` as not-correct, only **74/231 = 32.0 %** is
semantically faithful by default. Even after every opt-in installer runs (§3), the maximum distinct
named set is **93/231 = 40.3 %**; 138 indices remain `sr_stub`. Low-frequency stubs through
`GetObjectRefType` (`0x740`, fn 228), `Get*ArrayCritical` (`0x6f0`, fn 218) etc. are completeness-only,
but the fields/arrays/scalars gaps below are real-path blockers.

The gate-critical family is *not* a gap: `sr_CallStaticVoidMethodV` at fn 138 (`0x470`) and its V/A
siblings (`:613-622`) reach `sr_dispatch_call` (`:478-522`). The notifier `0x27eaf6e` loads cached
class `0x72a2f48` and method `0x72a2fc0`, calls thunk `0x25848e0`, which issues `call *0x470(%rax)` at
`0x258495a`. `gameDidLeave` (`0x72a2fa0`) already demonstrates this route fires (B05 §2, B06 §3).

---

## 2. Cached jmethodID globals — 49 non-NULL, only 27 meaningful

`sober_jni_seed_guest_jni_state()` (`:1345-1405`) is called **unconditionally by the harness**
(`guest_app_shell.c:1078`), so the default path does seed 49 slots:

```
range 0x72a2f48..0x72a3058 step 8  = 35 slots
range 0x72ca178..0x72ca1e0 step 8  = 14 slots
total                                = 49 slots   (100 % non-NULL)
flags 0x72a3000 and 0x7d8fb60       = 1
```

Two details the `49/49` headline hides:

1. **26 named entries** in `sr_cached_methods[]` (`:1296-1343`) carry real method names/signatures —
   e.g. `gameLoadedCallback(J)V` at `0x72a2fc0`, `onAppShellReloadNeeded()V` at `0x72a3020`, cached
   `jclass` at `0x72a2f48`, `getRobloxLocale` at `0x72ca188`, `getGameLocale` at `0x72ca190`.
   Add the correctly-typed jclass at `0x72a2f48` (`:1366-1371`) → **27/49 = 55.1 % correctly seeded**.
2. The range fill (`:1376-1390`) names every remaining NULL slot `cachedMethod@<rva>` with sig `()V`
   → **22/49 = 44.9 % are placeholder method objects**. Dispatch looks handlers up by method *name*
   (`:505-513`), and no handler is registered for `cachedMethod@…`, so any call through them mutates
   `env_calls`/`last_callback` but **never increments `callback_calls`**.

Concrete casualty: the `NativeUserJavaInterface` getters listed in `jni-map.md:118` (`getUsername`,
`getUserId`, `getMembershipType`, `getIsUnder13`, … at `0x72ca1a8..0x72ca1e0`) are **not** in
`sr_cached_methods[]` and no module seeds them (repo-wide grep finds only the two locale RVAs).
Thus even if the static-object/scalar slots (G4–G11) are repaired, those getters dispatch through
placeholders and return 0/NULL. The gate-critical pair is safe (2/2 correct), but the cached-ID domain
is only 55.1 % faithful.

---

## 3. Fields, arrays, scalars — 24 documented gap slots, 0 % correct by default

The 14 gaps of `B05 §3` map to **24 distinct fn indices**. Default vs opt-in repair:

| Domain | fn indices (offsets) | Guest evidence (RVA) | Default | Opt-in repair | Withhold knob |
| --- | --- | --- | --- | --- | --- |
| Fields | 90 `0x2f0`, 96 `0x320`, 98 `0x330` | `0x24ac975` (`dpiScale "F"`), `0x23846d4` (`viewportWidthMm "I"`) | **0/3 stubs** | `platform_fields.c:233-246` → 3/3 | `SOBER_PLATFORM_PARAMS_FIELDS` (:1503) |
| Object getters V/A + string gate | 111 `0x398`, 112 `0x3a0`, 165 `0x548` | thunk `0x23150f8` → `call *0x398` at `0x231516e`; 102+31 call sites | **0/3** (2 stubs, 1 `W`) | `static_object_calls.c:183-210` → 3/3 | `SOBER_STATIC_OBJECT_CALLS` (:1531) |
| Scalars | 114/115 `0x3b0`/`0x3b8`, 125 `0x408`, 126/127 `0x410`/`0x418`, 129/130 `0x428`/`0x430`, 135/136 `0x458`/`0x460` | `InitWithParams` thunk `0x24ace34: call *0x3b0`; `membershipType()I`, `getProcessTimestamp()J` | **0/9** (8 stubs, 1 `W`) | `scalar_returns.c:237-261` → 9/9 | `SOBER_SCALAR_RETURNS` (:1532) |
| Arrays | 167 `0x558`, 168 `0x560`, 169 `0x568`, 170 `0x570`, 172 `0x580` | wrapper `0x2a20fd0: call *0x580 at 0x2a20fe6` + `test rax,rax; je` | **0/5** (4 `W`, 1 stub) | `jni_arrays.c:163-176` patches 167-170 only → **4/5**; **172 `NewByteArray` is never fixed** | `SOBER_FAITHFUL_JNI_ARRAYS` (:1536) |
| Bulk/ref-type | 217 `0x6e8`, 218 `0x6f0`, 220 `0x700`, 228 `0x740` | 5/7/2/2 sites, outside app-shell path | 0/4 stubs | **none** | — |

Totals: **default 0/24 = 0 % correct**; with all opt-ins **19/24 = 79.2 %** repaired;
**5/24 = 20.8 % remain stubs even then** (172, 217, 218, 220, 228). And all repairs are gated by
env knobs that appear in the withhold chain, so a *genuine* run cannot use any of them.

`jni_strings.c:357-368` additionally replaces slots 161/162/165/166 with canonical UTF-16/UTF-8
semantics under `SOBER_CANONICAL_JSTRINGS` (:1457, withheld); `javavm_semantics.c:382-419` installs a
copy of the 231-slot table only for a faithful JavaVM under `SOBER_FAITHFUL_JAVAVM` (:1463, withheld)
and writes guest global `0x7cdec78`.

---

## 4. Callback counting, handler order and shadowing

Mechanics (`native_jni.c:478-522`, `:1020-1036`, `:1440-1455`):

- `sr_dispatch_call` logs *every* dispatch into `last_callback` (`:489-490`) but increments
  `callback_calls` **only when a named handler matches** (`:505-510`). Registered native methods run
  first (`:496-503`) and are **not counted**.
- `sober_jni_set_callback_handler` is a **name-keyed replace — last writer wins** (`:1024-1028`);
  B08 §0 confirms two headers claiming "first registered wins" are factually wrong. Effective handlers
  are a pure function of call order in `guest_app_shell.c`:
  default list (34 names, `:1048`) → `shell_callback` overrides for `onAppShellReloadNeeded` and
  `gameLoadedCallback` (`:1049-1050`) → opt-in JSON/singleton/StartApp/StartGame rewrites
  (`:1009-1273`, B06 §2 rows 2-15) → ALT thunk at `:1258+`.
- Concrete shadowing: `SOBER_STARTAPP_PARAMS=1` registers all ten `sp_get` getters, then
  `real_path_get` replaces all ten (`:1218-1227`) or `start_app_get` replaces
  `surface`/`platformParams`/`vrContext` (`:1231-1233`); the faithful module's counters under-report
  by 3 or 10 (B06 §2.1). `sober_game_params_make`'s 24 getters are never wired by the harness;
  the inline `game_params_get` at `:904-917` returns a **jstring for `placeId()` whose guest sig is
  `()J`** (`game_params.h:78`, B06 §2.2, B08 §5).
- Uncounted by default: 13 of 26 cached callbacks (B06 §3 — `gameDidLeave`, 5 purchase variants,
  `screenOrientationChanged`, `openNativeOverlay`, `saveImageToAlbum`, `onVrSessionStateUpdate`,
  `exitGameWithError`, `listenToMotionEvents`, diagnostic `onAppBridgeNotification`) and 7 of 21
  lifecycle names (`onSaveInstanceStateNative`, `onTextInputEventNative`, `onWindowInsetsChangedNative`,
  `setInputConnectionNative`, `onContentRectChangedNative`, `onSoftwareKeyboardVisibilityChangedNative`,
  `onEditorActionNative`).
- `callback_was_called()` (`:1446-1455`) is **name-only**; the harness ALT path
  (`SOBER_ALT_CALLBACK_PATH`) produces byte-identical counter state (B06 §1, B09 §0), so the gate at
  `guest_app_shell.c:1443-1444` cannot discriminate origin. `onAppShellReloadNeeded` has no native
  reader (`jni-map.md:101`, worker #467 / `analysis/jni/appshell-reload-trigger.md`), leaving
  `gameLoadedCallback` as the only admissible signal.
- Fragilities (B09 §0): `_Thread_local bool sr_in_dispatch` (`:180`) silently drops nested dispatch
  and is never cleared if a handler unwinds; `last_callback` records the last *attempt*, not the last
  *counted* callback; counters are plain non-atomics despite worker-thread callbacks.

Gate-critical handler coverage is nevertheless 2/2: both names are installed unconditionally. The
problem is not counting a genuine arrival — it is reaching it and proving it.

---

## 5. Provenance recorder — 0/1 operational (bootstrap deadlock)

`callback_provenance.c` implements the discriminator the gate needs: return-site pair
`0x2584960` (thunk `call *0x470`) / `0x27eafb3` (notifier return), cached handle equality at
`0x72a2f48`/`0x72a2fc0`, and `GENUINE` vs `HARNESS-THUNK` classification (`:31-37`, `:184-245`).
It is **never activated in the current wiring**:

1. `g_active` starts 0 (`:45`) and is written **only** by `sober_callback_provenance_install()` (`:163`).
2. The only harness-visible call site is `native_jni.c:876`, guarded by
   `if (sober_callback_provenance_is_active()) sober_callback_provenance_install(NULL);` —
   `is_active()` reads `g_active` (`:247-250`), which is 0 until `install()` runs. **The guard is
   always false — a bootstrap deadlock.** No caller in `src/linux/` references the module at all
   (repo-wide grep: only `native_jni.c:876`, the header and the lazy fallback).
3. The lazy fallback inside `on_dispatch` is unreachable: `:187` returns immediately when `g_active`
   is false, before `:188-190` could call `install()`.

Consequently `SOBER_CALLBACK_PROVENANCE=1` alone does nothing — `g_active` stays 0 and
`sober_callback_provenance_on_dispatch()` returns at `:187` for every dispatch. Even if it were
activated, `SOBER_CALLBACK_PROVENANCE` is itself in the withhold chain
(`guest_app_shell.c:1530`), so the gate would force `shell_cb = 0` anyway. **Usable provenance
coverage for a genuine marker = 0 %.**

---

## 6. Default vs opt-in — all 23 modules

`src/jni/CMakeLists.txt:12-35` builds 22 files into `sober_jni` (linked only into
`guest-init-host`, root `CMakeLists.txt:179`); `jni_probe.c:41` is a standalone self-test. Total 23.

| Module | Gate | Effect on JNI callback path |
| --- | --- | --- |
| `native_jni.c` | **default ON** | 231-slot table, 49-ID seed, dispatch, 34 handlers, counters, 2/2 gate names |
| `jni_probe.c` | default (standalone) | headless probe/self-test only |
| `platform_fields.c` | `SOBER_PLATFORM_PARAMS_FIELDS` | slots 90/96/98 (fields, G1-G3) |
| `static_object_calls.c` | `SOBER_STATIC_OBJECT_CALLS` | slots 111/112/165 (G4-G6) |
| `scalar_returns.c` | `SOBER_SCALAR_RETURNS` | slots 114/115/125/126/127/129/130/135/136 (G7-G11) |
| `jni_arrays.c` | `SOBER_FAITHFUL_JNI_ARRAYS` | slots 167/168/169/170 (G13 partial; G12 unfixed) |
| `jni_strings.c` | `SOBER_CANONICAL_JSTRINGS` | slots 161/162/165/166 (string fidelity) |
| `javavm_semantics.c` | `SOBER_FAITHFUL_JAVAVM` | faithful JavaVM, guest global `0x7cdec78` |
| `callback_provenance.c` | `SOBER_CALLBACK_PROVENANCE` | origin recorder — unreachable (§5) |
| `notifier_probe.c` | `SOBER_NOTIFIER_PROBE` | fail-closed notifier breakpoint (patches guest bytes) |
| `game_params.c`, `startapp_params.c`, `startapp_v2.c`, `startgame_v2.c` | `SOBER_GAME_PARAMS`, `SOBER_STARTAPP_PARAMS`, `SOBER_STARTAPP_V2`, `SOBER_STARTGAME_V2` | param getter objects; shadowed or unwired (§4) |
| `jni_json_bridge.c` | `SOBER_JSON_FLAG_BRIDGE`, `SOBER_JSON_APP_SINGLETON` | flag/app-singleton handlers |
| `jni_native_objects.c` | `SOBER_NATIVE_OBJECTS` | object views only, no handlers |
| `app_singleton_bridge.c`, `singleton_fix.c`, `singleton_services.c` | `SOBER_APP_SINGLETON_BRIDGE`, `SOBER_SINGLETON_FIX`, `SOBER_SINGLETON_SERVICES` | singleton state |
| `app488_fix.c`, `app488_inner.c` | `SOBER_APP488_FIX`, `SOBER_APP488_INNER` | handle 488 patching |
| `alloc_contract_fix.c` | `SOBER_ALLOC_CONTRACT_FIX` | allocator contract |
| `global_string_seed.c` | `SOBER_SEED_GLOBAL_STRING` | global string at `0x72a0520` |

The withhold chain (`guest_app_shell.c:1449-1546`, **87 `getenv()` checks**) covers every
guest-mutating or value-faking JNI knob, including all five slot-repair knobs, the JavaVM seed, the
provenance recorder, the notifier probe and `SOBER_GAME_LOADED_PATH`. README `:54-55` records "23 JNI
modules … 22 opt-in modules in the decision-013 withhold chain. All opt-ins are OFF by default."

---

## 7. Remaining JNI gaps that block a genuine callback — ranked chain

1. **JavaVM global never seeded on the default path.** `JNI_OnLoad` is skipped; global `0x7cdec78`
   stays NULL, and the default run faults in the guest at **`0x2eb4b65`** (`PROGRESS.md:342`,
   `native_jni.c:1400-1419`). Repair = `SOBER_SEED_JNI_VM` or `SOBER_FAITHFUL_JAVAVM`, both withheld.
2. **Getter/field/scalar/array slots are stubs or `W` by default** (§3): 0/24 gap slots correct.
   `getDeviceStaticParams` returns NULL through fn 111/112 (`0x398`/`0x3a0`), which drives the
   `"DeviceStaticParams is null"` → FindClass retry loop at `0x6c8f340` (`recursion-note.md:123-132`);
   `dpiScale`/`viewportWidthMm` read 0 through fn 98/96 (`0x330`/`0x320`).
3. **All slot repairs are withhold-listed** — enabling them makes `seeded_state` true and forces
   `shell_cb = 0` (`guest_app_shell.c:1547-1551`). This is the central catch-22: a genuine run cannot
   use the completed bridge.
4. **The genuine game-load route is itself opt-in and withhold-listed.** `SOBER_GAME_LOADED_PATH`
   (`:1476`) is the only harness route to `StartGameWithParam 0x2ebd51a` /
   `SendAppEventOnGameLoaded 0x2ebca9e`; when run it faults at guest **`0x31a1360`** on a TSD-sentinel
   handle (`PROGRESS.md:303-304`). The faithful 24-getter `StartGameParams` module is never wired; the
   inline stub returns a jstring for `placeId()J`.
5. **Provenance is dead code** (§5) and withhold-listed; name-only counting cannot exclude the ALT
   thunk `0x25848e0`. No origin-verified marker is possible.
6. **Cached-ID placeholders** (§2): 22/49 generic `()V` objects, including all `NativeUserJavaInterface`
   getters at `0x72ca1a8..0x72ca1e0`; their callbacks are uncounted and value-less.
7. **Residual unfixed slots** (§3): `NewByteArray` fn 172 (`0x580`, used by `onSaveInstanceStateNative`
   wrapper `0x2a20fd0`) and bulk/ref-type fn 217/218/220/228 have no implementation even opt-in.
8. **Not a blocker (do not misfile):** the gate dispatch slot itself is correct — fn 138 (`0x470`)
   is wired, and `gameDidLeave` (`0x72a2fa0`) proves the route dispatches. Closing 1-7 is necessary
   but still not sufficient without a guest-side notifier hit at `0x27eaf6e` / return `0x27eafb3`
   and phase-3 render `0x649d422`.

---

## 8. Ruling

**WITHHELD.** The bridge is a faithful *shape* (231 slots, 5 JavaVM slots, 49 cached handles, 34
handlers, 2/2 gate names) and the dispatch path for `gameLoadedCallback(J)V` is present, but the
semantic JNI domain required to reach and prove it is incomplete in the default configuration:
34.6 % slot coverage, 0/24 gap slots correct, 55.1 % cached-ID fidelity, 0 % provenance activation.
Enabling the repairs is disallowed by the decision-013 withhold chain, and the only genuine-path
opt-in currently faults at `0x31a1360`. Default observation remains `callback=0 render=1`, engine SHA
and BuildID unchanged. No `app shell rendered` marker is justified by this audit.

**Acceptance evidence still required (unchanged, strict):** notifier `0x27eaf6e` entered with
`RSI = placeId > 0` and return `0x27eafb3` observed; cached method `0x72a2fc0` non-NULL; bridge
`callbacks` incremented by ≥1 with an origin-verified `gameLoadedCallback`; `guest call 0x649d422`
render evidence; no ALT/HONESTY line; `seeded_state == 0`; engine SHA/BuildID unchanged.

## 9. Limits / honesty

- Static audit of one worktree against one SHA-bound engine; no runtime replay, no guest bytes
  executed by this auditor. Line numbers are from the current `src/` tree and may differ from
  earlier snapshots (B06/B08 note their own line migrations).
- `W` classifications follow `B05 §3`; raw `call *0xNN(` counts are upper bounds, and the gap claims
  rest on the individually cited guest sites, not those counts.
- The provenance deadlock (§5) is inferred from the absence of any `sober_callback_provenance_install`
  caller in `src/linux/` plus the two guard/early-return sites; it is a code-path finding, not a
  runtime trace. It should be confirmed by setting `SOBER_CALLBACK_PROVENANCE=1` and checking that
  `[callback-provenance] enabled …` never appears.
- This report changes no marker, gate, test, source, or engine byte. Marker: **WITHHELD**.
