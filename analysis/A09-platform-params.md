# A9 — PlatformParams audit: `StartAppWithParams` vs `UpdateSurfaceAppWithPlatformParams`

Owner: **worker A9 (Team A)**. Scope: **this file only**. No `src/` edits, no
`CMakeLists` edits, no gate/test edits, no engine patch, no callback emitted and
no decision-013 marker touched.

Target (read-only): `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2e50946b`, stripped x86-64
Android ELF. Every RVA below is for this build. Decoded Dalvik is in
`/tmp/opencode/mini/out/smali`.

Evidence classes: **OBSERVED** = bytes read directly from the frozen ELF with
`objdump -d --start-address/--stop-address`, raw C-string reads at the named
RVAs, or decoded smali; **DERIVED** = JNI slot arithmetic and call-graph
inference; **UNKNOWN** = explicitly not established. No runtime execution was
performed by this worker; runtime statements are quoted from lead logs with the
source named.

---

## 1. Executive answer

1. **`0x26feb9b` is not a native-layout writer — it is a JNI _field_ reader.**
   It fills a **0x34-byte stack scratch struct** and obtains its three inputs by
   `GetObjectClass` + `GetFieldID` + `GetFloatField`/`GetIntField` on the
   `PlatformParams` jobject, using field names `dpiScale`, `viewportWidthMm`,
   `viewportHeightMm` and signatures `"F"`, `"I"`, `"I"`. It does **not** call
   Java getter methods. (OBSERVED / DERIVED)
2. The **0x40-byte native object is built by the callers**, not by the parser:
   `0x26fe98f`/`0x26fec51` install vptr **`0x6dbca20`** in
   `StartAppWithParams`, `0x277328f`/`0x2773354` install vptr **`0x6dbcb20`**
   in `UpdateSurfaceAppWithPlatformParams`. Both copy the scratch struct into
   the allocation **shifted by +8 bytes**, so `native+0x08` is an
   **`ANativeWindow *`** (global `0x72a3308`), not `dpiScale`. (OBSERVED)
3. **Why we only get accessor callbacks:** the `surface()` /
   `platformParams()` / `vrContext()` accessors are read through the
   **method** path (`GetMethodID` + `CallObjectMethod`), which our bridge
   implements with named handlers; the three fields the parser actually needs
   are read through the **field** path (`GetFieldID` + `GetFloatField` /
   `GetIntField`), which our JNIEnv table leaves as `sr_stub` returning 0/NULL.
   Additionally the synthetic `Surface` cannot yield a real `ANativeWindow`, so
   global `0x72a3308` stays NULL, and `0x2773194` is never called on any current
   harness path. Result: callbacks fire, the native PlatformParams carries no
   usable surface/scale, so no frame. (OBSERVED + lead log)
4. **Correction to `analysis/native-object-layouts.md`:** its PlatformParams row
   lists a single vptr `0x6dbcb20` and offsets `+0x08 dpiScale / +0x0c width /
   +0x10 height / +0x18 flag / +0x28 flag`. Those offsets are the **parser
   scratch** offsets, and `0x6dbcb20` is the **UpdateSurface** vtable only. The
   StartApp vtable is `0x6dbca20`, and in the 0x40-byte native object every
   field is at scratch-offset **+8**. `src/jni/startapp_params.h` already has
   both vptr constants right (`0x6dbca20` = `_START`, `0x6dbcb20` = `_UPD`),
   but `startapp_params.c` and `jni_native_objects.c` still build the payload
   with the scratch offsets. (OBSERVED)

---

## 2. Entry points and exact RVAs

| RVA | Symbol / role | Evidence |
| --- | --- | --- |
| `0x26fe80f` | `nativeAppBridgeV2StartAppWithParams(JNIEnv*, jclass, StartAppParams)` — export | OBSERVED |
| `0x26fe8de` / `0x217cb8` / `0x2c2292` | helper `0x247b465(env, class, params, "platformParams", "()Lcom/roblox/engine/jni/model/PlatformParams;")` → object callback | OBSERVED |
| `0x26fe8a9` / `0x2a4f4c` / `0x250e71` | helper `0x247b465(..., "surface", "()Landroid/view/Surface;")` → object callback | OBSERVED |
| `0x26fe8fd` / `0x2346a4` / `0x2c22c1` | helper `0x247b465(..., "vrContext", "()Landroid/app/Activity;")` → object callback, then `NewGlobalRef` at `0x26fe922` | OBSERVED |
| `0x26fea5a` | Surface → `ANativeWindow_fromSurface` helper; stores window in global `0x72a3308` | OBSERVED |
| **`0x26feb9b`** | **PlatformParams parser** (`rdi`=scratch out, `rsi`=JNIEnv, `rdx`=PlatformParams jobject) | OBSERVED |
| `0x26febe0` / `0x535292` | field name `"dpiScale"` (sig string `0x46d6c0` = `"F"`) | OBSERVED |
| `0x26febfa` / `0x2c22da` | field name `"viewportWidthMm"` (sig `0x532c55` = `"I"`) | OBSERVED |
| `0x26fec12` / `0x3897f6` | field name `"viewportHeightMm"` (sig `0x532c55` = `"I"`) | OBSERVED |
| **`0x26fe98f`**, `0x26fec51` | `new(0x40)` + install vptr **`0x6dbca20`** (StartApp object) | OBSERVED |
| `0x26fec3e` | StartApp vtable `+0x30` copy ctor: copies source `+0x08..+0x38` → dest `+0x08..+0x38` | OBSERVED |
| **`0x2773194`** | `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams(JNIEnv*, jclass, Surface, PlatformParams)` — export, Java sig `(Landroid/view/Surface;Lcom/roblox/engine/jni/model/PlatformParams;)V` | OBSERVED |
| `0x277322d` → `0x26fea5a` | same Surface → `ANativeWindow` conversion | OBSERVED |
| `0x2773246` → `0x26feb9b` | same parser, same scratch layout | OBSERVED |
| **`0x277328f`**, `0x2773354` | `new(0x40)` + install vptr **`0x6dbcb20`** (UpdateSurface object) | OBSERVED |
| `0x27732d5` → `0x2eb6b2a` | native object passed through a stack guard into game-global-init | OBSERVED |
| `0x6dbca20 +0x10 = 0x26fec3e`; `+0x30 = 0x26fec82` | StartApp PlatformParams virtual copy ctor / consumer (consumer does `add $0x8,%rbx`) | OBSERVED (APS2) |
| `0x6dbcb20 +0x10 = 0x2773354`; `+0x30 = 0x2773398` | UpdateSurface PlatformParams virtual copy ctor / consumer (consumer does `add $0x8,%rbx`) | OBSERVED (APS2) |
| `0x72a3308` | global `ANativeWindow *` written at `0x26feaa0`, copied to `native+0x08` | OBSERVED |
| `0x232de10`, `0x247b465`, `0x24ac975`, `0x23846d4` | crash-log, method-getter, float-field-getter, int-field-getter helpers | OBSERVED |
| `0x2e5d420` | generic autovalue dispose helper (unrelated to field reading; see `native-object-layouts.md`) | OBSERVED |

Both vtables were resolved with `python3 /tmp/opencode/reloc.py dump <rva>`; they
are distinct tables with parallel slot layouts (not one table at two offsets).
`0x6dbca20` slot `+0x28 = 0x27730ce` and `0x6dbcb20` slot `+0x28 = 0x2773728`
both call `0x1dbe5be` (delete) and are deleting destructors; this reinforces
that these are two classes, not one.

---

## 3. `0x26feb9b` — what the guest actually reads, and how

### 3.1 Entry bytes (OBSERVED)

```
26feb9b  push rbp; ... ; mov rdx,r14 (PlatformParams jobject)
26febaf  mov rdi,rbx (scratch out)
26febb2  xorps xmm0,xmm0
26febb5  movups %xmm0,(%rdi)        ; out+0x00..0x0f = 0
26febba  mov  %rax,0x10(%rdi)       ; out+0x10 = 0
26febbe  movb $0x1,0x18(%rdi)       ; out+0x18 = 1
26febc2  mov  %rax,0x20(%rdi)       ; out+0x20 = 0
26febc6  movb $0x0,0x28(%rdi)       ; out+0x28 = 0
26febca  mov  %rax,0x2c(%rdi)       ; out+0x2c = 0
26febce  mov (%rsi),%rax            ; JNIEnv functions
26febd7  call *0xf8(%rax)           ; GetObjectClass(env, PlatformParams) -> r12
```

`0xf8` is canonical JNI slot 31 = `GetObjectClass` (DERIVED from
`JNINativeInterface_` order; our own bridge maps it at `fn[27]`).

### 3.2 The three field reads (OBSERVED)

```
26febe0  lea 0x535292(%rip),%rcx    ; "dpiScale"
26febf0  call 0x24ac975             ; get float field by name
26febf5  movss %xmm0,0x8(%rbx)      ; out+0x08 = dpiScale

26febfa  lea 0x2c22da(%rip),%rcx    ; "viewportWidthMm"
26fec0a  call 0x23846d4             ; get int field by name
26fec0f  mov  %eax,%r13d
26fec27  mov  %r13d,0xc(%rbx)       ; out+0x0c = widthMm

26fec12  lea 0x3897f6(%rip),%rcx    ; "viewportHeightMm"
26fec22  call 0x23846d4
26fec2b  mov  %eax,0x10(%rbx)       ; out+0x10 = heightMm
26fec3c  ret
```

Helper anatomy (OBSERVED):

* `0x24ac975` — `(env, class, object, name)`; calls `*(envfn + 0x2f0)` with
  `rcx = 0x46d6c0` (`"F"`) = **GetFieldID**, then tail-jumps through
  `*(envfn + 0x330)` = **GetFloatField**. Returns `xmm0`.
* `0x23846d4` — identical, but signature literal `0x532c55` (`"I"`) and
  tail-jump through `*(envfn + 0x320)` = **GetIntField**. Returns `eax`.

| Field name (RVA) | Java type / JNI sig | Guest helper | JNIEnv offset | Our bridge table slot | Destination |
| --- | --- | --- | --- | --- | --- |
| `dpiScale` (`0x535292`) | float, `"F"` (`0x46d6c0`) | `0x24ac975` | `0x2f0` GetFieldID, `0x330` GetFloatField | `fn[90]`, `fn[98]` — **both `sr_stub`** | scratch `+0x08` |
| `viewportWidthMm` (`0x2c22da`) | int, `"I"` (`0x532c55`) | `0x23846d4` | `0x2f0`, `0x320` GetIntField | `fn[90]`, `fn[96]` — **both `sr_stub`** | scratch `+0x0c` |
| `viewportHeightMm` (`0x3897f6`) | int, `"I"` | `0x23846d4` | `0x2f0`, `0x320` | same | scratch `+0x10` |

Slot arithmetic (DERIVED, cross-checked against the bridge's own comment at
`native_jni.c:837` "SETFN(\*t, 138 …) /* slot 142 / offset 0x470 */"): our
`SRNativeInterface` has four reserved pointers before `fn[231]`, so
`fn[index] = canonical_slot - 4` and guest `envfn + off` ⇒ `index = off/8 - 4`.
`0x108/8 - 4 = 29` = `sr_GetMethodID` (matches `native_jni.c:793`);
`0x2f0/8 - 4 = 90`, `0x320/8 - 4 = 96`, `0x330/8 - 4 = 98` — none of which are
assigned in `sr_build_tables()` (`native_jni.c:772-860`), so all three are
`sr_stub` (`native_jni.c:337-341`), which returns `NULL`.

### 3.3 Parser scratch struct (0x34 bytes, at `rdi`)

| Offset | Value | Source |
| --- | --- | --- |
| `+0x00` | 0 in parser; **overwritten by caller** with `ANativeWindow*` (global `0x72a3308`) | `0x26fe93d` / `0x277324b` |
| `+0x08` | float `dpiScale` | field read |
| `+0x0c` | int `viewportWidthMm` | field read |
| `+0x10` | int `viewportHeightMm` | field read |
| `+0x18` | byte `1` (zero-extended to u64) | parser init |
| `+0x20` | 0; **StartApp path overwrites** with `NewGlobalRef(vrContext)` | `0x26fe940` |
| `+0x28` | byte `0` | parser init |
| `+0x2c` | 0 | parser init |
| `+0x30` | 0 (read back by both callers into `-0xb0(%rbp)`) | parser init |

This is the layout `native-object-layouts.md` documented. It is **not** the
final native object layout.

---

## 4. The 0x40-byte native PlatformParams object (corrected layout)

Both callers allocate with `new(0x40)` (`0x1dbab02`), store a guest vptr at
`+0x00`, then copy scratch `+0x00..+0x30` into native `+0x08..+0x38`:

```
2773270  mov (%r15),%rax      ; scratch+0x00 (ANativeWindow*)
277327a  mov 0x8(%r15),%rax   ; scratch+0x08 (dpiScale)
...
2773285  mov $0x40,%edi; call 1dbab02
277328f  lea ... # 6dbcb20; mov %rcx,(%rax)   ; vptr
27732ae  movups %xmm0,0x8(%rax)    ; native+0x08 = scratch+0x00, +0x10 = scratch+0x08
27732b2  movups %xmm1,0x18(%rax)   ; native+0x18 = scratch+0x10, +0x20 = scratch+0x18
27732b6  movups %xmm2,0x28(%rax)   ; native+0x28 = scratch+0x20, +0x30 = scratch+0x28
27732c1  mov %rcx,0x38(%rax)       ; native+0x38 = scratch+0x30
```

| Native offset | Value | StartApp (`vptr 0x6dbca20`) | UpdateSurface (`vptr 0x6dbcb20`) |
| --- | --- | --- | --- |
| `+0x00` | vptr | `0x6dbca20` (`0x26fe98f`, `0x26fec51`) | `0x6dbcb20` (`0x277328f`, `0x2773354`) |
| `+0x08` | `ANativeWindow *` | global `0x72a3308` | global `0x72a3308` |
| `+0x10` | float `dpiScale` | scratch `+0x08` | scratch `+0x08` |
| `+0x14` | int `viewportWidthMm` | scratch `+0x0c` | scratch `+0x0c` |
| `+0x18` | int `viewportHeightMm` | scratch `+0x10` | scratch `+0x10` |
| `+0x20` | u64 flag = 1 | scratch `+0x18` | scratch `+0x18` |
| `+0x28` | 0 or `vrContext` global ref | scratch `+0x20` (StartApp writes vrContext here) | scratch `+0x20` = 0 |
| `+0x30` | int64 = 0 | scratch `+0x28` | scratch `+0x28` |
| `+0x38` | int64 = 0 | scratch `+0x30` | scratch `+0x30` |

The copy-ctor virtuals `0x26fec3e` (StartApp vtable slot `+0x10`) and
`0x2773354` (UpdateSurface slot `+0x10`) copy **source `+0x08..+0x38` →
destination `+0x08..+0x38` with no shift**, proving that `+0x08` (the window
pointer) is the canonical first payload field. Their consumer siblings
`0x26fec82` / `0x2773398` (slot `+0x30`) begin with `add $0x8,%rbx` — i.e. they
skip the vptr and hand the payload, starting at the `ANativeWindow *`, to a
deeper dispatch. (OBSERVED)

Latent consequence for our code: `SOBER_STARTAPP_PLATFORM_DPI = 0x08` in
`startapp_params.h:92` is the **scratch** offset. A synthetic object laid out
that way is not a guest-native PlatformParams: if any guest code ever treats
our handle's backing as native, it will read our `dpiScale` bits as an
`ANativeWindow *` and our width as `dpiScale`. Today the guest only reaches the
object through JNI fields, so this mismatch is dormant rather than the active
blocker — but it must be fixed before the object is claimed to be "guest-shaped".

---

## 5. Surface → `ANativeWindow`: the other half of "real frame"

`0x26fea5a` (OBSERVED) is called by **both** entries
(`0x26fe8d2` in StartApp, `0x277322d` in UpdateSurface):

```
26fea78  call ANativeWindow_fromSurface@plt (0x6d31ad0)   ; (env, surface)
26fea7d  mov %rax,%rbx
26fea80  mov 0x72a3308(%rip),%rdi                        ; previous window
26fea87  cmp %rdi,%rax
26fea90  incl 0x72a3310                                   ; update counter
26fea9b  call ANativeWindow_release@plt (0x6d31ac0)
26feaa0  mov %rbx,0x72a3308                               ; store new window
```

So a real frame requires an `android/view/Surface` jobject that the platform's
`ANativeWindow_fromSurface` can turn into a non-NULL native window. The
harness's `android/view/Surface` is a synthetic 16-byte object
(`startapp_params.c:297-299`, `guest_app_shell.c:1249`); with no real
`Surface`/`IGraphicBufferProducer` behind it, `ANativeWindow_fromSurface`
yields NULL, global `0x72a3308` stays NULL, and the native PlatformParams
`+0x08` goes out NULL.

Log tags corroborate the role (OBSERVED C-string reads): `0x3fb218` =
`"nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams"`, `0x3deb31` =
`"[FLog::JNIAppBridge] nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams:"`.

---

## 6. Exact real field values the Java app supplies

`PlatformParams.smali` declares the fields (OBSERVED):
`assetFolderPath:Ljava/lang/String;`, `dpiScale:F`, `isKeyboardDevice:Z`,
`isMouseDevice:Z`, `isTouchDevice:Z`, `viewportHeightMm:I`, `viewportWidthMm:I`.
(Only the three read by `0x26feb9b` matter to this audit; the other four are
copied by `PlatformParams.a()` for other surfaces.)

The JNI caller is `uh/f.I(Landroid/view/Surface;FII)V` (`uh/f.smali:500-524`):
it creates a fresh `PlatformParams`, writes `dpiScale = p2`,
`viewportWidthMm = p3`, `viewportHeightMm = p4`, then calls
`nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams(p1, params)`. Its caller
`uh/a.surfaceChanged(SurfaceHolder, int, int, int)` (`uh/a.smali:1257-1300`)
supplies:

| Field | Real producer (OBSERVED smali) | Real value shape |
| --- | --- | --- |
| `dpiScale` (F) | `cl/a.e(Context)` = `DisplayMetrics.density` (`cl/a.smali:148-158`) | positive float, typically `1.0`–`4.0` on Android; **never 0** |
| `viewportWidthMm` (I) | `cl/a.i(Context).x` = `round(widthPixels / xdpi * 25.4)` (`cl/a.smali:264-295`, `0x41cb3333` = 25.4f) | physical width in mm, e.g. ~70 for a phone; **positive int** |
| `viewportHeightMm` (I) | `cl/a.i(Context).y` = `round(heightPixels / ydpi * 25.4)` | physical height in mm, e.g. ~150; **positive int** |
| `surface` (arg) | `SurfaceHolder.getSurface()` in `surfaceChanged` | real `android/view/Surface` backed by a native window |
| `enable flag` (`native+0x20`) | parser literal `1` | fixed by guest |
| `native+0x08` | `ANativeWindow_fromSurface` result | non-NULL native window |

`uh/f.I()` is gated by `p0.d` (the "app started" boolean); this is the Java-side
reason `0x2773194` is only called after `StartAppWithParams`, and it confirms
the intended call order `… → StartAppWithParams → surfaceChanged →
UpdateSurfaceAppWithPlatformParams`.

---

## 7. Why our synthetic platformParams yields callbacks but no frame

1. **Method path works (callbacks).** `0x26fe80f` resolves `surface`,
   `platformParams`, `vrContext` via `0x247b465` = `GetMethodID` (guest
   `envfn+0x108`) + `CallObjectMethod`. Our bridge implements `fn[29]` and
   `fn[30..59]`, and `sr_dispatch_call()` dispatches to handlers registered by
   name (`native_jni.c:498-515`). `SOBER_STARTAPP_PARAMS=1` registers all ten
   StartAppParams getter names (`startapp_params.c:357-374`). Therefore the
   three accessor callbacks are observed. (OBSERVED)
2. **Field path does not work (no values).** The very next call, `0x26feb9b`,
   asks for `dpiScale:F`, `viewportWidthMm:I`, `viewportHeightMm:I` via
   `GetFieldID`/`GetFloatField`/`GetIntField` — JNI slots 90/98/96 in our table,
   all left as `sr_stub` → `NULL`/0. No handler is registered under any of those
   three names anywhere in `src/` (grep: only comments mention them). Thus the
   scratch struct keeps its zero defaults: `dpiScale` undefined/zero, both mm
   fields 0. The guest's 0x40 object is then built from zeros. (OBSERVED)
3. **No native window.** The synthetic `Surface` cannot satisfy
   `ANativeWindow_fromSurface`; global `0x72a3308` remains NULL, so native
   `+0x08` is NULL even if the numeric fields are later fixed. (OBSERVED static
   + inference about non-real Surface; lead logs report no frame.)
4. **Update entry never invoked.** `analysis/lua-app-start.md §4` item 9 and
   `analysis/appstate_fields.md:21` record `0x2773194` as **NEVER called** by
   any harness path; it exists only under the real Java `surfaceChanged`
   sequence. Even a perfect PlatformParams cannot produce a frame through a
   call that is not made. (Lead analysis)
5. **The synthetic object's payload is at the wrong offsets** (scratch vs
   native, §4). Harmless while the guest uses JNI field access, fatal if any
   consumer ever reads our backing as a native object. (OBSERVED)
6. **Decision 013 is unaffected.** These accessor callbacks are JNI plumbing;
   the lead's explicit ruling (`PROGRESS.md:276-280`,
   `docs/decisions/013-no-weak-app-shell-marker.md:56-64`) rejects them (and
   `onAppBridgeNotification`) as app-shell proof. Fixing PlatformParams alone
   would not open the gate. (Lead ruling)

Net: our object satisfies the **getter** contract of `StartAppParams` but not
the **field** contract of `PlatformParams`, and the surface half is absent.

---

## 8. Opt-in next step (analysis proposal — not enacted by A9)

A single default-OFF, gate-withheld fidelity module, to be owned by lead or a
later worker:

1. **Implement the JNI field family** in `src/jni/native_jni.c`
   (`sr_build_tables`): `fn[90] = GetFieldID`, `fn[96] = GetIntField`,
   `fn[98] = GetFloatField` (plus the sibling `fn[91..103]` for completeness).
   `GetFieldID` should return a shaped field handle keyed by
   `(class_name, name, sig)`; `GetIntField`/`GetFloatField` should read a typed
   value table attached to the SrObject named by the jobject. This is the
   minimum change that makes `0x26feb9b` see real numbers. Do **not** reuse the
   method-callback registry — it dispatches by method name and cannot answer a
   field read.
2. **Back the synthetic PlatformParams with the three fields** rather than raw
   scratch offsets: on `com/roblox/engine/jni/model/PlatformParams` register
   `dpiScale = "F"`, `viewportWidthMm = "I"`, `viewportHeightMm = "I"` with
   values `DisplayMetrics.density`-like `2.0f` and physical-mm `70`/`150`
   (defaults), overridable by env. Keep the 0x40-byte native payload but move
   it to the corrected layout (`+0x08` window placeholder, `+0x10` float,
   `+0x14`/`+0x18` ints, `+0x20` = 1).
3. **Replay the real Java step**: call `0x2773194` with a real `Surface`
   equivalent (a host `ANativeWindow*` seeded at global `0x72a3308` via an
   opt-in, not a fake vtable) after `StartAppWithParams`. Verify the guest
   writes a non-NULL `+0x08` and non-zero `+0x10/+0x14/+0x18` via a diagnostic
   readback at the allocation returned by `0x2773285`.
4. **Gate discipline:** new env var (e.g. `SOBER_PLATFORM_PARAMS_FIELDS=1`)
   default OFF, added to the decision-013 forced-withhold list. No direct
   guest-byte patch, no host-function vtable, no fabricated callback. Even a
   successful frame from this path is **not** app-shell proof; the gate may only
   open on a genuine `gameLoadedCallback(J)V` from notifier `0x27eaf6e` → thunk
   `0x25848e0` plus phase-3 render `0x649d422`, with every opt-in off.
5. **Ordering constraint:** this work is downstream of the existing
   `SOBER_STARTAPP_PARAMS` / `SOBER_NATIVE_OBJECTS` modules; it must run after
   `guest_base` is known (vptrs are `guest_base + RVA`) and after the FFlag ctor
   replay (per `PROGRESS.md` Phase 8) so the app-side objects exist.

Verification commands (no code change): `bash /tmp/opencode/dis.sh 26feb9b`,
`objdump -d --start-address=0x26feb9b --stop-address=0x26fec3c <engine>`,
`python3 /tmp/opencode/reloc.py dump 0x6dbca20|0x6dbcb20`, and a grep of the
bridge JNI log for `GetFieldID(dpiScale...)` / `GetFloatField` / `GetIntField`
once the opt-in exists. (UNKNOWN until implemented.)

---

## 9. Limitations and unknowns

* Static analysis only. The concrete runtime values of `dpiScale` and the two
  mm fields on this host are **UNKNOWN** (no Android `DisplayMetrics` exists in
  the harness); the ranges in §6 come from decoded Dalvik, not observation.
* Whether a non-NULL `ANativeWindow *` can be supplied faithfully without
  fabricating Android graphics internals is **UNKNOWN**; this is the hardest
  part of the opt-in and may remain blocked under decision 013.
* The precise deeper consumer reached through `0x6dbcb20 +0x30 = 0x2773398`
  (`vtable +0xb8` of the object from FFlag-gated global `0x72a31a8`) was not
  chased past the dispatch; it is **DERIVED** that it consumes the payload
  starting at `native+0x08`, not that it reads each field.
* No runtime backtrace was taken by this worker; the "callbacks but no frame"
   characterisation quotes `PROGRESS.md` Phase 7/8 and `analysis/lua-app-start.md`.
* No REA session was open for this task; all binary claims come from direct
  `objdump`/raw-byte reads of the frozen engine, cited by RVA above. The engine
  file was not modified (its SHA-256 is unchanged).

## 10. Evidence index

* Engine disassembly: `/tmp/opencode/fn_26fe80f.txt`,
  `/tmp/opencode/fn_2773194.txt`, `/tmp/opencode/fn_UpdateSurface.dis`,
  `/tmp/opencode/fn_StartAppWithParams.dis`, `/tmp/opencode/dis.sh`,
  `/tmp/opencode/reloc.py`, and direct `objdump -d` slices listed in §2.
* Decoded APK: `/tmp/opencode/mini/out/smali/com/roblox/engine/jni/model/PlatformParams.smali`,
  `ok/a.smali`, `uh/f.smali`, `uh/a.smali`, `uh/p.smali`, `uh/j0.smali`,
  `cl/a.smali`, `rg/a1.smali`.
* Harness (read only): `src/jni/native_jni.c` (`sr_build_tables`,
  `sr_dispatch_call`, `sr_stub`), `src/jni/startapp_params.c/.h`,
  `src/jni/jni_native_objects.c`, `src/linux/guest_app_shell.c`.
* Prior analysis (read only): `analysis/native-object-layouts.md` (corrected
  here), `analysis/lua-app-start.md`, `analysis/appstate_fields.md`,
  `analysis/game-loaded-path.md`, `PROGRESS.md`,
  `docs/decisions/013-no-weak-app-shell-marker.md`.
