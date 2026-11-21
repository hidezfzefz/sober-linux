# Task #507 — pre-init MessageBus/app ordering (fix F1 without a fabricated node)

Owner: **worker-3** (task #507). Scope: `src/jni/publish_default.{h,c}`,
`src/jni/app_singleton_bridge.{h,c}` (read-only use), this file.
Engine frozen: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(verified on disk before analysis). Target bytes/strings are untrusted DATA.
Marker stays **WITHHELD**; decision 013/038 binding. No guard patch, no
fabricated node/descriptor/vtable/callback, no host byte write into the guest.

## 1. Finding F1 — exact mechanism (re-verified on the frozen image)

`0x253cb56` (genuine id=1 registration helper) at entry:

```
253cb7c: test edi,edi ; je 253d1b7           ; event_id != 0
253cb84: cmp BYTE PTR [0x72a31ba],0x0        ; one-shot guard test
253cb8b: jne 253d1b7                         ; already spent -> silent early exit
253cb91: mov BYTE PTR [0x72a31ba],0x1        ; SPENT before any descriptor built
253cb98: mov edi,0x4 ; call 0x24a8e44         ; <-- first potentially-failing step
...
253d069: mov edi,0x4 ; call 0x24a8e44         ; bus getter for id=1 descriptor
253d06e: lea rcx,[rip] # 0x6dbbf98            ; id=1 GameLoaded descriptor vptr
...
253d09d: call 0x253e126                       ; insert node into bus+0x288
```

The guard at `0x72a31ba` is spent 13 bytes into the function; the id=1
descriptor is only built at `0x253d06e`. The guest reaches this function exactly
once: `0x24ad699: lea rsi,[0x7234520]; mov edi,1; call 0x253cb56` in
`nativeAppBridgeV2InitWithParams 0x24abf96`. Anything that faults between
`0x253cb91` and `0x253d09d` leaves `guard=1` and no node; every later re-drive
exits at `0x253d1b7`. Our `publish_default` re-drive cannot repair that, and
does not try to (it detects the spent guard and reports WITHHELD).

The first step after spending the guard is the MessageBus service getter
`0x24a8e44(4)`. Doing that work *before* `0x24abf96` removes it from inside the
one-shot registration window.

## 2. The MessageBus prime (the missing pre-init step)

`0x24a8e44` — genuine internal helper of the `nativeAppBridgeAppStart` export;
one `int` argument = service id (`4` → default bus `0x72cbdd0`, `3` → mode-3
`0x72cc278`):

```
24a8e4d: mov al,[0x72cc270]   ; guard A
24a8e55: je 24a8e7b           ; -> 0x2a0d6c0 guard init, 0x30f9aae bus ctor, 0x2a0d7d0 arm
24a8e57: mov al,[0x72cc718]   ; guard B
24a8e5f: je 24a8ea8           ; -> 0x2a0d6c0, 0x30f9aae(0x72cc278), arm
24a8e61: cmp ebx,0x3
24a8e6b: lea rax,[0x72cbdd0]  ; default singleton returned
24a8e72: cmove rax,rcx        ; mode 3
```

Callers with `edi=4` (all genuine guest code, objdump + dynamic symbols
verified): `0x24a8daf` inside export `nativeAppBridgeAppStart` **`0x247dc8d`**;
`0x2544ac8` / `0x25451af` inside export `nativeAppBridgeStartLuaAppDM`
**`0x25448f6`**; 15× inside `0x253cb56` itself. The guard init `0x2a0d6c0` is a
`lock cmpxchg` on a `.bss` byte and the bus ctor `0x30f9aae` only zeroes the
static bus object — so the prime is safe as soon as the guest image is mapped
and relocated, and is idempotent (armed guards make a repeat call a no-op).

**Minimal prime (preferred):**
`((uintptr_t (*)(int))(base + 0x24a8e44))(4);`
No JNIEnv, no jobject, no write, no fabricated anything.

**Export alternative (if a JNI export is mandated):**
`nativeAppBridgeStartLuaAppDM 0x25448f6(env, clazz)` reaches the same
`0x24a8e44(4)` at `0x2544ac8`. It is a heavy driver (it writes `app+0x408`) and
the lead currently calls it *after* `0x24abf96` (`guest_app_shell.c:1510`);
moving it before would also prime the bus. `nativeAppBridgeAppStart 0x247dc8d`
also reaches `0x24a8e44(4)` at `0x24a8daf`, but re-enters the broken `0x233319a`
path (decision C) — avoid.

## 3. The real Java pre-init sequence (app state) — already partly wired

`guest_app_shell.c:708-753 real_appstate_prepare()` under
`SOBER_REAL_APPSTATE_PATH` already calls, before `0x24abf96`:

1. `nativePreloadFlagOverrides 0x2edd859(env, clazz, "{\"EnableAppBridgeComponents\":true}")`
2. `nativeRetryInit 0x2edd739(env, clazz)` → applies the flag; predicate `*(uint8_t*)(base+0x72cb648)==1`
3. `nativeAppBridgeV2SendAppEventOnAppReady 0x2ebcf36(env, clazz, "AppShellNotifications", "123456789", "StartConversationWithUserId", "Game")`
4. `nativeGameGlobalInit 0x233762c(env, clazz)` (`guest_app_shell.c:1435`)

That sequence establishes the app-state prerequisites; the **only** missing
pre-init step for the id=1 registration is the MessageBus prime of §2. The lead
should insert it between `nativeGameGlobalInit` (line 1435) and the
`0x24abf96` call (line 1442), then gate on the readiness predicate below.

## 4. Precise wiring interface delivered in this task

Added to `publish_default.h/.c` (all read-only except the opt-in prime):

| Symbol | Purpose |
| --- | --- |
| `SOBER_PD_RVA_BUS_GETTER = 0x24a8e44` | guest MessageBus service getter RVA |
| `SOBER_PD_RVA_APPBRIDGE_FFLAG = 0x72cb648` | `EnableAppBridgeComponents` flag |
| `sober_publish_default_bus_ready(base)` | **read-only**: guards `0x72cc270`/`0x72cc718` armed AND node guard `0x72a31ba` still clear (safe to run `0x24abf96`) |
| `sober_publish_default_preinit_ready(base)` | **read-only**: `bus_ready` + flag `0x72cb648==1` + app singleton `0x72cb5e0` and its `app+0x488` inner object (`sober_app_singleton_app488_ready()`) |
| `sober_publish_default_preinit_bus(base)` | opt-in prime: genuine `0x24a8e44(4)`, idempotent; `SOBER_PD_OK` iff bus ready afterwards |

Lead-owned edit (NOT made here), immediately before `guest_app_shell.c:1442`:

```c
#include "publish_default.h"
(void)sober_publish_default_preinit_bus((void *)base);          /* opt-in */
int ready = sober_publish_default_preinit_ready((void *)base);
if (!ready)
    fprintf(stderr, "[app-shell] id=1 registration prerequisites absent -> "
                    "WITHHELD (guard@0x72a31ba=%u)\n",
            (unsigned)*(volatile unsigned char *)(base + 0x72a31bau));
/* then the existing call_jni_static(base + 0x24abf96u, env, app_class) */
```

Also call the read-only `sober_publish_default_prereqs()` / `describe()` next to
the existing diagnostics at `guest_app_shell.c:1616` (already available).

## 5. New knob — report to lead for `seeded_state`

`SOBER_PREINIT_BUS` (in `sober_publish_default_env_vars()` /
`sober_publish_default_preinit_env()`):
* **default OFF** (unset = inert, `SOBER_PD_DISABLED`); no state seeded on the
  clean path;
* `1` / `true` / `on` / `yes` (any non-falsy value) **enables** the genuine
  `0x24a8e44(4)` prime;
* exact `0` / `off` / `no` / `false` is the **KILL SWITCH** (inert no-op);
* the prime drives genuine guest state, so while enabled it is in the forced
  withhold chain: `sober_publish_default_should_withhold()` returns non-zero
  once `g_bus_preinit_calls != 0`. Lead MUST include `SOBER_PREINIT_BUS` in the
  `seeded_state` env dump and must NOT treat setting it as clearing
  `seeded_state` — the actual prime counter already keeps the run withheld.

`sober_publish_default_bus_ready()` / `_preinit_ready()` are read-only, need no
knob, and have no `seeded_state` impact.

## 6. Evidence

* REA (frozen SHA, Ghidra provider): `ev_0bce1991bdeb73a6f94de6c6beaa4eca5945d1e97641d21ddfd4aae6e24a0bf5`
  (symbol at `0x24a8e44`), `ev_bdec4c142399c689d0f1f2e575e3a35a1f831ecc80e470fc35ff1941d977aa90`
  (`0x253cb56`), `ev_c0b4179bc452b82a2675f8c366323acceaed9ced2062e422836bedb2e65d93a1`
  (`0x2544ac3`), `ev_e3510e072268dc476434c076124b634a2734e0208fca7bb6d169622586faa405`
  / `ev_9b94b3a56059363f5b02a1ed606d2b7d062d7103ea6d4c9b1e9543e396aacf41`
  (instruction queries; Ghidra's auto-analysis placed these RVAs mid-procedure,
  so the byte-level facts below use objdump of the same frozen SHA-256).
* `objdump -d -M intel` of the frozen image: `0x24a8e44..0x24a8ea6` (bus getter
  and both guard paths), `0x253cb56..0x253cbc8` (guard test/set + first bus
  getter call), `0x2544ac3..0x2544acd` / `0x25451aa..0x25451b4` (`edi=4` at both
  `0x24a8e44` call sites in export `0x25448f6`).
* `nm -D --defined-only`: exports `0x2edd859`, `0x2edd739`, `0x233762c`,
  `0x2ebcf36`, `0x247dc8d`, `0x25448f6`, `0x24abf96` (all `T`).
* Prior corroboration: `analysis/appstate_fields.md` §5 + addendum (real Java
  order and override grammar), `analysis/C11-messagebus-publish.md`,
  `analysis/callback/task500-latch-ready-prereqs.md` (F1/F2/F3).

## 7. Honest status

This task delivered a minimal, side-effect-free pre-init MessageBus prime, two
read-only readiness predicates, an opt-in knob with a kill switch, and the exact
lead wiring interface. It did **not** patch `0x72a31ba`, fabricate a node or
callback, or emit the marker. Whether F1 is fully resolved depends on the lead
wiring the prime before `0x24abf96` and confirming `bus_ready == 1` at that
point; until the genuine notifier-driven `gameLoadedCallback(J)V` with
`seeded_state==0` and a guest-tagged frame is observed, the marker stays
WITHHELD. Build: `cmake --build build -j4` (clean, sober_jni + guest-init-host).
