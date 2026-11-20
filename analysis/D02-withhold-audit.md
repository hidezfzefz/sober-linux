# D02 — Withhold-chain coverage audit across all of `src/`

Worker D2 / Team D · strict DECISION 013 · gate strict
Snapshot: **2026-11-20T21:44:21Z** · Scope: `analysis/D02-withhold-audit.md` only.
`src/` and `CMakeLists.txt` were **read only**; no edits were made.

## 0. Headline result

The seeded-state withhold chain in `src/linux/guest_app_shell.c` (the
`int seeded_state = getenv(...) || ...;` block) covers **71** knob names at the
snapshot instant. Across `src/` there are **168** distinct `SOBER_*` string
literals used as environment names (plus four non-`SOBER_*` host variables and
one dynamic family). **97** literals are outside the chain.

Of those 97:

| Class | Found | Still outside chain at snapshot | Withhold needed? |
|---|---|---|---|
| **Tier 1** — direct guest memory/code mutation, synthetic guest-object/state construction, guest-code-path injection, or a module enable knob | 23 | **0** (lead added all 23 during this audit) | Yes — now closed |
| **Tier 2** — value/strategy modifiers that steer a withheld module | 49 | **47** (the 2 ASB modifiers were added) | **YES — remaining gaps** |
| **Tier 3** — Android-compat values fed to the guest + guest-binary selection | 10 + 1 dynamic family | **10 + family** | Needs lead ruling; recommended withhold |
| Benign — diagnostics, logging, timings, output-marker paths, legitimate phase gates, host-only config | 40 | 40 | No |

**Remaining gap list at the final snapshot = 47 named Tier-2 value modifiers
(§4 Tier 2) + 10 Tier-3 names + the `SOBER_PROP_<name>` family = 57 named
withhold-worthy knobs outside the chain.** All 23 Tier-1 gaps found early in the
audit were closed by the lead while I was writing; §4 retains their full
enumeration as the audit record.

> **Chain volatility note.** The chain changed *during* this audit: 40 → 46 →
> 47 → 48 → 71 names. Between 21:43Z and 21:44Z the lead added all 23 Tier-1
> knobs plus the 2 ASB modifiers. The lead added `SOBER_STARTGAME_V2`, `SOBER_GAME_PARAMS`,
> `SOBER_CANONICAL_JSTRINGS`, `SOBER_STARTAPP_V2`, `SOBER_SCHEDULE_RUNNABLE`,
> `SOBER_SCHED_RVA`, `SOBER_TLS_BYTES`, `SOBER_ALT_CALLBACK_PATH`,
> `SOBER_START_APP`, `SOBER_FAITHFUL_APP_DELEGATE` and
> `SOBER_CLIENT_SETTINGS_INIT` while I was reading. Simultaneously other workers
> added new modules (`app488_fix.c`, `jni_strings.c`, `startapp_v2.c`,
> `startgame_v2.c`, `platform_fields.c`), which introduced further knobs. Every
> name listed below must be **subtracted** from the gap list if it is already in
> the chain when this report is consumed. §8 gives a one-shot recompute script.
> At the snapshot instant the seven knobs named in my task stand as follows (§3).

## 1. Method

1. `grep -rn getenv src --include='*.c' --include='*.h'` — all call sites.
2. Resolved indirect names: `ASB_ENV/STRATEGY/VERBOSE/RELEASEOLD`
   (`app_singleton_bridge.c:23-26`), `SOBER_FAITHFUL_JAVAVM_ENV`
   (`javavm_semantics.h:66`), `SOBER_APP488_FIX_ENV` (`app488_fix.h:64`),
   `SOBER_STARTAPP_V2_ENV` / `_ENV_LEGACY` (`startapp_v2.h:80-81`).
3. Resolved helper indirection: `env_int(name, ...)` and `env_or(name, ...)`
   wrappers in `android_compat.c`, `game_params.c`, `startapp_params.c`,
   `startgame_v2.c`, `startapp_v2.c` (a plain `getenv("...")` grep **misses**
   these — e.g. `SOBER_GAME_GAME_ID`, `SOBER_STARTAPP_PLACE`,
   `SOBER_SGV2_GAME_ID` are only passed to `env_or`).
4. Reconstructed the chain by regex from the declaration to its terminating
   semicolon, and subtracted it from the full literal set.
5. Classified each remaining knob by reading the guarded code path: *writes to
   guest memory / guest code bytes / guest globals*, *synthetic guest-shaped
   object construction*, *guest initializer or constructor invocation*, or
   *value that the guest consumes to build its own state* ⇒ withhold-worthy.
   Logging, page-watch, timing, host-path selection and output-marker paths ⇒
   benign.

D09 (`tests/D09-withhold-audit.sh`) only inspects **one file**
(`src/linux/guest_app_shell.c`); its "GAP LIST: (none)" result is correct for
that narrow scope and says nothing about the other 13 modules audited here.

## 2. Chain snapshot (71 names, verbatim order)

```
SOBER_ALT_CALLBACK_PATH, SOBER_START_APP, SOBER_SCHEDULE_RUNNABLE, SOBER_SCHED_RVA,
SOBER_TLS_BYTES, SOBER_STARTGAME_V2, SOBER_CLIENT_SETTINGS_INIT, SOBER_GAME_PARAMS,
SOBER_CANONICAL_JSTRINGS, SOBER_SEED_USER_CONTROLLER, SOBER_SEED_APP_DELEGATE,
SOBER_FAITHFUL_APP_DELEGATE, SOBER_SEED_APP_488, SOBER_SEED_JNI_VM,
SOBER_FAITHFUL_JAVAVM, SOBER_NATIVE_OBJECTS, SOBER_JSON_FLAG_BRIDGE,
SOBER_JSON_APP_SINGLETON, SOBER_SEED_GLOBAL_STRING, SOBER_GSEED_FORCE,
SOBER_GSEED_DIAG, SOBER_REPLAY_FFLAG_CTOR, SOBER_NATIVE_FLAGS_INIT,
SOBER_BIONIC_SF_FULL, SOBER_SCHED_R70_KEEP, SOBER_SCHED_NEST_ONE,
SOBER_LOG_APP_BRIDGE_NOTIFICATION, SOBER_GAME_LOADED_PATH, SOBER_LUA_APP_START,
SOBER_STARTAPP_PARAMS, SOBER_STARTAPP_V2, SOBER_APP_SINGLETON_BRIDGE,
SOBER_REAL_APPSTATE_PATH, SOBER_SEED_JNI_SINGLETON, SOBER_REAL_APPSTATE_FLAG,
SOBER_REAL_APPSTATE_POSTSETTINGS, SOBER_TLS_SEED, SOBER_STUB_TSD_SEED,
SOBER_HELPER_REPAIR_V2, SOBER_NO_SCHED_NEUTRAL, SOBER_VALIDATOR_PATCH,
SOBER_FINAL_PATCH, SOBER_FIELD_NORMALISER, SOBER_OBJECT_FIX, SOBER_ALLOC_SITE_FIX,
SOBER_LOG_HANDLE, SOBER_APP_SHELL_FIX, SOBER_CALL_SITE_FIX, SOBER_BIONIC_TSD_GOT,
SOBER_APP488_FIX, SOBER_PLATFORM_PARAMS_FIELDS, SOBER_HANDLE_SANITISER,
SOBER_NO_HANDLE_SANITISER, SOBER_CALLER_PTR_FORCE, SOBER_HELPER_REPAIR,
SOBER_HELPER_LOG, SOBER_FINNER_LOG, SOBER_FINNER_BP, SOBER_CURL_HOLDER_FIX,
SOBER_CALLER_PTR_FIX, SOBER_BOOTSTRAP_INTERPOSE, SOBER_BOOTSTRAP_NO_BRIDGES,
SOBER_BOOTSTRAP_NO_SEEDS, SOBER_NO_DIRECT_SEED_EMPTY, SOBER_SEED_MASK,
SOBER_CTOR0, SOBER_CTOR1, SOBER_CTOR_RANGES, SOBER_GUEST_ROUTE_A,
SOBER_APP_SINGLETON_STRATEGY, SOBER_APP_SINGLETON_RELEASE_OLD
```

## 3. The seven knobs named in my task

| Knob | Source | In chain at snapshot? | Verdict |
|---|---|---|---|
| `SOBER_BIONIC_TSD_GOT` | `src/linux/android_relocs.c:296` | YES (added mid-audit) | Initially a Tier-1 gap: writes two guest GOT slots (`base+0x72298b0`, `base+0x72295c8`) with `dlvsym` results. Now withheld. |
| `SOBER_SEED_APP_488` | `guest_app_shell.c:816,1339` | YES | Covered. |
| `SOBER_GAME_PARAMS` | `src/jni/game_params.c:129` | YES (added during audit) | Enable covered; **its 12 value modifiers are not** (§4 Tier 2). |
| `SOBER_LUA_APP_START` | `guest_app_shell.c:1246` | YES | Covered. |
| `SOBER_GAME_LOADED_PATH` | `guest_app_shell.c:1267` | YES | Covered. |
| `SOBER_STARTAPP_PARAMS` | `src/jni/startapp_params.c:113` | YES | Enable covered (alias `SOBER_STARTAPP_V2` was added mid-audit); **its 10 value modifiers and the 10 `SOBER_STARTAPP_V2_*` modifiers are not** (§4 Tier 2). |
| `SOBER_APP_SINGLETON_BRIDGE` | `app_singleton_bridge.c:23,43` | YES | Enable covered; **`_STRATEGY` and `_RELEASE_OLD` modifiers are not** (§4 Tier 2). |

## 4. Complete gap list

### Tier 1 — 23 confirmed guest-mutating / state-seeding knobs

**Status: all 23 were added to the withhold chain during this audit (last two
waves at 21:43Z / 21:44Z). None remain outside the chain at the final snapshot.**
Retained here as the complete evidence record.

| # | Knob | Evidence | Why it must withhold |
|---|---|---|---|
| 1 | `SOBER_BIONIC_TSD_GOT` | `src/linux/android_relocs.c:296-304` | Direct writes to guest GOT slots 0x72298b0 / 0x72295c8. |
| 2 | `SOBER_APP488_FIX` | `src/jni/app488_fix.c:76,87-149` | Guest allocator + writes a guest-shaped wrapper object into `app+0x488`. |
| 3 | `SOBER_HANDLE_SANITISER` | `bionic_bootstrap.c:113-136` | mprotect + overwrites 12 guest code bytes at 0x23331ae; stub writes 0 to `[rbx+0x68]`. |
| 4 | `SOBER_NO_HANDLE_SANITISER` | `bionic_bootstrap.c:113` | Modifier of #3 (suppresses the patch); changes startup state profile. |
| 5 | `SOBER_CALLER_PTR_FORCE` | `bionic_bootstrap.c:904-909` | Forces substitution of a guest-held pointer with a JNI handle. |
| 6 | `SOBER_HELPER_REPAIR` | `bionic_bootstrap.c:984-997` | Writes a substitute pointer into a guest slot (`*(void**)slot = sub`). v1 was initially missing; both v1 and `_V2` are now withheld. |
| 7 | `SOBER_HELPER_LOG` | `bionic_bootstrap.c:1109-1140` | mprotect + patch 5 guest code bytes at 0x23331ae with a near-jmp. |
| 8 | `SOBER_FINNER_LOG` | `bionic_bootstrap.c:1146-1187` | mprotect + patch 5 guest code bytes at 0x23330e7. |
| 9 | `SOBER_FINNER_BP` | `bionic_bootstrap.c:1192-1199` | Writes 0xcc (int3) into guest code at 0x23330e7. |
| 10 | `SOBER_CURL_HOLDER_FIX` | `bionic_bootstrap.c:1204-1234` | Patches guest code at 0x245771e and 0x234ddc0. |
| 11 | `SOBER_CALLER_PTR_FIX` | `bionic_bootstrap.c:1239-1311` | Patches guest code at 0x6a26e78 with a near-jmp to a host stub. |
| 12 | `SOBER_BOOTSTRAP_INTERPOSE` | `bionic_bootstrap.c:1422-1424` | Enables auto-apply of all bootstrap seeds/bridges from a stub constructor. |
| 13 | `SOBER_BOOTSTRAP_NO_BRIDGES` | `bionic_bootstrap.c:1329-1332` | Skips bridge install + calloc-slot patch; changes guest startup state. |
| 14 | `SOBER_BOOTSTRAP_NO_SEEDS` | `bionic_bootstrap.c:1348-1350` | Skips all four guest-global seeds; changes guest startup state. |
| 15 | `SOBER_NO_DIRECT_SEED_EMPTY` | `bionic_bootstrap.c:663-688` | Switches the empty-string global from direct field writes to calling guest initializer 0x225dd2a. |
| 16 | `SOBER_SEED_MASK` | `bionic_bootstrap.c:723-729` | Selects which of the four guest globals are seeded (default 0xf). |
| 17 | `SOBER_CTOR0` | `guest_dlopen_host.c:443-447` | Injects guest init_array[0] (FFlag ctor 0x2e62390) out of sequence. Bypasses `SOBER_REPLAY_FFLAG_CTOR`, which *is* in the chain. |
| 18 | `SOBER_CTOR1` | `guest_dlopen_host.c:451-460` | Injects guest init_array[1] (protobuf once-guard 0x1dba8f3). |
| 19 | `SOBER_CTOR_RANGES` | `guest_dlopen_host.c:428-437,638-640` | `native`/`all` change which of the 3618 guest constructors run (`all` includes ctor[0]). |
| 20 | `SOBER_GUEST_ROUTE_A` | `guest_render.c:112-126` | Calls guest EGL `create_context` 0x649c21e + swap on a second context — guest state construction. |
| 21 | `SOBER_APP_SINGLETON_STRATEGY` | `app_singleton_bridge.c:24,240-243` | `slice` vs `replay`: selects how the guest app singleton is constructed in guest memory. |
| 22 | `SOBER_APP_SINGLETON_RELEASE_OLD` | `app_singleton_bridge.c:26,220-226` | Releases the previous singleton through its guest vtable `+8`. |
| 23 | `SOBER_PLATFORM_PARAMS_FIELDS` | `src/jni/platform_fields.c:84,128+` | New module (added mid-audit); installs synthetic PlatformParams DPI/viewport field bindings consumed by the guest via JNI getters. Enable knob is not withheld. |

### Tier 2 — 49 modifiers of modules whose enable knob is in the chain

**Status: 47 remain outside the chain at the final snapshot.** The 2 ASB
modifiers (`SOBER_APP_SINGLETON_STRATEGY`, `SOBER_APP_SINGLETON_RELEASE_OLD`)
were added during the audit. These do not allocate a new module, but they set
the fake values / pick the behaviour that the withheld module seeds into the
guest. If the enable knob withholds but a modifier is set *without* it, no state
is manufactured; the false-green risk is when both are set and the chain sees
only the enable name — or when a modifier is used with the module's **alias**
(see `SOBER_STARTAPP_V2` in §5). Under strict "every opt-in withholds", all
belong in the chain.

**The 47 remaining Tier-2 gaps are exactly the four value-override families
below, excluding the 2 ASB names:**

- `SOBER_GAME_*` (12) — `SOBER_GAME_GAME_ID`, `SOBER_GAME_ACCESS_CODE`,
  `SOBER_GAME_USERNAME`, `SOBER_GAME_PLACE_ID`, `SOBER_GAME_USER_ID`,
  `SOBER_GAME_UNDER13`, `SOBER_GAME_CONVERSATION_ID`,
  `SOBER_GAME_JOIN_REQUEST_TYPE`, `SOBER_GAME_REFERRED_BY`, `SOBER_GAME_DPI`,
  `SOBER_GAME_WIDTH_MM`, `SOBER_GAME_HEIGHT_MM`.
- `SOBER_STARTAPP_*` (10) — `SOBER_STARTAPP_PLACE`, `_SCRIPT`, `_USERNAME`,
  `_THEME`, `_USER_ID`, `_UNDER13`, `_MEMBERSHIP`, `_DPI`, `_WIDTH_MM`,
  `_HEIGHT_MM`.
- `SOBER_SGV2_*` (12) — `SOBER_SGV2_GAME_ID`, `_ACCESS_CODE`, `_USERNAME`,
  `_PLACE_ID`, `_USER_ID`, `_UNDER13`, `_CONVERSATION_ID`,
  `_JOIN_REQUEST_TYPE`, `_REFERRED_BY`, `_DPI`, `_WIDTH_MM`, `_HEIGHT_MM`.
- `SOBER_STARTAPP_V2_*` (10) — `_PLACE`, `_SCRIPT`, `_USERNAME`, `_THEME`,
  `_USER_ID`, `_UNDER13`, `_MEMBERSHIP`, `_DPI`, `_WIDTH_MM`, `_HEIGHT_MM`.
- `SOBER_PLATFORM_PARAMS_*` (3) — `_DPI`, `_WIDTH_MM`, `_HEIGHT_MM`.

**`SOBER_GAME_PARAMS` value overrides (12)** — `src/jni/game_params.c:185-209`
(`env_or` at 181 / direct `getenv`):
`SOBER_GAME_GAME_ID`, `SOBER_GAME_ACCESS_CODE`, `SOBER_GAME_USERNAME`,
`SOBER_GAME_PLACE_ID`, `SOBER_GAME_USER_ID`, `SOBER_GAME_UNDER13`,
`SOBER_GAME_CONVERSATION_ID`, `SOBER_GAME_JOIN_REQUEST_TYPE`,
`SOBER_GAME_REFERRED_BY`, `SOBER_GAME_DPI`, `SOBER_GAME_WIDTH_MM`,
`SOBER_GAME_HEIGHT_MM`.
These populate the 24-field StartGameParams mirror consumed by the guest's
getters (`gp_apply_env` → `gp_ensure_handles` → `sober_game_params_build`).

**`SOBER_STARTAPP_PARAMS` value overrides (10)** — `src/jni/startapp_params.c:155-174`:
`SOBER_STARTAPP_PLACE`, `SOBER_STARTAPP_SCRIPT`, `SOBER_STARTAPP_USERNAME`,
`SOBER_STARTAPP_THEME`, `SOBER_STARTAPP_USER_ID`, `SOBER_STARTAPP_UNDER13`,
`SOBER_STARTAPP_MEMBERSHIP`, `SOBER_STARTAPP_DPI`, `SOBER_STARTAPP_WIDTH_MM`,
`SOBER_STARTAPP_HEIGHT_MM`.

**`SOBER_STARTGAME_V2` value overrides (12)** — `src/jni/startgame_v2.c:191-215`:
`SOBER_SGV2_GAME_ID`, `SOBER_SGV2_ACCESS_CODE`, `SOBER_SGV2_USERNAME`,
`SOBER_SGV2_PLACE_ID`, `SOBER_SGV2_USER_ID`, `SOBER_SGV2_UNDER13`,
`SOBER_SGV2_CONVERSATION_ID`, `SOBER_SGV2_JOIN_REQUEST_TYPE`,
`SOBER_SGV2_REFERRED_BY`, `SOBER_SGV2_DPI`, `SOBER_SGV2_WIDTH_MM`,
`SOBER_SGV2_HEIGHT_MM`.

**`SOBER_STARTAPP_V2` value overrides (10)** — `src/jni/startapp_v2.c:170-187`:
`SOBER_STARTAPP_V2_PLACE`, `SOBER_STARTAPP_V2_SCRIPT`,
`SOBER_STARTAPP_V2_USERNAME`, `SOBER_STARTAPP_V2_THEME`,
`SOBER_STARTAPP_V2_USER_ID`, `SOBER_STARTAPP_V2_UNDER13`,
`SOBER_STARTAPP_V2_MEMBERSHIP`, `SOBER_STARTAPP_V2_DPI`,
`SOBER_STARTAPP_V2_WIDTH_MM`, `SOBER_STARTAPP_V2_HEIGHT_MM`.

**`SOBER_PLATFORM_PARAMS_FIELDS` value overrides (3)** — `src/jni/platform_fields.c:110-115`:
`SOBER_PLATFORM_PARAMS_DPI`, `SOBER_PLATFORM_PARAMS_WIDTH_MM`,
`SOBER_PLATFORM_PARAMS_HEIGHT_MM`. Feed synthetic DPI/viewport mm into the
guest-visible PlatformParams field bindings.

**`SOBER_APP_SINGLETON_BRIDGE` modifiers (2)** — `app_singleton_bridge.c:24-26,220,240`:
`SOBER_APP_SINGLETON_STRATEGY`, `SOBER_APP_SINGLETON_RELEASE_OLD` — both already
listed in Tier 1.

→ Tier-2 entries: 12 + 10 + 12 + 10 + 3 + 2 = 49; unique Tiers 1+2 names found
by this audit = 23 + 49 − 2 = 70. Of those, the lead has withheld 23 (all Tier 1)
plus the 2 ASB modifiers, leaving **47 Tier-2 value modifiers outside the chain
at the final snapshot**.

### Tier 3 — guest-visible configuration seeding; needs lead ruling (10 + family)

No direct guest memory write, but each injects a value the guest reads to build
its own configuration/state. If "state-seeding" is read broadly (as the
module-specific value overrides above are), these are gaps too.

| Knob | Source | Effect |
|---|---|---|
| `SOBER_ASSET_ROOT` | `android_compat.c:168` | Overrides `AAssetManager` root — changes asset bytes the guest reads. |
| `SOBER_SCREEN_WIDTH_DP` | `android_compat.c:307` | `AConfiguration.screenWidthDp`. |
| `SOBER_SCREEN_HEIGHT_DP` | `android_compat.c:308` | `AConfiguration.screenHeightDp`. |
| `SOBER_SCREEN_SIZE` | `android_compat.c:309` | `AConfiguration.screenSize`. |
| `SOBER_NAV_HIDDEN` | `android_compat.c:310` | `AConfiguration.navHidden`. |
| `SOBER_LANG` | `android_compat.c:311` | `AConfiguration.language`. |
| `SOBER_COUNTRY` | `android_compat.c:312` | `AConfiguration.country`. |
| `SOBER_WINDOW_WIDTH` | `android_compat.c:504` | `ANativeWindow.width`. |
| `SOBER_WINDOW_HEIGHT` | `android_compat.c:505` | `ANativeWindow.height`. |
| `SOBER_GUEST_PATH` | `guest_dlopen_host.c:498` | Selects the guest ELF itself; host config, but changes all state. |
| `SOBER_PROP_<name>` / `SOBER_PROP_<name with .→_>` | `android_compat.c:719-730` | **Dynamic family** backing `__system_property_get()` — arbitrary guest-visible system-property seeding; cannot be enumerated as a fixed name. |

## 5. Cross-cutting findings

1. **`SOBER_STARTAPP_V2` alias parity.** `startapp_v2.c:127-128` enables the V2
   module on *either* `SOBER_STARTAPP_V2` **or** the legacy alias
   `SOBER_STARTAPP_PARAMS`. At the first snapshot the chain contained only the
   legacy alias; the lead added `SOBER_STARTAPP_V2` during the audit. Both names
   must be kept in the chain permanently, otherwise whichever name is dropped
   becomes a concrete false-green bypass. The ten `SOBER_STARTAPP_V2_*` value
   modifiers remain uncovered.
2. **v1/v2 pair mismatch (closed mid-audit).** `SOBER_HELPER_REPAIR` performs the
   same guest slot write as `_V2` in `bionic_bootstrap.c:984-997`. Both are now
   in the chain; keep them paired.
3. **Constructor-injection bypass (closed mid-audit).** `SOBER_CTOR0` and
   `SOBER_CTOR_RANGES=all` can inject the same guest FFlag ctor as
   `SOBER_REPLAY_FFLAG_CTOR`; `SOBER_CTOR0`, `SOBER_CTOR1` and `SOBER_CTOR_RANGES`
   are now all withheld.
4. **D09's "no gaps" is file-local.** It parses only `guest_app_shell.c`. All
   47 Tier-2 gaps live in other translation units. A project-level gate needs
   a source-tree-wide check (the literal+alias+`env_or` extraction in §1).
5. **Marker paths are not knobs.** `SOBER_SHELL_MARKER`, `SOBER_FRAME_MARKER`,
   `SOBER_INIT_MARKER` are output file paths for the deterministic markers; they
   intentionally stay out of the withhold list.
6. **Legitimate gates must stay out.** `SOBER_RENDER` and `SOBER_APP_SHELL`
   enable the real phase-3/phase-4 path that the DECISION 013 gate requires
   (`render_ok = sober_render_phase3_done()`); adding them to `seeded_state`
   would make the marker impossible. They are correctly absent.

## 6. Benign knobs (explicitly checked, no withhold needed)

- `guest_app_shell.c`: `SOBER_APP_SHELL_DIAGNOSTIC`, `SOBER_APP_SHELL_WAIT_MS`,
  `SOBER_APP_SLOT_DIAG`, `SOBER_JNI_VERBOSE`, `SOBER_TRACE_FREE_SLOT`,
  `SOBER_WINDOW_HOLD_MS`.
- `native_trace.c` (18): `SOBER_TRACE`, `_LOG`, `_FD`, `_DEPTH`, `_MIN`, `_MAX`,
  `_WATCH`, `_MAGIC`, `_SCANBYTES`, `_DUMP`, `_WATCH_AUTO`, `_START_FILE`,
  `_GUEST_BASE`; `_CALLOC`, `_USABLE`, `_NO_SIGTRAP_HANDLER` (the two `_WATCH_AUTO`
  arms temporarily `mprotect` a page but write no values and are log-only).
- `guest_dlopen_host.c`: `SOBER_DUMP_HANDLE`, `SOBER_WATCHDOG`,
  `SOBER_GDB_PAUSE_MS`.
- `bionic_bootstrap.c`: `SOBER_BOOTSTRAP_DEBUG`.
- `native_jni.c`: `SOBER_JNI_STRING_DEBUG`, `SOBER_TRACE_HANDLE_FIELDS`,
  `SOBER_POLL_FIELDS`, `SOBER_NDK_STUB_LIBC`, `JAVA_HOME`.
- `jni_json_bridge.c` / `app_singleton_bridge.c` / `startgame_v2.c`:
  `SOBER_JSON_VERBOSE`, `SOBER_APP_SINGLETON_VERBOSE`,
  `SOBER_STARTGAME_V2_VERBOSE`.
- `guest_render.c` / `native_render.c`: `SOBER_RENDER_BACKEND`, `DISPLAY`,
  `WAYLAND_DISPLAY`; `main.c`: `_`.

## 7. Recommended action (informational; D2 made no edits)

Tier 1 is now closed. The remaining action is to add the **47 Tier-2 value
modifiers** (four override families listed at the head of §4 Tier 2) to the
`seeded_state` expression in `src/linux/guest_app_shell.c` (lead-owned), and to
treat the `SOBER_PROP_*` family as a prefix match if Tier 3 is accepted. Re-run
`tests/D09-withhold-audit.sh --run` afterwards (note it will still report
"no gaps" because of its single-file scope), then use the §8 script for a
tree-wide check.

## 8. Recompute the raw difference (source is changing concurrently)

```python
import re, pathlib
src = pathlib.Path('src'); lit = set()
for p in src.rglob('*'):
    if p.suffix in ('.c','.h'):
        lit |= set(re.findall(r'"(SOBER_[A-Z0-9_]+)"', p.read_text(errors='replace')))
t = (src/'linux'/'guest_app_shell.c').read_text()
m = re.search(r'int\s+seeded_state\s*=(.*?);', t, re.S)
chain = set(re.findall(r'getenv\("(SOBER_[A-Z0-9_]+)"\)', m.group(1))) if m else set()
print(len(chain), len(lit), len(lit-chain))
print('\n'.join(sorted(lit-chain)))
```

Reminder from §1: this literal regex does **not** resolve the dynamic
`SOBER_PROP_<name>` family and must be supplemented by reading
`android_compat.c:716-731`.

---
*No source files were modified by worker D2.*
