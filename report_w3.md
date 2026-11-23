# n7w3 #694 — 3rd N14 replay call, args, and missing log line

Read-only; cores 2-3; no runs/device. Every claim quotes a raw line.
Evidence: `raw_n14_source.txt`, `raw_third_call.txt`, `raw_n14_log.txt`.

Faulting instruction first: host libc `7c8ff: 48 8b 47 08 mov rax,[rdi+0x8]`
= `_IO_fflush+0x9f`, NULL rdi, `si_addr=0x8`.

## Exact call site / args / log statement — PROVEN

Quoted source `guest_app_shell.c:1760-1766`:
> `rc = ((long (*)(void *, void *, void *))(base + 0x2383c18u))`
> `         (env, app_class, opaque);`
> `fprintf(stderr, "[N14_REPLAY 3/3] nativePostClientSettingsLoaded"`
> `                "Initialization3 0x2383c18([object Object])"`
> `                " -> rc=%ld\n", rc);`
> `n14_log_guard(base, "after nativePostClientSettingsLoadedInitialization3");`

So the third replay is at **`guest_app_shell.c:1760-1761`**, calling native
`0x2383c18` (`nativePostClientSettingsLoadedInitialization3`) with exactly
3 arguments: `(JNIEnv* env, jclass app_class, jobject opaque)`. `opaque` is
set at `:1736` to `param_obj` — quoted `void *opaque = param_obj;` with the
comment "use the harness's real native-backed shared_param_obj (not a
fabricated empty object)". If `SOBER_N2_JAVA_OBJECT` is set, `:1755-1758`
replace it with `sober_native_java_list(...)` (the DEX-typed List).

The log statement immediately after is `:1762-1764`
`[N14_REPLAY 3/3] nativePostClientSettingsLoadedInitialization3 0x2383c18([object Object]) -> rc=%ld`,
followed by `n14_log_guard` at `:1766`.

## Why the log never prints — PROVEN

**There is no source statement between the call and the fprintf** — the
fprintf at `:1762` is the very next statement after `rc = … (…)` at `:1760`.
Therefore the only way the line can be absent is that the native call never
returns. Raw harness log `n14_1.err` (quoted):
> `:3724 [N14_REPLAY 1/3] nativeOnPreCreated …`
> `:3726 [N14_REPLAY 2/3] nativeOnResumed …`
> `:3728 [W30_FAULT] rip=0x00007f6508f078ff si_addr=0x8 insn=48 8b 47 08 …`
> `:3730 signal=11 tid=1672729 rip=0x00007f6508f078ff si_addr=0x8`

A whole-file grep for `N14_REPLAY 3/3` in `n14_1.err` returns **0** lines
(statistically confirmed by the harness report: "The 3rd native's log line is
ABSENT in all 3 runs — it faulted before returning"). So the fault occurs
inside the call at `:1760`; control never reaches `:1762`.

## Trace between call and log — PROVEN / UNPROVEN

Source path: `guest_app_shell.c:1760` call → (no intervening statement) →
`:1762` fprintf. Native path (quoted disasm from earlier waves):
`2383c18` entry saves the jobject (`2383c23: mov %rdx,%rbx`,
`2383c26: mov %rdi,%r14`) → `2383c67: mov %r14,%rdi` /
`2383c6a: mov %rbx,%rsi` / `2383c6d: call 2383c84` → JNIEnv calls → a
`fflush@plt` site (`6d32020`) whose NULL rdi reaches glibc `_IO_fflush+0x9f`
→ SIGSEGV. **UNPROVEN** which exact guest `fflush@plt` site supplies the NULL
rdi: harness n3 showed the entry jobject was valid (`rdx=0x56302c1ba750`,
`[rdx]=0x56300e0ebec0`) and that the inner NULL was "not from entry
rdi/rdx"; static-B named two guest flush sites (FLog `0x2e61ca0`, fatal
helper `0x2ea6a10`) but neither is proven to be the one firing here.

The other opt-in call to the same native at `guest_app_shell.c:897`
(`SOBER_REAL_APPSTATE_POSTSETTINGS`, quoted `((void (*)(void*,void*,void*))
(base+0x2383c18u))(env, clazz, list);`) is a **different** call site with its
own log at `:899-901`; it is not the N14 replay and is off by default.

## What NOT done
No apply/build, runs/gdb/perf, device/AVD/Frida/phone, new captures, guest
writes, gate edits, seeder, fake returns/remaps, hand-setting bytes, no
credentials, no captures/logs/tokens in git.

## First divergence + next step
First divergence: the 3rd replay call at `guest_app_shell.c:1760` faults
before its `:1762` log line; capture's #552 returned and logged. Next: harness
identifies the exact inner `fflush@plt` caller with the NULL rdi (faulting
instruction first).
