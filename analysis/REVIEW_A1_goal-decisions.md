# Auditor A1 — Original goal vs. delivered decisions

Analysis-only review. No source, doc, test, README, CMakeLists, git, or engine
files were modified. Engine identity is treated as frozen: BuildID
`204ad049…`, SHA `3e498d6494…`. The marker is WITHHELD under decisions
013/038; nothing in this report claims success.

Sources read: `PROGRESS.md`, `README.md`, all `docs/decisions/*.md`, and
`git log --all`. Evidence-file existence was checked on disk; the reports
themselves were **not** independently re-executed — the status column reflects
what the project's own records claim, read critically.

## 1. The original goal (reconstructed)

`README.md` (lines 3–5) and decision 000 state it plainly: reverse engineer the
Sober/Roblox client and reimplement it so it runs **natively on Linux** — a real
ELF, with **no translation layer** (no Wine, Proton, box64, qemu, mocktail,
basalt, or Android compat runtime). Decision 003 sharpens it: the guest
`libroblox.so` is already x86-64; "going native" means replacing **every**
Android/Bionic/JNI dependency with our own glibc/X11/EGL/GL implementation.
Decision 004 corrects an early mistake: the surface is Android NDK/Bionic, not
Win32. Decision 000 fixes the deliverable (`build/sober-native`, ELF, ldd-clean).

Decisions 006, 010, 012 turned that goal into staged milestones, each with its
own "done" marker: `guest init complete` → `first frame rendered` →
`app shell rendered`. Decision 012 is the final acceptance milestone: a native
JNI bridge, the guest's app-shell startup driven, a **natively rendered
login/app-shell frame**, and a deterministic `app shell rendered` marker
asserted by `tests/app-shell.sh`. Decisions 013 and 038 define what may count
as proof: a notifier-driven guest→Java gate callback
(`gameLoadedCallback(J)V` from notifier `0x27eaf6e`, `placeId != 0`, thunk
return `0x27eafb3`, or a genuinely notifier-driven `onAppShellReloadNeeded`),
**plus** the phase-3 render RVA `0x649d422`, **plus** `seeded_state == 0`
(no opt-in guest mutation), with `tests/app-shell.sh` staying RED until then.

Formal done definition — `README.md` lines 105–111, "Verification (done = all
true)": ELF check, no translation deps in `ldd`, runs with no compat runtime,
every ABI-map symbol implemented or explicitly marked unsupported. **Every one
of those four boxes is still unchecked** in the README.

## 2. Decision → promise → delivery → evidence

Legend: **Delivered** = the decision's own stated condition was met and is
recorded; **Partial** = some meetable parts landed but the promised end state
did not; **Not delivered** = the promised fix/milestone failed or was ruled
out; **Blocked (honest)** = the decision itself records an honest non-achievement
(not a success of the original goal); **N/A** = no record exists.

| Decision ID | Promise (what it committed to) | Delivered / partial / not delivered | Evidence file |
| --- | --- | --- | --- |
| 000 | Native ELF `build/sober-native`; no Wine/Proton/box64/qemu/compat runtime; C17/C++20, POSIX, REA/Ghidra evidence, untrusted target text | **Partial** — ldd-clean PIE scaffold exists and no runtime is used, but the ELF does not run the Roblox client end-to-end | `docs/decisions/000-stack-and-constraints.md`, `build/sober-native`, `tests/verify-native.sh` |
| 001 | Exclusive path ownership, `team_send` handoffs, per-task DoD | **Delivered** (process only) — structure and commit handoffs followed | `docs/decisions/001-folder-layout.md`, `README.md` (layout table) |
| 002 | `abi-map.md` canonical symbol table + REA `findings.json`/`report.md` + Ghidra `interface-map.md`, all evidence-backed | **Partial** — files exist, but `abi-map.md` is stale (rows still "required"/"next blocker: init_array crashes") and the full 573-import/AMediaCodec/AImageReader surface remains documented stubs | `docs/interfaces/abi-map.md`, `analysis/rea/findings.json`, `analysis/rea/report.md`, `analysis/ghidra/interface-map.md` |
| 003 | Identify real target, translation layer, ship exports/imports lists + host-ABI profile | **Delivered** — correct BuildID/SHA, x86-64 Android engine, mocktail/basalt identified; lists shipped | `docs/decisions/003-target-identification.md`, `docs/interfaces/libroblox-exports.txt`, `docs/interfaces/libroblox-imports.txt` |
| 004 | Stop Win32 stubs; reimplement Android NDK/Bionic surface | **Partial** — correction adopted; NDK shim, 411 versioned symbols, 174 named imports built, but media/image stubs unsupported | `docs/decisions/004-android-ndk-compat-not-win32.md`, `docs/interfaces/abi-map.md`, `build/ndk-stubs/libc.so` |
| 005 | Maintain a verification log; "remaining work before PASS" tracked | **Partial** — log delivered, but the "Remaining work before this log can say PASS overall" section is never closed; overall PASS never declared | `docs/decisions/005-verification-log.md`, `tests/verify-native.sh` |
| 006 | Guest `init_array` completes natively via `tests/guest-dlopen.sh`, deterministic `guest init complete` | **Delivered** — 3618 ctors, 565390 relocs, exit 0 with marker | `tests/guest-dlopen.sh`, `docs/decisions/005-verification-log.md` (Phase 2 final), `PROGRESS.md` |
| 007 | Memfd `DT_INIT_ARRAYSZ=0` ordering control with 6 safety conditions | **Delivered** (with the stated caveat: on-disk engine unchanged, in-memory loader metadata altered) | `docs/decisions/007-init-ordering-control.md`, `tests/guest-dlopen.sh`, `docs/decisions/005-verification-log.md` |
| 008 | Apply all 565390 AOSP APS2 packed RELA before bootstrap; confirm decode | **Delivered** — decode confirmed with REA evidence, all relocations applied | `docs/decisions/008-packed-relocations.md`, `analysis/bootstrap/bootstrap-map.md` |
| 009 | Fix self-hosting `dlsym` in versioned libc shim so all 411 slots resolve | **Delivered** (core fix; `sysconf` → 4096) — note the decision's required removal of worker-1's `repair_stub_libc_slots()` workaround is not independently verified here | `docs/decisions/009-versioned-libc-self-hosting-bug.md`, `src/compat/bionic_libc.c`, `docs/decisions/005-verification-log.md` |
| 010 | Phase 3: native window + EGL + first frame + gate + preserve phase 2 | **Delivered** | `docs/decisions/010-native-window-milestone.md`, `tests/first-frame.sh` |
| 011 | Record phase-3 completion with real guest render RVA `0x649d422` and gated marker | **Delivered** | `docs/decisions/011-phase3-complete.md`, `tests/first-frame.sh` |
| 012 | Phase 4: native JNI bridge, app-shell startup, **login/app-shell frame**, `app shell rendered` asserted by `tests/app-shell.sh` | **Not delivered** — JNI bridge (231 slots, 49 IDs) and strict test shipped, but no genuine gate callback, no app-shell frame, marker withheld, test stays RED | `docs/decisions/012-jni-app-shell-milestone.md`, `tests/app-shell.sh`, `docs/decisions/018-phase4-blocked.md` |
| 013 | Reject weak marker; require real guest→Java gate callback + render; fix Bionic TSD; keep strict gate | **Partial** — strict gate, TSD fix and withhold chain (22 knobs) delivered; the promised app-shell proof never achieved (marker correctly withheld) | `docs/decisions/013-no-weak-app-shell-marker.md`, `docs/decisions/038-strict-gate-alt-thunk-ruling.md`, `analysis/D02-withhold-audit.md` |
| 014 | Bypass validator `0x245605d` (`mov eax,4; ret`) to pass `nativeAppBridgeAppStart` | **Not delivered / superseded** — fault merely moved; patches later reverted by 017 | `docs/decisions/014-validator-bypass.md`, REA `ev_d838d804` |
| 015 | Trial bypass of shared-pointer helper `0x2358475` (`xor eax,eax; ret`) — follow-up to 014; no decision file exists, only a commit subject and 014's "decision 015" reference | **Not delivered / superseded** — experimental, swept up by 017's "stop per-deref patches" | `git commit 7534cbf`, `docs/decisions/014-validator-bypass.md` |
| 016 | No file, commit, or cross-reference anywhere in the repo | **N/A — no record exists** (number gap; cannot reconstruct a promise) | none |
| 017 | Stop whack-a-mole deref patches; revert narrow patches; fix synthetic object layout at source | **Partial** — patches reverted, ruling issued, but the source-level object-layout fix never converged | `docs/decisions/017-stop-deref-patches.md`, `docs/decisions/018-phase4-blocked.md` |
| 018 | Honestly declare phase 4 BLOCKED; trace writer of bogus `[rbx+0x68]` | **Blocked (honest)** — bridge + strict test delivered, no guest callback | `docs/decisions/018-phase4-blocked.md`, `docs/decisions/021-source-fixes-exhausted.md` |
| 019 | Zero host allocator blocks to fix the bogus handle | **Not delivered** — implemented, fault unchanged; later proven the handle is not from our allocator | `docs/decisions/019-zero-guest-allocations.md`, `analysis/handle/handle-origin.md` |
| 020 | Fix startup RVA call ABI (correct arg counts/types) | **Partial** — advanced startup from 3 to 5 RVAs, still faulted in `AppStart` | `docs/decisions/020-startup-rva-abi.md`, `docs/decisions/021-source-fixes-exhausted.md` |
| 021 | Declare phase-4 source-level fixes exhausted; milestone blocked | **Blocked (honest)** | `docs/decisions/021-source-fixes-exhausted.md` |
| 022 | Settle guard `0x2350044` semantics pending GDB watchpoint | **Partial** — proven a pure null-check and the constant proven stale code bytes, but the GDB watchpoint never landed | `docs/decisions/022-guard-disassembly.md`, `analysis/handle/guard-analysis.md` |
| 023 | Opt-in field-normalisation stub at `0x23331ca` | **Partial / superseded** — removed one fault, exposed the next garbage field; replaced by 024 | `docs/decisions/023-field-normalisation.md`, `docs/decisions/024-object-level-fix.md` |
| 024 | Fix object pointer once at source with a zeroed shaped object | **Not delivered** — `free(): invalid size` heap corruption; superseded by 025 | `docs/decisions/024-object-level-fix.md`, `docs/decisions/025-refbase-reconstruction.md` |
| 025 | Full guest-shaped RefBase reconstruction with vtable/refcount | **Not delivered / ruled out** — layout doc exists, but 026 concluded there is no RefBase metadata to reconstruct | `docs/decisions/025-refbase-reconstruction.md`, `analysis/handle/refbase-layout.md`, `docs/decisions/026-final-blocked.md` |
| 026 | Close phase 4 BLOCKED after exhaustive attempts; strict gate intact | **Blocked (honest)** | `docs/decisions/026-final-blocked.md`, `tests/app-shell.sh` |
| 027 | Phase 4b: capture allocation site with a working tracer, apply ONE targeted fix | **Not delivered** — tracer attempts failed; milestone blocked | `docs/decisions/027-phase4b-runtime-trace.md`, `docs/decisions/032-phase4b-final-blocked.md` |
| 028 | 7th bridge at calloc slot `0x726c0c0`; fix JNI string slots 159/161/162/163/165/166; fix test rebuild guard | **Partial** — slots fixed, bridges added; fault persisted | `docs/decisions/028-root-cause-allocator-and-jni-slots.md`, `docs/decisions/029-trace-findings.md` |
| 029 | Report phase-4b trace findings (still blocked) | **Partial / blocked** — honest findings, no fix | `docs/decisions/029-trace-findings.md`, `analysis/trace/` |
| 030 | Conclude the faulting handle is not our zeroed block; next: ptrace watchpoint | **Delivered (diagnostic conclusion)** — allocator work stopped; watchpoint still unresolved | `docs/decisions/030-bad-object-not-our-handle.md` |
| 031 | Seed/zero `[obj+0x68]` and `[obj+0x20]` at the app-shell call site `0x2333168` | **Not delivered** — faulted on the bogus pointer; opt-in and OFF | `docs/decisions/031-call-site-seed.md`, `docs/decisions/032-phase4b-final-blocked.md` |
| 032 | Close phase 4b BLOCKED; strict gate intact | **Blocked (honest)** | `docs/decisions/032-phase4b-final-blocked.md` |
| 033 | Zero stale curl holder at caller B (`0x245771e` → `xor edi,edi; nop`) | **Partial / not proven** — opt-in attempt; 035 later proved the consumed object is a code address, not a heap handle | `docs/decisions/033-curl-holder-fix.md`, `docs/decisions/035-entry-logger-result.md` |
| 034 | No file, commit, or cross-reference anywhere in the repo | **N/A — no record exists** (number gap) | none |
| 035 | Non-perturbing entry logger to identify the bogus object pointer | **Delivered (diagnostic)** — proved the object is guest code address `0x233263f` (curl-wrapper epilogue); the underlying fix was not achieved | `docs/decisions/035-entry-logger-result.md` |
| 036 | No file, commit, or cross-reference anywhere in the repo | **N/A — no record exists** (number gap) | none |
| 037 | Phase 5: exercise guest's own thunk `0x25848e0` and pass `app-shell.sh` — **with mandatory honesty notice** | **Partial** — ALT path produces a genuine guest thunk callback, but 038 downgrades it to diagnostic-only and withholds the marker; never proof | `docs/decisions/037-callback-thunk-milestone.md`, `docs/decisions/038-strict-gate-alt-thunk-ruling.md` |
| 038 | Keep gate strict; define real marker (notifier callback + render + clean state); `app-shell.sh` stays RED; no success | **Delivered (gate enforcement)** — decisions 013/037 amended, test stays RED, marker WITHHELD; the app-shell proof itself is absent | `docs/decisions/038-strict-gate-alt-thunk-ruling.md`, `tests/app-shell.sh`, `analysis/D03-gate-decision-audit.md` |

Notes on the table: 015 has no `docs/decisions/` file (only a git commit subject
and a follow-up reference inside 014). 016, 034 and 036 have no file, commit,
or cross-reference at all — they are number gaps, so no promise can be
reconstructed; I did not invent one. Several "Delivered (honest)" / "Delivered
(diagnostic)" rows are decisions that correctly record a **failure to reach the
goal**; they must not be read as goal progress.

## 3. What the project was supposed to prove for 100% completion

For the original goal to be proven, all of the following would have to hold
**together**, on the frozen engine (`204ad049…`, SHA `3e498d6494…`):

1. `build/sober-native` is a native Linux ELF that runs the real 120 MB
   `libroblox.so` with **no** mocktail/basalt/Wine/box64/qemu/Android runtime
   (`ldd` = libc/ld-linux only; runs with no compat runtime installed).
2. The guest fully initializes natively: 3618 constructors, 565390 APS2
   relocations, `guest init complete` (decisions 006–009).
3. A native X11/Wayland window and a **real guest render call** `0x649d422`
   produce `first frame rendered` (decisions 010–011).
4. The real app-start state machine, **with no seeded/synthetic guest state**
   (`seeded_state == 0`), delivers a notifier-driven gate callback —
   `gameLoadedCallback(J)V` from `0x27eaf6e` with `placeId != 0` and thunk
   return to `0x27eafb3`, or a notifier-driven `onAppShellReloadNeeded()V` —
   and a **natively rendered login/app-shell frame**, so that `tests/app-shell.sh`
   legitimately emits `app shell rendered` (decisions 012, 013, 037, 038).
5. Every ABI-map symbol is implemented or documented unsupported with a reason,
   and the README "done = all true" verification checklist is fully checked.

Items 1–3 are proven. Item 4 — the decisive product claim — is **not**. Item 5
is not (the checklist is entirely unchecked; `abi-map.md` is stale; media/image
APIs remain stubs).

## 4. How much of the promise is actually fulfilled

**Honest answer: the infrastructure is delivered; the goal is not.**

The README's headline block shows eight "100%" lines — but they measure
**plumbing**, not the product: engine identity, 3618/3618 constructors,
565390/565390 relocations, 3/3 init+window+frame, 231/231 JNI table slots,
49/49 cached IDs, 2/2 verification, and 1/1 "gate enforced." The gate being
"enforced" means the marker is correctly **withheld**, not that the app shell
was reached. The two lines that actually measure the goal read:
"Genuine app-shell callback observed **0/1 — 0%**" and "app shell rendered
marker **WITHHELD**." The subsystem table repeats this: `gameLoadedCallback`
is ⬜ (notifier `0x27eaf6e` never entered by the guest), login/app-shell frame
is ⬜, app state is ⚠️ (guest-shaped opt-in modules only).

Across `PROGRESS.md`, phases 4 through 10 record an unbroken chain of honest
blocks: phase 4 (018/026), 4b (027/032), 4c, phase 5's ALT path explicitly
flagged as a bypass with no frame proven (037), phase 6/7 still blocked on a
real gate callback, and phases 8–10 accumulating default-OFF diagnostic seeds
and a 22-knob withhold chain. The final verification of every phase repeats
the same invariant: phase-2/3 markers present, zero `app shell rendered`
emissions, default path `callback=0 render=1`, engine SHA unchanged. No
notifier-driven `gameLoadedCallback`/`onAppShellReloadNeeded` has ever been
observed; the callbacks that *did* fire (`onAppBridgeNotification`, accessor
getters, the harness-triggered ALT thunk) are explicitly rejected as proof.

So, in terms of the original promise: the team built and verified a native
loader/harness that gets the real engine to initialization and a guest-driven
first frame with zero translation runtime — a genuine, substantial result —
but **the client does not run natively in the sense the goal demanded**. There
is no proven login/app-shell frame, no qualifying gate callback, the acceptance
test stays RED, and the formal done checklist is untouched. Roughly: the
technical groundwork is largely in place, while the end-goal acceptance
criterion is at 0%. Per decisions 013/038, the marker stays WITHHELD and **no
success is claimed**.

## 5. Verdict

**PARTIAL at the infrastructure level; NOT DELIVERED at the goal level.**
The project proved it can load, initialize and drive a first native frame of
the frozen x86-64 Android engine without any translation runtime — real,
verified engineering. It did **not** prove the thing it set out to prove: a
natively running Roblox login/app-shell, witnessed by a genuine notifier-driven
guest→Java gate callback with clean state. `app shell rendered` is WITHHELD,
`tests/app-shell.sh` is intentionally RED, the app-shell callback metric is
0/1, and the README's "done = all true" boxes are all still empty. Decisions
013 and 038 deliberately prevent any success claim; this review keeps the gate
strict and makes no claim of success.
