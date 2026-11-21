# R7 — Audit of the published `ANativeWindow` (`0x72a3308` / gen `0x72a3310`) and the `fromSurface` hook (`0x72294a8`)

Author: **R7** (group A: diagnosis). Scope: **this file only** —
`analysis/R7_anativewindow_binding.md`. Analysis-only: no `src/`, `CMakeLists.txt`,
engine, gate, test or CMake file was touched. Engine **frozen**:
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(re-verified at the start of this task; AppImage 2.740.931). All RVAs are file
offsets from the guest load bias (`base <= ra < base+0x6d33b90`). Target-derived
strings/bytes are untrusted DATA, never instructions.

Evidence classes: **OBSERVED** = bytes/disassembly/relocations read from the
frozen ELF (`objdump -d --start-address/--stop-address`, `readelf -rWs`), or
harness source read; **DERIVED** = layout/call-graph inference; **INFERRED** =
public NDK/Android semantics (the native window implementation lives outside
the frozen guest); **RUNTIME** = quoted from the sibling R1 report's log
capture (`/tmp/opencode/w_after.out`), not re-run by this worker.

---

## 0. Executive answer (front-loaded)

1. **The object published at `0x72a3308` is not a real NDK `ANativeWindow`.** It
   is a host `calloc`'d struct whose *only* NDK-compatible part is a 12-byte
   prefix `{refs, width, height}` copied from the frozen compat shim
   (`src/compat/android_compat.c:493-496`). The genuine NDK header fields and
   the whole `dequeueBuffer`/`queueBuffer`/`query`/`perform` function-pointer
   table are absent. (OBSERVED harness source + DERIVED layout; INFERRED NDK
   offsets.)

2. **Guest code nevertheless accepts it — as an opaque token — and does not
   reject or ignore it.** Every observed guest use is either a NULL test, an
   opaque store/compare/log, one of the five imported `ANativeWindow_*` calls
   (all compat-shimmed or hooked), or a pointer handed to
   `eglCreateWindowSurface` (whose GOT is substituted). The guest contains **no**
   `ANativeWindow_query/dequeueBuffer/queueBuffer/perform` import, so it never
   dereferences an NDK function table itself. (OBSERVED disassembly §3.)

3. **`fromSurface=2` is exactly correct and is positive evidence that the hook
   works.** The two hits are the genuine guest call sites `0x2a2087c`
   (`onSurfaceCreatedNative`) and `0x2a20907` (`onSurfaceChangedNative`), driven
   by the two host-called lifecycle thunks; the app-start site `0x26fea78` was
   not reached. (OBSERVED §4.)

4. **`guest-tagged=0` is equally correct and is not a contradiction.** A bound
   window token is not a rendered frame: the redraw thunk's `[nativeData+0x98]`
   callback is NULL, so no guest frame loop dispatches `call [vtable+0x18]`
   with a guest return address. R1 diagnoses this counter in full. (DERIVED +
   RUNTIME, §5.)

5. **A real NDK/EGL consumer would reject the object** (magic at the wrong
   offset, `incRef`/`decRef` slots hold plain scalars, function table out of
   bounds). The harness never lets that happen: compat getters read the 12-byte
   prefix and `eglCreateWindowSurface` is replaced with an X11-handle shim. So
   the fake is *shielded*, not *validated*. (§2/§3/§6.)

6. **The decision-013 gate does not validate the window structure at all** —
   its conjunct is a bare `*(void **)(base+0x72a3308) != NULL`
   (`guest_app_shell.c:1777-1780`), trivially satisfied by render_guest's own
   host store. Marker stays **WITHHELD**; nothing here is app-shell proof.

---

## 1. What is actually published at `0x72a3308` / `0x72a3310`

### 1.1 Canonical path (`source=module` in the observed run)

`src/jni/render_guest.c:350-469` `sober_render_guest_install()`:

* `s_anw = calloc(1, sizeof(SoberGuestANativeWindow))` (`:375`) — **host heap, not
  guest allocator**, 0x60 bytes on x86-64 (see §2.2).
* `refs = 0x10000` (`:387`) — deliberately huge so guest pause/destroy releases
  cannot free it (`sober_render_anw_release`/compat `free` at zero).
* `width/height` from `cfg` or `SOBER_WINDOW_WIDTH/HEIGHT` (defaults 1280×720)
  (`:380-389`).
* `magic = 0x41574e57` (`'AWNW'`), `version = 1` at `+0x0c/+0x10` (`:390-391`).
* `vtable = base + 0x71912e8` (`:392`) — note this is the **render context
  vtable**, not an ANativeWindow vtable; it is a mislabelled tag (§2.2).
* Publish: `*(uintptr_t*)(base+0x72a3308) = s_anw` (`:420-421`), then
  `__atomic_fetch_add(base+0x72a3310, 1)` (`:422-423`). This mirrors the guest's
  own store `0x26feaa0` and increment `0x26fea90`, but is a **host write** — no
  guest instruction executed it.
* `rg_hook_from_surface()` (`:239-252`) returns the **same** `s_anw` and does
  `++s_anw->refs` — a plain integer, not `incRef`.

Then `rg_bind_surface()` (`:289-331`) calls the four lifecycle thunks and, at
`:326-329`, re-publishes `0x72a3308 = s_anw` after the replay. Generation is
**not** bumped again (the thunks never touch it).

### 1.2 Local fallback path (`source=local`, module not linked)

`src/linux/guest_render.c:148-225` builds a second, structurally identical
`s_gr_anw`, publishes it at `0x72a3308` and bumps gen (`:192-194`). But the
installed fromSurface hook here is **native_render's** `sr_hook_anw_from_surface`
(`src/render/native_render.c:991-999`), which returns
`sober_render_get_active()->window` — a `SoberNativeWindow*`
(`native_render.c:331-341`), **a different object** from the published
`s_gr_anw`. Both share the compat `{refs,w,h}` prefix, so they remain
ABI-compatible with compat `getWidth/getHeight/release`, but the identity
diverges. In the observed run `source=module`, so this divergence is latent, not
active; it matters for A/B diagnostics.

### 1.3 Generation counter semantics (OBSERVED)

Guest `0x26fea5a` increments `0x72a3310` **only when the returned pointer differs
from the stored one** (`0x26fea8a je 26feb16`, `incl` at `0x26fea90`). The harness
increments once at install and re-publishes the same pointer, so a stable
generation of 1 is expected. If the `0x26fe80f`/`0x2773194` app-start path were
also driven, `fromSurface` would return the same pointer and the guest would log
*“Same native window … No need to do anything!”* (`0x46f987`) with **no**
generation increment — a useful consistency check (§7).

---

## 2. Structural acceptability — three different consumers

### 2.1 To guest code: **accepted** (opaque handle, zero validation)

The guest only imports five ANativeWindow symbols (OBSERVED `objdump -T` /
`readelf -Ws`, all `DF *UND*`):

```
ANativeWindow_release  GOT 0x72294a0   ANativeWindow_fromSurface  GOT 0x72294a8
ANativeWindow_acquire  GOT 0x72297c0   ANativeWindow_getHeight    GOT 0x72297c8
ANativeWindow_getWidth GOT 0x72297d0
```

There is **no** import of `ANativeWindow_query`, `dequeueBuffer`, `lockBuffer`,
`queueBuffer`, `cancelBuffer`, `perform`, or `setSwapInterval`. The guest therefore
never touches the NDK function-pointer table itself; those slots are consumed by
EGL/libnativewindow on a real Android system, which this harness replaces.

### 2.2 NDK layout vs synthetic layout (DERIVED / INFERRED)

Real NDK `ANativeWindow` offsets below are **INFERRED from the public
`android/native_window.h` / `android_native_base_t` definitions**, not read from
the frozen guest (the guest does not define the struct). They are stable across
Android versions at least for `common` and the leading scalars. Synthetic offsets
are **OBSERVED** from the actual compiled struct layout (verified with a local
`offsetof` compile, since the header comments are partly wrong — see below).

| Field | Real NDK (`android_native_base_t` + body) | `SoberGuestANativeWindow` (actual ABI) | Consequence |
| --- | --- | --- | --- |
| magic | `+0x00` (`ANATIVEWINDOW_MAGIC`) | `+0x0c` = `0x41574e57` | Magic at wrong offset; real validator rejects |
| version | `+0x04` | `+0x10` = 1 | Wrong |
| reserved[2] | `+0x08` | `+0x14` | Wrong |
| `incRef` fn ptr | `+0x10` | `+0x10` = `version=1`, high half `reserved=0` → `(void*)0x1` | Real `ANativeWindow_acquire` would call address 1 → fault |
| `decRef` fn ptr | `+0x18` | `+0x18` = `native_handle` (X11 window / wl_egl_window*) | Real release would call a non-code pointer |
| flags | `+0x20` | `+0x30` = `native_handle` | Wrong |
| handles | `+0x28` | `+0x38` = `egl_display` | Wrong |
| format | `+0x30` | `+0x54` | Wrong |
| min/maxSwapInterval | `+0x34/+0x38` | `+0x58/+0x5c` | Wrong |
| `setSwapInterval` … `query`/`perform` | `~+0x64 … +0x8c/+0x94` | **past end of struct** (`sizeof = 0x60`) | Reads adjacent host heap; no buffer ops at all |
| refcount | `incRef`/`decRef` virtual ops | plain `uint32_t refs` (0x10000) | Not NDK refcount semantics |
| width/height | via `query(window, NATIVE_WINDOW_WIDTH/HEIGHT, &v)` | literal `int32` at `+0x04/+0x08` | Real `ANativeWindow_getWidth` derefs `query` → junk |

**Minor finding:** `src/jni/render_guest.h` comments the fields as
`vtable +0x1c`, `native_window +0x24`, … `max_swap +0x58` and implies a 0x5c-byte
object. Actual x86-64 ABI inserts 4 bytes of padding after `reserved[2]` to
align `void *vtable`: the real offsets are `vtable +0x20`, `native_window +0x28`,
… `max_swap +0x5c`, `sizeof = 0x60`. No code reads those offsets (all are
write-only decoration, §2.4), so there is **no functional impact**, but the
comment/metadata is inaccurate and should not be relied on for offset checks.

### 2.3 To the compat shim / hooked GOT functions: **accepted by construction**

`struct ANativeWindow { unsigned refs; int32_t width, height; }`
(`android_compat.c:493-496`) and the acquire/release/getWidth/getHeight bodies
(`:508-513`) read exactly `+0x00/+0x04/+0x08`. `SoberGuestANativeWindow` (and
`SoberNativeWindow`, `native_render.c:331-341`) reproduce those three words, so
the still-installed compat functions operate on the fake correctly: `refs`
increments/decrements, `getWidth/getHeight` return 1280/720. This is ABI
compatibility with **our shim**, not with the NDK.

The only remaining "real NDK" consumer in the guest path is
`eglCreateWindowSurface(dpy, cfg, window, attribs)` — OBSERVED at
`0x649d1df-0x649d1e9`, where the window is loaded from `ctx+0x18` into `RDX` and
passed to `eglCreateWindowSurface@plt` (`0x6d33470`, GOT `0x722a178`). The
GOT/compat shim substitutes the real X11 handle
(`sr_hook_egl_create_window_surface`, `native_render.c:1003-1012`, installed via
`sober_fn_eglCreateWindowSurface` at `guest_render.c:317-321` /
`render_guest.c:426-430`), so the fake object's layout is never interpreted by
EGL. (DERIVED from OBSERVED code.)

### 2.4 Write-only decoration

`vtable`, `magic`, `version`, `reserved`, `flags`, `format`, `min_swap`,
`max_swap`, and all the `egl_*` mirrors are **written but never read** anywhere
in `render_guest.c`, `guest_render.c`, or the guest (`grep` whole `src/`). Only
the pointer value and the compat prefix words matter. Even a perfect NDK header
painted onto this object would not change guest behaviour in the current harness.

---

## 3. Guest code walk — where it accepts, NULL-checks, or skips (OBSERVED)

| RVA | Action on the window / object | Validation of structure? |
| --- | --- | --- |
| `0x26fea78` | `call ANativeWindow_fromSurface@plt`; result → `rbx` | none; compare `old==new`, else `incl gen`, `release(old)`, `mov %rbx,0x72a3308`, log; NULL-tested only at `0x26feb75` for the release |
| `0x26fe93d` / `0x277324b` | window embedded at `native_params+0x00` | none (opaque copy) |
| `0x2a20850` (created) | release old `[nd+0x140]`; fromSurface `0x2a2087c` → `[nd+0x140]`; `test rax` at `0x2a20888`; then `test [nd+0x88]` at `0x2a20894` | NULL checks only; callback `+0x88` is NULL in synthetic `nativeData` → `jmp *%rcx` at `0x2a208a6` skipped |
| `0x2a208c0` (changed) | `ANativeWindow_acquire(old)` `0x2a208e5`; `release(old)`; fromSurface `0x2a20907`; `test`/compare; `[nd+0xa0]` and `[nd+0x88]` NULL-checked (`0x2a2092b`, `0x2a2094f`); `getWidth/getHeight` `0x2a20963/0x2a20975` → `[nd+0x148/+0x14c]`; on a dimension change, `[nd+0x90]` NULL-checked at `0x2a209be` and called at `0x2a209ce` | NULL checks + width/height via compat shim; all callbacks NULL → indirect calls at `0x2a20933/0x2a20957` (and `0x2a209ce`) skipped |
| `0x2a20a00` (redraw) | load `[nd+0x140]` → `rsi`, `test`; load `[nd+0x98]` → `rax`, `test`, `jmp *%rax` at `0x2a20a20` | NULL checks only; `[+0x98]==NULL` → **frame loop never entered** |
| `0x2a20a30` (destroyed) | load `[nd+0x140]`; `[nd+0xa0]` NULL-checked (`0x2a20a4f`); `ANativeWindow_release` `0x2a20a65`; zero `[nd+0x140]` | NULL checks only; high `refs` keeps window alive |
| `0x649c21e → 0x649c272` | alloc 0x48 ctx, vptr `0x71912e8` at `+0`, window at `+0x18`, `ANativeWindow_acquire` `0x649c2d6`, `getWidth/getHeight` `0x649c303/0x649c30f` | no structural validation; size used only for EGL config |
| `0x649d1c2` | `eglCreateWindowSurface(dpy, cfg, [ctx+0x18], 0)` `0x649d1e9` | window interpreted by EGL — **substituted by hook** |
| gate `guest_app_shell.c:1777-1780` | `*(void**)(base+0x72a3308) != NULL` | bare non-NULL; satisfies conjunct with a host-written pointer |

**Conclusion:** no guest code rejects the object. The only branch that could be
read as "rejection" is `test rax` at `0x2a20888` / `0x2a20917` etc., which fires
only on a NULL pointer; our pointer is non-NULL. The only code that *ignores* it
structurally is the guest itself (it never dereferences NDK fields) — but it does
not ignore the *binding*: it stores and reuses it across all four lifecycle
thunks and EGL init.

---

## 4. Why `fromSurface=2` (and why that is honest)

The hook at GOT `0x72294a8` is installed and entered; `s_from_surface_calls`
(`render_guest.c:61,242`) counts entries. The four thunks called by
`rg_bind_surface()` produce:

| Thunk | fromSurface call site | Calls |
| --- | --- | --- |
| created `0x2a20850` | `0x2a2087c` | 1 |
| changed `0x2a208c0` | `0x2a20907` | 1 |
| redraw `0x2a20a00` | none (reads `[nd+0x140]`) | 0 |
| destroyed `0x2a20a30` | none (releases `[nd+0x140]`) | 0 |
| **total** | | **2** |

The third guest call site, `0x26fea78` (reached from
`0x26fe80f` StartAppWithParams and `0x2773194` UpdateSurface), is **not** reached
in this run — consistent with R1/C08/A09 (the field-slot path is `sr_stub`).
`fromSurface=2` therefore proves: (a) the GOT hook is live, (b) two genuine guest
call sites ran, (c) the returned token was stored into guest state
(`[nativeData+0x140]`) and then consumed by acquire/getWidth/getHeight/release.
It does **not** prove a Java `Surface`, an NDK window, or a frame.

Note also the counter's identity semantics: `rg_hook_from_surface` returns the
same `s_anw` each time and increments its integer `refs`; the guest's own
`0x26fea5a` "same window" branch (which would log `0x46f987`) is not involved for
these two lifecycle calls.

---

## 5. Why `guest-tagged=0` is consistent with a bound window

R1's report (`analysis/R1_zero_tagged_swaps.md`) establishes this in detail; the
short version, confirmed against source and disassembly here:

* The only swap at GOT `0x722a1a8` is the **host's** direct call to `0x649d422`
  (`guest_render.c:354`, Route B with a 0x48-byte stack context). Its RA is in
  host code, so `rg_hook_swap` correctly increments `s_swaps_synthetic`, not
  `s_swaps_guest`.
* `0x649d422` ends in a tail `jmp` to `eglSwapBuffers@plt`, so the RA at the GOT
  is the caller's. A guest-tagged swap requires a real guest `call [vtable+0x18]`
  through `[0x71912e8+0x18] = 0x7191300 = 0x649d422`.
* No such call occurs because the redraw callback `[nativeData+0x98]` is NULL
  (`render_guest.c:402-404`), and `0x2a20a00` skips `jmp *%rax` at `0x2a20a20`
  (OBSERVED above). No guest GLES draw dispatcher (`0x6496d0c`) runs →
  `draw_calls=0`.

A window that is *bound* but never *drawn to* is exactly the expected state; the
two counters measure different things. Neither is a bug to paper over.

---

## 6. What a genuine surface would require

Layers that are currently replaced or absent (INFERRED from NDK/Android
semantics; guest-side requirements OBSERVED in C08 §1-4):

1. **Real Java `Surface`.** `ANativeWindow_fromSurface(JNIEnv*, jobject)` reads
   the private native handle (`mNativeObject:J`) of `android.view.Surface` and
   resolves it to an `android::Surface*` with a live `IGraphicBufferProducer`.
   It is **not** a Java getter. Our `s_surface_token[2]` (`render_guest.c:57,
   415`) is an opaque address, so a faithful NDK implementation would return
   NULL for it. A genuine object must be created by Android/ART, not `calloc`'d.
2. **Real NDK `ANativeWindow`**: correct `common` header (`magic` at `+0x00`,
   version, `reserved`, non-NULL `incRef`/`decRef` into `libandroid.so`), the full
   `setSwapInterval`/`dequeueBuffer`/`lockBuffer`/`queueBuffer`/`cancelBuffer`/
   `query`/`perform` table, and a backing buffer queue. Real
   `getWidth/getHeight` go through `query()`, so scalar `+0x04/+0x08` alone are
   insufficient.
3. **JNI field slots in the bridge.** The guest reads `PlatformParams` via
   `GetFieldID`/`GetFloatField`/`GetIntField` at env offsets `0x2f0/0x330/0x320`
   (`0x26feb9b`), and a faithful `fromSurface` would need `GetLongField` for
   `mNativeObject`. Those slots are `sr_stub` in `src/jni/native_jni.c` (C08 §3.3).
4. **A real EGL consumer**, i.e. `eglCreateWindowSurface` at GOT `0x722a178`
   interpreting the ANativeWindow instead of being substituted with an X11
   handle; plus a genuine frame loop that dispatches `call [vtable+0x18]` with a
   guest RA and issues GLES draws.
5. **Genuine nativeData callbacks** `[+0x88]/[+0x98]/[+0xa0]` plus the
   dimension-change callback `[+0x90]` (OBSERVED at `0x2a209b4-0x2a209ce`), from
   a live GameActivity surface object driven with a real `Surface` jobject, so
   the redraw/created/changed paths do not terminate at the NULL checks in §3.
   (Corroborated by sibling report `analysis/P8_anw_guest_consumption.md` §0/§6.)
6. **Decision 013:** even with 1-5, the marker additionally needs the genuine
   notifier-driven `gameLoadedCallback(J)V` (notifier `0x27eaf6e` → thunk
   `0x25848e0`) with all seed knobs off. No surface-side change can lift it.

---

## 7. How to observe a genuine binding / frame (concrete checklist)

Ordered from cheap structural checks to decisive provenance checks:

1. **Object memory at the published pointer** (`base + read64(base+0x72a3308)`):
   * `+0x00 == 0x41574e57` (real NDK magic at offset 0, not `+0x0c`);
   * `+0x10`/`+0x18` are non-NULL code pointers inside a mapped native library
     (`incRef`/`decRef`), not `0x1`/an X11 handle;
   * the `query` slot is callable and returns 1280/720 for
     `NATIVE_WINDOW_WIDTH/HEIGHT`; refcount changes across `acquire`/`release`.
   Today: fails on every one — proof it is not a genuine NDK object.
2. **Guest provenance of the store.** Require the return address of
   `ANativeWindow_fromSurface` to be inside guest `.text` at `0x26fea78`,
   `0x2a2087c` or `0x2a20907` (OBSERVED call sites), and a generation delta at
   `0x72a3310` driven by `incl` at `0x26fea90` — not by host
   `render_guest.c:422-423`. Today: the publish is a host write; the two fromSurface
   hits are real but come from host-driven thunks.
3. **Hook all five GOT slots together** (`0x72294a8`, `0x72294a0`, `0x72297c0`,
   `0x72297c8`, `0x72297d0`), as C08 §4 Phase A step 3 requires; hooking only
   `fromSurface` sends the object into compat `release` at `0x26fea9b`.
4. **Surface identity.** Observe a real `android.view.Surface` jobject with a
   valid `mNativeObject`; the `surface()` getter path `0x247b465` →
   `CallObjectMethodV` must return it, and a faithful `fromSurface` must derive
   the window from it rather than ignore the argument.
5. **Lifecycle callbacks non-NULL.** Check `[nativeData+0x88]/[+0x98]/[+0xa0]`
   (and `[+0x90]` for a dimension change) hold guest engine callbacks and are
   actually entered (guest RA) when
   `0x2a20850/0x2a208c0/0x2a20a00/0x2a20a30` run.
6. **Decisive render predicate** (T4-4 R1-R4 / R1 §5): a guest-tagged swap at
   GOT `0x722a1a8` with RA in `[base, base+0x6d33b90)` from a real
   `call [vtable+0x18]` at `0x7191300`, plus at least one counted guest GLES draw
   from the wrapped pointers loaded via `0x722a180` (dispatcher `0x6496d0c`),
   with a stable RA across ≥2 tagged swaps. Current state `0/0` fails closed.
7. **Gate honesty.** The `0x72a3308 != NULL` conjunct must be treated as
   satisfied-by-seed until item 2 is proven; do not count it as guest evidence.
   Every diagnostic knob that seeds these objects must stay in the
   `seeded_state` force-withhold chain (`guest_app_shell.c:1336-1340`, etc.).

---

## 8. Limits, honesty, and final statement

* **Engine frozen**; SHA-256/BuildID re-verified. No guest bytes were patched by
  this worker; all GOT writes discussed are those of the existing harness source.
* NDK struct offsets in §2.2 are **INFERRED** from public Android headers; the
  frozen guest does not define the struct and imports only the five functions.
  The synthetic offsets are OBSERVED/compiled locally. The *direction* of the
  mismatch (magic at `+0x0c` instead of `+0x00`; scalar where `incRef` belongs;
  no function table) is robust to version differences.
* No runtime was executed by this worker. The `fromSurface=2` /
  `guest-tagged=0` values are OBSERVED in the sibling R1 report's captured log
  (`w_after.out`) and are fully explained by the source/disassembly above.
* The header offset comment error (§2.2) is a documentation defect only; no code
  reads those offsets, so it is not a correctness bug.
* **Final ruling:** the published object is structurally acceptable **only** to
  our own compat/hook layer and to guest code that treats it as an opaque token;
  it is not a genuine NDK `ANativeWindow` and would be rejected by any real
  consumer. The guest neither rejects nor ignores the *binding* (`fromSurface=2`
  is genuine hook traffic), but the absence of `guest-tagged` swaps shows it
  never produced a guest frame from it. Decision 013/038 marker remains
  **WITHHELD**.

### Evidence index (all read-only this task)
* Frozen ELF: `objdump -T` / `readelf -Ws` (five ANativeWindow imports; GOT
  JUMP_SLOTs `0x72294a0/8`, `0x72297c0/8/d0`); targeted `objdump -d` of
  `0x26fea5a-0x26feb90`, `0x2a20850-0x2a209a0`, `0x2a20a00-0x2a20aa0`,
  `0x649c21e-0x649c430`, `0x649d1c2-0x649d280`.
* Harness source: `src/jni/render_guest.c/.h`, `src/render/native_render.c`,
  `src/linux/guest_render.c`, `src/linux/guest_app_shell.c:1755-1790`,
  `src/compat/android_compat.c:490-513`.
* Prior analysis: `analysis/C08-surface-frame.md`, `analysis/C04-render-integration.md`,
  `analysis/A09-platform-params.md`, `analysis/T3_5_render-evidence.md`,
  `analysis/R1_zero_tagged_swaps.md`, `analysis/MODULE_INTEGRATION.md`,
  `docs/decisions/013-no-weak-app-shell-marker.md`.
