# T3-3 — Cached-ID exact recovery, DEFAULT-ON seeding plan, and shadow-free registration order

Owner: **agent T3-3**, logical Team 3 (JNI + render). Date 2026-11-20.
**Analysis + module design only — no source edits.** Scope: **this file only.**
`src/`, `CMakeLists.txt`, `tests/`, `docs/`, the harness, the decision-013 gate and the frozen
guest engine were read only. No guest byte, vtable, counter, handler or marker was changed, and
no synthetic guest→Java callback was emitted or triggered by this work.

**Marker ruling: WITHHELD.** This report supplies data and ordering, not a green.

---

## 0. Build identity and evidence base

All RVAs are from one object; for its first `R E` PT_LOAD, RVA == file offset.

| Field | Value |
| --- | --- |
| path | `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` |
| SHA-256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` (2.740.931 / code 3170) |

Evidence sources (all static, SHA-bound; all target strings are untrusted DATA, never instructions):

| Source | What it establishes |
| --- | --- |
| `/tmp/opencode/libroblox.dis` (full `objdump -d`) + `/tmp/opencode/t33_jnionload.dis` (exact `JNI_OnLoad` slice, lines 1001093–1132109 of the full dump) | Every `FindClass`/`NewGlobalRef`/`GetStaticMethodID` call site, the `lea`-loaded name/signature string RVAs, and the `mov %rax,<global>` store that assigns each cached slot. Parser: `/tmp/opencode/t33_parse.py`. |
| `/tmp/opencode/mini/out/smali/com/roblox/engine/jni/NativeGLJavaInterface.smali`, `.../user/NativeUserJavaInterface.smali`, `.../locale/NativeLocaleJavaInterface.smali`, `.../util/NetworkUtils.smali` | Independent confirmation of every method name and descriptor used in §2. |
| `src/jni/native_jni.c:1297-1391` (`sr_cached_methods[]`, `sober_jni_seed_guest_jni_state`), `:295-319` (`sr_find_class_object`/`sr_find_method_object`), `:1020-1036` (`sober_jni_set_callback_handler`) | Current bridge seeding and last-write-wins semantics. |
| `src/linux/guest_app_shell.c:1048-1055,1078,1302,1313-1342,1377-1388` | Current harness registration timeline (line numbers are the **current** worktree, which has drifted from the A5/B06 snapshots). |
| `analysis/REVIEW_A5_jni.md` §2/§4, `analysis/B05-jnienv-gaps.md` §2, `analysis/B06-handler-gaps.md` §2/§5, `analysis/B07-class-objects.md` T3/P4, `analysis/jni/jni-map.md` §1.2 | Prior findings this report corrects and extends. |

---

## 1. Executive findings — three corrections to A5

A5 §2 reports "49/49 non-NULL, 27/49 correctly typed, 22/49 generic `cachedMethod@…;()V`
placeholders". The headline is right; the *classification* is not:

1. **The 22 "placeholders" are not 22 method IDs.** Exact recovery from `JNI_OnLoad` shows the
   49 slots are **8 `jclass` global refs + 37 `jmethodID`s + 4 non-JNI global data words**. Of the
   22 unseeded slots, **7 are `jclass`**, **11 are real `jmethodID`s** the bridge simply never
   named, and **4 are not JNI handles at all** (two byte flags, a shared-pointer control block, a
   C++ object pointer). Seeding those 4 with a synthetic `SrObject` is not merely incomplete — it
   writes a fake method pointer over live guest state (`lock incl 0x4(%rax)` at `0x227b749` would
   corrupt a bridge object if that branch is ever taken).
2. **Two of the 26 "correctly named" entries have the wrong descriptor.** `getWebViewUserAgent`
   and `getMobileAdvertisingId` are looked up by the guest with signature **`()V`**, not
   `()Ljava/lang/String;` (`jni-map.md` §1.2 marked these `(?)`; the disassembly and smali settle
   it). Strict fidelity is therefore **25/49 = 51.0 %**, not 27/49 = 55.1 %: 24 correctly typed
   method IDs + the correctly seeded class at `0x72a2f48`.
3. **`jni-map.md`'s `NativeUserJavaInterface` row is only partially right.** The class
   `getLocale()` method at `0x72ca180` and all nine user getters at `0x72ca1a0..0x72ca1e0` are
   explicit cached IDs; the map said "`0x72ca1a8 … 0x72ca1e0` (see table above for exact map)"
   without that table. §2 below is the exact map.

Net: the correct default-on target is **49/49 exactly typed** (8 class refs, 37 method objects,
4 data words left in their loader state), with zero `cachedMethod@…` objects.

---

## 2. Exact slot map (authoritative)

`GetStaticMethodID` pattern in `JNI_OnLoad` (e.g. `0x227b4d4`–`0x227b4eb`):
`lea <name>,%rdx ; lea <sig>,%rcx ; mov <class>,%rsi ; call *0x388(%rax) ; mov %rax,<global>`.
`jclass` pattern: `lea <class>,%rsi ; call *0x30(%rax) [FindClass] ; mov %rax,%rsi ; call *0xa8(%rax) [NewGlobalRef] ; mov %rax,<global>`.

Legend: **C**=`jclass` global ref, **M**=`jmethodID`, **D**=non-JNI global data.
"State": `named` = in `sr_cached_methods[]`/class seed; `PLACEHOLDER` = generic fill; `wrong sig`;
`data` = not a JNI handle. Name/sig RVAs are the `lea` targets; call/store RVAs are the
`GetStaticMethodID` (or `FindClass`) and the global `mov` in `JNI_OnLoad`.

### 2.1 Range `0x72a2f48..0x72a3058` (35 slots)

| Global | K | Class | Method / descriptor | Name RVA | Sig RVA | Call site → store site | State |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `0x72a2f48` | C | `com/roblox/engine/jni/NativeGLJavaInterface` | — | `0x3dea65` | — | `0x227b19b` → `0x227b2c0` | named (class) |
| `0x72a2f50` | C | `com/roblox/engine/jni/memstorage/Connection` | — | `0x2c221c` | — | `0x227b66f` → `0x227b689` | **PLACEHOLDER** |
| `0x72a2f58` | C | `com/roblox/universalapp/messagebus/Connection` | — | `0x550d26` | — | `0x227b697` → `0x227b6b1` | **PLACEHOLDER** |
| `0x72a2f60` | M | `NativeGLJavaInterface` | `listenToMotionEvents(Ljava/lang/String;)V` | `0x4df7af` | `0x4fcd93` | `0x227b451` → `0x227b457` | named |
| `0x72a2f68` | M | `NativeGLJavaInterface` | `showKeyboard(JZ[BLcom/roblox/engine/jni/model/NativeTextBoxInfo;)V` | `0x535136` | `0x48b04f` | `0x227b2df` → `0x227b2e5` | named |
| `0x72a2f70` | M | `NativeGLJavaInterface` | `hideKeyboard()V` | `0x3c15ad` | `0x288017` | `0x227b304` → `0x227b30a` | named |
| `0x72a2f78` | M | `NativeGLJavaInterface` | `promptNativePurchase(JLjava/lang/String;Ljava/lang/String;)V` | `0x3c15ba` | `0x38973e` | `0x227b329` → `0x227b32f` | named |
| `0x72a2f80` | M | `NativeGLJavaInterface` | `promptNativePurchase(JLjava/lang/String;)V` | `0x3c15ba` | `0x4a7016` | `0x227b34e` → `0x227b354` | named |
| `0x72a2f88` | M | `NativeGLJavaInterface` | `promptNativePurchaseWithPayload(JLjava/lang/String;Ljava/lang/String;)V` | `0x3fb14f` | `0x38973e` | `0x227b373` → `0x227b379` | named |
| `0x72a2f90` | M | `NativeGLJavaInterface` | `promptNativePurchaseWithPaymentSessionId(JLjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V` | `0x418726` | `0x332719` | `0x227b398` → `0x227b39e` | named |
| `0x72a2f98` | M | `NativeGLJavaInterface` | `promptNativePurchaseWithPaymentSessionId(JLjava/lang/String;Ljava/lang/String;)V` | `0x418726` | `0x38973e` | `0x227b3bd` → `0x227b3c3` | named |
| `0x72a2fa0` | M | `NativeGLJavaInterface` | `gameDidLeave()V` | `0x36c5ef` | `0x288017` | `0x227b407` → `0x227b40d` | named |
| `0x72a2fa8` | M | `NativeGLJavaInterface` | `screenOrientationChanged(I)V` | `0x2fac3d` | `0x48afcc` | `0x227b476` → `0x227b47c` | named |
| `0x72a2fb0` | M | `NativeGLJavaInterface` | `openNativeOverlay(Ljava/lang/String;Ljava/lang/String;)V` | `0x46f832` | `0x517f58` | `0x227b49b` → `0x227b4a1` | named |
| `0x72a2fb8` | M | `NativeGLJavaInterface` | `onDataModelNotificationCallback(Ljava/lang/String;Ljava/lang/String;)V` | `0x550d06` | `0x517f58` | `0x227b4c0` → `0x227b4c6` | named |
| `0x72a2fc0` | M | `NativeGLJavaInterface` | **`gameLoadedCallback(J)V`** (gate) | `0x4fcdc1` | `0x433046` | `0x227b4e5` → `0x227b4eb` | named ✓ |
| `0x72a2fc8` | M | `NativeGLJavaInterface` | `onLuaTextBoxChangedCallback(Ljava/lang/String;)V` | `0x4df7c4` | `0x4fcd93` | `0x227b50a` → `0x227b510` | named |
| `0x72a2fd0` | M | `NativeGLJavaInterface` | `onLuaTextBoxPropertyChangedCallback()V` | `0x3a574d` | `0x288017` | `0x227b52f` → `0x227b535` | named |
| `0x72a2fd8` | M | `NativeGLJavaInterface` | `saveImageToAlbum(Ljava/lang/String;)V` | `0x535143` | `0x4fcd93` | `0x227b59e` → `0x227b5a4` | named |
| `0x72a2fe0` | M | `NativeGLJavaInterface` | `onVrSessionStateUpdate(I)V` | `0x2c2205` | `0x48afcc` | `0x227b5c3` → `0x227b5c9` | named |
| `0x72a2fe8` | C | `com/roblox/engine/jni/model/NativeTextBoxInfo` | — | `0x46f844` | — | `0x227b6bf` → `0x227b6d9` | **PLACEHOLDER** |
| `0x72a2ff0` | C | `com/roblox/engine/jni/util/NetworkUtils` | — | `0x4354ed` | — | `0x227b621` → `0x227b63b` | **PLACEHOLDER** |
| `0x72a2ff8` | M | `NetworkUtils` | `getPublicIPv4Addresseses()Ljava/lang/String;` | `0x36c5fc` | `0x24e9ef` | `0x227b65a` → `0x227b661` | **PLACEHOLDER** |
| `0x72a3000` | D | — | `uint8` JNI_OnLoad-completion flag; `movb $1` at `0x227b143`, tested at `0x227affa` | — | — | — | **data (placeholder written then half-overwritten)** |
| `0x72a3008` | M | `com/roblox/universalapp/logging/LoggingProtocol` | `getProcessTimestamp()J` | `0x2defc6` | `0x33061d` | `0x227afed` → `0x227aff3` | named |
| `0x72a3010` | C | `LoggingProtocol` | — | `0x535154` | — | `0x227afa8` → `0x227afce` | **PLACEHOLDER** |
| `0x72a3018` | M | `NativeGLJavaInterface` | `exitGameWithError(I)V` | `0x217c3d` | `0x48afcc` | `0x227b3e2` → `0x227b3e8` | named |
| `0x72a3020` | M | `NativeGLJavaInterface` | `onAppShellReloadNeeded()V` | `0x4a702d` | `0x288017` | `0x227b42c` → `0x227b432` | named ✓ |
| `0x72a3028` | M | `NativeGLJavaInterface` | `onAppBridgeNotification(Ljava/lang/String;Ljava/lang/String;)V` | `0x3dea91` | `0x517f58` | `0x227b554` → `0x227b55a` | named |
| `0x72a3030` | M | `NativeGLJavaInterface` | **`getWebViewUserAgent()V`** | `0x3fb16f` | `0x288017` | `0x227b5e8` → `0x227b5ee` | **WRONG SIG** in bridge (`()Ljava/lang/String;`) |
| `0x72a3038` | M | `NativeGLJavaInterface` | **`getMobileAdvertisingId()V`** | `0x4fcdf4` | `0x288017` | `0x227b60d` → `0x227b613` | **WRONG SIG** in bridge (`()Ljava/lang/String;`) |
| `0x72a3040` | M | `NativeGLJavaInterface` | `onExtendedAnalyticsRecvCallback([BI)V` | `0x4fcdd4` | `0x250e39` | `0x227b579` → `0x227b57f` | named |
| `0x72a3048` | D | — | shared-pointer control block: `lea` `0x227b6fc`, refcount `lock incl 0x4(%rax)` `0x227b749`, NULL reset `0x227b868` | — | — | — | **data (placeholder overwrites live pointer)** |
| `0x72a3050` | D | — | `uint8` flag: read `0x227b6e0`, address passed to callee `0x227b854`/`0x227b873` | — | — | — | **data (placeholder overwrites flag)** |
| `0x72a3058` | D | — | C++ object pointer (vptr `0x6dbc188`): read `0x227b204`, written `0x230cce4` | — | — | — | **data (placeholder overwrites live pointer)** |

### 2.2 Range `0x72ca178..0x72ca1e0` (14 slots)

Guest class strings: `NativeLocaleJavaInterface` = `0x454656`, `NativeUserJavaInterface` = `0x470acc`.
Common sig strings: `()Ljava/lang/String;` = `0x24e9ef`, `()J` = `0x33061d`, `()Z` = `0x3a5891`, `()I` = `0x4a4e18`.

| Global | K | Class | Method / descriptor | Name RVA | Sig RVA | Call site → store site | State |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `0x72ca178` | C | `com/roblox/engine/jni/locale/NativeLocaleJavaInterface` | — | `0x454656` | — | `0x227ba85` → `0x227ba9f` | **PLACEHOLDER** |
| `0x72ca180` | M | `NativeLocaleJavaInterface` | `getLocale()Ljava/lang/String;` | `0x351249` | `0x24e9ef` | `0x227babe` → `0x227bac5` | **PLACEHOLDER** |
| `0x72ca188` | M | `NativeLocaleJavaInterface` | `getRobloxLocale()Ljava/lang/String;` | `0x26f129` | `0x24e9ef` | `0x227bae8` → `0x227baee` | named |
| `0x72ca190` | M | `NativeLocaleJavaInterface` | `getGameLocale()Ljava/lang/String;` | `0x536392` | `0x24e9ef` | `0x227bb11` → `0x227bb17` | named |
| `0x72ca198` | C | `com/roblox/engine/jni/user/NativeUserJavaInterface` | — | `0x470acc` | — | `0x227bb7d` → `0x227bb97` | **PLACEHOLDER** |
| `0x72ca1a0` | M | `NativeUserJavaInterface` | `getUserId()J` | `0x4e084a` | `0x33061d` | `0x227bbb6` → `0x227bbbd` | **PLACEHOLDER** |
| `0x72ca1a8` | M | `NativeUserJavaInterface` | `getIsUnder13()Z` | `0x4e0854` | `0x3a5891` | `0x227bbe0` → `0x227bbe6` | **PLACEHOLDER** |
| `0x72ca1b0` | M | `NativeUserJavaInterface` | `getUsername()Ljava/lang/String;` | `0x2fb9e0` | `0x24e9ef` | `0x227bc09` → `0x227bc0f` | **PLACEHOLDER** |
| `0x72ca1b8` | M | `NativeUserJavaInterface` | `getDisplayName()Ljava/lang/String;` | `0x3fc178` | `0x24e9ef` | `0x227bc32` → `0x227bc38` | **PLACEHOLDER** |
| `0x72ca1c0` | M | `NativeUserJavaInterface` | `getAlternateName()Ljava/lang/String;` | `0x26f139` | `0x24e9ef` | `0x227bc5b` → `0x227bc61` | **PLACEHOLDER** |
| `0x72ca1c8` | M | `NativeUserJavaInterface` | `getPlatformName()Ljava/lang/String;` | `0x4e0861` | `0x24e9ef` | `0x227bc84` → `0x227bc8a` | **PLACEHOLDER** |
| `0x72ca1d0` | M | `NativeUserJavaInterface` | `getMembershipType()I` | `0x2a5ebb` | `0x4a4e18` | `0x227bcad` → `0x227bcb3` | **PLACEHOLDER** |
| `0x72ca1d8` | M | `NativeUserJavaInterface` | `getHasRobloxSubscription()Z` | `0x589cfc` | `0x3a5891` | `0x227bcd6` → `0x227bcdc` | **PLACEHOLDER** |
| `0x72ca1e0` | M | `NativeUserJavaInterface` | `getTheme()Ljava/lang/String;` | `0x2dff1c` | `0x24e9ef` | `0x227bcff` → `0x227bd05` | **PLACEHOLDER** |

Smali cross-check: `NativeUserJavaInterface.smali` lines 19/31/43/55/67/79/91/103/115 declare
`getAlternateName()Ljava/lang/String;`, `getDisplayName()Ljava/lang/String;`,
`getHasRobloxSubscription()Z`, `getIsUnder13()Z`, `getMembershipType()I`,
`getPlatformName()Ljava/lang/String;`, `getTheme()Ljava/lang/String;`, `getUserId()J`,
`getUsername()Ljava/lang/String;` — byte-for-byte the §2.2 descriptors.
`NativeLocaleJavaInterface.smali:43` is `getLocale()Ljava/lang/String;` (the method A5 missed).
`NetworkUtils.smali:15` is `getPublicIPv4Addresseses()Ljava/lang/String;`.
`NativeGLJavaInterface.smali:129,150` are `getMobileAdvertisingId()V` and
`getWebViewUserAgent()V` (both invoke a registered getter interface and return void).

Consumer-side confirmation of descriptor correctness (method object is passed to a typed call):
`0x72ca1a8` is loaded at `0x24acbb8` and dispatched through helper `0x30db044` →
`call *0x3b0(%rax)` (`CallStaticBooleanMethodV`) at `0x24ace34` — matches `()Z`.
`0x72ca1a0` is loaded at `0x24acd25` and dispatched through helper `0x24acd0a` →
`call *0x428(%rax)` (`CallStaticLongMethodV`) at `0x227bdba` — matches `()J`.
`0x72ca180` is loaded at `0x24ac286`/`0x2edd215` and dispatched through object-call helper
`0x23150f4` — matches `()Ljava/lang/String;`.

### 2.3 Reclassification of the 22 placeholders

| Category | Slots | Count |
| --- | --- | --- |
| `jclass` (must be `SR_K_CLASS`) | `0x72a2f50`, `0x72a2f58`, `0x72a2fe8`, `0x72a2ff0`, `0x72a3010`, `0x72ca178`, `0x72ca198` | 7 |
| real `jmethodID` with exact name+sig | `0x72a2ff8`, `0x72ca180`, `0x72ca1a0`, `0x72ca1a8`, `0x72ca1b0`, `0x72ca1b8`, `0x72ca1c0`, `0x72ca1c8`, `0x72ca1d0`, `0x72ca1d8`, `0x72ca1e0` | 11 |
| non-JNI global data (must be left alone) | `0x72a3000`, `0x72a3048`, `0x72a3050`, `0x72a3058` | 4 |
| **Total** | | **22** |

With the already-correct `0x72a2f48` class, the full 49-slot census is:
**8 classes + 37 method IDs + 4 data words.**

---

## 3. DEFAULT-ON seeding plan (module design — not implemented here)

### 3.1 Design goal and constraints

* **Default ON** means the exact seeds are installed on every run with **no env knob**, so no
  entry of the decision-013 withhold chain (`guest_app_shell.c` 87 `getenv()` checks) is touched.
  A read-only diagnostic kill switch (`SOBER_CACHED_IDS_DEFAULT=0`) is permitted for A/B only; it
  is **not** a withhold-listed "state seed" knob and must not gate correctness in the normal run.
* Seed **only guest slots that are genuinely JNI handles**; do not fabricate `SrObject`s over the
  four data globals. For those, preserve loader state (`0` / `NULL`) except the one documented
  completion flag `0x72a3000 = 1`.
* No guest byte patch, no host vtable in guest memory, no callback emission, no counter or gate
  change. Installing correct identities cannot by itself produce app-shell proof.

### 3.2 Proposed module: `src/jni/cached_ids_default.{h,c}` (NEW, T3-3 design)

```
/* default ON; returns number of slots written, or SOBER_JNI_ERR on bad args */
int  sober_cached_ids_default_seed(SoberJni *j, void *guest_base);
/* idempotent verification: 0 = all 49 slots exact, >0 = mismatch count */
int  sober_cached_ids_default_verify(SoberJni *j, void *guest_base,
                                     char *buf, size_t cap);
const char *sober_cached_ids_default_describe(char *buf, size_t cap);
```

Private table (one row per slot; `kind` ∈ {CLASS, METHOD, DATA}):

```
typedef struct {
    uintptr_t   rva;
    int         kind;            /* 1=class, 2=method, 3=data */
    const char *class_name;      /* non-NULL for CLASS and METHOD */
    const char *method;          /* non-NULL for METHOD */
    const char *sig;             /* non-NULL for METHOD */
    uintptr_t   data_value;      /* DATA only: 0/NULL except 0x72a3000 -> 1 */
} SrExactCached;
```

Fill it with the 49 rows of §2 verbatim. **Two corrections relative to the current
`sr_cached_methods[]`:**
1. `0x72a3030` and `0x72a3038` descriptors become `()V`.
2. Add the 11 missing methods and the 7 missing classes from §2.3.

Installer algorithm (per row, in table order):

```
clazz = (kind==CLASS||kind==METHOD) ? sr_find_class_object(j, class_name) : NULL;
obj   = (kind==CLASS) ? clazz
      : (kind==METHOD) ? sr_find_method_object(j, clazz, method, sig)
      : NULL;
if (kind==DATA)  *(uintptr_t*)(base+rva) = data_value;   /* no SrObject */
else            *(void**)(base+rva) = obj;              /* must be non-NULL */
```

`sr_find_method_object`'s dedup key is `(name, sig, class_name)` (`native_jni.c:305-313`), so
creating `getIsUnder13()Z` under `NativeUserJavaInterface` can never be confused with the
similarly named StartApp/StartGame getter `isUnder13()Z` — that is exactly why the class name
matters even though dispatch is name-only. The installer is **idempotent**: calling it twice
returns the same deduplicated objects (it must not allocate duplicates).

### 3.3 Wiring point and ordering versus guest calls

Keep the existing unconditional call site `guest_app_shell.c:1078` (currently
`sober_jni_seed_guest_jni_state(j, base)`) and change it to:

```
seed_rc = sober_cached_ids_default_seed(j, base);   /* default-on exact seed */
```

Placement constraints (all already satisfied by the existing call site):

* **Before** `nativeAppBridgeV2InitWithParams` `base+0x24abf96` (`:1249`), because that call
  consumes `0x72ca178/0x72ca180/0x72ca188/0x72ca190` and the `NativeUserJavaInterface` IDs at
  `0x72ca1a0..0x72ca1e0` (consumer sites `0x24ac286`, `0x24acbb8`, `0x24acc47`, `0x24acc65`,
  `0x24acc8e`, `0x24acd25`, `0x24ace73`, `0x24ad0dd`, `0x24ad195`).
* **Before** the notifier family can fire — cached class `0x72a2f48` and method `0x72a2fc0`
  are loaded by notifier `0x27eaf6e` (class load `0x27eaf98`, thunk `0x25848e0`,
  `call *0x470(%rax)` at `0x258495a`, return `0x27eafb3`).
* **After** `sober_jni_init(j)` and after any `sr_find_class_object` warm-up; no guest export
  that reads cached IDs may run before it.
* The two completion flags remain `0x72a3000 = 1` and `0x7d8fb60 = 1`; the latter is outside
  the seed ranges and is written by the existing function — preserve it.

### 3.4 `sober_jni_seed_guest_jni_state` changes required (lead-owned)

* Delete the range fill at `native_jni.c:1377-1391` that creates `cachedMethod@<rva>;()V`.
* Either extend `sr_cached_methods[]` in place to all 37 method rows and add a parallel class
  table for the 8 class rows, or delegate entirely to §3.2's module and leave a thin wrapper.
* Explicitly skip `0x72a3000`, `0x72a3048`, `0x72a3050`, `0x72a3058` in every loop over the two
  ranges (today they are overwritten as method objects).
* Fix the two `()V` descriptors at `native_jni.c:1332-1335`.

---

## 4. Definitive handler registration order (shadow-free)

### 4.1 The mechanism that must be respected

`sober_jni_set_callback_handler` (`native_jni.c:1020-1036`) is a **name-keyed replace: the last
writer wins**. The `startapp_params.h` and `game_params.h` comments claiming "the bridge
dispatches the first handler registered for a name" are factually wrong (A5 §4, B06 §2). Every
name therefore needs exactly one intended owner, and that owner must be the **last** writer.

### 4.2 One owner per name (the invariant)

| Name group | Names | Sole intended owner | Forbidden writers |
| --- | --- | --- | --- |
| StartAppParams getters (10) | `surface`, `platformParams`, `appStarterPlace`, `appStarterScript`, `appUserId`, `isUnder13`, `username`, `membershipType`, `selectedTheme`, `vrContext` | `sober_startapp_v2_getter` installed by `sober_startapp_v2_make`, re-asserted by `sober_startapp_v2_reassert` | `start_app_get` (`guest_app_shell.c:162`), `real_path_get` (`:721`), `sp_get` in `startapp_params.c` |
| StartGameParams getters (24) | `surface`, `platformParams`, `deviceParams`, `placeId`, `userId`, `accessCode`, `linkCode`, `gameId`, `isUnder13`, `username`, `conversationId`, `reservedServerAccessCode`, `callId`, `joinRequestType`, `referralPage`, `launchData`, `gameJoinContext`, `eventId`, `joinAttemptId`, `joinAttemptOrigin`, `isoContext`, `referredByPlayerId`, `gameIdToExclude`, `vrContext` | `sober_startgame_v2_getter` installed by `sober_startgame_v2_make`, re-claimed by `sober_startgame_v2_reclaim` | inline `game_params_get` (`guest_app_shell.c:904`), `gp_get` in `game_params.c` |
| Gate callbacks (2) | `gameLoadedCallback`, `onAppShellReloadNeeded` | `shell_callback`, or the origin-checked handler of B06 §5.1 for `gameLoadedCallback` | every module; no param/singleton module may register these names |
| `run` | `run` | `run_callback` (`:1302`) only under `SOBER_SCHEDULE_RUNNABLE` | none |
| Diagnostics | `onAppBridgeNotification` | `shell_callback` only under `SOBER_LOG_APP_BRIDGE_NOTIFICATION` (`:1054-1055`); diagnostic only, never a gate signal | none |

Note the two param groups overlap on six names (`surface`, `platformParams`, `appUserId`/`userId`,
`isUnder13`, `username`, `membershipType`, `vrContext`). This is *not* shadowing if the two param
objects are built and consumed at different times (StartApp before `base+0x26fe80f`, StartGame
before `base+0x2ebd51a`): each owner is re-asserted immediately before its own guest call, so the
other group's handler is never the last writer when a given object is read. If both objects are
alive simultaneously, the later re-assert defines the owner — record it in the log, do not rely
on registration order implicit in an `if` chain.

### 4.2.1 The `placeId()J` hazard (must be fixed, not merely reordered)

`game_params.h:78` and `startgame_v2.h` define `placeId` as `()J`. The inline harness handler
`game_params_get` (`guest_app_shell.c:904-917`) returns a **jstring** for it. No registration
order makes that correct — the inline handler must be removed, and `sober_startgame_v2_make`
used so `placeId` returns an `int64_t`.

### 4.3 Exact order (current-worktree line numbers)

1. **P0 — bridge** `sober_jni_create` / `sober_jni_init` (`guest_app_shell.c:~937-941`): no handlers yet.
2. **P1 — default-on exact cached-ID seed** `sober_cached_ids_default_seed(j, base)` at `:1078`.
   No handler registration here; this is identity data only.
3. **P2 — base handlers** `sober_jni_install_default_app_shell_handlers(j)` at `:1048` (34 names
   including both gate names). Intended as the *fallback* owner.
4. **P3 — param getters, single owner each**:
   * StartApp: call `sober_startapp_v2_make(j, base)` once (currently `:1313` calls the legacy
     `sober_startapp_params_make`). Then **delete** the `real_path_get` block `:1321-1330` and
     the `start_app_get` block `:1334-1336`, or if they are retained for a diagnostic, call
     `sober_startapp_v2_reassert(j)` as the **last** registration before `base+0x26fe80f` (`:1340`).
   * StartGame: call `sober_startgame_v2_make(j, base)` then
     `sober_startgame_v2_reclaim(j)` immediately before `base+0x2ebd51a` (`:1386`) — the existing
     `:1377-1378` already does this; **delete** the inline `game_params_get` registrations
     `:1381-1384` so they cannot be the last writer.
5. **P4 — gate callbacks, final and unconditional**: after *all* other registrations,
   `sober_jni_set_callback_handler(j, "onAppShellReloadNeeded", shell_callback, NULL)` and
   `sober_jni_set_callback_handler(j, "gameLoadedCallback", shell_callback /* or origin-checked */, NULL)`.
   In the current tree these are at `:1049-1050`, i.e. **before** the param registrations; that is
   acceptable only because no param module registers those two names. To make the invariant
   structural rather than conventional, move them to the end of `guest_app_shell_run()` (or
   remove the two names from the default list at `native_jni.c:1057-1073` and register them
   exactly once in P4). Keep `SOBER_LOG_APP_BRIDGE_NOTIFICATION` diagnostic-only.
6. **P5 — no thunk**: never call `base+0x25848e0` from the harness; never set
   `SOBER_ALT_CALLBACK_PATH`; never enable a withhold-listed seed knob.

### 4.4 Why this is DEFAULT-ON and does not weaken the gate

* §3's seed and §4's ordering add no new env knob to the withhold chain, emit no callback, patch
  no guest byte, and set no guest-state seed flag. `seeded_state` stays 0.
* Owner-by-name is a *determinism* fix: it makes counters attributable. It does not create a
  `gameLoadedCallback` count; only a real notifier entry at `0x27eaf6e` can do that.
* The only admissible gate signal remains an origin-verified `gameLoadedCallback` (B06 §5.1);
  `onAppBridgeNotification` and harness-triggered thunks stay disqualified (user ruling +
  decision 013).

---

## 5. Acceptance evidence (explicit RVAs, checkable)

### 5.1 Seeding acceptance (run with all withhold knobs unset)

1. `sober_cached_ids_default_verify(j, base, buf, cap)` returns **0**: 49/49 slots exact.
2. Repo-wide log search finds **zero** `cachedMethod@` objects (`native_jni.c:1386` string gone).
3. Gate identities:
   * `*(void**)(base+0x72a2fc0)` non-NULL, `kind==SR_K_METHOD`, `class_name ==
     "com/roblox/engine/jni/NativeGLJavaInterface"`, `name=="gameLoadedCallback"`, `sig=="(J)V"`.
   * `*(void**)(base+0x72a2f48)` kind `SR_K_CLASS` with the same class name.
4. Placeholder identities now exact (spot checks): `0x72ca1a8` → `getIsUnder13()Z` on
   `NativeUserJavaInterface`; `0x72ca180` → `getLocale()Ljava/lang/String;` on
   `NativeLocaleJavaInterface`; `0x72a2ff8` → `getPublicIPv4Addresseses()Ljava/lang/String;` on
   `NetworkUtils`; `0x72a3010` → `SR_K_CLASS LoggingProtocol`.
5. The 2 descriptors corrected: `0x72a3030`/`0x72a3038` seed with sig `()V`.
6. Data globals: `*(uint8_t*)(base+0x72a3000)==1`; `0x72a3048`, `0x72a3050`, `0x72a3058` hold
   no `SrObject` (no `SR_OBJ_MAGIC`), i.e. the range fill no longer touches them.
7. Guest-side consumers of the newly exact IDs are reached without a null-dispatch:
   `0x24acbb8` (`getIsUnder13` → `call *0x3b0` at `0x24ace34`), `0x24acd25`
   (`getUserId` → `call *0x428` at `0x227bdba`), `0x24ac286`/`0x2edd215` (`getLocale`).

### 5.2 Registration-order acceptance

1. Introspection/diagnostic dump lists exactly one owner per name in §4.2; specifically:
   * all ten StartApp getters owned by the `startapp_v2` handler (or exactly the documented
     fallback when the module is off), and **not** by both;
   * all 24 StartGame getters owned by `startgame_v2`, with `placeId` returning an `int64_t`,
     and `game_params_get` absent from the handler table;
   * `gameLoadedCallback` and `onAppShellReloadNeeded` owned by `shell_callback`/origin-checked
     handler after P4, and by no other module.
2. No `ALT PATH guest call 0x25848e0` line; `SOBER_ALT_CALLBACK_PATH` unset.
3. Counters attributable: `sober_startapp_v2_getter_calls`/`sober_startgame_v2_getter_calls`
   equal dispatch counts for names they own (no -3/-10 under-report as in A5 §4).

### 5.3 Gate acceptance (unchanged, strict — decision 013)

* Guest: notifier `0x27eaf6e` entered with `RSI = placeId > 0`; return address `0x27eafb3`
  observed in the frame chain; cached method `0x72a2fc0` non-NULL; thunk `0x25848e0` reached
  from the notifier and issues `call *0x470(%rax)` at `0x258495a`.
* Bridge: origin-verified `gameLoadedCallback` logged; `callbacks` increased by ≥1 over baseline;
  `seeded_state == 0`; no `SOBER_*` seed/withhold knob active.
* Render: `guest call 0x649d422` observed (phase-3 surface path, fed by
  `nativeAppBridgeV2UpdateSurfaceAppWithPlatformParams 0x2773194`).
* No `HONESTY NOTICE`, no `ALT PATH`; engine SHA-256 `3e498d64…1335` and BuildID
  `204ad049…6c9` unchanged after the run.

---

## 6. Limits / honesty

* This is static analysis of one frozen engine plus the current worktree. No runtime replay was
  performed by T3-3; the §5 steps are acceptance criteria for the lead/verifier, not results.
* Every `JNI_OnLoad` RVA, string RVA and store site in §2 was read from the SHA-bound
  disassembly; method descriptors were independently checked against the recovered smali. Where
  a signature was previously inferred with `?` (`jni-map.md`), the disassembly + smali pair is
  authoritative and the `?` is removed.
* The classification of `0x72a3048`/`0x72a3050`/`0x72a3058` as non-JNI data is based on their
  use sites (`lea`, `lock incl 0x4(%rax)`, byte test, vptr `0x6dbc188`), not on any runtime
  observation; the safe default (leave at loader value) should be A/B'd against the current
  synthetic-seed behavior before being adopted.
* `getWebViewUserAgent()V`/`getMobileAdvertisingId()V` are void methods that notify a registered
  getter interface (smali `NativeGLJavaInterface.smali:129,150`); seeding them with `()V` fixes
  identity, but the *value* still depends on the getter/listener path. This report does not claim
  those values are produced.
* The registration plan depends on APIs that already exist in the worktree
  (`sober_startapp_v2_reassert`, `sober_startgame_v2_reclaim`) but are opt-in; making them
  default-on, and moving P4, are choices for the lead who owns `guest_app_shell.c`.
* No `src/`, `CMakeLists.txt`, gate, test, doc, marker or engine byte was changed. Marker:
  **WITHHELD**.
