# n2 #668 STEP 2 — DEX type of #552 arg + guest JNIEnv method list

Read-only; cores 2-3. Every success claim quotes a raw line. Evidence:
`raw_dex_signature.txt`, `raw_dex_callsite.txt`, `raw_dex_codeconst.txt`,
`raw_jnienv_calls.txt`, `raw_strings.txt`, `dex_parse.py`, `dex_callsite.py`.

## DEX (authoritative classes2.dex from base.apk SHA 114a6f8e)

Quoted raw parse output:
> `METHOD idx=15180 class=Lcom/roblox/engine/jni/NativeGLInterface; name=nativePostClientSettingsLoadedInitialization3 proto='(Ljava/util/List;)V'`
> `CLASS_DEF idx=1953 access=0x1 super=Ljava/lang/Object; interfaces_off=0 class_data_off=5571488`
> `CLASS_DATA static_fields=0 instance_fields=0 direct_methods=83`

So declared parameter = **`Ljava/util/List;`** (one param), declaring class
`Lcom/roblox/engine/jni/NativeGLInterface;` (super Object, **no interfaces**,
**no fields**).

Java call site, verified from raw DEX `invoke-static` at file offset `0x335152`:
> `CALLER class=Luh/e$f; method=b proto=(Ljava/lang/Integer;)V access=0x1 code_off=0x335124 insns=0x335134..0x33517c target=0x335152`

Prior callgraph doc line (quoted): `| nativePostClientSettingsLoadedInitialization3(Ljava/util/List;)V | 0x2383c18 | 1 | classes2.dex:000e ← Luh/e$f;->b(Ljava/lang/Integer;)V |`.

Object construction/factory, resolved from the caller's code constants
(`raw_dex_codeconst.txt`):
> `method[12921] Lcom/roblox/client/RobloxApplication;->f()Landroid/content/Context;`
> `method[44244] Lyj/l2;->a(Landroid/content/Context;)Ljava/util/List;`
> `method[15180] Lcom/roblox/engine/jni/NativeGLInterface;->nativePostClientSettingsLoadedInitialization3(Ljava/util/List;)V`

So `Luh/e$f;->b()` obtains a Context via `RobloxApplication.f()`, calls
`Lyj/l2.a(Context)` to get the `List`, and passes it to the native method.

## Guest #552 JNIEnv calls on the argument

`0x2383c23: mov %rdx,%rbx` and `0x2383c98: mov %rsi,%r14` save the jobject.
Quoted disassembly with resolved JNI functions:
> `2383cb6: call *0x30(%rax)` → fn[2] **FindClass**, name `java/util/List`
> `2383cca: call *0x30(%rax)` → fn[2] **FindClass**, name `com/roblox/engine/jni/model/ApplicationExitInfoCpp`
> `2383cfc: call *0x108(%rax)` → fn[29] **GetMethodID**(List, `get`, `(I)Ljava/lang/Object;`)
> `2383d2a: call *0x108(%rax)` → fn[29] **GetMethodID**(List, `size`, `()I`)
> `2383d33: mov %r14,%rsi` → helper `0x238463c`: `23846b6: call *0x190(%rax)` → fn[46] **CallIntMethodV** (invokes `size`)
> `2383d6b: mov %r14,%rsi` → helper `0x2315d18`: `2315d92: call *0x118(%rax)` → fn[31] **CallObjectMethodV** (invokes `get`)
> `2383dac: call *0x2f0(%rax)` → fn[90] **GetFieldID**(ApplicationExitInfoCpp, `mPid`, `I`)
> `2383dbf: call *0x320(%rcx)` → fn[96] **GetIntField**

Strings quoted: `0x217ca9 'java/util/List'`, `0x46f8ef 'com/roblox/engine/jni/model/ApplicationExitInfoCpp'`, `0x3305b9 'get'`, `0x250e4f '(I)Ljava/lang/Object;'`, `0x54eb7e 'size'`, `0x4a4e18 '()I'`, `0x56d0e9 'mPid'`, `0x532c55 'I'`.

**EXACT method/field list the guest needs:** `java/util/List.size()I`,
`java/util/List.get(I)Ljava/lang/Object;`, and field
`com/roblox/engine/jni/model/ApplicationExitInfoCpp.mPid:I`. JNIEnv functions
used: FindClass, GetMethodID, CallIntMethodV, CallObjectMethodV, GetFieldID,
GetIntField.

**Limits:** 2 Java methods + 1 field — **NOT >5 methods**. Framework classes = 2
(`java/util/List`, `com/roblox/engine/jni/model/ApplicationExitInfoCpp`) — **NOT
>3 framework classes**. Both limits are satisfied for a DEX-typed object build.

## What NOT done
No build, no apply, no harness change, no runs/gdb/perf, no device/AVD/Frida/phone,
no new captures, no guest writes/gate edits/seeder/fake returns/path remaps, no
credentials, no captures/logs/tokens in git. Full `Lyj/l2.a()` body not decoded
(concrete List implementation UNPROVEN); other branches of the 0x2383c18 body
were not exhaustively attributed to the #552 argument.

## First divergence + next step
First divergence: capture shows only `[object Object]` while guest requires
`List.size/get` + `ApplicationExitInfoCpp.mPid`. Next: harness builds a DEX-typed
object with exactly those methods/fields (within the stated limits).
