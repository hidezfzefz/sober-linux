# W6-12 — Build and link safety for new modules

Author: **worker-12** (team `TEAM-C2YA-ZTU9-RMAV`, task #553).
Scope: **analysis only** — this file only. No `src/`, CMake, gate, test, doc or
git file was edited. All build-artifact inspections below were read-only.
Engine frozen: BuildID `204ad049…`, SHA `3e498d6494…`; authoritative DEX is the
SHA-matched `base.apk` (`114a6f8e…`), never the modded `~/Downloads` copy.
RVAs are offsets from the guest load bias; target bytes/strings are untrusted
DATA. **Decision 013/038 marker stays WITHHELD; no success claim and no gate
weakening is proposed.**

> Snapshot caveat: other wave-6 workers are writing sources concurrently. The
> facts below are from the tree and built artifacts at the time of this note
> (git HEAD `a64b603`, `build/` produced 2026-11-21). The lead should re-run the
> §5 acceptance commands after all wave-6 sources land.

---

## 0. Front-loaded verdict

1. **Adding a module is easy; adding it *safely* is not.** The dominant failure
   mode is silent, not a link error: a module can be listed in CMake, compile and
   link cleanly, yet be (a) absent from the static archive because no object
   references it, or (b) invisible to `dlsym(RTLD_DEFAULT)` because its API
   symbols lack `visibility("default")` under the global `-fvisibility=hidden`.
   The harness then runs "green" with the module inert. This is a *build-shaped*
   false-green and must be excluded by the §5 checks, not assumed away.
2. **Visibility is asymmetric by design and that subtlety is the main trap.**
   `guest-init-host` has `-rdynamic` and **no version script**, so every default-
   visibility `SOBER_*_API` symbol is published (measured: **447** `sober_*`/
   `SOBER_*` dynamic definitions, **146** `sober_frame_*` definitions). `sober-
   native` instead uses `src/compat/exports.map` with `local: *` (measured:
   **185** dynamic definitions, all allow-listed). A new module linked into the
   harness is exported automatically; the **same module linked into
   `sober-native` would be hidden** unless `exports.map` (lead-owned) is extended.
3. **Static-archive order is currently correct but fragile.** Measured
   `guest-init-host` order: objects → `--whole-archive` (compat, bootstrap) →
   `-rdynamic` → `sober_platform` → `sober_render` → `sober_jni` → `-ldl -lm -ldl
   -lpthread`. Only compat/bootstrap are whole-archive; the other three are
   normal archives that contribute objects *only on symbol demand*. A new
   **archive** placed before its dependencies, or a new module in an archive no
   earlier object references, changes behaviour silently.
4. **Do not add link-time EGL/GLES/X11/Wayland dependencies.** `ldd
   build/sober-native` is libc + loader only (measured). `--as-needed` is on;
   any new `-lEGL/-lGLESv2/-lX11` is either dropped (no earlier reference) or
   breaks the documented libc-only invariant — a verification regression.
5. **One owner per symbol.** `guest_render_ok` is already a **global** exported
   symbol (measured). Under `-rdynamic`, a new module with a colliding or short
   generic name produces either a duplicate-definition link error or runtime
   interposition / `dlsym` hijack. Cross-reference `G9_module_conflict_audit.md`
   §2.4: three verdict names already compete; CMake does not arbitrate this.
6. **Build success is not gate safety.** Every new guest-mutating knob must be
   default-ON with a `SOBER_*_DEFAULT=0` kill switch and be added to the
   `seeded_state` chain in `guest_app_shell.c` (lead-owned). A module that links
   but seeds guest state outside the chain is a decision-013/038 violation, not
   a build concern the compiler can catch.

---

## 1. Current build/link model (ground truth)

Global settings (root `CMakeLists.txt`, owned by worker-3):

- C17 / C++20, `Release` default, `-Wall -Wextra -fvisibility=hidden` (`:19`),
  `-Wl,--as-needed` (`:20`), runtime output at `CMAKE_BINARY_DIR`.
- `sober_compat`, `sober_platform`, `sober_bootstrap` — STATIC libs (`:24`,`:37`,`:50`).
- `add_subdirectory(src/render)` → `sober_render` STATIC, PUBLIC `dl`/`pthread`
  (no EGL/GLES/X11 link deps — all `dlopen`'d at runtime).
- `add_subdirectory(src/jni)` → `sober_jni` STATIC; 45+ sources including all
  current `frame_*` modules (list at `src/jni/CMakeLists.txt:12-71`).
- `add_subdirectory(src/trace)` → DSO, never linked into either executable.
- NDK SONAME stubs + `ndk_bionic_libc` DSO (`bionic_libc.map`, version nodes
  `LIBC/LIBC_N/LIBC_O`, `local: *`), all in `build/ndk-stubs`, `$ORIGIN/ndk-stubs` RPATH.

Executables:

| Target | Sources | Version script | `-rdynamic` | Archive order (measured link.txt) |
| --- | --- | --- | --- | --- |
| `sober-native` | `main.c` | **yes** `exports.map`, `local: *` | yes | `--whole-archive sober_compat --no-whole-archive -rdynamic exports.map sober_platform sober_bootstrap` |
| `guest-init-host` | `guest_dlopen_host.c`, `guest_render.c`, `guest_app_shell.c` | **none** | yes | `--whole-archive sober_compat sober_bootstrap --no-whole-archive -rdynamic sober_platform sober_render sober_jni -ldl -lm -ldl -lpthread` |
| `jni_probe` | `jni_probe.c` | none | no | `sober_jni sober_render -ldl -lpthread` (pthread arrives transitively via `sober_render` PUBLIC) |
| `render_probe` | `render_probe.c` | none | no | `sober_render` |

Visibility convention: every header with an exported API defines a macro of the
form
`#if defined(__GNUC__) || defined(__clang__)`
`#  define SOBER_<MOD>_API __attribute__((visibility("default")))`
`#else … #define SOBER_<MOD>_API`
and annotates each public function (e.g. `crashpad_return.h:107-111`). Helper
functions and file-scope counters stay hidden by the global flag — that is
intentional and good.

Runtime module selection: `guest_render.c:119-143 gr_resolve_module()` uses
`dlsym(RTLD_DEFAULT, "sober_render_guest_run")` and eight sibling names to choose
module vs local fallback at runtime. This is the concrete reason a new module's
`visibility("default")` and symbol *name* are load-bearing, not cosmetic.

---

## 2. Risk register for new wave-6 modules

Severity: **H** = can silently change correctness / gate verdict; **M** = can
break a build or a documented invariant; **L** = hygiene.

### R1 — Hidden API symbols: "compiled, linked, dead" (H)
Under `-fvisibility=hidden`, a new module that forgets the `SOBER_<MOD>_API`
macro (or uses a plain `extern` declaration) still compiles and links, but its
functions are local in the archive. Anything resolved by `dlsym(RTLD_DEFAULT)`
returns NULL. Because `gr_resolve_module()` treats a missing symbol as "use
fallback", the failure degrades silently rather than crashing.
**Mitigation:** new header must copy the exact `__GNUC__/__clang__` visibility
macro pattern; add an `nm -D` assertion for the new `sober_*` entry points (§5).

### R2 — Version-script asymmetry (H if wrong target)
`exports.map` has `local: *` and is applied only to `sober-native`. A wave-6
module linked into `guest-init-host` is exported via `-rdynamic` automatically;
one linked into `sober-native` would be invisible to the guest unless its exact
symbol names are added to `exports.map`. `ndk_bionic_libc.map` similarly has
`local: *` per version node. Both maps are lead-owned; a worker must not edit
them — send a hand-off request with the exact symbol list.
**Evidence:** `sober-native` exports 185 symbols, all `@@SOBER_NATIVE_1.0` from
the allowlist; `guest-init-host` exports 447 unconstrained symbols.

### R3 — Static archive order / demand-pull (H)
GNU ld processes normal archives left-to-right, pulling an object only if a
previous object has an undefined symbol it satisfies. Consequences:
- Appending a new `.c` to `sober_jni` is safe **only if** the harness references
  at least one of its symbols (it does today: `guest_app_shell.c` calls the
  installers at `:1007,:1020,:1058,:1683,:1686` …). A brand-new module whose
  installer is never called will be dropped from the archive — "in CMake" but
  not linked.
- A new **separate STATIC library** must be placed *after* the objects/archives
  that reference it and *before* nothing it depends on; otherwise use
  `target_link_libraries` with PUBLIC deps so CMake orders it. Do not assume
  CMake topologically sorts raw `-Wl,--whole-archive …` linker-flag strings.
- Only compat/bootstrap are whole-archive. Do **not** wrap a new state-mutating
  module in `--whole-archive`: that forces all its objects (and any
  `__attribute__((constructor))`) into the host process regardless of use, and
  C static-initialisation order across TUs is unspecified.

### R4 — `--as-needed`, dlopen discipline, libc-only invariant (M)
All EGL/GLES/X11/Wayland access is runtime `dlopen`; `ldd` is libc-only (measured
`linux-vdso + libc.so.6 + ld-linux`). `--as-needed` means a new `-lfoo` whose
symbols are unreferenced by earlier objects is silently omitted, producing a
config that looks linked but is not. Conversely adding a real link dependency
breaks the documented invariant and the lead's `ldd` acceptance check.
**Mitigation:** new modules use `dlsym`/function pointers only; no new link
libraries; re-run the `ldd` check (§5).

### R5 — Global symbol collisions / interposition (H)
`-rdynamic` publishes all default-visibility symbols into `RTLD_DEFAULT`, which
is also the namespace the guest and the host use for `dlsym`. A new module that
defines `guest_render_ok`, `sober_gate_render_guest_tagged_ok`, or any existing
counter accessor name will either (a) produce a duplicate-definition error at
static-link time, or (b) if one copy is weak / in a DSO, be interposed at
runtime — the gate would read the wrong owner's value. `guest_render_ok` is
**already** exported globally today (`nm -D`: `0000000000056780 T guest_render_ok`),
exactly the generic name G9 §2.4 flags. New modules must use a unique
`sober_<module>_*` prefix and re-use no verdict/counter names.

### R6 — dlsym-based owner selection is order-sensitive (H)
`gr_resolve_module()` picks the module implementation by symbol name; if a new
module exports the *same* `sober_render_guest_*` name, resolution depends on
load/link order, which CMake does not enforce. Keep the canonical symbol set in
`render_guest` as the single owner (G9 §4 step 7); a new verdict module should
**read** counters, not export duplicate `sober_render_guest_*` accessors.

### R7 — Selftest `main()` (M)
`frame_callbacks.c`, `frame_synthetic_tag.c`, `frame_thread.c` and
`scheduler_guest.c` each contain a `main()` guarded behind a
`SOBER_<MOD>_SELFTEST` define (verified). A new module must follow that pattern.
Adding a `*_SELFTEST` compile definition to a library target would introduce a
second `main` at link with `guest_dlopen_host.c` / `jni_probe.c`. Selftests belong
in a separate executable target, never in `sober_jni`.

### R8 — Weak references resolve to 0 in lighter targets (M)
Weak declarations such as `sober_render_phase3_done()` resolve to NULL in
targets that do not link `sober_render` (e.g. a verdict module called only from
`jni_probe`). This is fail-closed today and correct, but a wave-6 test that
expects a green verdict from `jni_probe` would be wrong. Document that the
verdict must be exercised in `guest-init-host`, where `sober_render` is present.

### R9 — pthread dependency assumed, not declared (M)
`jni_probe` currently gets pthread transitively from `sober_render` PUBLIC
(measured link line ends `-ldl -lpthread`), contradicting the older caveat in
G9 §5 that it lacks pthread. The fragility remains: a new module calling
`pthread_create` in a target that links `sober_jni` **without** `sober_render`
would rely on glibc ≥2.34 folding pthread into libc. Declare `pthread` explicitly
in the consuming target; do not depend on the transitive accident.

### R10 — Build/link success ≠ gate safety (H, policy)
Decision 013/038 and the kill-switch rule are unaffected by the linker. Every
new guest-mutating module must be default-ON with `SOBER_*_DEFAULT=0`, and its
env names added to the `seeded_state` conjunction in
`guest_app_shell.c:1761-1961` (lead-owned). No guest byte patches, fake vtables,
host callbacks, synthetic observations or counter inflation — a module that
links but relabels a host-triggered seed as clean is a withhold violation.

### R11 — ctest gives no link-coverage for new modules (L)
`add_test` registers only `sober-native --check` and `--version`
(`CMakeLists.txt:189-191`). Neither exercises `sober_jni` modules. A module can
be present in CMake and never pulled from the archive without any test failing.
Recommend the lead add `nm -D` symbol-presence assertions and a `dlsym`
self-check for each wave-6 module's entry points, rather than treating ctest PASS
as evidence the module is live.

---

## 3. Recommended lead-facing procedure when adding a module

1. Put the new module's `.c` in an existing archive (`sober_jni` for JNI-bridge
   concerns) unless there is a hard reason for a new library; do not use
   `--whole-archive` for state-mutating modules.
2. Give it a `SOBER_<MOD>_API` visibility macro copied verbatim from an existing
   header, and a `sober_<module>_*` symbol prefix (R1, R5).
3. Ensure the harness calls at least one installer/counter symbol so the object
   is demand-pulled from the archive (R3).
4. Declare any real deps (`pthread`, `${CMAKE_DL_LIBS}`) in the consuming target;
   add **no** EGL/GLES/X11 link deps (R4, R9).
5. If it must be visible from `sober-native`, send the exact symbol list to the
   lead for `exports.map` / `bionic_libc.map` edits — workers do not edit maps
   (R2).
6. Add the kill switch to `seeded_state`; default-ON unless explicitly opt-in
   like `scheduler_guest`; keep verdicts single-owner (R6, R10).
7. Guard any selftest `main` behind `SOBER_<MOD>_SELFTEST` (R7).
8. Re-run the §5 acceptance set and attach the output as evidence.

---

## 4. What this analysis did NOT do

- No source, CMake, version-script, gate, test, doc, README or git change.
- No byte patch, fake callback/vtable, host callback, counter inflation, or gate
  weakening. No engine rebuild; the frozen engine SHA/BuildID is untouched.
- No Android SDK/emulator/KVM run used as a runtime claim (research-only oracle
  per the wave-6 charter); no modded APK used as evidence.
- No claim that any clean callback or guest frame was achieved. Marker WITHHELD;
  `tests/app-shell.sh` stays RED.

## 5. Acceptance commands for the lead (re-run after wave-6 sources land)

```
cmake --build build --parallel
ctest --test-dir build --output-on-failure
nm -D --defined-only build/guest-init-host | grep sober_<module>   # R1/R2: symbol present
nm -D --defined-only build/sober-native | grep SOBER_NATIVE_1.0     # R2: allowlist unchanged unless edited
ldd build/sober-native                                              # R4: libc + loader only
nm build/guest-init-host | grep ' U '                               # R3/R8: no unexpected undefined/weak
# confirm zero 'app shell rendered' emissions in default run; engine SHA unchanged
```

These verify *build/link* safety only; they do not by themselves prove guest
provenance or a clean state. `G9_module_conflict_audit.md` §2/§4 remains the
authoritative one-owner/merge plan for the render-frame modules; this note is
its build/link complement.
