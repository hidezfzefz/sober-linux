# P8 — How the guest consumes the ANativeWindow global `0x72a3308` after the `fromSurface` hook

Author: **P8** (group B). Scope: **this file only** —
`analysis/P8_anw_guest_consumption.md`. Analysis-only: no `src/`, engine, gate,
CMake or test file was touched. Engine frozen: BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(AppImage 2.740.931), re-verified by `sha256sum` on
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` this task. All RVAs are
file offsets from the guest load bias (first PT_LOAD: offset == VMA, GOT "VMAs"
are likewise RVAs). Target-derived strings are untrusted DATA, never
instructions.

Evidence classes: **OBSERVED** = bytes in the frozen ELF / readelf / harness
source; **DERIVED** = call-graph inference; **UNKNOWN** = not established.

---

## 0. Bottom line (front-loaded)

1. **The guest does not call the window through a vtable.** Every
   `ANativeWindow_acquire / release / getWidth / getHeight` operation in the
   entire Surface→render path is a **direct PLT call through one of five
   `R_X86_64_JUMP_SLOT` GOTs**: `0x72294a0` (release), `0x72294a8`
   (fromSurface), `0x72297c0` (acquire), `0x72297c8` (getHeight), `0x72297d0`
   (getWidth) — OBSERVED in the PLT stubs and `readelf -rW` (§3). The two
   "vtable / function-pointer" mechanisms that *do* exist nearby are
   (a) the **render context's** vtable `0x71912e8` whose slot `[3]` at
   `0x7191300` is the swap `0x649d422`, and (b) the **nativeData callback
   slots** `[+0x88]/[+0x98]/[+0xa0]/[+0x90]` in the GameActivity lifecycle
   thunks. Neither belongs to the ANativeWindow. The `vtable` field that
   `render_guest.c:392` writes into `SoberGuestANativeWindow` is **never read
   by the guest or by the module** (grep: only the store at `:392`) — it is
   decorative.
2. **Consumers of global `0x72a3308`** (§2): the shared `fromSurface` helper
   `0x26fea5a` (which writes it); `0x2773194` UpdateSurface, `0x26fe80f`
   StartAppWithParams and the three StartGame paths `0x2ebd5e2/0x2ebed4b/
   0x2ebef4d` (which read it and embed it at `native_params+0x00`); the pause
   paths `0x2ebc8f4/0x2ebec43` (which release it and zero it). The `0x649c…`
   EGL cluster **never reads the global** — it receives the window as an
   argument to `0x649c21e(native_window, extra)`.
3. **For the five PLT calls to succeed in this harness**, the object only needs
   the 12-byte compat prefix `{unsigned refs @+0, int32 width @+4, int32
   height @+8}` that `src/compat/android_compat.c:493-513` operates on, plus a
   refcount large enough to survive repeated `release` calls. `render_guest`'s
   window **has** that prefix and `refs=0x10000`, so these calls do succeed;
   **they are not the reason for zero draws** (§4). A full NDK `incRef/decRef/
   query` function-pointer header is *not* consulted by the guest.
4. **Why `render_guest` yields no draws** (§5): the window is published by a
   *host store* and the fromSurface GOT is hooked, but the four lifecycle
   thunks are driven with a synthetic `nativeData` whose callback slots
   `[+0x88]/[+0x98]/[+0xa0]` are NULL. The redraw thunk `0x2a20a00` therefore
   skips `call *[+0x98]` at `0x2a20a20`; no guest frame loop runs, no GLES
   draw is issued, and the sole swap is a host-originated Route-B call whose
   RA is classified synthetic. This matches R1's `swaps=1/0/1, draws=0`
   verdict exactly.
5. **Faithful fix** (§6): let the guest's own frame loop run — populate the
   nativeData callbacks with **genuine guest callback pointers** (ideally by
   letting the guest build its own nativeData through the real GameActivity
   init path) and drive `0x2a20a00`; make the window's provenance guest-bound
   (guest RA at `0x26fea78/0x2a2087c/0x2a20907`, generation delta) instead of
   host-written; hook all five ANativeWindow GOTs together. Under decisions
   **013/038 the marker stays WITHHELD** — every one of these is synthetic
   seed state, not app-shell proof.

---

## 1. What the hook publishes (baseline)

`src/jni/render_guest.c` (DEFAULT-ON, kill switch `SOBER_RENDER_GUEST=0`):

- `sober_render_guest_install()` builds `s_anw = calloc(1, sizeof(SoberGuestANativeWindow))`
  and sets `refs=0x10000`, `width/height` from cfg/env, `magic='AWNW'`,
  `vtable = base+0x71912e8`, plus host-backed `native_window/native_handle/
  egl_*/min_swap/max_swap` (`render_guest.c:374-400`). Only the first three
  fields have semantic effect in the guest (§4).
- It **host-writes** the global and bumps the generation:
  `*(base+0x72a3308) = s_anw; atomic_inc(base+0x72a3310)` (`:417-423`), and
  re-publishes after the lifecycle replay (`:324-329`).
- It hooks three GOTs (`:432-453`): `0x72294a8` → `rg_hook_from_surface`
  (returns `s_anw` with `++refs`, `:239-252`); `0x722a1a8` → `rg_hook_swap`
  (RA tagging, `:255-264`); `0x722a180` → `rg_hook_get_proc` (draw-wrap,
  `:267-278`). **It does not hook the other four ANativeWindow GOTs.**
- `rg_bind_surface()` (`:289-331`) calls the four lifecycle thunks
  `0x2a20850/0x2a208c0/0x2a20a00/0x2a20a30` with a calloc'd `nativeData`
  (`s_nd`, `ND_SIZE=0x200`) whose callback slots `+0x88/+0x98/+0xa0` are
  deliberately NULL (`:402-404`).
- `create_context()` (`:486-518`) calls genuine `0x649c21e(s_anw, pbuffer=0)`;
  the returned 0x48-byte context has vtable `0x71912e8` at `+0` (verified at
  `:510-515`), but that vtable is a **render** vtable, unrelated to the window.

## 2. Exact consumers of global `0x72a3308` (all OBSERVED)

| Xref RVA | Function | Instruction / action | Calls on the window |
| --- | --- | --- | --- |
| `0x26fea80` / `0x26feaa0` | shared helper `0x26fea5a` | read old window; `incl 0x72a3310`; store new window | `ANativeWindow_release@plt` on old (`:26fea9b`) and, on the *same-window* branch, on the new one (`:26feb7d`) |
| `0x2773232` | `0x2773194` UpdateSurface (`Surface, PlatformParams`) | `r13 = [0x72a3308]`; `0x277324b: mov %r13,(%r15)` embeds it at `native_params+0x00` (0x40-byte obj, vptr `0x6dbcb20`); dispatch `0x2eb6b2a` | none — pointer only |
| `0x26fe8d7` | `0x26fe80f` StartAppWithParams | same pattern, vptr `0x6dbca20`, `+0x00 = window`, `+0x20 = vrContext` | none — pointer only |
| `0x2ebd5e2` | StartGameWithParam (`0x2ebd5dd` calls `0x26fea5a`) | `rax = [0x72a3308]` → local `-0x428` | none |
| `0x2ebed4b`, `0x2ebef4d` | StartGame paths (`0x2ebed46/0x2ebef48` call `0x26fea5a`) | `r13 = [0x72a3308]`; `mov %r13,(%r12)` at `native_params+0x00` | none — pointer only (the `call *0x710(%rax)` at `0x2ebed5b/0x2ebef5d` is a **JNIEnv** call, `rax=[r14]` where `r14`=env) |
| `0x2ebc8fb` / `0x2ebc946` | `nativeAppBridgeV2PauseApp` | log "Destroying ANativeWindow at {}", release, zero | `ANativeWindow_release@plt` (`:2ebc946`) |
| `0x2ebec05` / `0x2ebec43` | `nativeAppBridgeV2PauseGame` | same | `ANativeWindow_release@plt` (`:2ebec4f`), then `movq $0,0x72a3308` |

**No read of `0x72a3308` exists in `0x649c272`/`0x649d1c2`/`0x649d422`.**
Those routines get the window through `create_context`'s first argument,
stored at `ctx+0x18` (`0x649c2cf`) and used only for `acquire/getWidth/
getHeight/eglCreateWindowSurface`. The swap `0x649d422` reads only `ctx+0x20`
(EGLDisplay) and `ctx+0x28` (EGLSurface) and tail-jmps `eglSwapBuffers@plt`
(`0x6d334d0`) — it never touches a window.

The app-start binding point is the window embedded at `native_params+0x00`
and dispatched through `0x2eb6b2a`, **not** the global itself (C08 §2.1/§2.2).

## 3. The calls are PLT/JUMP_SLOT, not window-vtable (critical clarification)

Disassembly of `0x26fea5a`, `0x2a20850/0x2a208c0/0x2a20a00/0x2a20a30`, and
`0x649c272` shows every window operation is a `call <symbol>@plt`. The PLT
stubs are plain `jmp *GOT(...)` and `readelf -rW` confirms the five slots:

```
0x72294a0  R_X86_64_JUMP_SLOT  ANativeWindow_release
0x72294a8  R_X86_64_JUMP_SLOT  ANativeWindow_fromSurface
0x72297c0  R_X86_64_JUMP_SLOT  ANativeWindow_acquire
0x72297c8  R_X86_64_JUMP_SLOT  ANativeWindow_getHeight
0x72297d0  R_X86_64_JUMP_SLOT  ANativeWindow_getWidth
```

| Operation | Guest call sites | GOT |
| --- | --- | --- |
| fromSurface | `0x26fea78`, `0x2a2087c`, `0x2a20907` | `0x72294a8` |
| release | `0x26fea9b`, `0x26feb7d`, `0x2a2086b`, `0x2a208f6`, `0x2a209e3`, `0x2a20a65`, `0x2ebc946`, `0x2ebec4f`, `0x649d2bc`-teardown | `0x72294a0` |
| acquire | `0x2a208e5`, `0x649c2d6` | `0x72297c0` |
| getWidth | `0x2a20963`, `0x2a20985`, `0x649c303` | `0x72297d0` |
| getHeight | `0x2a20975`, `0x2a20993`, `0x649c30f` | `0x72297c8` |

Notable gating details (OBSERVED):

- In `egl_init` `0x649c272`, `acquire`/`getWidth`/`getHeight` at
  `0x649c2d6/0x649c303/0x649c30f` are reached **only when the `extra`
  argument (r15) is NULL** (`test r15,r15; jne 0x649c362` at `0x649c2c2`).
  This is the `pbuffer`/borrowed-window branch (C04 §1.4, R1 §0).
- In `0x26fea5a`, the `same-window` branch `je 0x26feb16` is taken whenever
  `fromSurface` returns the pointer already in the global; the hook does
  return the *same* `s_anw` every time, so each subsequent call reaches
  `0x26feb7d` and decrements `refs` again — this is why the large initial
  refcount is required.
- The lifecycle thunks' "callbacks" (`call *%rcx` at `0x2a208a6`,
  `call *%rcx` at `0x2a20933/0x2a20957`, `call *%r8` at `0x2a209ce`,
  `jmp *%rax` at `0x2a20a20`, `call *%rax` at `0x2a20a54`) are **function
  pointers in `nativeData` at `+0x88/+0x98/+0xa0/+0x90`**, i.e. engine
  callbacks dispatched by GameActivity. They are not part of the window and
  are not `incRef/decRef/query`.

**Implication:** a faithful reconstruction does *not* need to install an NDK
`ANativeWindow` vtable with `incRef/decRef/query` for the guest to call — the
guest never dereferences such a table. What it needs is for the five PLT
imports to resolve to wrappers that agree on the object's layout.

## 4. What a guest-shaped window must contain for those calls to succeed

**Scenario A — current harness (compat shim resolves the GOTs).** The five
imports resolve to `src/compat/android_compat.c:498-513`, which operate on:

```
struct ANativeWindow { unsigned refs; int32_t width, height; };  // 12 bytes
ANativeWindow_acquire(w)  { if (w) w->refs++; }
ANativeWindow_release(w)  { if (w && w->refs && --w->refs == 0) free(w); }
ANativeWindow_getWidth(w) { return w ? w->width  : 0; }
ANativeWindow_getHeight(w){ return w ? w->height : 0; }
```

So the requirement is: a valid pointer with readable/writable `refs @+0`,
`width @+4`, `height @+8`, and a refcount that never reaches zero across the
release sites in §3 (the helper releases the old window, the same-window
branch releases the new one, `onSurfaceDestroyedNative` releases, pause paths
release and zero). `SoberGuestANativeWindow.refs = 0x10000`
(`render_guest.c:387`) satisfies this; its `vtable`, `native_window`,
`native_handle`, `egl_*`, `magic`, `min/max_swap` fields are **write-only** in
this module and unread by the guest — they do not contribute.

**C08 warning still applies in general.** C08 §4 Phase-A step 3 notes that
hooking only `fromSurface` and returning a full NDK-layout object (refcount
stored in a `decRef` function pointer, not a plain integer) would make the
still-installed compat `ANativeWindow_release` at `0x26fea9b` decrement the
wrong word and corrupt/free it. `render_guest` avoids this only because it
deliberately uses the compat 12-byte prefix. **All five GOTs must be hooked
together with wrappers that agree on one object layout** for any non-prefix
layout to be safe.

**Scenario B — faithful NDK semantics (informational, not exercised).** Real
`ANativeWindow_acquire/release` call `w->common.incRef/decRef` and
`getWidth/getHeight` call `w->query(w, NATIVE_WINDOW_WIDTH/HEIGHT)` — i.e. a
full `android_native_base_t` header plus `query` function pointer would be
needed *if the PLTs resolved to libandroid*. This harness never does; the
guest calls the imports, not those pointers inline. Do not add a fake vtable
to the window: decision 013 forbids host-function vtables, and here it would
be inert anyway.

## 5. Why `render_guest`'s window leads to no draws

The window is accepted; the failure is in the *frame loop*, not in window
layout. Chain (matches R1 §4/§5 verbatim):

1. `from_surface=2`: GOT `0x72294a8` is hooked and hit twice — once from
   `0x2a2087c` (created) and once from `0x2a20907` (changed). Both reach our
   hook, which returns `s_anw`.
2. `life=1/1/1/1`: `rg_bind_surface()` drives the four thunks with the
   calloc'd `nativeData`, but its callback slots `[+0x88]/[+0x98]/[+0xa0]`
   are NULL (`render_guest.c:402-404`; header evidence `render_guest.h:15-18`:
   guest NULL-checks them at `0x2a20897`, `0x2a2092b`, `0x2a2094f`,
   `0x2a20a18`, `0x2a20a4f`).
3. The redraw thunk `0x2a20a00` loads `[nativeData+0x98]` at `0x2a20a11` and
   `jmp *%rax` at `0x2a20a20`; with `[+0x98]==NULL` the guest **skips the
   frame/redraw trigger**. No guest frame loop ⇒ no GLES draw dispatcher
   `0x6496d0c` ⇒ `draw_calls=0` (the 5 wrapped procs are never invoked).
4. The only swap is the host's Route-B call: `guest_render.c:312-354` builds a
   stack `0x48`-byte ctx, fills `+0x20/+0x28`, and calls `0x649d422` directly.
   Because `0x649d422` **tail-jmps** `eglSwapBuffers@plt`, the RA captured by
   `rg_hook_swap` is the *host* return site, so `rg_in_guest(ra)` is false:
   `swaps=1/0/1`, `guest_tagged=0`. No `call [vtable+0x18]` → `0x649d422`
   with a guest RA ever occurs.
5. The gate's window conjunct `*(base+0x72a3308) != NULL`
   (`guest_app_shell.c:1778-1780`) is satisfied only by the **host store** at
   `render_guest.c:326-329`/`:417-423` — a false-green seed, not evidence the
   guest consumed the window.

Root cause in one line: **the window is published and its PLT accessors work,
but nothing ever makes the guest *use* it to produce a frame.** Giving the
window a "guest-shaped vtable" would not help — that field is never read.

## 6. Faithful fix (ranked; all are WITHHELD diagnostics under 013/038)

1. **[Highest — flips `draws` and `guest_tagged`] Make the redraw callback
   chain genuine.** Populate `nativeData+0x88/+0x98/+0xa0` (and `+0x90` for
   changed-size) with **real guest callback pointers**, not host functions.
   The most faithful way is to let the guest construct its own GameActivity
   nativeData through the genuine `initializeNativeCode` path (the engine
   fills these slots itself); then drive `0x2a20a00` with that object so
   `0x2a20a11/0x2a20a20` executes. The guest frame loop then issues GLES draws
   through the already-wrapped `eglGetProcAddress` pointers and dispatches
   `call QWORD PTR [vtable+0x18]` (`[0x7191300]=0x649d422`) with a guest RA,
   making `s_swaps_guest` and `draw_calls` positive. (R1 §6 item 1; C04 §2.3.)
2. **[High] Make window provenance guest-bound, not host-written.** Verify a
   guest RA at `0x26fea78/0x2a2087c/0x2a20907` for the store and a generation
   delta at `0x72a3310`. Supply a genuine-shaped Java `Surface` carrying a
   `mNativeObject:J` (C08 §4 Phase B) so the guest's own `0x26fea5a` performs
   the publish; stop relying on `render_guest.c:326-329`. This removes the
   false-green window conjunct.
3. **[High] Hook all five ANativeWindow GOTs together** — `0x72294a8`,
   `0x72294a0`, `0x72297c0`, `0x72297c8`, `0x72297d0` (C08 §2.7/§4 Phase-A
   step 3) — with wrappers operating on one coherent object, so refcount and
   dimensions are consistent across fromSurface/release/acquire/getWidth/
   getHeight. Keep the compat 12-byte prefix (or convert every wrapper
   together); hooking only `fromSurface` corrupts the shim at `0x26fea9b`.
4. **[Medium] Count a guest draw in the same window.** Require `draws≥1`
   bracketed by two tagged swaps; the draw sites are imported JUMP_SLOTs
   `0x7229fc8/0x722a040/0x722a048` and extension table `0x77a4fe0/8`,
   `0x77a4fa0/8/b0`, `0x77a4f98`, `0x77a4f78` (R1 §6 item 2). Never count the
   host `dlsym`'d `glClear` (`native_render.c:817-830`).
5. **[Required for the marker, highest honesty weight] Close Leg C.** Pair
   any guest-tagged render with a genuine notifier-driven `gameLoadedCallback`
   (notifier `0x27eaf6e` → thunk `0x25848e0`, class `0x72a2f48`, method
   `0x72a2fc0`, `placeId>0`, RA `base+0x27eafb3`) with **all seeds off** (R1
   §6 item 6). No render-side change alone lifts the marker.

**Decision 013/038:** items 1–4 produce *observability* of a guest frame path
using synthetic state. The log line `marker withheld: diagnostic guest-state
seed active` must keep firing; success is a correct `guest-tagged render`
diagnostic, never `app shell rendered`. Marker stays **WITHHELD**.

## 7. Limits / unknowns

- The exact addresses of the genuine guest callbacks for `nativeData+
  0x88/+0x98/+0xa0/+0x90` were not pinned here; finding them (or, better,
    letting the guest build nativeData itself) is a prerequisite for fix 1.
    They are engine function pointers, not window vtables.
- The internal implementation of Android's `ANativeWindow_fromSurface` is
  **INFERRED** from NDK semantics (C08 §5); the guest-side contract (a valid
  refcounted pointer returned via the PLT) is **OBSERVED**.
- Whether the app-shell frame consumes `0x72a3308` versus `nativeData+0x140`
  as its drawable remains **UNKNOWN** (C08 §5); map both paths and seed them
  together.
- The precise guest frame-loop call site of `call [vtable+0x18]` is still not
  statically pinned (~11,600 `call *0x18(%r..)` sites, C04 §2.3); capture it by
  RA from a real guest-tagged swap.

## 8. Evidence index

**This task (OBSERVED):** frozen engine `sha256sum`; `objdump -d` of
`0x26fea5a`, `0x2773210-0x27732e0`, `0x2ebc8c0-0x2ebc956`,
`0x2ebec05-0x2ebec74`, `0x2ebd5c0-0x2ebd654`, `0x2ebed30-0x2ebedbf`,
`0x2ebef30-0x2ebefbf`, `0x649c21e-0x649c5b0`, `0x649d422-0x649d440`, PLT stubs
`0x6d31ac0/0x6d31ad0/0x6d32100/0x6d32110/0x6d32120`; `readelf -rW | grep
ANativeWindow`; `/tmp/opencode/r4_thunks.dis` (full four lifecycle thunks).

**Prior analysis (read only):** `analysis/render/render-map.md` §2-5;
`analysis/C08-surface-frame.md` §2, §4, §5; `analysis/R1_zero_tagged_swaps.md`
§0-7; `analysis/C04-render-integration.md` §1-3; `analysis/A09-platform-params.md`;
`analysis/T3_5_render-evidence.md` §2-3; `docs/decisions/013`, `038`.

**Harness (read only):** `src/jni/render_guest.c:239-331,374-453,486-518`;
`src/jni/render_guest.h:15-18,145-178,183-207`;
`src/compat/android_compat.c:493-513`; `src/render/native_render.c:331-341,
890-1048`; `src/linux/guest_render.c:312-354`; `src/linux/guest_app_shell.c:
1772-1780`.

No file other than this report was created or modified. Marker withheld.
