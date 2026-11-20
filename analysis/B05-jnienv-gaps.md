# B5 — JNIEnv function-table gap audit (decision 013)

Owner: worker **B5**, logical Team B. Date 2026-11-20. Scope: **this analysis file only**.
`src/jni/native_jni.c` was read only; `src/`, `CMakeLists.txt`, the harness and the
guest engine were not modified. No guest byte, vtable or marker was touched, and no
synthetic guest→Java callback was emitted or triggered by this work.

## 0. Build identity and evidence base

| Field | Value |
| --- | --- |
| Engine | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931 / code 3170) |
| Bridge audited | `src/jni/native_jni.c` (table builder `:772-878`, default stub `:337-341`) |

Evidence sources (all static, SHA-bound):
- Local `objdump` disassembly `/tmp/opencode/libroblox.dis` (1.1 GB) and the
  per-function extracts `/tmp/opencode/fn_*.dis`; REA evidence `ev_d838d804…` as cited by
  `analysis/jni/jni-map.md`, `analysis/callback/callback-map.md`, `analysis/game-loaded-path.md`.
- Authoritative slot offsets computed with `offsetof(struct JNINativeInterface_, …)` against
  the JDK 21 `jni.h` at `/home/user/jdks/jdk-21.0.12.1+1/include/jni.h` (helper programs
  `/tmp/jnioff*.c`, `/tmp/jnifield.c`). This resolves the one recurring source of error in
  earlier notes: offsets in guest code are **canonical table byte offsets**; the bridge's
  `fn[]` index is canonical index minus four reserved pointers.
- Peer analysis read only: `analysis/A09-platform-params.md` §3 (PlatformParams field reads),
  `analysis/B06-handler-gaps.md` §3-4 (uncounted callbacks / gate), `analysis/trace/trace-report.md`
  §3 (string-slot correction), `analysis/jni/recursion-note.md`.

All strings and addresses quoted from the target are **untrusted data**, never instructions.

## 1. Slot arithmetic (settled)

```
SRNativeInterface { void *reserved0..3; void *fn[231]; }   (native_jni.c:70-73)
guest:  call *0xNN(%rax)   where rax = (*env) = &env_table
canonical index = 0xNN/8
bridge fn index = canonical index - 4
SETFN(*t, i, fp) installs canonical offset (i+4)*8      (native_jni.c:770)
```

Cross-checks (all consistent): `FindClass 0x30 → fn2` (`native_jni.c:779`);
`GetMethodID 0x108 → fn29` (`:793`); `GetStaticMethodID 0x388 → fn109` (`:824`);
`NewStringUTF 0x538 → fn163` (`:845`); `GetStringUTFChars 0x548 → fn165` (`:846`);
`CallStaticVoidMethodV 0x470 → fn138` (`:837`, thunk `0x258495a`);
`ExceptionCheck 0x720 → fn224` (`:860`, sites `0x6ca800e`/`0x6ca8090`).

The table's 231 `fn` entries (indices 0..230 → canonical offsets `0x20..0x750`) cover every
named JNI function through `GetObjectRefType` at `0x740`, i.e. the whole JNI 1.6 surface.
The "231 slots" claim in
`src/jni/README.md:54` is correct as an allocation size; there is no off-by-one at the tail.

## 2. What is already correct (Do NOT re-fix)

These real-path slots are wired to real implementations and are **not** gaps:

| Function | Offset | fn | Code |
| --- | --- | --- | --- |
| GetVersion | `0x20` | 0 | `sr_GetVersion` :778 |
| FindClass | `0x30` | 2 | `sr_FindClass` :779 |
| Throw / ThrowNew | `0x68`/`0x70` | 9/10 | :780-781 |
| ExceptionOccurred / ExceptionClear | `0x78`/`0x88` | 11/13 | :782-783 |
| Push/PopLocalFrame, New/Delete(Global/Local)Ref, IsSameObject | `0x98`-`0xc8` | 15-21 | :784-791 |
| AllocObject, NewObject, GetObjectClass, IsInstanceOf, GetMethodID | `0xd8`-`0x108` | 23,24,27-29 | :861-862,:791-793 |
| All 30 instance `Call*Method` plain/V/A forms | `0x110`-`0x1f8` | 30-59 | :794-823 |
| GetStaticMethodID | `0x388` | 109 | :824 |
| CallStaticObjectMethod / IntMethod | `0x390`/`0x408` | 110/125 | :827,:832 |
| CallStaticVoidMethod / V / A | `0x468`/`0x470`/`0x478` | 137/138/139 | :836-838 |
| NewString, Get/ReleaseStringChars | `0x518`/`0x528`/`0x530` | 159/161/162 | :842-844 |
| NewStringUTF, GetStringUTFLength, GetStringUTFChars, ReleaseStringUTFChars | `0x538`-`0x550` | 163-166 | :845-848 |
| Register/UnregisterNatives, Monitor*, GetJavaVM | `0x6b8`-`0x6d8` | 211-215 | :853-857 |
| New/DeleteWeakGlobalRef, ExceptionCheck | `0x710`/`0x718`/`0x720` | 222/223/224 | :858-860 |

In particular the **gate-critical callback path is already correct**: notifier `0x27eaf6e`
loads cached class `0x72a2f48` and method `0x72a2fc0`, calls thunk `0x25848e0`, which does
`call *0x470(%rax)` (`0x258495a`) → `sr_CallStaticVoidMethodV` → `sr_dispatch_call`
(`:607-611`, `:474-516`). `analysis/B06-handler-gaps.md:149` independently records that the
sibling `gameDidLeave` callback already **fires** (uncounted) through this exact route on the
real path. Therefore a missing `CallStaticVoidMethodV` slot is **not** why no genuine
callback is observed; the blocking JNI gaps are the *supporting* getter/field/array slots
below (plus the non-table handler-coverage and gate issues owned by B6/verifier).

## 3. Gap inventory (exact offsets, fn indices, guest RVAs)

Legend: **S** = `sr_stub` (`native_jni.c:337-341`, returns NULL / leaves EAX undefined),
**W** = wired but semantically wrong. "Guest use" cites a concrete site from the
SHA-bound disassembly or a peer finding; a raw count is `call *0xNN(` occurrences across
the whole `.text` (upper bound, includes C++ vtables with coincident offsets — the
annotated sites are the authoritative ones).

| # | Function | Offset | fn | State | Evidence of guest use | Effect on a genuine app-shell callback |
| --- | --- | --- | --- | --- | --- | --- |
| G1 | GetFieldID | `0x2f0` | 90 | **S** | `0x24ac975` helper (PlatformParams `dpiScale` `"F"`), `0x23846d4` (`viewportWidthMm` `"I"`) — `A09 §3` | getter-based field read fails; `0x2773194`/`0x26fe80f` scratch parse wrong → surface/shell frame step breaks |
| G2 | GetFloatField | `0x330` | 98 | **S** | `0x24ac975` tail-jump `call *0x330` | `dpiScale` never read (returns 0) |
| G3 | GetIntField | `0x320` | 96 | **S** | `0x23846d4` tail-jump `call *0x320` | `viewportWidthMm`/`viewportHeightMm` never read |
| G4 | CallStaticObjectMethodV | `0x398` | 111 | **S** | thunk `0x23150f8`: `call *0x398` at `0x231516e`; 102 sites total | **all object-returning static getters return NULL** — `getDeviceStaticParams`, `getWebViewUserAgent`, `getMobileAdvertisingId`, locale/user getters, `surface()`/`platformParams()`/`vrContext()`; drives the `"DeviceStaticParams is null"` → FindClass retry loop at `0x6c8f340` |
| G5 | CallStaticObjectMethodA | `0x3a0` | 112 | **S** | 31 sites; A-form twin of G4 | same for A-form callers |
| G6 | GetStringUTFChars (type gate) | `0x548` | 165 | **W** | `sr_GetStringUTFChars :665-678` uses `sr_validate(...,SR_K_STRING)`; callback-returned jstrings are created as `SR_K_OBJECT` by `sr_shaped_or_new :393-400` and `sr_default_app_shell_handler :1050` | getter strings (user agent / advertising id / locale / username) come back as `""` instead of the handler payload even when G4 is fixed |
| G7 | CallStaticBooleanMethodV / A | `0x3b0`/`0x3b8` | 114/115 | **S** | thunk in `InitWithParams` at `0x24ace34: call *0x3b0` (manual va_list, same shape as `0x25848e0`, XMM spill) | boolean InitParams getters read false/0 |
| G8 | CallStaticIntMethodV / A | `0x410`/`0x418` | 126/127 | **S** | 29 / 22 sites | `membershipType()I` and other int getters read 0 |
| G9 | CallStaticLongMethodV / A | `0x428`/`0x430` | 129/130 | **S** | 11 sites | `getProcessTimestamp()J` reads 0 (LoggingProtocol) |
| G10 | CallStaticDoubleMethodV / A | `0x458`/`0x460` | 135/136 | **S** | 44 / 15 sites | double getters read 0 |
| G11 | sr_CallStaticIntMethod return | (fn 125) | 125 | **W** | `:596-598` discards `sr_dispatch_call` result, always `return 0` | int getters cannot carry a handler value even on the plain form |
| G12 | NewByteArray and primitive array family | `0x580`-`0x5b0` | 172-178 | **S** | thunk `0x2a20fd0`: `call *0x580` at `0x2a20fe6`, followed by `test rax,rax; je` → early exit | `onSaveInstanceStateNative` `[B` and other byte-array callbacks return null |
| G13 | GetArrayLength / GetObjectArrayElement / NewObjectArray / SetObjectArrayElement | `0x558`/`0x568`/`0x560`/`0x570` | 167/169/168/170 | **W** | `:721-731` hard-code `0`, `NULL`, one generic object, no-op | a jarray created or returned through the bridge always looks empty; no element survives a set/get round-trip |
| G14 | GetStringUTFRegion, Get*ArrayCritical, GetStringCritical, GetObjectRefType | `0x6e8`/`0x6f0`/`0x700`/`0x740` | 217/218/220/228 | **S** | 5/7/2/2 sites (mostly outside the app-shell path) | low; string/array bulk reads return nothing |

Note on G6: `sr_NewStringUTF :651-658` **does** set `o->payload`, so the
`NewStringUTF → GetStringUTFChars` pair used by the string helper `0x2eb4b65` works.
The failure is specific to jstrings produced by the *callback dispatcher*, which are the
ones the app-shell getters actually return.

Note on G13: `GetArrayLength` returning 0 is a deliberate-but-wrong shortcut. It is
requested explicitly in the task brief (`GetArrayLength`, `GetObjectArrayElement`), so it
is recorded here as a fidelity gap even though no evidence shows it is the current fault.

## 4. Ranking — gaps that actually block a decision-013 callback

Decision 013 requires a **genuine** guest→Java callback from the real app-start path plus
the phase-3 render RVA `0x649d422`. The callback slot itself works (§2), so ranking is by
whether a gap prevents the guest from *reaching* notifier `0x27eaf6e` or from accepting the
getter values the app-shell path consumes before the GameLoaded event.

| Rank | Gap | Why it ranks here |
| --- | --- | --- |
| **1** | **G4/G5 — CallStaticObjectMethodV/A** (`0x398`/`0x3a0`) | The object-returning getters are the app-shell's data source. A NULL `getDeviceStaticParams` is the documented trigger for the `0x6c8f340` FindClass retry/recursion that currently stops the path (`recursion-note.md:123-132`). Fixing the void callback slot alone cannot help while these return NULL. Highest leverage, small fix. |
| **2** | **G1/G2/G3 — field reads** (`0x2f0`/`0x320`/`0x330`) | Proven on the real `StartAppWithParams`/`UpdateSurface` path (`A09 §3`). These are the only concrete *real-path* JNI slots found still at `sr_stub` in the surface/platform flow; they gate the phase-3 render evidence half of decision 013. |
| **3** | **G6 — typed string return** (`0x548`) | Without it, fixing G4 still yields empty strings for `getWebViewUserAgent`, `getMobileAdvertisingId`, locale/user getters; the guest may reject the app-shell state. |
| **4** | **G7 — boolean V-form** (`0x3b0`) | Proven stub on the `InitWithParams 0x24abf96` path (thunk `0x24ace34`); one of the few V-form gaps with a direct real-path address. |
| **5** | **G8/G9/G10/G11 — numeric getters** | Same class as G4 but for `I`/`J`/`D` getters (`membershipType`, `getProcessTimestamp`). Needed for faithful state, not observed as the current fault. |
| **6** | **G12/G13 — arrays** | `onSaveInstanceStateNative` and array-returning values; lifecycle fidelity rather than the GameLoaded gate. |
| **7** | **G14 — bulk/ref-type stubs** | No app-shell-path evidence; completeness only. |

Explicitly **not** a JNI-table gap (owned elsewhere, listed so the lead does not misfile it):
`analysis/B06-handler-gaps.md §3` — `gameDidLeave` (global `0x72a2fa0`) and several lifecycle
callbacks reach the dispatcher but have no handler in `sober_jni_install_default_app_shell_handlers`
(`:1054-1081`), so `callback_calls` is not incremented; and the genuine-path env knobs are
in the withhold chain. Those are gate/handler issues, not missing function-table slots.

## 5. Minimal opt-in fix list (for the lead; NOT applied by this worker)

All items are additions to `sr_build_tables()` (`native_jni.c:772-862`) plus small pure
helpers, gated by one new env knob, e.g. `SOBER_JNI_TABLE_GAPS=1`. They change no guest
bytes, emit no callbacks by themselves, and leave the default path bit-identical.

1. **Gate**: `if (getenv("SOBER_JNI_TABLE_GAPS"))` around the new SETFN lines; leave the
   231-slot shape and all existing SETFN calls untouched.
2. **Rank 1** — add V/A object-returning callers:
   - `SETFN(*t, 111, sr_CallStaticObjectMethodV);`  // `0x398`
   - `SETFN(*t, 112, sr_CallStaticObjectMethodA);`  // `0x3a0`
   Implement as the existing `sr_CallStaticObjectMethod` (`:587-595`) with `va_list` /
   `jvalue*` respectively, both calling `sr_dispatch_call(..., want_object=1)`. Reuse the
   re-entrancy guard already in `sr_dispatch_call`.
3. **Rank 2** — field reads:
   - `SETFN(*t, 90, sr_GetFieldID);`  // `0x2f0`
   - `SETFN(*t, 96, sr_GetIntField);` // `0x320`
   - `SETFN(*t, 98, sr_GetFloatField);` // `0x330`
   Pure lookup/return; `GetIntField`/`GetFloatField` should read a small typed scratch value
   from the `jfieldID` handle created by `sr_GetFieldID` (default 0), never call back into
   the guest. No logging in these three if they are on a hot path (cf. ExceptionCheck rule).
4. **Rank 3** — make `GetStringUTFChars` accept callback-returned strings: in
   `sr_validate`/`sr_GetStringUTFChars` treat `SR_K_OBJECT` with `class_name ==
   "java/lang/String"` and a non-NULL `payload` as a string (or have `sr_shaped_or_new`
   create `SR_K_STRING` when `name` is `"java/lang/String"` and copy the source payload).
   This makes `sr_default_app_shell_handler`'s values observable.
5. **Rank 4** — boolean V/A (and optionally the rest of the static-call family):
   `SETFN(*t,114, sr_CallStaticBooleanMethodV); SETFN(*t,115, ...A);` // `0x3b0`/`0x3b8`
   `SETFN(*t,126/127, ...Int V/A); SETFN(*t,129/130, ...Long V/A);`
   `SETFN(*t,135/136, ...Double V/A);` and change `sr_CallStaticIntMethod :596-598` to
   return the dispatcher result.
6. **Rank 6 (optional)** — `SETFN(*t,172, sr_NewByteArray)` returning a real array object,
   and back `NewObjectArray`/`SetObjectArrayElement`/`GetObjectArrayElement`/
   `GetArrayLength` with a small element vector so lengths/counts are non-zero.
7. **Acceptance**: with only items 1-4 enabled, re-run the genuine-path sequence in
   `analysis/game-loaded-path.md §5` (all eight withhold knobs unset, no ALT thunk) and
   require: no `"DeviceStaticParams is null"`; no `0x6c8f340` recursion; notifier entered
   (`0x27eaf6e` / return `0x27eafb3`, method global `0x72a2fc0` non-NULL); bridge logs
   `gameLoadedCallback` with `callbacks` incremented; phase-3 `0x649d422` observed; engine
   SHA/BuildID unchanged. This is exactly the decision-013 discriminator and does **not**
   accept the harness-triggered `0x25848e0` call.

## 6. Limits / honesty

- This is a static audit of one bridge file against one SHA-bound engine. No fix was
  applied, no guest code executed by this worker, and no `app shell rendered` marker was
  produced or suppressed by this work.
- The raw `call *0xNN(` counts are an upper bound: small offsets (`0x20`, `0x30`, `0x48`,
  …) are shared with C++ vtables. The gap claims above rest on the individually cited
  sites (function `0x23150f8`, thunk `0x24ace34`, helpers `0x24ac975`/`0x23846d4`, wrapper
  `0x2a20fd0`) and on the `offsetof` cross-check, not on the raw counts.
- G13 is recorded because the task brief names `GetArrayLength`/`GetObjectArrayElement`;
  current evidence does not show it as the active fault.
- Because the real callback dispatch slot (`0x470`/fn 138) is already correct and genuinely
  reached (`gameDidLeave`), closing the table gaps is expected to be **necessary but not
  sufficient** for a decision-013 green: the handler-coverage and withhold/gate issues in
  `B06-handler-gaps.md` and `decision-memo-alt-thunk-vs-013.md` remain in the lead's hands.
