# Module integration decisions (team `TEAM-HCUY-MAGB-VPX7`, task #479)

Owner: lead. Date: 2026-11-21. Scope: resolve duplicate/parallel modules delivered
by the 20-agent audit wave and the three-worker integration team. Engine identity
frozen: BuildID `204ad049…`, SHA `3e498d6494…`. Strict decisions 013/038 remain
binding; the marker stays WITHHELD until genuine notifier-driven callback + guest
render + clean state.

## 1. One module per bridge slot (authoritative)

| Slot / concern | Authoritative default module | Retire / retain as A/B only | Env kill switch |
| --- | --- | --- | --- |
| TSD GOT `0x72298b0`/`0x72295c8` | `tsd_default` | old `SOBER_BIONIC_TSD_GOT` inline rebind | `SOBER_TSD_DEFAULT=0` |
| JavaVM global `0x7cdec78` | `javavm_default` | `javavm_semantics`, `SOBER_SEED_JNI_VM` | `SOBER_JAVAVM_DEFAULT=0` |
| FFlag ctor[0] `0x2e62390` | `fflag_default` | `SOBER_REPLAY_FFLAG_CTOR` | `SOBER_FFLAG_DEFAULT=0` |
| Scheduler nesting `obj+0x70` | `scheduler_guest` | `SOBER_SCHED_*` hacks | opt-in `SOBER_SCHEDULER_GUEST=1` |
| Allocator bridges / slot `0x726c0c0` | `alloc_default` | `alloc_contract_fix` | `SOBER_ALLOC_DEFAULT=0` |
| Singleton `0x72cb5e0` + services | `singleton_default` | `singleton_fix`, `singleton_services`, `app_singleton_bridge` | `SOBER_SINGLETON_DEFAULT=0` |
| `app+0x488` inner object | `app488_default` | `app488_fix`, `app488_inner`, `SOBER_SEED_APP_488` | `SOBER_APP488_DEFAULT=0` |
| `app+0x408` delegate | `delegate_default` | `SOBER_SEED_APP_DELEGATE`, fake host-vtable seed | `SOBER_DELEGATE_DEFAULT=0` |
| `app+0x440` controller | `controller_default` | `SOBER_SEED_USER_CONTROLLER` | `SOBER_CONTROLLER_DEFAULT=0` |
| PlatformParams JNI fields (fn 90/96/98) | `fields_default` | `platform_fields` | `SOBER_FIELDS_DEFAULT=0` |
| Object/scalar/array JNI slots | `jnislots_default` | `static_object_calls`, `scalar_returns`, `jni_arrays` | `SOBER_JNISLOTS_DEFAULT=0` |
| StartGameParams getters / placeId | `placeid_default` | `game_params`, `startgame_v2` (A/B only) | `SOBER_PLACEID_DEFAULT=0` |
| MessageBus publish (id 1) | `publish_default` | none | `SOBER_PUBLISH_DEFAULT=0` |
| Positive callback provenance | `provenance_gate` | `callback_provenance` diagnostic | fail-closed default-ON, kill `SOBER_PROVENANCE_GATE=0` |
| Guest render / surface | `render_guest` | phase-3 synthetic swap retained for ancestry | `SOBER_RENDER_GUEST=0` |

Rules:
- Retired modules stay in the tree for A/B but are **not installed** by default.
- Every authoritative module self-gates on its kill switch; lead wiring must call
  it unconditionally and never duplicate the state of another module.
- All new knobs and modifiers must be in the `seeded_state` withhold chain.

## 2. Render evidence contract (T3‑5 question resolved)

`render_ok = sober_render_phase3_done() && guest_tagged_render_ok()`.

- Phase-3 remains required ancestry only; the synthetic first-frame swap must not
  be sufficient.
- `guest_tagged_render_ok()` requires: at least one swap whose return address at
  GOT `0x722a1a8` lands in guest `.text`, at least one counted guest GLES draw
  (including extension-table draws at `0x77a4fe0/8`), and a stable
  `ANativeWindow` binding visible at global `0x72a3308`.
- **Decision on T3‑5's open question:** `ANativeWindow_fromSurface` being
  installed is **supporting evidence, not a hard requirement**. A genuinely clean
  run may use the real NDK path without our hook. The decisive signals are the
  guest-tagged swap/draw pair plus the window global; a missing fromSurface hook
  must not manufacture a false green or a false red.
- Mixed synthetic/guest evidence fails closed (`guest_tagged=0`).

## 3. Boot order (worker-2 wiring)

1. `tsd_default_install()` (after guest mapped/relocated, before any guest RVA).
2. `javavm_default_install(j, base)` (after `sober_jni_init()`).
3. `fflag_default_replay(base)` (after ctor replay/arena, before post-settings).
4. `scheduler_guest_install(base)` + `on_attach("FunctionMarshaller")`.
5. `singleton_default_install(base)` → `app488_default_install_from_wrapper(base)`
   → `delegate_default_install_from_wrapper(base)` → `controller_default_install(base)`.
6. `placeid_default_install(j, base)` → `publish_default_drive(...)` or
   `publish_default_publish_seeded(base)` after `StartGameWithParam 0x2ebd51a`.
7. `render_guest_install/run(cfg)` after surface/params are live.

## 4. Open item

`analysis/T2_5_flags_latch.md` (real path to set `0x7cdd149` without byte writes)
had not landed when this table was written; the flags-loaded latch remains the
unresolved prerequisite for a clean TaskScheduler init on the Lua path. It does
not change the one-module-per-slot decisions above.
