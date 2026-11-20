# Guest JNI callback thunk 0x25848e0 / notifier 0x27eaf6e — Phase 5

Owner: **worker-1** (task #441). Scope: `analysis/callback/` only. No code edits.
Target: AppImage `libroblox.so`, BuildID `204ad049…` only.
Evidence: REA `ev_d838d804…` (full `.text` disassembly),
`ev_38ee286e…` (dynamic symbols), `ev_34eea014…` (sections),
`ev_af5de532…` (engine identity); plus local `objdump -d` / raw-byte reads of
the same SHA-256 and worker-2's `src/jni/native_jni.c` mapping (verified).

## 1. Thunk 0x25848e0 — exact SysV ABI

Disassembly (REA `ev_d838d804…`):

```
25848e0: push rbp ; mov rbp,rsp ; push rbx ; sub $0xd8,rsp
25848ec: test %al,%al
25848ee: je 2584919
25848f0: movaps %xmm0,-0xb0(%rbp)   ... (spill XMM0..XMM7 if AL != 0)
2584919: lea -0xe0(%rbp),%rax        ; 48-byte GP register save area
2584920: mov %rcx,0x18(%rax)         ; vararg 0 (RCX)
2584924: mov %r8,0x20(%rax)          ; vararg 1
2584928: mov %r9,0x28(%rax)          ; vararg 2
258492c: mov 0x72293f8(%rip),%rbx    ; ABI/JNI singleton (stack canary)
258493a: lea -0x30(%rbp),%rcx        ; va_list struct at rbp-0x30
258493e: mov %rax,0x10(%rcx)         ; va_list+0x10 reg_save_area = rbp-0xe0 (GP save base)
2584942: lea 0x10(%rbp),%rax
2584946: mov %rax,0x8(%rcx)          ; va_list+0x8 overflow_arg_area = rbp+0x10 (stack varargs)
258494a: movabs $0x3000000018,%rax   ; gp_offset=0x18(24), fp_offset=0x30(48)
2584954: mov %rax,(%rcx)             ; va_list+0 = {gp=0x18, fp=0x30}
2584957: mov (%rdi),%rax             ; JNIEnv -> function table
258495a: call *0x470(%rax)           ; *** JNI slot 142 = CallStaticVoidMethodV ***
```

**ABI (confirmed):**

| Register | Meaning |
| --- | --- |
| RDI | `JNIEnv*` in C++ form — pointer whose first word is the JNINativeInterface table (our `SREnvObj.functions`) |
| RSI | `jclass` (static method's class) |
| RDX | `jmethodID` |
| RCX, R8, R9 | first three varargs (GP save area offsets `0x18/0x20/0x28`; gp_offset 0x18 means 3 GP regs consumed by the named args) |
| AL | hidden flag: 0 = do not spill XMM0–7; 1 = spill them to `rbp-0xb0..-0x40` |
| stack | additional varargs beyond the three register slots |
| return | `void` (EAX ignored) |

The offset `0x470` = 142 × 8 = **global JNI slot 142** = `CallStaticVoidMethodV`.
This matches worker-2's bridge, which wires `fn[138]` (4 reserved slots + 138)
to `sr_CallStaticVoidMethodV` at `src/jni/native_jni.c:831` with the comment
"slot 142 / offset 0x470". The thunk's manual va_list layout (gp_offset 24,
fp_offset 48, reg_save_area, overflow_arg_area) is the standard SysV AMD64
va_list — our `va_list`-taking bridge stub is the correct counterpart. The
XMM0–7 spill area (`rbp-0xb0..-0x40`) is exactly `reg_save_area+fp_offset`
(`rbp-0xe0+0x30`), confirming the layout is internally consistent.

**AL nuance:** AL is the hidden variadic flag. AL=0 skips the (irrelevant for a
`(J)V` callback) XMM spill; if AL happens to be nonzero the thunk merely spills
XMM0–7 and still reads the single jlong from the GP area — so a stray AL is
harmless for this diagnostic, not a fault. The thunk reads class/method from
RSI/RDX **arguments**, not from globals (the notifier loads the globals and
passes them as args). It returns `void`; a `long`-returning function-pointer
cast is harmless because the result is discarded.

## 2. Notifier 0x27eaf6e — how it calls the thunk

```
27eaf7b: mov %rsi,%rbx                 ; notifier arg -> vararg 0 (jlong)
27eaf7e: mov 0x72293f8(%rip),%r15      ; ABI/JNI singleton
27eaf8c: lea -0x30(%rbp),%r14
27eaf93: call 0x227b9e2                 ; obtain JNIEnv into [r14]
27eaf98: mov 0x72a2f48(%rip),%rsi       ; cached CLASS (NativeGLJavaInterface)
27eaf9f: mov 0x72a2fc0(%rip),%rdx       ; cached METHOD (gameLoadedCallback)
27eafa6: mov (%r14),%rdi                ; JNIEnv
27eafa9: mov %rbx,%rcx                  ; vararg 0 = notifier's RSI
27eafac: xor %eax,%eax                  ; AL=0 (no XMM spill)
27eafae: call 0x25848e0                 ; thunk -> CallStaticVoidMethodV
```

So to exercise the thunk directly, supply:

- RDI = JNIEnv (`SREnvObj*`) from the bridge;
- RSI = value of cached class global `0x72a2f48`;
- RDX = value of cached method global `0x72a2fc0`;
- RCX = a **jlong value** (the callback signature is `(J)V`, so this is a
  number, not a pointer);
- AL = 0.

The notifier has no direct static callers (indirect dispatch only).

## 3. Cached class / method globals (verified provenance)

| Global | Set at | Contents |
| --- | --- | --- |
| `0x72a2f48` | `0x227b2c0` | `NewGlobalRef(FindClass("com/roblox/engine/jni/NativeGLJavaInterface"))` — class string `0x3dea65`, JNI FindClass slot `0x30`, NewGlobalRef slot `0xa8` |
| `0x72a2fc0` | `0x227b4eb` | `GetStaticMethodID(class, "gameLoadedCallback", "(J)V")` — name string `0x4fcdc1`, signature string `0x433046`, GetStaticMethodID slot `0x388` |

Both are plain BSS globals (no APS2 relocation targets them) and must be
seeded/non-NULL before the thunk is invoked, otherwise `CallStaticVoidMethodV`
will receive a NULL class or method. The JNI bridge already seeds all cached
IDs/flags (`SOBER_*` seeding path, decisions 027–036); worker-3 should verify
these two specifically.

## 4. Risks / caveats (must be honoured)

1. **JavaVM GetEnv / env shape** — the thunk dereferences RDI *as a C++ JNIEnv*
   (`mov (%rdi),%rax; call *0x470(%rax)`). Pass `SREnvObj*` (first field
   `functions`), not a raw `JNINativeInterface*`. A wrong shape faults at
   `0x2584957`.
2. **Varargs / AL flag** — AL must be 0 to skip the XMM spill; RCX is a **jlong
   value** because `gameLoadedCallback` is `(J)V`. Passing a pointer or leaving
   AL set can misbuild the va_list. The bridge's `sr_CallStaticVoidMethodV`
   takes a real `va_list` and must match the thunk's manual layout (gp 0x18 /
   fp 0x30) — this is already the case.
3. **Reentrancy** — `sr_CallStaticVoidMethodV` dispatches by method name to a
   handler; the handler must not call back into the guest in a way that
   re-enters the broken `0x233319a` path (decision C: do not call
   `nativeAppBridgeAppStart 0x247dc8d`). Use the benign startup RVAs only.
4. **Globals must be live** — class `0x72a2f48` and method `0x72a2fc0` must be
   non-NULL at call time; verify the bridge's seeding populated them.
5. **Honesty** — per decision A/B, this is an **opt-in diagnostic** that proves
   a genuine guest→Java callback through the guest's own thunk plus the
   phase-3 render evidence. It does **not** prove a login/app-shell frame;
   every report must state that explicitly, and the strict decision-013 gate
   must not emit `app shell rendered` on this alone.

## 5. Evidence

- REA `ev_d838d804…` — disassembly of thunk `0x25848e0`, notifier `0x27eaf6e`,
  `JNI_OnLoad` caching sites `0x227b2c0` / `0x227b4eb`.
- REA `ev_38ee286e…`, `ev_34eea014…`, `ev_af5de532…`.
- Local raw-byte read of signature/name strings at `0x4fcdc1` ("gameLoadedCallback")
  and `0x433046` ("(J)V"); class string `0x3dea65`.
- `src/jni/native_jni.c:831` — `SETFN(*t, 138, sr_CallStaticVoidMethodV)`
  (global slot 142 / offset 0x470), cross-checked by worker-2.
