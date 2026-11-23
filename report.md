# n3 #670 — faulting `mov rax,[rdi+0x8]`, rdi provenance, #552 call order

Read-only; cores 2-3; no runs/device. Every success claim quotes a raw line.
Evidence: `raw_fault.txt`, `raw_order.txt`, `raw_capture.txt`.

## FAULTING INSTRUCTION FIRST — PROVEN

Quoted from `/tmp/opencode/n1-harness/n14_1.err:3728`:
> `[W30_FAULT] rip=0x00007f6508f078ff si_addr=0x0000000000000008 rsp=0x00007fff907cd540 rbp=0x00007fff907cd5b0 insn=48 8b 47 08 74 05 48 85`

Bytes `48 8b 47 08` = **`mov rax,[rdi+0x8]`**, then `74 05` (je), `48 85` (test).
Fault registers quoted at `:3736`: `rdi=0x0000000000000000`, `rax=0x0000000000000000`, hence `si_addr=0x8` — NULL+8 dereference, not a stack-guard fault.

RVA / containing function: harness's own report states explicitly
> `rip=0x00007f6508f078ff (host/shared-lib, no guest_rva)` (n1-harness/report_n14.md).

Guest base in that run was `0x7f64f9cf8000` (raw `n14_1.err:7`), and
`0x7f6508f078ff - 0x7f64f9cf8000 = 0xf20f8ff`, far beyond the engine size
(120,041,712 B), confirming it is **not** in guest `.text`. So there is **no
guest RVA**; the containing function is host-side. Exact host symbol is
**UNPROVEN** — no raw line maps that RIP to a function name (the harness
labels it `host/shared-lib`).

## Entry-to-fault path from #552 `0x2383c18` — PROVEN links

Quoted raw lines from `n14_1.err` (in order):
> `:3702 [app-shell] W24 JNI_OnLoad FIRST 0x227af5a -> ver=0x10006`
> `:3708 [app-shell] AVD boundary nativeInitClientSettings 0x2359277 -> rc=0 D08_len=0 latch=1`
> `:3715 [app-shell] controller ensure_flag -> rc=0 flag=1`
> `:3724 [N14_REPLAY 1/3] nativeOnPreCreated 0x23239c5("ActivitySplash") -> rc=-6066930334832433271`
> `:3726 [N14_REPLAY 2/3] nativeOnResumed 0x2328066("ActivitySplash") -> rc=-6066930334832433271`
> `:3728 [W30_FAULT] rip=0x…08ff …`

There is **no** `N14_REPLAY 3/3` line — the third native (`0x2383c18`,
`nativePostClientSettingsLoadedInitialization3`) faulted before its log line.
The guest entry prologue (quoted earlier) saves the jobject arg:
`2383c23: mov %rdx,%rbx` / `2383c26: mov %rdi,%r14`; the host bridge receives
that jobject as its first argument and faults on `[rdi+8]`.

## Where rdi comes from — PROVEN / UNPROVEN

Raw fault dump shows **`rdi=0x0000000000000000`**. With `SOBER_N2_JAVA_OBJECT`
default OFF, no Java object was built, so the third native received a NULL
jobject argument. Therefore the faulting `rdi` is the **JNI jobject argument**
of the replayed #552 native (passed through the host bridge), **not** a global
and **not** a TLS-holder field. PROVEN that rdi==0; the exact host function
that derefs `+8` is UNPROVEN.

## Which STARTUP native sets the source — PROVEN (capture)

Quoted from `w12-j2n-300.log`:
> `566:J2N #551 t=290317ms com.roblox.engine.jni.NativeGLInterface.nativeInitClientSettings("{…}", null, "GoogleAndroidApp")`
> `567:J2N #552 t=290696ms com.roblox.engine.jni.NativeGLInterface.nativePostClientSettingsLoadedInitialization3([object Object])`

`nativeInitClientSettings` (#551, RVA `0x2359277`) is the startup native
immediately before #552 and is the one the harness also replays at `:3708` —
the source of the post-settings path. Earlier capture lines show lifecycle
natives (`nativeOnPreCreated` #67 at t=2406ms, `nativeOnResumed` #105 at
t=20511ms) long before #551/#552.

## Is #552 called at the same point in harness? — PARTLY PROVEN / DIFFERS

Capture order (quoted): lifecycle natives at t≈2.4 s / 20.5 s → #551
t=290317 → #552 t=290696 → #612 BaseDirs t=290866.
Harness order (quoted lines above): JNI_OnLoad FIRST → #551 boundary →
controller flag → replayed `nativeOnPreCreated`/`nativeOnResumed` → #552 fault.
So #552 is still **after #551 and before BaseDirs**, but the two lifecycle
natives are replayed immediately before it rather than at their captured early
times, and the harness adds a `JNI_OnLoad FIRST` step that the capture hook
does not record. Order is therefore **not identical**; the relative
post-#551/pre-BaseDirs position is preserved.

## What NOT done
No apply, no build, no runs/gdb/perf, no device/AVD/Frida/phone, no new
captures, no guest writes, gate edits, seeder, fake returns or path remaps, no
credentials, no captures/logs/tokens in git.

## First divergence + next step
First divergence: harness passes NULL for the #552 jobject (object OFF), so the
host bridge derefs `[NULL+8]`; capture passed a real Java `List`. Next: harness
logs the #552 argument at the host bridge entry (faulting instruction first).
