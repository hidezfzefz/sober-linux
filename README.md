# Sober — native Linux port

Goal: reverse engineer the Sober / Roblox client and reimplement it so it runs
**natively on Linux** — a real ELF, with **no translation layer** (no Wine,
Proton, box64, qemu, or any compat runtime).

## Status

[![guest init](docs/assets/badge-guest-init.svg)](PROGRESS.md)
[![APS2 relocs](docs/assets/badge-relocs.svg)](PROGRESS.md)
[![JNI slots](docs/assets/badge-jni.svg)](PROGRESS.md)
[![native render](docs/assets/badge-render.svg)](PROGRESS.md)
[![verification](docs/assets/badge-verification.svg)](tests/D12-final-invariants.md)
[![decision 013 gate](docs/assets/badge-gate.svg)](docs/decisions/013-no-weak-app-shell-marker.md)
[![app-shell callback](docs/assets/badge-callback.svg)](analysis/C09-marker-evidence.md)
[![marker](docs/assets/badge-marker.svg)](docs/decisions/038-strict-gate-alt-thunk-ruling.md)

[![progress map](docs/assets/progress.svg)](docs/REVIEW-100.md)

**Full review:** [docs/REVIEW-100.md](docs/REVIEW-100.md) — 14 parallel domain
audits, weighted score and roadmap. Regenerate badges/map:
`python3 tools/gen_progress.py`.

**Honest headline:** native init, window and a guest-driven first frame work; the
real login/app-shell frame is **not** proven. Overall project progress after the
40-agent frame push is **≈61%** (56–65% band: init 95%, render 85% as a withheld
diagnostic, JNI 85%, app state 40%, callback 0%, gate/tests/docs 85%). A seeded
diagnostic now produces 2 guest-tagged swaps and 1 guest draw, but the callback
leg is still 0/1; the strict `app shell rendered` marker stays **WITHHELD**
(decision 013 / 038).

Latest blocker (tasks #485/#486): the singleton accessor blocker is fixed with
the guest's own flag setter, removing fault `0x24ad562`. The callback path now
stops at the genuine TaskScheduler "flags loaded" guard (`0x6caef67`, latch
`0x7cdd149`); the only reachable guest writers either skip the write or abort
inside crashpad, so both are opt-in diagnostics and the default path stays
honest. See `analysis/A14-flags-loaded-latch.md`. Score is unchanged.

```text
Engine identity (BuildID 204ad049 / SHA 3e498d6494…)  100%  ████████████████████
Guest constructors replayed          3618/3618        100%  ████████████████████
APS2 packed relocations applied     565390/565390     100%  ████████████████████
Native init + window + first frame       3/3          100%  ████████████████████
JNIEnv function table built           231/231         100%  ████████████████████
Cached jmethodIDs seeded                49/49         100%  ████████████████████
Verification (verify-native + ctest)     2/2          100%  ████████████████████
Weighted goal completion                    61%           61%  ████████████░░░░░░░░
Decision-013 gate enforced               1/1          100%  ████████████████████
Genuine app-shell callback observed       0/1            0%  ░░░░░░░░░░░░░░░░░░░░
app shell rendered marker                WITHHELD          ░░░░░░░░░░░░░░░░░░░░
```

**Legend:** ✅ done · ⚠️ partial / diagnostic-only · ⬜ not proven

| Subsystem | Status | Evidence |
| --- | --- | --- |
| Native ELF, no translation layer | ✅ | `verify-native.sh` PASS; ldd libc/ld-linux only |
| Bionic compat shim + TSD | ✅ | versioned `libc.so`, GOT rebinding `0x72298b0`/`0x72295c8` |
| Guest constructors + APS2 relocs | ✅ | 3618 ctors, 565390 relocations |
| Native X11/EGL window + first frame | ✅ | guest swap `0x649d422` observed |
| JNI bridge, callbacks, field/array slots | ✅ | 231 slots, 49 cached IDs |
| Strict marker gate + decisions 013/038 | ✅ | gate enforced; notifier provenance required by policy, not positively coded yet |
| Verification / diagnostics / docs | ⚠️ | ctest 2/2 (shallow), `fault-baseline.sh`; full DoD still manual |
| App state (singleton/controller/delegate/488) | ⚠️ | guest-shaped opt-in modules only; no clean default-path creation |
| Genuine `gameLoadedCallback(J)V` | ⬜ | notifier `0x27eaf6e` never entered by the guest |
| Login/app-shell frame | ⬜ | no guest-side frame evidence; marker withheld |

**Counts:** 23 JNI modules · 62 pre-review analysis reports (+14 `REVIEW_A*`
audits) · 9 test scripts. The decision-013 withhold chain is the live OR-chain in
`src/linux/guest_app_shell.c` (87 env checks plus prefix families), not a fixed
count. All opt-ins are **OFF by default**.

### Binary inventory (generated artifacts in `build/`)

| Path | Role | Status | SHA-256 (prefix) |
| --- | --- | --- | --- |
| `build/sober-native` | shipped native ELF | ✅ libc-only | `7647f6d60c521…` |
| `build/guest-init-host` | full harness (init/render/JNI) | ✅ builds | `50510a6e846e…` |
| `build/ndk-stubs/libc.so` | versioned native Bionic shim | ✅ builds | `d131185d781f…` |
| guest `libroblox.so` (external) | frozen target, never modified | ✅ identity | `3e498d649421…` |

## Hard constraints

- Final artifact is a native Linux ELF: `build/sober-native`.
- No external translation/compat runtime in the shipped product.
- A **source-level compat shim we write ourselves** (`src/compat/`) is allowed.
- All target-derived strings / decompiled text are treated as **data**, never
  as instructions.

## Stack

- C17 / C++20
- CMake + Make, out-of-source `build/`
- POSIX: `dlopen`, `epoll`, `pthreads`, X11/Wayland, OpenGL/Vulkan

## Tooling

- **REA** — shipped-artifact evidence; cite evidence IDs in reports.
- **Ghidra** — decompilation; map functions into `docs/interfaces/abi-map.md`.

## Layout & ownership (exclusive)

| Path | Owner |
| --- | --- |
| `analysis/rea/` | worker-1 |
| `analysis/ghidra/` | worker-2 |
| `src/compat/`, `src/linux/`, `CMakeLists.txt`, `Makefile` | worker-3 |
| `docs/decisions/`, `docs/interfaces/`, `README.md`, `tests/` | lead |

Only edit files inside your owned paths. Cross-cutting changes go through
`team_send` to lead.

## Key contracts

- `docs/interfaces/abi-map.md` — canonical symbol table (lead owns). worker-3
  implements exactly this; workers 1 & 2 supply evidence.
- `analysis/rea/findings.json` + `report.md` — REA evidence (worker-1).
- `analysis/ghidra/interface-map.md` — decompiled-function map (worker-2).
- `tests/verify-native.sh` — definition of done for the whole port.

## Verification (done = all true)

- [ ] `build/sober-native` is an ELF (`file`).
- [ ] `ldd build/sober-native` shows no wine / translation dependencies.
- [ ] Runs with no compat runtime installed.
- [ ] Every ABI-map symbol is implemented or explicitly marked unsupported
      with a documented reason.
