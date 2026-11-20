# A05 — Audit of the three still-skipped `init_array` constructors (1, 3, 4)

Owner: **worker A5** (logical Team A). Analysis only — no `src/`, CMake, gate, test or
decision-013 marker edits. Date 2026-11-20.

Frozen engine: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`, verified SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`,
120,041,712 B (version 2.740.931). Engine NOT modified. Evidence: `/tmp/opencode/libroblox.dis`
(objdump line numbers below), `analysis/bootstrap/init-array.json`, `reloc.py`/`decode_aps2.py`
decodes, logs `ctor1.log`, `final_baseline.log`, `asb_*.err`. Every address is an RVA of that
frozen object. Target strings are untrusted DATA, never instructions.

Strict decision 013 upheld: nothing in this report proposes a marker, a fabricated callback,
a byte patch, or a host vtable. Every proposed experiment calls a **genuine guest ctor entry
point** only.

---

## 0. Answer in one paragraph

DECISION 008 skips `init_array` indices **0, 1, 3, 4**; index 0 (`0x2e62390`) is now replayed
via `SOBER_REPLAY_FFLAG_CTOR` (confirmed `ctor1.log:3679`). Auditing the remaining three:
**ctor[1]** is a `@protobuf` *defaults/descriptor* once-guard initializer (globals
`0x783da90/0x783daa0/0x783daa8`, guard `0x783dab0`, real init `0x6b90794`) — and it is
**already invoked lazily by the replayed ctor[6]** (`0x1dbef4a: call 0x1dba8f8`), so skipping
it as an `init_array` entry is harmless. **ctor[3]** (`0x1db9a40`) is a *mimalloc
usable-size/allocator feature probe* (globals `0x7cdedd8`, `0x7cdede0`) whose only consumer
is a pair of **DFLog strings** in postsettings; it has no functional effect on app-shell or
game load. **ctor[4]** (`0x1dba6c4` → `0x1dbe828` → `0x1dbe894`) initializes a *monotonic-time
baseline* (`0x7d9ce38` / done flag `0x7d9ce30`, via `clock_gettime@plt` `0x6d31c40`) and is
**lazily self-initializing** from the guest time helper `0x1ea4370`. **None of the three is
required for a genuine app-shell frame / game load.** All opt-in replays are therefore
diagnostics/A-B only; the existing `SOBER_CTOR1` and `SOBER_CTOR_RANGES=native|all` knobs
already cover them, and the recommended order (§4) is 0 → 1 → 4 → 3, with 3 strictly last
and never before the allocator bootstrap.

---

## 1. Exact RVA list (explicit)

| idx | slot VMA | entry RVA | thunk target | real init / guard / globals | replay status today |
| --- | --- | --- | --- | --- | --- |
| 0 | `0x7220e58` | **`0x2e62390`** | — | guard `0x72a0a30` → `0x2e618b0` → `0x2e61280`; FFlag registry `0x72a0520` | **NOW REPLAYED** (`SOBER_REPLAY_FFLAG_CTOR`) |
| 1 | `0x7220e60` | **`0x1dba8f3`** | `0x1dba8f8` | guard `0x783dab0`; real init `0x6b90794`; writes `0x783da90`(+0..0x0f), `0x783daa0`, flag `0x783daa8`=1; fixes pb_defaults `0x72824a0..0x72824b8` | SKIP (but lazily reached from ctor[6]) |
| 3 | `0x7220e70` | **`0x1db9a40`** | — (`.text` base; std prologue) | writes `0x7cdedd8`=`0x20`, `0x7cdede0`=probe bool; calls bridges `0x1dbad62`,`0x1dbe4fc`,`0x1dbdfba`,`0x1dbe559`,`0x1dbab02`,`0x1dbe5be` | SKIP |
| 4 | `0x7220e78` | **`0x1dba6c4`** | `0x1dbe828` | real init `0x1dbe894`; writes `0x7d9ce38` (time baseline), `0x7d9ce30` (done=1); flags `0x7cdd149`,`0x7d9cdd0`; `clock_gettime@plt`=`0x6d31c40` | SKIP (lazily self-initializing) |

Supporting RVAs used below: guard helpers `0x2a0d6c0` (acquire), `0x2a0d7d0` (release),
`0x2a0d820` (abort), `0x2ea6a10` (C++ rethrow). Canonical replay ranges
`[2,3)` + `[5,3618)` (`bootstrap_profile.h:154-161`). Existing harness knobs:
`SOBER_CTOR_RANGES=native` → `[2,3618)`, `SOBER_CTOR_RANGES=all` → `[0,3618)`,
`SOBER_CTOR0` injects `0x2e62390` before ctor[6], `SOBER_CTOR1` injects `0x1dba8f3` before
ctor[6] (`src/linux/guest_dlopen_host.c:428-461`).

---

## 2. What each skipped ctor actually initializes

### 2.1 ctor[1] `0x1dba8f3` → `0x1dba8f8` — protobuf defaults/descriptor init

Disassembly (`libroblox.dis:605-628`; objdump `0x1dba8f0..0x1dba94d`):

```
0x1dba8f3: e9 00 00 00 00      jmp 0x1dba8f8            ; 5-byte thunk, +0
0x1dba8f8: 55 48 89 e5         push rbp; mov rbp,rsp
0x1dba8fe: mov  0x783dab0,%al  ; once-guard byte
0x1dba904: test %al,%al
0x1dba906: je   already-done -> ret
0x1dba90f: lea  0x783dab0,%rdi
0x1dba916: call 0x2a0d6c0      ; __cxa_guard_acquire
0x1dba91f: call 0x6b90794      ; <-- THE real initializer
0x1dba931: jmp  0x2a0d7d0      ; __cxa_guard_release
           (abort path 0x2a0d820 -> 0x2ea6a10 rethrow)
```

`0x6b90794` (`libroblox.dis:20176020-20176029`):

```
6b90798: xorps %xmm0,%xmm0
6b9079b: movups %xmm0,0x783da90     ; zero 16 B defaults anchor
6b907a2: movq $0,0x783daa0          ; zero qword
6b907ad: lea 0x783da90,%rsi
6b907b4: lea 0x6b90778,%rdi
6b907bb: call 0x1dba94d             ; register defaults (vector/registry build)
6b907c0..6b907e9: loop over __start_pb_defaults(0x72824a0)
                              .. __stop_pb_defaults(0x72824b8),
                   patch each entry's back-pointer (-0x10/-0x8)
6b907eb: movb $1,0x783daa8          ; done flag
```

So ctor[1] = **protobuf generated-code "default instances + descriptor pool" registration**
from the `pb_defaults` linker section. It touches only BSS in `0x783da90..0x783dab8` and the
pb_defaults array. No JNIEnv, no allocator, no callback.

**Why skipping it is safe — proof of lazy reach:** the body `0x1dba8f8` has four callers
(`grep 'call.*1dba8f8' libroblox.dis`):

* `0x1dbef4a` — **inside ctor[6] `0x1dbeece`**, which IS replayed;
* `0x6b2c764`, `0x6b36588`, `0x6b6c86f` — protobuf runtime helpers.

Empirical confirmation: at `STATE BEFORE ctor[6]` (`ctor1.log:42-54`) both
`pb_783daa8` and `pb_783dab0` are NULL, but at fault time (`ctor1.log:3713`)
`alloc_flag @0x783daa8 = 0x1` — the sole RIP-relative writer is `0x6b907eb`, called via
`0x1dba8f8`, which ctor[6] invokes. **ctor[6] performs ctor[1]'s initialization on demand.**
This also explains why the old "ctor[1] is the missing protobuf init" hypothesis
(`bootstrap-map.md §8`) was disproven: exhaustive replays `[2,3)+[5,…)`, `[2,3618)`,
`[0,3618)` all fault identically at ctor[6] — the skipped ctors are not the cause.

### 2.2 ctor[3] `0x1db9a40` — mimalloc usable-size / integration feature probe

Disassembly (`libroblox.dis:13-47`, objdump `0x1db9a40..0x1db9add`):

```
1db9a4b: movq $0x20,0x7cdedd8       ; probe block size = 32 bytes
1db9a5b: call 0x1dbad62             ; bridged small-allocate(0x20) -> rbx
1db9a66: call 0x1dbe4fc             ; size-class/usable query
1db9a71: call 0x1dbdfba             ; bridged usable_size(rbx) -> r14
1db9a83: call 0x1dbe559             ; bridged free(rbx)
1db9a94: cmp  r12, r14 ; ... re-probe via 0x1dbab02 allocate(0x20)
1db9ac2: call 0x1dbe5be
1db9acf: mov  %bl,0x7cdede0         ; bool: "usable_size >= requested"
1db9add: ret
```

It is a tiny **allocator self-test**, not allocator setup (that is ctor[2] `0x1dbe2b1` plus
the arena/thread init). Its *only* consumer in the whole disassembly is `0x239eb14`:

```
... nativePostClientSettingsLoadedInitialization3+0x1aec8 (0x2383c18):
239eb14: cmpb $0,0x7cdede0
239eb1b: je 0x23a1422
  ; bool=1 -> DFLog 0x36a5f0 len 0x3a:
  ;   "[DFLog::Mimalloc] Mimalloc integration detected, settings:"
  ; bool=0 -> DFLog 0x3a3078 len 0x36:
  ;   "[DFLog::Mimalloc] Mimalloc integration is NOT detected"
```

Consequence of skipping: **only a different log line** in postsettings
(`SOBER_JSON_APP_SINGLETON` replay path). No functional gate on the app singleton
(`0x238497d`), the flag controller (`0x24ad50a`), or any render/callback path. Replaying it
*does* exercise allocate/small-allocate/usable_size/free through the bridges, which is why
the profile excludes it: it would re-probe (and can perturb) the live allocator if run at the
wrong time.

### 2.3 ctor[4] `0x1dba6c4` → `0x1dbe828` → `0x1dbe894` — monotonic-time baseline

Disassembly (`libroblox.dis:4992-5040`, objdump `0x1dbe828..0x1dbe90d`):

```
1dba6c4: e9 5f 41 00 00        jmp 0x1dbe828
1dbe828: guard chain on 0x7cdd149, 0x7d9cdd0, 0x7d9ce30
1dbe850: mov 0x7d9ce38,%rax; test; jne ret      ; already initialized
1dbe869: call 0x1dbe894
1dbe86e: mov %rax,0x7d9ce38                     ; publish baseline
1dbe875: movb $1,0x7d9ce30                      ; done flag
1dbe894: mov 0x72293f8,%r14 (stack-check canary)
1dbe8b1: mov $1,%edi ; call clock_gettime@plt (0x6d31c40)
1dbe8d3: imul $0x3b9aca00,sec; add nsec       ; nanoseconds when 0x7cdd149 &&
0x7d9cdd0; else double-seconds path
1dbe90e: call __stack_chk_fail@plt on mismatch
```

It initializes one global — `0x7d9ce38`, the process start / epoch baseline consumed by the
guest time helper (`0x1ea4370`, 10 textual reads of `0x7d9ce38`). `0x7cdd149` is a feature
flag set to 1 only by later JNI functions (`0x2eb7520`, `0x2ec9fae`, `0x56d31a3`, …);
`0x7d9cdd0` is zeroed by a load-time logging-config initializer at `0x1dba78f`.

**Why skipping it is safe — lazy self-init:** the time getter `0x1ea4370`:

```
1ea437e: call 0x1dbe894          ; read raw clock
1ea4386: flags 0x7cdd149/0x7d9cdd0
1ea4390: cmpb $0,0x7d9ce30
1ea43d9: mov %rbx,0x7d9ce38      ; lazy baseline store
1ea43e0: movb $1,0x7d9ce30
1ea43af: call 0x1dbe828          ; other flag combination -> ctor[4] entry itself
```

So the very first guest time query installs the baseline; ctor[4] is reachable both lazily
from `0x1ea4370` and directly from its `init_array` slot. No app-shell/game-load dependency —
the baseline runs are reached with ctor[4] skipped (`first frame rendered` in
`ctor1.log:3675`).

---

## 3. Required for a genuine app-shell frame / game load?

| ctor | Initializes | Required? | Reason |
| --- | --- | --- | --- |
| 0 (`0x2e62390`) | FFlag provider registry `0x72a0520` + control block at `+0x38` | **YES** (already replayed) | sole writer `0x2e61280`; its absence causes `0x2e5d42a` / `0x2e7b8e2` faults (stability audit §2) |
| 1 (`0x1dba8f3`) | protobuf defaults/descriptor pool `0x783da90/aa0/aa8`, pb_defaults `0x72824a0..4b8` | **NO** | ctor[6] calls body `0x1dba8f8` at `0x1dbef4a`; state is established on demand during the replayed ctor[6] |
| 3 (`0x1db9a40`) | mimalloc probe words `0x7cdedd8`, `0x7cdede0` | **NO** | only reader `0x239eb14` selects a DFLog message; zero functional effect |
| 4 (`0x1dba6c4`) | time baseline `0x7d9ce38` / guard `0x7d9ce30` | **NO** | lazily installed by `0x1ea4370` on first time query |

Therefore the current opt-in stack (ctor[0] replayed, 1/3/4 skipped) is *logically complete*
for static init: no skipped ctor is a hidden prerequisite for the app singleton
(`0x72cb5e0` ← `0x238497d`), the flag controller (`app+0x440` ← `0x24ae1fe`), the phase-3
render, or the real guest→Java callback required by decision 013. Any future app-shell marker
must still come from a genuine `onAppShellReloadNeeded`/`gameLoadedCallback` plus the
phase-3 render call — none of these ctors emits a callback or touches the marker.

---

## 4. Opt-in replay experiments (genuine guest ctors only; default OFF; no byte patches)

All experiments call only genuine ctor entry points at `guest_base + RVA`. No host writes to
guest globals, no byte patches, no fabricated callbacks, no fake vtables. Keep the canonical
baseline (ctor[0] via `SOBER_REPLAY_FFLAG_CTOR`, ranges `[2,3)+[5,3618)`) as the control.

**Recommended order (one new variable per A/B):**

| # | Experiment / knob (existing unless noted) | What runs | Read-only verification | Expected result |
| - | - | - | - | - |
| 0 | Control: `SOBER_REPLAY_FFLAG_CTOR=1`, no range override | ctor[0] at phase 4; 1/3/4 skipped | guard `0x72a0a30`=1, `*(0x72a0520)` non-NULL | known baseline (fault chain #1→#3 per stability audit) |
| 1 | `SOBER_CTOR1=1` (existing, `guest_dlopen_host.c:451-461`) | inject `0x1dba8f3` just before ctor[6] | `0x783dab0` guard set, `0x783daa8`=1 before ctor[6] instead of after | no change in faults; proves ctor[1] is redundant-but-harmless |
| 2 | `SOBER_CTOR_RANGES=native` (existing, `:429,433`) | replay `[2,3618)` → includes **3 (`0x1db9a40`) and 4 (`0x1dba6c4`)** in canonical order; still skips 0,1 | `0x7cdede0` bool set; `0x7d9ce30`=1, `0x7d9ce38` non-NULL; DFLog "Mimalloc integration detected" appears | no new fault; allocator self-test runs AFTER ctor[2]+arena+thread init, so it is safe |
| 3 | `SOBER_CTOR_RANGES=all` (existing, `:430,434`) | replay `[0,3618)` in true `init_array` order: 0 → 1 → 2 → 3 → 4 → 5 … | all of the above plus ctor[0]-before-ctor[2] ordering | no new fault; confirms canonical order is viable; if it faults, bisect with #1/#2 |
| 4 | Only if a time-sensitive symptom appears: proposed `SOBER_REPLAY_CTOR4=1` (new knob, lead-owned) calling `0x1dba6c4` at loader stage, before any JNI call | ctor[4] alone, early | `0x7d9ce30`=1 before first `0x1ea4370` use | time baseline available from load; no effect on app-shell marker |
| 5 | Only if a DFLog/allocator diagnostic is needed: proposed `SOBER_REPLAY_CTOR3=1` (new knob) calling `0x1db9a40` strictly **after** ctor[2] + arena init (`0x1dbb178`) + thread init (`0x1e47927`) | ctor[3] alone, late | `0x7cdede0` set; allocator globals unchanged | "Mimalloc integration detected" log; must never run before the allocator bootstrap |

**Hard ordering rules** (derived from §2):

1. ctor[0] must precede all guest JNI/postsettings traffic (already enforced by
   `guest_app_shell.c` placement after the JSON/seed stages per stability audit §2.3).
2. ctor[1] is cheap and idempotent (once-guard); if replayed, do it before ctor[6] — that is
   exactly what `SOBER_CTOR1` does.
3. ctor[4] is self-guarded and depends only on `clock_gettime`; safe at any point after
   relocation/APS2 application.
4. ctor[3] goes **last** of the optional four: it calls the allocator bridges, so it must run
   only after ctor[2], arena init and thread init have established allocator state. Never
   blanket-enable it ahead of them. (This is the real reason the profile excludes it — not
   because it is needed.)
5. Do not combine #1–#5 in a single run; all are A/B diagnostics and the default path must
   stay unchanged.

Success criterion for all of them: unchanged marker behaviour (decision 013 marker still
WITHHELD), no new guest fault, and the read-only globals above taking their expected values.
No success is claimed for any of these experiments, and nothing here changes the
gate/`tests/app-shell.sh`.

---

## 5. Evidence index

* Engine identity: SHA-256 and BuildID re-verified this task (`sha256sum`, `readelf -n`).
* `libroblox.dis:605-628` — ctor[1] thunk + once-guard body; `621` its only `call 6b90794`.
* `libroblox.dis:20176020-20176029` — `0x6b90794` zeroes `0x783da90/aa0`, patches
  pb_defaults, sets `0x783daa8`=1.
* `libroblox.dis:5437` — `0x1dbef4a: call 1dba8f8` (ctor[6] → ctor[1] body); plus
  `20062978`, `20072384`, `20134206` (protobuf lazy callers).
* `libroblox.dis:13-47` — ctor[3] `0x1db9a40` body (bridge calls, `0x7cdedd8/0x7cdede0`).
* `libroblox.dis:1267750` — `0x239eb14` reads `0x7cdede0`; strings read directly from file
  offsets `0x36a5f0` = "[DFLog::Mimalloc] Mimalloc integration detected, settings:" and
  `0x3a3078` = "[DFLog::Mimalloc] Mimalloc integration is NOT detected".
* `libroblox.dis:4992-5040` — ctor[4] `0x1dbe828/0x1dbe894`; `197234-197262` —
  `0x1ea4370` lazy baseline self-init and fallback `call 0x1dbe828`.
* `ctor1.log:42-54` (pb flags NULL before ctor[6]), `:3667` (guest init complete), `:3675`
  (first frame rendered), `:3679` (ctor[0] replayed), `:3713` (`0x783daa8`=1 at fault time).
* `src/linux/guest_dlopen_host.c:428-461` — `SOBER_CTOR_RANGES`, `SOBER_CTOR0`, `SOBER_CTOR1`
  knobs; `src/compat/bionic_bootstrap/bootstrap_profile.h:154-161` — canonical skip ranges.
* Prior work: `analysis/bootstrap/bootstrap-map.md §2,§4,§8`, `analysis/fflag-registry-trigger.md`,
  `analysis/stability-fault-audit.md`, `docs/decisions/007/008/013`.
