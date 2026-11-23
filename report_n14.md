# OVERNIGHT n1-4 static-A #656 — three JNI natives vs installer

Read-only; cores 2-3; no runs/device. Every success claim quotes a raw line.
Evidence: `raw_three_entries.txt`, `raw_closure_mask.txt`, `dis_fn_2a0e8c0.txt`.

Faulting instruction first: `0x6ca7f95: call 0x2a13350` (`si_addr=rsp-8`).

## 1. Entry prologues, JNI arg count/types — PROVEN

Quoted raw disassembly:
- `0x23239c5` = `Java_…JNIActivityLifecycleCallbacks_nativeOnPreCreated`:
  `23239c5: push %rbp` / `23239c6: mov %rsp,%rbp` / `23239c9: mov (%rdi),%rax`
  (env->functions) / `23239cc: mov %rdx,%rsi` (3rd native arg = jstring)
  / `23239cf: xor %edx,%edx` (isCopy=NULL) / `23239d1: call *0x548(%rax)`
  (GetStringUTFChars). **3 native args: (JNIEnv* rdi, jclass rsi, jstring rdx)**;
  1 Java-visible arg (String activity name).
- `0x2328066` = `…nativeOnResumed`: identical prologue,
  `232806a: mov (%rdi),%rax` / `232806d: mov %rdx,%rsi` /
  `2328072: call *0x548(%rax)`. **3 native args (env, class, jstring)**; 1 Java arg.
- `0x2383c18` = `Java_…NativeGLInterface_nativePostClientSettingsLoadedInitialization3`:
  `2383c18: push %rbp` / `2383c19: mov %rsp,%rbp` / `2383c1c: push %r14` /
  `2383c1e: push %rbx` / `2383c1f: sub $0x10,%rsp` /
  `2383c23: mov %rdx,%rbx` (3rd arg saved) / `2383c26: mov %rdi,%r14` (env saved).
  **3 native args (JNIEnv* rdi, jclass rsi, jobject rdx)**; 1 Java arg
  (`[object Object]`, per capture `J2N #552`).

## 2. Do they reach `0x2a14330 → 0x2a0e7c0 → 0x2a0ed45` — PROVEN / NO

- `nativeOnPreCreated` (body scanned `0x23239c5..0x2328026`): **NO** call to
  `0x2a14330`, `0x2a0e7c0`, `0x2a0e8c0` or `0x2a0ed45` found.
- `nativeOnResumed` (body scanned `0x2328066..0x2328100`): **NO** such call.
- `nativePostClientSettingsLoadedInitialization3` (body `0x2383c18..0x23c1200`):
  **YES**, quoted `23c1136: e8 f5 31 65 00 call 2a14330` — then
  `2a14337: jmp 2a0e7c0` → `2a0e83b: call 2a0e8c0` → `2a0ed45: mov %r15,0x28(%rbx)`.

So of the three, only `nativePostClientSettingsLoadedInitialization3` reaches
the installer. The other two use the ordinary GetStringUTFChars path.

## 3. What is masked into `obj+0x28` — PROVEN

From the containing function `0x2a0eba0` (last prologue before the store):
> `2a0ebce: movabs $0xffffffffffff,%r14`   (low-48-bit mask)
> `2a0ed0f: mov (%rax),%r15`               (tagged closure pointer from local)
> `2a0ed12: and %r14,%r15`                 (strip high 16 tag bits)
> `2a0ed18: mov $0x3,%edx`                 (mode 3)
> `2a0ed1d: call 2a13620`                  (zero-init helper for the closure)
> `2a0ed45: mov %r15,0x28(%rbx)`           (store canonical pointer into obj+0x28)

So `obj+0x28` holds **a canonicalised closure/task function pointer**
(`tagged & 0x0000ffffffffffff`), not a length, flag or jmethodID. The sibling
`2a0e8e4: movabs $0xffff000000000000,%r14` is the complementary high-tag mask
used elsewhere in the same region. Closure layout (static-B #653): `+0` = function
pointer, `+8` = context. Exact closure type UNPROVEN.

## What NOT done
No diff applied, no patch, no runs/gdb/perf, no device/AVD/Frida/phone, no new
captures, no guest writes, gate edits, seeder, ledger edits, no credentials.
Full-body scan absence is from the quoted address ranges; exact closure type
and the other two natives' full call graphs UNPROVEN.

## First divergence + next step
First divergence: capture-order lifecycle natives reach GetStringUTFChars but
not the installer; the post-settings native does reach it. Next: harness entry
breakpoint at `0x2a14330` on the nativePostClientSettings thread.
