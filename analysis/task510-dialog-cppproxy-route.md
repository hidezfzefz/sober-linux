# Task #510 — Is a REAL `IPlatformSystemDialogHandler$CppProxy` / nativeRef
# obtainable through shipped JNI exports to reach a `0x56d*` flags-latch writer?

Owner: worker-1. **Analysis only** (plus comment-only edits to
`src/jni/flags_latch_default.{c,h}`). Frozen engine: AppImage
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(re-verified before and after this analysis). All RVAs below are against that
object (RVA == file offset for the first `R E` PT_LOAD). Target bytes are
untrusted DATA. No guest byte writes, no fake vtable/object/callback, no gate
weakening, marker WITHHELD; decisions 013/038 binding.

## Verdict

**Prior finding CONFIRMED, with one factual correction to A15.** There is no
shipped JNI export, and no native factory, that can hand the harness a genuine
`IPlatformSystemDialogHandler$CppProxy` with a valid nativeRef. Consequently no
`open`/`dismiss`/`dismissAll` call can be made on a real dialog object, and none
of the four `0x56d*` dialog-family latch writers is reachable through the
CppProxy route. Default route stays `none`/inert; no new knob is added.

### Correction to A15 §2/§3 (small, important)

A15 states that `nativePassKeyEvent 0x2eb67ec` calls `0x56d2d35` but that
"neither contains a `movb $1,0x7cdd149`". That is **wrong for `0x56d2d35`**:
`0x56d31a3: movb $1,0x7cdd149` is inside it. The reason the path is still inert
is the *gate*, not the absence of the writer (see §3 below). A15's bottom-line
(`0x56d*` not a viable app-start route) is unchanged.

## 1. The four dialog-family writers and their true enclosing functions

objdump mislabels the whole `0x56d*` cluster as
`..._native_1dismissAll+0x1cc....` because the nearest preceding **dynamic**
symbol is the tiny JNI export `native_1dismissAll` at `0x3a09e06` (0x46 bytes).
The writers live in two different *internal* functions with no symbol:

| Writer | Enclosing function (prologue) | Gate |
| --- | --- | --- |
| `0x56d31a3` | **F1 = `0x56d2d35`** (`push rbp; sub $0xe8,rsp`) | `0x56d3199: cmpq $0,-0xe8(%rbp); je 0x56d31d2` → needs `r9 != 0` |
| `0x56d3386` | F1 (`0x56d2d35`), second site | same function / state |
| `0x56d6a4b` | **F2 = `0x56d6779`** | `0x56d6a46: test r12,r12; je 0x56d6a76` → needs `r12 != 0` |
| `0x56d6b80` | F2 (`0x56d6779`), second site | `0x56d6b7b: test r12,r12; jne 0x56d6ba3` → complementary |

F1 entry stores the 6th argument: `0x56d2d49: mov %r9,-0xe8(%rbp)`. After the
latch store it *uses* that pointer (`0x56d31c6: mov -0xe8(%rbp),%rdi; call
0x599a714`), so `r9` is a genuine native context object, not a boolean to be
faked. F1 also calls the genuine init helpers
`0x23f8c3c nativePostClientSettingsLoadedInitialization3`, `0x2e57c20`
(std::string assign), `0x235a58c nativeInitClientSettings`, `0x2f28afc
nativePreloadFlagOverrides`, `0x232de10 nativeInitCrashpad` — it is the
crash/analytics context-init path, entered on dialog/key events, **not** on
app-start.

## 2. The CppProxy JNI exports never call F1/F2

All systemdialog exports (complete dynsym list, 9 total):

| Export | RVA | size |
| --- | --- | --- |
| `..._IPlatformSystemDialogHandler$CppProxy_native_1isAvailable` | `0x3a09abe` | 0x44 |
| `..._IPlatformSystemDialogHandler$CppProxy_native_1open` | `0x3a09b02` | 0x2bb |
| `..._IPlatformSystemDialogHandler$CppProxy_native_1dismiss` | `0x3a09dbd` | 0x49 |
| `..._IPlatformSystemDialogHandler$CppProxy_native_1dismissAll` | `0x3a09e06` | 0x46 |
| `..._IPlatformSystemDialogHandler$CppProxy_nativeDestroy` | `0x3a09a3d` | 0x81 |
| `..._ISystemDialogCallback$CppProxy_native_1onOkClicked/onCancelClicked/onOpened/onClosed` | `0x3a0934c/0x3a093ca/0x3a09448/0x3a094c6` | 0x7e each |
| `..._ISystemDialogCallback$CppProxy_nativeDestroy` | `0x3a092ac` | 0x2d |

They are thin djinni dispatchers. E.g. `native_dismissAll 0x3a09e06`:
`mov 0x10(%rdx),%rdi; mov (%rdi),%rax; call *0x28(%rax)` (vtable slot
**+0x28**); `native_dismiss 0x3a09dbd`: `call *0x20(%rax)`; `native_isAvailable
0x3a09abe`: `call *0x10(%rax)`; `native_open 0x3a09b02`: `call *0xf8(%rax)`,
`*0xc0(%rax)`, `*0x328(%rax)`. `rdx` is the 3rd JNI argument, the `jlong
nativeRef` (a pointer to a native wrapper whose interface pointer is at +0x10).
**No call to `0x56d2d35`/`0x56d6779` appears in any of these exports.**

The vtable targets for the null/absent platform impl are the stubs at
`0x369d4b6` (open, logs string `0x3a9daa`), `0x369d562` (dismiss, `0x21bdd8`),
`0x369d608` (dismissAll, `0x4e3d0b`): each checks `this+0x18`; if NULL it calls
`0x232de10` with `"[DFLog::SystemDialogHandler] … Not available on the current
platform."` and returns `-1`, otherwise it tail-calls `*(vt+0x18/0x20/0x28)`
into the installed Java platform impl. **Neither arm reaches a `0x56d*`
writer or the latch.**

## 3. Why the reachable F1 call is inert

Only two static callers of F1 exist:

* `0x2eb8296` inside exported `nativePassKeyEvent 0x2eb67ec`:
  `0x2eb8293: xor %r9d,%r9d` → `r9 == 0` → **writer skipped** at
  `0x56d31a1`. A15 §2 was right that this path does not set the latch, but the
  mechanism is this hardcoded `r9=0`, not a missing writer.
* `0x56d755c` (internal, inside F2's neighbor cluster):
  `0x56d7539: mov 0x10(%rbp),%r9` → `r9` is a genuine caller-supplied context
  pointer. The enclosing function `0x56d6779` has exactly one caller
  (`0x56d6765`), itself reached only through internal functions
  (`0x56d66d6 ← 0x56d65ef ← 0x56d654a ← 0x2eb7745/0x2ee6d5a`), i.e. deeper
  analytics code. **No exported JNI function reaches it with a valid context.**

F2's two writers likewise depend on `r12` (a caller-supplied pointer at
`[rbp+0x18]`); F2 is only ever called internally. Fabricating `r9`/`r12` or a
wrapper object to force these writes is explicitly disallowed.

## 4. The CppProxy nativeRef cannot be obtained — native side has no factory

* Java smali (`/tmp/opencode/apkdex/classes2.dex`, decompiled at
  `/tmp/opencode/mini/out/smali/com/roblox/protocols/systemdialogplatforminterface/generated/IPlatformSystemDialogHandler$CppProxy.smali`):
  `private constructor <init>(J)V`:
  ```
  cmp-long v0, p1, 0
  if-eqz v0, :cond_0
  iput-wide p1, ->nativeRef
  invoke-static {p0,p1,p2}, Lcom/snapchat/djinni/NativeObjectManager;->register(Ljava/lang/Object;J)V
  return
  :cond_0
  new-instance RuntimeException, "nativeRef is zero"; throw
  ```
  All methods are `private native` taking `nativeRef` as the leading `jlong`:
  `native_dismiss(JJ)`, `native_dismissAll(J)`, `native_isAvailable(J)`,
  `native_open(J,SystemDialogRequest,ISystemDialogCallback)J`, `nativeDestroy(J)`.
  This exactly matches exports in §2 and confirms the prior "ctor `(J)V`
  throws on nativeRef=0, NativeObjectManager.register exists" finding.
* The CppProxy class-name string
  `0x58e636 = "com/roblox/protocols/systemdialogplatforminterface/generated/IPlatformSystemDialogHandler$CppProxy"`
  has **exactly one** code xref in all of `.text`: `0x2390571`, inside the
  cache initializer `0x2390564`. That routine only does `FindClass` +
  `GetMethodID`/`GetFieldID` for `<init>(J)V`, `nativeRef:J`, `isAvailable()Z`,
  `open(...)J`, `dismiss(J)V`, `dismissAll()V`. **There is no `NewObject` of the
  CppProxy class anywhere in the engine**: native never constructs it. It is
  created only by Java code. No exported `Java_*create*SystemDialog*` exists
  among the 555 `Java_` exports; there is also no systemdialog `setPlatformImpl`
  export (the Android impl is Java).
* RTTI confirms the architecture: `RBX::IPlatformSystemDialogHandler`
  (`0x6aac36`), `RBX::SystemDialogHandler` (`0x6aa957`),
  `RBX::ISystemDialogHandler` (`0x6aaa5d`), `RBX::JNIIPlatformSystemDialogHandler::JavaProxy`
  (`0x6def18`), `djinni::ProxyCache<JavaProxyCacheTraits>::Handle<GlobalRef<jobject>, JNIIPlatformSystemDialogHandler::JavaProxy>`
  (`0x6def4a`), `RBX::Subsystem<RBX::ISystemDialogHandler,false>`
  (`0x6aa990`). On Android the handler implementation is the Kotlin singleton
  `com/roblox/protocols/systemdialog/PlatformSystemDialogHandler` (smali in the
  same dex tree), so the native side is the *caller* of Java, not a producer of
  a CppProxy.
* The internal helper `0x23901a2` does `FindClass("com/roblox/protocols/systemdialog/PlatformSystemDialogHandler")`
  and issues JNIEnv calls at `+0x480` (slot 144) and `+0x488` (slot 145).
  Our bridge does **not** implement those slots (`native_jni.c` has no `SETFN`
  for 140/141). `0x23901a2` has zero static callers and zero function-pointer
  references in the binary — it is not a callable shipped export.

## 5. Knobs / seeded_state hand-off to lead

No new knob was added; default route stays `none` (inert, no guest call,
`should_withhold()==0`). Existing knobs already in the withhold/seeded_state
chain (unchanged):
`SOBER_FLAGS_LATCH_DEFAULT`, `SOBER_FLAGS_LATCH_ROUTE`
(`none|client-settings|crashpad-jni|crashpad-valid|crashpad-main|narrow`),
`SOBER_FLAGS_LATCH_ARGV`. Any non-`none` route sets `g_active` and forces the
marker withhold. `sober_flags_latch_default_latch()` remains a read-only view
of guest byte `0x7cdd149`.

## 6. Evidence / how verified

* `objdump -T` dynsym enumeration (555 `Java_` exports; 9 systemdialog, no
  factory, no systemdialog `setPlatformImpl`).
* `objdump -d` of `0x56d2d35` (writers `0x56d31a3`, `0x56d3386`), `0x56d6779`
  (`0x56d6a4b`, `0x56d6b80`), the 9 CppProxy exports, `0x23901a2`, `0x2390564`,
  `0x239044d`, `0x369d4b6/562/608`, and call sites `0x2eb8293/96`, `0x56d7539/5c`.
* `/tmp/opencode/libroblox.dis` full-disassembly xrefs: writer call graph,
  CppProxy-class-string single xref, RTTI names, "Not available" strings.
* Java smali + dex strings: private `<init>(J)V`, `"nativeRef is zero"`,
  `NativeObjectManager.register`, `native_*` signatures.
* Bridge audit: `src/jni/native_jni.c` JNIEnv fn table (slots 0x480/0x488 absent).
* `cmake --build build -j4` (see task note for result). No source behaviour
  changed; comment-only clarification in my two owned files.

## 7. Honesty / limits

* This is static analysis of the frozen artifact plus its shipped dex; no live
  guest execution was used to *claim* a latch. No object, vtable, callback or
  byte was fabricated. The crashpad route remains genuinely terminating (lead
  #491/A14 §6); the client-settings route remains gated. Marker stays WITHHELD
  and the callback leg remains 0/1.
* The single correction (A15 wrongly said `0x56d2d35` has no latch writer) does
  not create a route: the only export reaching it hardcodes `r9=0`.
