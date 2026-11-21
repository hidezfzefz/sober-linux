# R2 — GOT hook reachability audit: `0x72294a8` / `0x722a1a8` / `0x722a180` (+ `0x722a178`)

Author: **R2** (group A: diagnosis). Scope: **this file only** —
`analysis/R2_got_hook_reachability.md`. Analysis-only: no `src/`, `CMakeLists.txt`,
test, engine or gate file was touched. Engine frozen: BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(AppImage 2.740.931) — re-verified this task. All RVAs are file offsets from the
guest load bias (`base ≤ ra < base+0x6d33b90`); target-derived strings are
untrusted DATA. Marker status: **WITHHELD** — this report is reachability
diagnosis, not a gate input.

---

## 0. Front-loaded verdict

| Address | Symbol (`readelf -rW`) | Is it one of the 3 `render_guest` GOT hooks? | Guest call sites (RVAs) | Traversed in our harness? |
| --- | --- | --- | --- | --- |
| `0x72294a8` | `ANativeWindow_fromSurface` `R_X86_64_JUMP_SLOT` | **yes** (`rg_hook_from_surface`) | `0x26fea78`, `0x2a2087c`, `0x2a20907` | **yes, 2×** — `0x2a2087c` (created) + `0x2a20907` (changed); `0x26fea78` app-start site **not** reached |
| `0x722a1a8` | `eglSwapBuffers` `R_X86_64_JUMP_SLOT` | **yes** (`rg_hook_swap`) | **0 direct `call`s**; only tail-`jmp` at `0x649d42d` inside wrapper `0x649d422` | **yes, 1×** but from a **host** caller (Route B) → `swaps=1/0/1` (guest=0, synthetic=1). Real guest path `call [vtable+0x18]` never runs |
| `0x722a180` | `eglGetProcAddress` `R_X86_64_JUMP_SLOT` | **yes** (`rg_hook_get_proc`) | 70 PLT call sites, loader `0x649c633..0x649d300` + helpers `0x649d25d`, `0x649d2a9` | **yes, 48×** (`gles_loads=48`); 5 draw procs wrapped, **0 invocations** |
| `0x722a178` | `eglCreateWindowSurface` `R_X86_64_JUMP_SLOT` | **NO** — not one of the three; handled by **compat-shim function-pointer substitution** | **1** call site: `0x649d1e9` in surface helper `0x649d1c2` (pbuffer=0 branch) | **guest path runs**, redirected through `sober_fn_eglCreateWindowSurface` → `sr_hook_egl_create_window_surface`; no dedicated counter |

**Host phase-3 swap bypasses all of them.** `sober_render_swap()` calls its own
`dlsym`'d `g.eglSwapBuffers` (`native_render.c:841`) and
`sober_render_anw_unlock_and_post()` likewise (`:954`); neither traverses a guest
GOT slot or the compat trampoline. The counter trio reads `0/0/0` before the
harness's *separate* direct guest call, which is the proof.

---

## 1. Reloc / PLT evidence (exact RVAs)

### 1.1 `readelf -rW libroblox.so` — the four JUMP_SLOTs

```
00000000072294a8  0000001400000007 R_X86_64_JUMP_SLOT  ANativeWindow_fromSurface + 0
000000000722a178  0000001c700000007 R_X86_64_JUMP_SLOT  eglCreateWindowSurface + 0
000000000722a180  0000001c800000007 R_X86_64_JUMP_SLOT  eglGetProcAddress + 0
000000000722a1a8  0000001cd00000007 R_X86_64_JUMP_SLOT  eglSwapBuffers + 0
000000000722a148  0000001c100000007 R_X86_64_JUMP_SLOT  eglMakeCurrent + 0   (context, not hooked)
```

The render-map's `—` in the `eglGetProcAddress` "GOT VMA" column
(`analysis/render/render-map.md:64`) is an omission: the live relocation stream
puts it at `0x722a180`, exactly where `render_guest.h:142` and `render_guest.c:444`
expect it.

### 1.2 PLT stubs — each `jmp *GOT(slot)`

```
6d31ad0 <ANativeWindow_fromSurface@plt>:  ff 25 d2 79 4f 00  jmp *0x72294a8(%rip)
6d33470 <eglCreateWindowSurface@plt>:     ff 25 02 6d 4f 00  jmp *0x722a178(%rip)
6d33480 <eglGetProcAddress@plt>:          ff 25 fa 6c 4f 00  jmp *0x722a180(%rip)
6d334d0 <eglSwapBuffers@plt>:             ff 25 d2 6c 4f 00  jmp *0x722a1a8(%rip)
```

Consequence: overwriting a JUMP_SLOT intercepts **every** PLT caller of that
symbol, and only those callers. There is no direct (non-PLT) call to the resolved
address in guest `.text` for these imports.

### 1.3 Binding model — why the forward-to-original chain is safe

`guest_dlopen_host.c:524` loads the guest with
`dlopen(g_fd_path, RTLD_NOW | RTLD_LOCAL)`. `RTLD_NOW` resolves all 542 JUMP_SLOTs
**at load time** (verified for `sysconf/mmap/mprotect/prctl` at
`guest_dlopen_host.c:570-585`), before any hook is installed in phase 3. So each
`s_got_old[i]` captured by `rg_got_write()` (`render_guest.c:217-235`) holds the
**real** resolved function (a host compat trampoline, exported via `-rdynamic`,
`CMakeLists.txt:140,176`), not the PLT resolver: no lazy binder can later
overwrite our hook. `rg_hook_*` forwarding therefore reaches the genuine driver
unchanged.

---

## 2. GOT `0x72294a8` — `ANativeWindow_fromSurface`

### 2.1 Guest code paths (all three static call sites)

```
0x26fea78  e8 53 30 63 04  call 6d31ad0 <ANativeWindow_fromSurface@plt>
           ; helper 0x26fea5a (nativeAppBridgeV2StartAppWithParams);
           ; result -> global 0x72a3308, generation++ at 0x72a3310 (0x26feaa0)
0x2a2087c  e8 4f 12 31 04  call 6d31ad0 <@plt>   ; onSurfaceCreatedNative thunk 0x2a20850
0x2a20907  e8 c4 11 31 04  call 6d31ad0 <@plt>   ; onSurfaceChangedNative thunk 0x2a208c0
```

The created thunk NULL-checks `[nativeData+0x88]` at `0x2a20897` and tail-jmps it
at `0x2a208a6`; the changed thunk NULL-checks `[+0xa0]`/`[+0x88]` at
`0x2a2092b`/`0x2a2094f` and calls `ANativeWindow_getWidth`/`getHeight` through
PLT `0x6d32120`/`0x6d32110` at `0x2a20963`/`0x2a20975` (GOT `0x72297c0`/`0x72297d0`).
Redraw `0x2a20a00` and destroyed `0x2a20a30` do **not** call `fromSurface`.

### 2.2 Harness traversal

`sober_render_guest_run()` → `rg_bind_surface()` (`render_guest.c:289-331`, or the
contract-identical local fallback `guest_render.c:205-214`) calls all four
thunks with `tok = &s_surface_token` (**non-NULL**) and `nativeData+0x140`
initially NULL. The disassembly shows a non-NULL 4th argument (Surface) is the
precondition for both `fromSurface` calls, so:

- `0x2a2087c` hit once (created), `0x2a20907` hit once (changed) →
  **`from_surface=2`** in the log.
- `0x26fea78` **not hit**: that site belongs to the Java-driven app-start helper
  and the harness has no JNI `Surface` (render-map §5, `analysis/R1_zero_tagged_swaps.md §4`).
- The hook returns the synthetic `SoberGuestANativeWindow` with a bumped refcount
  (`render_guest.c:239-252`); the guest stores it at `nativeData+0x140`. The store
  to global `0x72a3308` is then **host-written** (`render_guest.c:324-329`), not
  the guest's `0x26feaa0` store. Callback slots `+0x88/+0x98/+0xa0` are NULL, so
  the chain terminates without a guest frame.

Runtime proof (`/tmp/opencode/w_final.out`, and identical `w_after.out`):
`... life=1/1/1/1 from_surface=2 ...`.

---

## 3. GOT `0x722a1a8` — `eglSwapBuffers`

### 3.1 The only guest route: a tail jump, never a direct call

`readelf`/`objdump` find **zero** `call 6d334d0` instructions in `.text`. The sole
entry is the flat swap wrapper:

```
649d422: 48 8b 47 20   mov 0x20(%rdi),%rax     ; EGLDisplay
649d426: 48 8b 77 28   mov 0x28(%rdi),%rsi     ; EGLSurface
649d42a: 48 89 c7      mov %rax,%rdi
649d42d: e9 9e 60 89 00 jmp 6d334d0 <eglSwapBuffers@plt>   ; tail jmp
```

Guest frame path (documented, **not executed in our harness**):
`call QWORD PTR [vtable+0x18]` with vtable `0x71912e8` and
`[0x7191300] = 0x649d422` (render-map §3) → tail-jmp → hook, with the RA being
the guest instruction after that indirect call → `rg_in_guest(ra)` true →
`guest-tagged`.

### 3.2 What actually happens in the harness

`guest_render.c:312-354` (Route B) builds a **host stack** `uint64_t ctx[9]`
(0x48 bytes), fills `ctx+0x20` (EGLDisplay) and `ctx+0x28` (EGLSurface) from the
native render, then calls `base+0x649d422` **from host code**. Because the
wrapper tail-jmps, the stack still holds the host return address, so
`rg_hook_swap()` (`render_guest.c:255-264`) sees `ra` outside
`[base, base+0x6d33b90)` and classifies the swap **synthetic**:

```
[render] render_guest owns guest-swap counter hook at GOT 0x722a1a8
[render] guest call 0x649d422 swap(...) -> rc=0 | guest-driven swaps observed: 1
[render] render_guest counters: ... swaps(total/guest/synth)=1/0/1 lifecycle=1/1/1/1
```

The module's `sober_render_guest_swap_via_vtable()` (`render_guest.c:520-548`) —
the only API that would dispatch the real vtable slot — is **never called** by any
harness/gate file (definition and header declaration are its only references),
and its own comment (`:545`) says its RA would be host anyway.

`guest_render.c:328-332` deliberately refuses to overwrite this slot with
`sr_hook_egl_swap_buffers` when the module is present, so the single hit is the
module's `rg_hook_swap`, not native_render's counter.

---

## 4. GOT `0x722a180` — `eglGetProcAddress`

### 4.1 Guest code paths

70 static call sites through PLT `0x6d33480`; the dense block begins:

```
649c633: e8 48 6e 89 00  call 6d33480 <eglGetProcAddress@plt>
649c64b: e8 30 6e 89 00  call 6d33480
649c668: e8 13 6e 89 00  call 6d33480
649c680: e8 fb 6d 89 00  call 6d33480
... span 0x649c633..0x649d300; helpers 0x649d24d, 0x649d25d, 0x649d285, 0x649d2a9
```

These are the guest's GLES entry-pointer loader, reached from EGL device creation
`0x649c21e → 0x649c272`.

### 4.2 Harness traversal

`sober_render_guest_create_context()` calls genuine `0x649c21e(window, pbuffer=0)`
(`render_guest.c:502-504`; equivalently `guest_render.c:218-219`) and the guest
runs its loader with RAs inside `base+0x649c633..base+0x649d300`, so
`rg_hook_get_proc()` (`render_guest.c:267-278`) increments `s_gles_proc_loads`
and wraps draw-named results. Log: `gles_loads=48 / proc_loads=48`,
`draw_wrapped=5`, `draw_calls=0`. The 48 count is runtime (conditional loader
paths), the 70 is a static call-site count; both are consistent. The wrappers are
never invoked because the redraw callback `[nativeData+0x98]` is NULL
(`0x2a20a11` load, `0x2a20a18` test, `0x2a20a20` tail-jmp skipped) → no guest
frame loop → no GLES draw dispatcher at `0x6496d0c`.

---

## 5. `0x722a178` — `eglCreateWindowSurface`: a different mechanism

This is the important disambiguation. `render_guest.c` writes exactly **three**
GOT slots (`:433-453`, restored at `:562-567`); `0x722a178` appears only as a
header constant (`render_guest.h:143`) and is **never written**. The header's
phrase "substituted by our EGL hook" means:

```
guest PLT:  0x649d1e9  call 6d33470 <eglCreateWindowSurface@plt>
            -> jmp *GOT[0x722a178]  (bound at RTLD_NOW to host compat trampoline)
compat:     gl_egl_forward.c:210-221 TRAMP macro -> jmp *sober_fn_eglCreateWindowSurface
substitute: render_guest.c:426-430 / guest_render.c:317-321 set that pointer to
            native_render's sr_hook_egl_create_window_surface (native_render.c:1002-1012),
            which replaces the ANativeWindow with r->native_handle (real X11 handle).
```

Guest caller: the single site `0x649d1e9` in surface helper `0x649d1c2`; the
branch at `0x649d1c9` tests `ctx+0x10` — non-NULL → pbuffer path `0x649d1d8`
(GOT `0x722a170`), NULL → window path `0x649d1e9`. `render_guest` passes
`pbuffer=0`, so the harness takes the window branch during `create_context` and
the substitution is exercised (log: `installed EGL window-surface hook into compat
shim`, followed by a successful `ctx=… vt=…`). Note the *host* window setup
(`sober_render_init` → `g.eglCreateWindowSurface`, `native_render.c:581`) is a
direct `dlsym` call and does **not** traverse `0x722a178`.

There is **no counter** for this hook: reachability must be shown with a gdb
breakpoint on `sr_hook_egl_create_window_surface` / the compat trampoline
`eglCreateWindowSurface`, or inferred from a successful `ctx` with `pbuffer=0`.

---

## 6. Host phase-3 swap: proven bypass

| Host call | Code | Target | Traverses guest GOT? |
| --- | --- | --- | --- |
| first-frame swap | `sober_render_swap()` → `native_render.c:841` | `g.eglSwapBuffers`, dlsym'd from native_render's own `dlopen("libEGL.so.1", RTLD_LAZY\|RTLD_LOCAL)` (`:184,:223`) | **No** — bypasses GOT `0x722a1a8` and the compat `eglSwapBuffers` trampoline |
| ANativeWindow buffer post | `sober_render_anw_unlock_and_post()` → `native_render.c:954` | same `g.eglSwapBuffers` | **No** |
| guest Route-B swap | `guest_render.c:354` `guest_swap(ctx)` | guest `0x649d422` → PLT → GOT `0x722a1a8` | **Yes** — but RA is host → synthetic |

Decisive evidence from the log sequence:

```
[render] GL version: OpenGL ES 3.2 Mesa 26.2.2 | frames swapped: 1     <- host swap done
[render] enabled=1 installed=1 ... swaps=0 guest=0 synthetic=0          <- hook still 0
[render] render_guest counters: ... swaps(total/guest/synth)=0/0/0
... host calls guest 0x649d422 ...
[render] render_guest counters: ... swaps(total/guest/synth)=1/0/1      <- first hook hit
```

So the host's `phase3_done` / `first frame rendered` signal is generated entirely
off the direct `dlsym` path; it cannot be used as evidence that any GOT hook ran.

---

## 7. Reachability matrix (harness, default-on, module present)

| Hook | Guest callers | Guest path taken here? | Host/other path taken here? | Counter |
| --- | --- | --- | --- | --- |
| `0x72294a8` fromSurface | `0x26fea78`, `0x2a2087c`, `0x2a20907` | `0x2a2087c`, `0x2a20907` (host-driven thunks) | `0x26fea78` no (no JNI Surface) | `from_surface=2` |
| `0x722a1a8` swap | tail-jmp `0x649d42d` from `0x649d422`; normally `call [0x7191300]` | **no** guest-dispatch | host direct `0x649d422` (Route B) once | `swaps=1/0/1` |
| `0x722a180` getProcAddr | 70 sites, `0x649c633..0x649d300`, `0x649d25d`, `0x649d2a9` | **yes** during `create_context` | host `g.eglGetProcAddress` (dlsym) bypasses | `gles_loads=48`, `draw_wrapped=5`, `draw_calls=0` |
| `0x722a178` createWindowSurface (not a GOT hook) | `0x649d1e9` via `0x649d1c2` (pbuffer=0) | **yes** via `sober_fn_eglCreateWindowSurface` substitution | host `g.eglCreateWindowSurface` bypasses | none (use gdb) |

---

## 8. Test plan to prove each hook is reached

All commands are read-only w.r.t. `src/`, engine and gate; use a custom
`SOBER_FRAME_MARKER` under `/tmp` so no repo marker is touched.

**T0 — static relocation proof (already run, deterministic).**
```
sha256sum /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so   # = 3e498d64...
readelf -rW  ... | grep -E '72294a8|722a178|722a180|722a1a8'      # 4x JUMP_SLOT
objdump -d --start-address=0x6d334d0 --stop-address=0x6d334e0 ...  # jmp *0x722a1a8
objdump -d --start-address=0x6d31ad0 --stop-address=0x6d31ae0 ...  # jmp *0x72294a8
objdump -d --start-address=0x649d422 --stop-address=0x649d432 ...  # tail jmp at 0x649d42d
objdump -d --start-address=0x649d1c2 --stop-address=0x649d1ee ...  # call 0x6d33470 @0x649d1e9
objdump -d --start-address=0x2a20850 --stop-address=0x2a20920 ...  # fromSurface @0x2a2087c/0x2a20907
```
Pass = exact addresses and `R_X86_64_JUMP_SLOT` types above.

**T1 — counter proof for the three GOT hooks (`SOBER_RENDER_GUEST` default ON).**
Run `bash tests/first-frame.sh` (or the direct host invocation it wraps) and
assert on stderr, in order:
1. `enabled=1 installed=1 ... got_from=0x72294a8 swap=0x722a1a8 getproc=0x722a180` — hooks installed.
2. `from_surface=2` — GOT `0x72294a8` hit twice. Kill-switch control:
   `SOBER_RENDER_GUEST=0` must give `enabled=0 installed=0` and `from_surface=0`.
3. `gles_loads=48` (>0) and `draw_wrapped=5` — GOT `0x722a180` hit from guest RAs.
4. `swaps(total/guest/synth)=0/0/0` immediately after `frames swapped: 1` — negative control proving the **host first-frame swap bypasses** the hook.
5. After `guest call 0x649d422 ...`, `swaps(total/guest/synth)=1/0/1` — GOT `0x722a1a8` hit exactly once, host RA. (A *guest-tagged* hit would require `guest≥1`; its absence is correct, see R1.)

**T2 — per-hook breakpoints (definitive, one shot).** Symbols are present in
`build/guest-init-host`: `rg_hook_from_surface@0x47920`, `rg_hook_swap@0x47790`,
`rg_hook_get_proc@0x47c20`, `sr_hook_egl_create_window_surface@0x21cd0`, compat
trampoline `eglCreateWindowSurface@0x18161`, `sober_render_swap@0x23aa0`. Under
`gdb -batch -x /tmp/r2_gdb_hooks.cmd ./guest-init-host <guest.so>` with
auto-continuing `commands` blocks, expect:
- `rg_hook_from_surface` **2** hits (created, changed); `bt` shows RA at
  `base+0x2a20881` / `base+0x2a2090c` (the instruction after each `call`).
- `rg_hook_get_proc` **48** hits; RAs inside `base+0x649c633..0x649d300`.
- `rg_hook_swap` **1** hit; RA is a **host** address in `guest_render.c` — not in guest `.text`.
- `sr_hook_egl_create_window_surface` / compat `eglCreateWindowSurface` **≥1** hit
  during `create_context` — this is the only direct proof for the `0x722a178` path.
- `sober_render_swap` hit with **no** interleaved `rg_hook_swap` — bypass proof.
- A negative control with `SOBER_RENDER_GUEST=0`: zero hits at all four hook
  breakpoints, `sober_render_swap` still hit once.

**T3 — positive guest-tagged control (proves the *guest* swap path reaches the
hook).** Set a genuine guest callback at `[nativeData+0x98]` (and realistically
`+0x88`/`+0xa0`) for the `0x2a20a00` redraw thunk so the guest frame loop runs a
`call [vtable+0x18]` → `0x649d422`. Success criterion for *reachability only*:
`guest-tagged swaps ≥ 1` and a stable guest RA at `0x649d42d`'s caller, plus
`gles_draw_calls ≥ 1` from the wrapped procs. This remains a **synthetic seed**
under decision 013/038 and must not lift the marker.

**T4 — integrity invariants (every run).** `sha256sum` before/after = `3e498d64…`;
`readelf -n` BuildID unchanged; engine bytes never patched (only 8-byte GOT stores
and the two window globals at `0x72a3308`/`0x72a3310`).

---

## 9. Verdict

- All three `render_guest` GOT hooks are correctly placed on real
  `R_X86_64_JUMP_SLOT`s whose PLT stubs jump through them, and **all three are
  reached in the harness** — `0x72294a8` twice, `0x722a180` 48 times, `0x722a1a8`
  once.
- The `0x722a1a8` hit is from the **host's** direct `0x649d422` call, so it is
  correctly tagged synthetic; the genuine guest vtable-dispatched swap path
  (`call [0x7191300]`) is **never** exercised.
- `0x722a178` is **not** a GOT hook: the guest does traverse its single call site
  `0x649d1e9` during `create_context`, but interposition happens via the compat
  shim's `sober_fn_eglCreateWindowSurface`, not via an 8-byte GOT store.
- The host's phase-3 first-frame swap **bypasses all four** (direct `dlsym`
  `g.eglSwapBuffers`); the `0/0/0 → 1/0/1` counter transition is the proof.
- Consistent with R1: `guest_tagged=0` / `draws=0` are honest, and the marker
  remains **WITHHELD**. No false green is introduced by this audit.

Files consulted (read-only): `src/jni/render_guest.c/.h`, `src/render/native_render.c`,
`src/linux/guest_render.c`, `src/linux/guest_dlopen_host.c`,
`src/linux/android_relocs.c`, `src/compat/gl_egl_forward.c`,
`analysis/render/render-map.md`, `analysis/R1_zero_tagged_swaps.md`,
`analysis/MODULE_INTEGRATION.md`, `tests/first-frame.sh`, and harness logs
`/tmp/opencode/w_after.out`, `/tmp/opencode/w_final.out`; plus direct
`sha256sum`/`readelf -rW`/`objdump -d`/`nm` of the frozen
`libroblox.so` and `build/guest-init-host`. No file other than this report was
modified.
