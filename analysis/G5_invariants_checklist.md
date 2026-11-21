# G5 — Final 20-item invariant checklist for the frame integration

Author: **G5** (group D). Scope: **this file only** — `analysis/G5_invariants_checklist.md`.
Analysis-only; no `src/`, test, CMake, doc, engine or git change was made by this agent.

**Front-loaded verdict.** All 20 checks below must return their stated expected value
at the final integration snapshot. They are conjunctive: any FAIL blocks delivery.
Engine is **frozen**; decisions 013/038 are binding; the `app shell rendered` marker
stays **WITHHELD** on every default-path run. No check may be satisfied by
synthetic/guest-seeded evidence (host Route-B swap, ALT thunk, `onAppBridgeNotification`,
host-written window global, or any `SOBER_*` guest-state opt-in).

Defaults for all commands: run from repo root `/home/user/sober-linux`; guest =
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` (AppImage 2.740.931).

## Frozen constants (reference for every check)

| Constant | Expected value |
| --- | --- |
| Engine SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| Engine BuildID (`readelf -n`) | `204ad049599c96afb75b8a21d5051063e821d6c9` |
| Guest executable-RA window | `[base, base+0x6d33b90)` (first PT_LOAD, `R E`) |
| Render vtable / swap slot | `0x71912e8`, `[3] @ 0x7191300 = 0x649d422` (9 methods `[0]..[8]`) |
| Swap GOT / draw loader GOT | `0x722a1a8` / `0x722a180` (`R_X86_64_JUMP_SLOT`) |
| ANativeWindow global / gen | `0x72a3308` / `0x72a3310` |
| Pre-existing fault RVA | `0x2eb4b65` (`signal=11`, `rdi=(nil)`), informational only |
| CTest set | exactly 2: `sober_native_check`, `sober_native_version` |

---

## 1. Build

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
```
**Expected:** both rc=0; `Configuring done` / `Generating done`; no compiler/linker
warnings; all 18 targets built — `sober-native`, `guest-init-host`, `sober_render`,
`sober_jni`, `native_trace`/`libnative_trace.so`, `render_probe`, `jni_probe`, NDK
stubs (`log`, `android`, `mediandk`, `OpenMAXAL`, `OpenSLES`, `m`, `dl`),
`ndk_bionic_libc`, `sober_compat`, `sober_platform`, `sober_bootstrap`.
**Do this before checks 5/19** so the README binary SHA table is read against the
final artifacts.

## 2. ctest 2/2

```bash
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
```
**Expected:** `-N` lists exactly 2 tests (`sober_native_check`, `sober_native_version`);
run → `100% tests passed, 0 tests failed out of 2`, rc=0. `fault-baseline.sh`, D08,
D09, `app-shell.sh` are **absent** from `build/CTestTestfile.cmake`.

## 3. verify-native

```bash
bash tests/verify-native.sh
```
**Expected:** rc=0; §1 `file build/sober-native` → `ELF 64-bit LSB pie executable,
x86-64, ...`; §2 ldd printed with no `wine|proton|box64|qemu|wibo`; §3/§4 print the
expected manual notices (no compat runtime, ABI-map coverage).

## 4. ldd libc-only

```bash
ldd build/sober-native
ldd build/guest-init-host
```
**Expected (both):** exactly three entries — `linux-vdso.so.1`,
`libc.so.6 => /usr/lib64/libc.so.6`, `/lib64/ld-linux-x86-64.so.2` (addresses vary).
No X11/EGL/GL/Wayland link dependency; no translation layer. X11/EGL are runtime
`dlopen` only.

## 5. Engine SHA / BuildID unchanged

```bash
sha256sum /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
readelf -n /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so | awk '/Build ID:/{print $3; exit}'
```
**Expected:** SHA `3e498d649421…`, BuildID `204ad049…`, and **identical before and
after every harness run** (first-frame ×3, fault-baseline, app-shell, guest-dlopen,
D08). Reject any Flatpak `791c3f9e…` artifact.

## 6. first-frame

```bash
bash tests/first-frame.sh
```
**Expected:** rc=0 (3/3 clean runs); stdout exactly `guest init complete` +
`first frame rendered`; stderr `guest call 0x649d422 swap(...) -> rc=0/1` and
`guest-driven swaps observed: 1`; `backend=x11-egl-gles2 ... surfaceless=1`;
`OpenGL ES 3.2 Mesa 26.2.2 | frames swapped: 1`; window
`SoberNativeRenderPhase3`; SHA unchanged. **Honest caveat:** D01 recorded one
intermittent `XIO: fatal IO error 2 … on X server ":0"` at Xwayland teardown that
forced a nonzero exit without losing any asserted marker; D12's 3/3 were clean.
Judge on the asserted markers, but re-run if the XIO flake appears.

## 7. guest-dlopen

```bash
bash tests/guest-dlopen.sh
```
**Expected:** rc=0; `guest Build ID: 204ad049…`; on-disk SHA before == after ==
`3e498d649421…`; `guest init complete` + `/tmp/sober-guest-init.marker`;
`byte deltas: none on disk; private memfd copy changes only DT_INIT_ARRAYSZ`
(loader metadata RVA `0x7220e58`, count `28944 → 0`; no code/data bytes touched);
3618/3618 ctors replayed and 565390/565390 APS2 relocations applied.

## 8. fault-baseline (default-OFF)

```bash
bash tests/fault-baseline.sh
```
**Expected:** **script rc=0** (informational), harness exit **139**;
`signal=11 (Segmentation fault) si_addr=(nil)`, `rdi=(nil)`,
`faulting guest RVA: 0x2eb4b65`; bridge `bridge=ready env_slots=231 vm_slots=5
native_jvm=1`; preserved stdout `guest init complete` / `first frame rendered`;
**no marker emitted or checked**; absent from ctest (2 tests) and verify-native.
Never convert this into an asserted test.

## 9. jni_probe

```bash
./build/jni_probe --probe
./build/jni_probe
```
**Expected:** both rc=0. Probe line:
`jni probe: bridge=ready env_slots=231 vm_slots=5 native_jvm=1 jvm_path=JAVA_HOME/lib/server/libjvm.so`.
Full self-test: `GetVersion -> 0x10006`, handle seed
`magic=0xc0dedbad +0x20=0 +0x68=0`, `CallStaticVoidMethodV/A installed at
fn[138]/fn[139]`, `jni bridge self-test OK`. Table contract: 231/231 JNIEnv slots,
49/49 cached jmethodIDs.

## 10. Withhold audit (default-OFF)

```bash
bash tests/D09-withhold-audit.sh
SOBER_D09_WITHHOLD_AUDIT=1 bash tests/D09-withhold-audit.sh
```
**Expected:** bare → `D09 withhold audit: default-OFF (no source read, no side
effects)`, rc=0. Explicit → `getenv SOBER knobs referenced : 135`,
`withhold-chain knobs found : 129`, raw difference = only the 6 benign
(`SOBER_APP_SHELL_DIAGNOSTIC`, `SOBER_APP_SHELL_WAIT_MS`, `SOBER_APP_SLOT_DIAG`,
`SOBER_JNI_VERBOSE`, `SOBER_TRACE_FREE_SLOT`, `SOBER_WINDOW_HOLD_MS`), and
`-- GAP LIST ... (none)`. Every new guest-mutating knob/modifier must live in
`seeded_state`. D08 stays explicit-informational; neither is wired into
CMake/ctest/verify-native. (README's "87 env checks" is historic; current parse
finds 129 — refresh the doc.)

## 11. Zero marker

```bash
grep -c '^app shell rendered$' /tmp/sober-*.out /tmp/sober-*.err 2>/dev/null
ls -l /tmp/sober-app-shell.marker 2>&1
```
**Expected:** 0 emissions in every `/tmp/sober-*.out|.err`; no marker file;
default-path stdout contains only `guest init complete` and `first frame rendered`.
This is the decision-013 assertion that catches false greens.

## 12. Strict gate

```bash
bash tests/app-shell.sh
```
**Expected:** rc=**1** at §3 — current script emits
`FAIL: no qualifying guest->Java app-shell callback observed` (older snapshots:
`FAIL: no genuine guest->Java JNI callback observed`); stderr
`marker withheld: callback=0 render=1 (env calls=33 callbacks=0 last='isVrDevice')`.
Gate requires a genuine guest→Java completion callback
(`onAppShellReloadNeeded`/`gameLoadedCallback`) **plus** phase-3 render **plus**
clean state. `onAppBridgeNotification` is diagnostic-only and rejected.

## 13. No ALT

```bash
unset SOBER_ALT_CALLBACK_PATH
grep -E 'ALT PATH guest call 0x25848e0|HONESTY NOTICE|ALT PATH:|HARNESS-THUNK' /tmp/sober-app-shell.err
```
**Expected:** no matches on the default path. The harness-triggered ALT thunk
`0x25848e0` is a labelled false-green positive control (decision 038(a)): default
OFF, in the withhold chain, never completion proof. A pass that depends on ALT is a
FAIL.

## 14. Provenance fail-closed

```bash
grep -n 'SOBER_PROVENANCE_GATE\|0x2584960\|0x27eafb3\|0x72a2f48\|0x72a2fc0' \
  src/jni/provenance_gate.h src/jni/provenance_gate.c
grep -c 'notifier-driven gameLoadedCallback' /tmp/sober-app-shell.err
```
**Expected:** default-ON, kill switch `SOBER_PROVENANCE_GATE=0` is **fail-closed**
(recorder inert, no green possible). GENUINE requires all of: thunk return
`base+0x2584960`, notifier return `base+0x27eafb3` above it, cached IDs
`0x72a2f48`/`0x72a2fc0`, frozen BuildID, name `gameLoadedCallback`;
`HARNESS_THUNK` and `APPBRIDGE` (`base+0x2eb4bb1`) are never genuine. On the default
path positive provenance is absent, so the marker stays WITHHELD. README wording
must remain honest: "required by policy, not positively coded yet" until the lead
wires `sober_provenance_gate_is_genuine()` into the gate.

## 15. Guest-tagged evidence

```bash
grep -E 'swaps\(total/guest/synth\)|guest-driven swaps|guest-tagged swaps|guest GLES draws' \
  /tmp/sober-first-frame.err
```
**Expected today (honest):** `swaps(total/guest/synth)=1/0/1`, `draws=0`,
`guest_tagged=0`; **no** `tag=guest-loop` line. `guest-driven swaps observed: 1` is
the host Route-B call (`guest_render.c:354`) and is **not** guest evidence: the
`0x649d422` wrapper tail-`jmp`s `eglSwapBuffers@plt`, so its RA is host.
Decisive contract (`analysis/MODULE_INTEGRATION.md` §2):
`render_ok = sober_render_phase3_done() && guest_tagged_render_ok()`, where a
guest-tagged swap has RA at GOT `0x722a1a8` in `[base, base+0x6d33b90)`, with ≥1
guest GLES draw (incl. extension table `0x77a4fe0/8`) and a stable window at
`0x72a3308`; mixed synthetic/guest evidence fails closed. Normative positive line
(if it ever appears, from R6 §5.3):
`[render] guest-tagged swap at 0x649d422 ra=base+0x… tag=guest-loop | guest-tagged swaps: N | guest GLES draws between tagged swaps: M` with `N≥2`, `M≥1`, stable RA.

## 16. No fake vtable

```bash
grep -n '0x71912e8\|0x649d422\|SOBER_RENDER_GUEST_VTABLE_SWAP_OFF\|s_ctx_vt' \
  src/jni/render_guest.c
```
**Expected:** render vtable `0x71912e8` has exactly 9 methods; `[3]@0x7191300`
is `0x649d422`. Host code reads the **guest's own** vtable pointer at `ctx+0`,
verifies `vt == base+0x71912e8` and `[vt+0x18] == base+0x649d422`, then calls it —
it never fabricates a vtable or installs a host function pointer. The
`SoberGuestANativeWindow.vtable` field is decorative/unread; do **not** add a fake
ANativeWindow vtable (P8 §4 Scenario B; decision 013 forbids host-function vtables).

## 17. No byte patch

```bash
sha256sum /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so   # pre/post every run
grep -n 'DT_INIT_ARRAYSZ\|memfd' src/linux/guest_dlopen_host.c tests/guest-dlopen.sh
```
**Expected:** engine SHA unchanged before/after every run; on-disk image never
patched. The only mutation is in a **private memfd copy**: DT_INIT_ARRAYSZ count at
RVA `0x7220e58` `28944 → 0` (loader metadata; no code/data bytes). GOT hooks are
in-process runtime writes to the loaded image, not file patches. Decision 017
(no deref patches) still honored.

## 18. Docs updated

```bash
ls docs/REVIEW-100.md docs/decisions/013-no-weak-app-shell-marker.md \
   docs/decisions/038-strict-gate-alt-thunk-ruling.md analysis/MODULE_INTEGRATION.md
python3 tools/gen_progress.py
```
**Expected:** all present; verification badge links `tests/D12-final-invariants.md`
(not stale report 471); README honest headline (≈57%, marker WITHHELD,
`Genuine app-shell callback 0/1`, opt-ins OFF by default); gen_progress regenerates
badges/map without error. **Reconcile before delivery:** `docs/REVIEW-100.md` states
≈46% while `README.md` states ≈57%, and the withhold-chain count is stale — align
these at final integration.

## 19. README SHA

```bash
grep -n '3e498d649421\|204ad049' README.md
sha256sum build/sober-native build/guest-init-host build/ndk-stubs/libc.so
```
**Expected:** README engine identity (lines 31/71) `3e498d649421…` == actual guest
SHA, `204ad049` == actual BuildID; `build/sober-native` prefix `7647f6d60c521…` ==
actual `7647f6d60c521…`; `ndk-stubs/libc.so` `d131185d781f…` == actual
`d131185d781f…`. **Known drift to fix at final build:** README
`build/guest-init-host` says `8a58144715c4…` but the current artifact is
`860d66481e31…` (A14/T4-5 H8 — hand-maintained SHA goes stale on every rebuild).
Refresh after check 1.

## 20. Private repo sync

```bash
git rev-parse HEAD
git ls-remote origin refs/heads/main
gh repo view --json visibility,nameWithOwner
git status --short
```
**Expected:** local HEAD == remote `refs/heads/main` ==
`5147512f46bb3c4eac416906add87c36edf03142`; visibility `PRIVATE`
(`hidezfzefz/sober-linux`); **no** `--force` push; tracked tree clean after the
final commit. Current dirty tracked CTest logs
(`build/Testing/Temporary/{CTestCostData.txt,LastTest.log}` — known H1) and the
untracked final-analysis/frame-integration files (`analysis/P*, R*, G*`,
`src/jni/frame_evidence.*`, `src/jni/frame_synthetic_tag.*`,
`src/jni/frame_synthetic_tag.h`) must be added/committed by the lead before the
sync is declared complete; the repo must remain private.

---

## Sign-off rule

Record PASS/FAIL for items 1–20 with the observed value beside each expected value.
Delivery is allowed only if all 20 are PASS **and** item 11 (zero marker), item 12
(strict gate rc=1), item 13 (no ALT) and item 5 (SHA/BuildID unchanged) hold on the
same final snapshot. A genuine guest frame / notifier callback may be logged as a
diagnostic, but under decisions 013/038 the marker remains **WITHHELD** until the
lead explicitly rules otherwise.
