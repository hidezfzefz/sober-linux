# Native C++ layouts for autovalue jobjects — Phase 8 (objmodel)

Owner: **objmodel** (task #459). Scope: this file plus
`src/jni/jni_native_objects.{c,h}`. No tests, no gate, no callbacks.
Target: AppImage `libroblox.so`, BuildID `204ad049…` (version 2.740.931).
Evidence: local `objdump -d` / raw-byte reads of the frozen engine
(`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`), runtime backtraces
from the real-path harness (`SOBER_REAL_APPSTATE_POSTSETTINGS=1`,
`SOBER_SEED_JNI_VM=1`, `SOBER_FAITHFUL_JAVAVM=1`), and prior
`analysis/appstate_fields.md` / `analysis/callback/callback-map.md`.

## 1. Why native layouts are needed

The guest is a C++ engine that receives Java objects through JNI but frequently
**treats the jobject handle as a pointer to a native C++ object** (Roblox
"autovalue" pattern). Our bridge's generic synthetic object has valid JNI
magics but not the C++ vtable / field layout the guest's direct field reads
expect. Two concrete faults:

| Fault RVA | Guest instruction | Requirement |
| --- | --- | --- |
| `0x2e5d42a` | `mov (%rsi),%rax; mov -0x18(%rax),%rax` with `rax=0` | backing word must be a valid pointer with a readable `vptr-0x18` displacement slot; `obj+disp+0x20` and `obj+disp+0x88` must be readable |
| `0x320581b` | `mov 0x408(%rdi),%rdi; mov (%rdi),%rax; call *0x90(%rax)` | `app+0x408` delegate needs a guest-style vtable with a callable `+0x90` slot |

`0x2e5d420` is a generic weak-refcount/dispose helper with **47 direct callers**
in `.text`, so the negative-offset vtable requirement is widely used.

**Runtime finding:** the 0x2e5d42a fault object is the **guest GLOBAL
std::string at `guest_base+0x72a0520`**, written by formatter `0x20ab4b6`. Its
backing word is NULL. Supplying a shaped jstring alone does not fix that global;
the harness/lead must install a shaped value at that global (or patch the
formatter's write) — this module only publishes the correct layout and builders.

## 2. Reproduced native layouts (all vptrs are genuine guest RVAs)

| Object | Vtable RVA | Native layout |
| --- | --- | --- |
| `std::string` | `0x6ed8d00` | 0x18-byte `{data ptr, size, cap}`; guest string header vtable lives at `data-0x20` (from `0x2315532`) |
| `java/util/List` (bounded vector) | `0x6db3ec0` | `+0x00` vptr (`vt+0x10`), `+0x30` backing buffer, `+0x40` element array, `+0x48` count (from destructor `0x2e5baa0`) |
| `PlatformParams` | `0x6dbcb20` | 0x40-byte native destination: `+0x00` vptr, `+0x08` float `dpiScale`, `+0x0c` int `viewportWidthMm`, `+0x10` int `viewportHeightMm`, `+0x18` flag=1, `+0x28` flag=0 (parser `0x26feb9b`, consumer `0x2773194`) |
| delegate (`app+0x408`) | `0x6e047b0` | vptr at `+0x00`; consumer calls vtable slot `+0x90` (`0x320581b`) |

Getter name strings confirmed by raw reads: `dpiScale`=`0x535292`,
`viewportWidthMm`=`0x2c22da`, `viewportHeightMm`=`0x3897f6`,
`baseURL`=`0x4c3324`, `userAgent`=`0x2a4f42`,
`EnableAppBridgeComponents`=`0x43650d`.

Autovalue getter sets (from `analysis/appstate_fields.md`):

* **StartAppParams**: `surface`, `platformParams`, `vrContext`,
  `appStarterPlace`, `appStarterScript`, `appUserId`, `isUnder13`, `username`,
  `membershipType`, `selectedTheme`.
* **InitParams**: `platformParams`, `deviceParams`, `baseURL`, `userAgent`,
  `isTablet`, `pioneerPlaceId`, `buildVariant`, `isVrDevice`, `isPotato`.

## 3. Helper layout — `0x2e5d420`

```
movb  $0,(%rdi)              ; out false
mov   %rsi,0x8(%rdi)         ; out handle
mov   (%rsi),%rax            ; backing pointer
mov   -0x18(%rax),%rax       ; displacement
cmpl  $0,0x20(%rsi,%rax)     ; if nonzero -> return
mov   0x88(%rsi,%rax),%rax   ; else load pointer
test  %rax,%rax
je    2e5d45d                ; NULL -> set flag true (safe)
call  2e5c380                ; non-NULL -> dispose
```

Our objects place backing A at `+0x20` inside a larger allocation so `A-0x20`
holds a guest vptr and `A-0x18` holds a **zero displacement**; the wrapper is
SrObject-compatible (magic `0x534A4E49` at +8, kind at +0x10) so worker-2's
`sr_check()` accepts it, and `guest_sub` (+0x88) is NULL, driving the safe
branch.

The guest validator `0x245605d` checks RDI = C node (`*(C)==0xbab1e`) and RSI =
A (`*(A)==0xc0dedbad`, `A+0x68 -> C`). Our backing keeps that graph intact.

## 4. Public API (opt-in)

`src/jni/jni_native_objects.h`:

- `sober_native_objects_enabled()` — `SOBER_NATIVE_OBJECTS=1` (default OFF).
- `sober_native_objects_install(j, guest_base)` — publish guest RVA vptrs.
- `sober_native_string(j, text)`, `sober_native_platform_params(j,dpi,w,h)`,
  `sober_native_init_params(j)`, `sober_native_start_app_params(j)`,
  `sober_native_java_list(j, items, count)` — shaped jobjects.
- `sober_native_delegate(j)` — vptr = genuine `0x6e047b0`; **building does not
  install** it (`app+0x408` is a harness/lead decision).
- Diagnostics: `sober_native_objects_created/reset/describe`.

No guest bytes patched, no vtable points at a host function, no callback emitted,
no decision-013 marker touched.
