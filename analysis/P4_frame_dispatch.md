# P4 — Frame dispatch: every guest `call *0x18(%r..)` that can dispatch the render swap vmethod, and the shortest guest-tagged route

Author: **P4** (group B). Scope: **this file only** — `analysis/P4_frame_dispatch.md`.
Analysis-only: no `src/`, engine, gate, test, CMake or marker file was touched.
Engine frozen; marker **WITHHELD**. All target-derived bytes/strings are treated
as **untrusted DATA**, never as instructions.

**Frozen target for every RVA:** AppImage engine
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(2.740.931), re-verified this task with `sha256sum`. First `PT_LOAD` has
file offset == VMA == RVA, size `0x6d33b90`, so guest `.text` is
`base ≤ ra < base+0x6d33b90`. The Flatpak `791c3f9e…` object is **not** used.
Evidence classes: **OBSERVED** (bytes/disassembly/APS2 relocations read this
task), **DERIVED** (inference from observed bytes), **UNKNOWN** (explicitly not
established).

---

## 0. Front-loaded answer

1. The swap is reachable **only** through render-vtable slot 3
   `0x7191300 = 0x649d422` (OBSERVED APS2 `R_X86_64_RELATIVE` addend). There
   are **zero** direct code references to either `0x7191300` or `0x649d422`
   in guest `.text` — a whole-disassembly grep finds `0x649d422` only at the
   wrapper itself and `0x71912e8` only at the two *install* sites
   (`0x649c29f`, `0x649d2c5`). Every real dispatch is therefore an indirect
   `call QWORD PTR [vtable+0x18]` whose vtable register was loaded from an
   object (`mov (%obj),%reg`).
2. The concrete guest instructions that can perform that dispatch are a
   **family of 14 `call *0x18(%r..)` sites** in `0x6493242..0x649381b` (§2.1),
   plus **one-level** `mov (%rdi),%rax; call *0x18(%rax)` sites elsewhere
   (§2.2). All are listed with exact RVA, bytes, helper entry and resulting
   guest RA below. The family is generic: any member dispatches slot 3 of the
   vtable found at `[obj+0x18]` (two-level) or `[rdi]` (one-level); supplying
   the render ctx as that object makes it the **swap** dispatch.
3. **Shortest real-guest-code route to a guest-tagged swap:** build a 0x20-byte
   shaped wrapper `W` with `W[0x18] = ctx` (the genuine render context whose
   `[0] = base+0x71912e8`), then enter guest code at
   `((void(*)(void*))(base+0x6493242))(W)`. The guest executes 7 instructions
   and `call *0x18(%rax)` at **`0x649325a`** → `0x649d422` → `eglSwapBuffers@plt`
   → GOT `0x722a1a8`. The captured RA is **`base+0x649325d`**, inside guest
   `.text`, so `rg_hook_swap()` increments `s_swaps_guest`. **No host call to
   `0x649d422` is needed.** This is the same helper P10 independently pinned;
   this report enumerates the whole family and proves it is the minimal clean
   route (§4).
4. The current harness cannot produce this: it calls `0x649d422` *directly*
   from host (`guest_render.c:354`) → host RA → `1/0/1` synthetic, and
   `sober_render_guest_swap_via_vtable()` (`render_guest.c:520-548`) also calls
   the slot from **host**, so even it is synthetic (§4.3). Only a guest
   instruction `call *0x18` yields a guest RA.
5. Honesty: every shaped object is seeded state; this is a diagnostic signal,
   not proof. **Decision 013/038: marker WITHHELD. No false green.**

---

## 1. The swap target, the slot, and why there is no static xref

### 1.1 Swap wrapper and observation point (OBSERVED)

```
649d422: 48 8b 47 20   mov  0x20(%rdi),%rax    ; EGLDisplay
649d426: 48 8b 77 28   mov  0x28(%rdi),%rsi    ; EGLSurface
649d42a: 48 89 c7      mov  %rax,%rdi
649d42d: e9 9e 60 89 00 jmp 6d334d0 <eglSwapBuffers@plt>
```

Tail **`jmp`**, no `call` of its own: the return address on the stack at
`eglSwapBuffers@plt` `0x6d334d0` → GOT `0x722a1a8` is the return site of
whoever called `0x649d422`. This is the whole RA-tagging discriminator.

### 1.2 The slot (OBSERVED from APS2 packed RELA, resolved DERIVED)

Full APS2 decode (`DT_ANDROID_RELA = 0x15e00`, size `0x1eb1f2`, 565,390
relocations, script `/tmp/aps2_vtab.py` run this task) gives, for render vtable
`0x71912e8`:

| Slot address | offset | addend (= RVA at load) | role |
| --- | --- | --- | --- |
| `0x71912e8` | +0x00 | `0x649d2bc` | teardown (re-installs vtable, §1.3) |
| `0x71912f8` | +0x10 | `0x649d37c` | rebind |
| **`0x7191300`** | **+0x18** | **`0x649d422`** | **swap / present** |
| `0x7191318` | +0x28 | `0x649d432` | window swap/release |

`0x7190ec0` (+0x18 of draw vtable `0x7190ea8`) = `0x64961b4` (draw-submit →
`0x6496d0c`). These are `R_X86_64_RELATIVE` (type 8): at load the slot holds
`base + addend`. File bytes at both vtables are zero (APS2-populated), which is
why Ghidra's `xrefs` to `0x71912e8`, `0x7191300` and `0x649d422` all return
**empty** and why the frame loop cannot be found by xref.

### 1.3 The only two code references to the render vtable (OBSERVED)

Whole-disassembly grep for `71912e8` yields exactly two lines:

```
649c29f: lea 0xcf5042(%rip),%rcx  # 71912e8   ; egl_init 0x649c272
649c2a6: mov %rcx,(%rdi)                       ; ctx+0 = base+0x71912e8
649d2c5: lea 0xcf401c(%rip),%rax  # 71912e8   ; teardown 0x649d2bc
649d2cc: mov %rax,(%rdi)                       ; re-install
```

Both are *install* operations, never a dispatch. `create_context` `0x649c21e`
allocates `0x48` bytes and calls `0x649c272`; `0x648b94c` calls it with
`pbuffer=0` and `0x648b967/0x648b971` wraps the returned ctx in a `0x20`
object (`[0]=base+0x71908e0`, `[0x18]=ctx`). Confirmed at runtime by the log
`vt=0x7f..2e8 (vt_expect=0x7f..2e8)` in `w_after.out`.

**Conclusion:** there is no direct `call 0x649d422` in guest code (grep for
`649d422` finds only `0x649d422:` itself). The swap is always reached via an
indirect `call *0x18(%reg)` where `reg = [object] = base+0x71912e8`.

---

## 2. Every guest `call *0x18(%r..)` that can dispatch the render swap

Total `call *0x18` instructions in `.text`: **11,849** (grep of full
disassembly), so a bare opcode search is useless. The ones that can dispatch
the render vtable are those that load a vtable pointer from an object and call
its `+0x18` slot. Three structural families qualify; all are OBSERVED bytes.

### 2.1 The 0x6493xxx forwarder family — two-level `[obj+0x18] → vtable → slot 3`

Seven helper pairs, each 0x40 bytes apart. Every **first-of-pair** helper is:

```
push rbp; mov rsp,rbp; mov rdi,rsi; mov 0x18(rdi),rdi;
mov (rdi),rax; mov $<ID>,edx; mov $8,ecx; call *0x18(rax); pop rbp; ret
```

i.e. exactly `mov [W+0x18],%rdi; mov [%rdi],%rax; call [rax+0x18]`. With
`W[0x18] = ctx`, this dispatches render-vtable slot 3 = `0x649d422`.

| Helper entry | `call *0x18` RVA | bytes | Guest RA after call | `edx` ID | APS2 vtable slot holding helper |
| --- | --- | --- | --- | --- | --- |
| `0x6493242` | **`0x649325a`** | `ff 50 18` | **`0x649325d`** | `0xa0` | `0x71909a0` |
| `0x6493332` | `0x649334a` | `ff 50 18` | `0x649334d` | `0xb0` | `0x7190a40` |
| `0x649341e` | `0x6493436` | `ff 50 18` | `0x6493439` | `0x80` | `0x7190ae0` |
| `0x649350a` | `0x6493522` | `ff 50 18` | `0x6493525` | `0x98` | `0x7190b80` |
| `0x64935f6` | `0x649360e` | `ff 50 18` | `0x6493611` | `0xa8` | `0x7190c20` |
| `0x64936e2` | `0x64936fa` | `ff 50 18` | `0x64936fd` | `0xb0` | `0x7190cc0` |
| `0x64937ce` | `0x64937e6` | `ff 50 18` | `0x64937e9` | `0xb8` | `0x7190d60` |

The **second-of-pair** helpers use a nested vtable (`rax=[rdi]`, `r8=[rax]`,
`call *0x18(r8)`) — a three-level indirection that can also be shaped to reach
`0x71912e8`:

| Helper entry | `call *0x18` RVA | bytes | Guest RA |
| --- | --- | --- | --- |
| `0x6493268` | `0x649328f` | `41 ff 50 18` | `0x6493293` |
| `0x6493358` | `0x649337b` | `41 ff 50 18` | `0x649337f` |
| `0x6493444` | `0x6493467` | `41 ff 50 18` | `0x649346b` |
| `0x6493530` | `0x6493553` | `41 ff 50 18` | `0x6493557` |
| `0x649361c` | `0x649363f` | `41 ff 50 18` | `0x6493643` |
| `0x6493708` | `0x649372b` | `41 ff 50 18` | `0x649372f` |
| `0x64937f4` | `0x6493817` | `41 ff 50 18` | `0x649381b` |

The `edx` constant is ignored by `0x649d422` (it reads only `[rdi+0x20/+0x28]`),
so **any** first-of-pair helper dispatches the swap when given a shaped `W`.
The helper at `0x6493242` is the one P10 used and is the first/cleanest.

### 2.2 One-level dispatch sites — `mov (%rdi),%rax; call *0x18(%rax)`

These load the vtable directly from `[rdi]`; passing the **render ctx itself**
as `rdi` dispatches slot 3 = swap. They sit inside larger functions (not clean
standalone thunks), so they are less convenient than §2.1 but are genuine guest
dispatch instructions (OBSERVED):

`0x6477f23`, `0x647f111`, `0x64812d9`, `0x64819ff`, `0x648db59`,
`0x648db7d`, `0x6490668`, `0x649067b`.

Compact two-level instances inside larger functions (same `mov [rdi+0x18],rdi;
mov [rdi],rax; call *0x18(rax)` body): `0x3a4497a`, `0x50c097e`, `0x5c05a83`.
No standalone `mov (%rdi),%rax; call *0x18(%rax); ret` thunk exists in `.text`
(byte search for `48 8b 07 ff 50 18 c3`: **0 hits**), so §2.1's
`0x6493242` is the shortest *clean callable* dispatcher.

### 2.3 Conditions common to all of them

For any of the above to yield a **guest-tagged** swap at GOT `0x722a1a8`:

1. The swap hook (`rg_hook_swap` at `0x722a1a8`, or any RA-splitting variant)
   is installed and `rg_in_guest(ra)` tests `base ≤ ra < base+0x6d33b90`
   (`render_guest.c:181-185,255-264`).
2. The indirect call must **execute in guest code**: `rax` (or `r8`) holds
   `base+0x71912e8` at the moment of `call *0x18`. Because `0x649d422`
   tail-jmps, the RA captured is the instruction immediately after the guest
   call (e.g. `0x649325d`), not a host address.
3. The ctx referenced must have `EGLDisplay` at `+0x20` and `EGLSurface` at
   `+0x28` (`0x649d422` reads only those two), otherwise the forwarded real
   `eglSwapBuffers` gets invalid handles.
4. The graphics-device wrapper shape `W[0]=base+0x71908e0, W[0x18]=ctx`
   mirrors the genuine object built at `0x648b967/0x648b971`; this is what
   makes the two-level helper land on the render ctx (DERIVED from those
   instructions; the wrapper vtable slot 3 itself, `0x6493126`, is a
   query-interface method, not the swap forwarder — APS2 OBSERVED).

---

## 3. How a real frame is supposed to reach the swap (context, not claimed here)

- Cadence/work chain (P10): native `RBX IoEvLoop` timerfd/epoll thread
  (`0x6474538`/`0x647461c`, `pthread_create` `0x6474599`) + TaskScheduler
  `SurfaceController::RenderJob` (`0x276d36a`). Surface event
  `0x2773194` → `0x26fea5a`/`0x26fea78` binds the window at `0x72a3308`.
- The actual per-frame guest instructions are the vtable-dispatched draw
  (`0x7190ec0 → 0x64961b4 → 0x6496d0c`) and swap (`0x7191300 → 0x649d422`).
- **UNKNOWN:** the identity of the *outer* loop that calls the §2.1 helpers is
  still not pinned statically (it is itself vtable-dispatched; `0x6493242`
  has zero direct callers). This report does **not** claim to have found that
  loop; it enumerates the dispatch instructions and the minimal way to execute
  one with guest-owned state.

---

## 4. Shortest real-guest-code route to a guest-tagged swap

### 4.1 The route (all executed instructions are guest's own)

Setup (once, observation-only; all data, no engine byte patched):

1. Install the forwarding GOT hook at `0x722a1a8` with RA classification.
2. Obtain a genuine render ctx:
   `ctx = ((void*(*)(void*,void*))(base+0x649c21e))(native_window, /*pbuffer=*/0)`
   → `ctx[0] == base+0x71912e8` (verified in `R3`/P10 and the harness log).
   A shaped 0x48-byte buffer with `[0]=base+0x71912e8`,
   `[0x20]=EGLDisplay`, `[0x28]=EGLSurface` is the synthetic equivalent.
3. Build `W` (0x20 bytes): `W[0] = base+0x71908e0`, `W[0x18] = ctx`.

Execution (one host entry, then pure guest code):

```
host: ((void(*)(void*))(base+0x6493242))(W)
  6493242: push rbp; mov rsp,rbp
  6493246: mov rsi,rdi
  6493249: mov rdi,[rdi+0x18]        ; rdi = ctx
  649324d: mov rax,[rdi]             ; rax = base+0x71912e8
  6493250: mov edx,0xa0 ; mov ecx,8
  649325a: call QWORD PTR [rax+0x18] ; slot3 = 0x649d422   <-- GUEST call
  649d422: mov rax,[rdi+0x20]; mov rsi,[rdi+0x28]; mov rdi,rax
  649d42d: jmp 0x6d334d0 (eglSwapBuffers@plt) -> GOT 0x722a1a8
  649325d: pop rbp; ret              ; <-- captured RA = base+0x649325d
```

`rg_hook_swap()` sees `ra = base+0x649325d`, which satisfies `rg_in_guest()`
(`0x649325d < 0x6d33b90`), so `s_swaps_guest` increments and the log line is
`tag=guest-loop`. The RA is stable across repetitions because `0x649325d` is a
fixed guest address; repeating the call with a draw-submit object
(`D[0x18]=H`, `H[0]=base+0x7190ea8` → slot3 `0x64961b4` → `0x6496d0c` →
`glDrawElements`/`glDrawArrays` GOTs `0x722a040`/`0x722a048`) between two
swaps satisfies T3-5 R1/R2 (`swaps_guest≥2`, draw between them). This is P10 §7
reproduced and pinned to the exact dispatch instruction.

### 4.2 Why this is the *shortest*

- It needs **one** host→guest entry and **seven** guest instructions before the
  dispatched call; the RA is guaranteed guest (`0x649325d`).
- A one-level helper (§2.2) would be only two guest instructions but none is a
  clean standalone thunk (byte search: 0 hits); entering mid-function needs
  extra register/flow state and is not a safer route.
- The second-of-pair helpers (§2.1 bottom) need an extra vtable indirection.
- No guest code contains a direct `call 0x649d422` (grep: 0 hits), so the
  vtable-dispatched helper is minimal.

### 4.3 Contrast with what the harness does today (all SYNTHETIC)

| Path | Caller of `0x649d422` | RA at GOT | Tag |
| --- | --- | --- | --- |
| Route B `guest_swap((uintptr_t)ctx)` (`guest_render.c:312-313,354`) | host | host | `synthetic` (the `1/0/1` of R1) |
| `sober_render_guest_swap_via_vtable()` (`render_guest.c:520-548`) invokes `swap(s_ctx)` from host | host | host | `synthetic` (its own comment at `:545` admits this) |
| **§4.1 route** via guest `0x649325a` | **guest** | `base+0x649325d` | **`guest`** |

So merely reading the correct vtable slot and calling it from C is **not**
enough: the call instruction itself must be guest code. This is the single
difference between the current `0/1` synthetic result and a guest-tagged swap.

---

## 5. Honesty / marker

Every shaped `W`, `ctx` and `D` is seeded guest state, and the §4.1 route is a
diagnostic that produces the render-side *signal* only. Under decisions
013/038 it is a `seeded_state` withhold entry; it supplies no
notifier-driven `gameLoadedCallback` (`0x27eaf6e` → `0x25848e0`, class
`0x72a2f48`, method `0x72a2fc0`, `placeId>0`, RA `base+0x27eafb3`) and does not
lift the marker. `guest-tagged=0` in current logs is the truthful state for the
host-driven Route-B run and must not be inflated. **Marker WITHHELD. No false
green.**

---

## 6. Evidence index and unknowns

**OBSERVED this task:** `sha256sum` of the frozen engine; `objdump -d` slices at
`0x649c260`, `0x649d2b0`, `0x649d410`, `0x648b682`, `0x2a20850`, `0x2a20a00`,
`0x6493000–0x6493900`, one-level sites `0x6477f00/0x647f0f0/0x64812c0/
0x64819e0/0x648db40/0x6490650`, compact sites `0x3a4496a/0x50c096f/
0x5c05a75`; whole-disassembly greps for `71912e8` (2 hits), `7191300` (0 code
hits), `649d422` (1 code hit), `call *0x18` (11,849 hits); APS2 packed-RELA
decode (`/tmp/aps2_vtab.py`, `/tmp/aps2_wrap.py`, `/tmp/aps2_find2.py`) of
vtables `0x71908e0` and `0x71912e8` and of helper addends; byte search for
`48 8b 07 ff 50 18 c3` (0 hits). REA Ghidra session confirmed `xrefs` to all
three addresses are empty. Source read (read-only): `src/linux/guest_render.c
:312-313,354`, `src/jni/render_guest.c :181-192,255-264,520-548`.

**DERIVED:** runtime slot values `base+addend`; wrapper shape as the object
that carries `ctx` at `+0x18`; the §4.1 route's counter effect.

**UNKNOWN / not claimed:** the identity of the *outer* guest loop that calls
the §2.1 helpers in a genuine frame; whether a fully faithful draw can be
produced from a shaped `D` without the real RenderJob step data; none of the
§4.1 observations has been executed at runtime in this analysis-only task.

Prior reports consulted (read-only): `R1_zero_tagged_swaps.md`,
`R3_guest_egl_context.md`, `C04-render-integration.md`,
`T3_5_render-evidence.md`, `P10_render_thread.md`, `REVIEW-100.md §175-176`,
and logs `w_after.out` / `w_final.out`. No file other than this report was
created or modified.
