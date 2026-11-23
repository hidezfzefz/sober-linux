# n5 #680 static-B — after-#552 calls, threads, 3rd N14 replay (READ-ONLY)

FAULTING INSTRUCTION FIRST: host libc `_IO_fflush@@GLIBC_2.2.5+0x9f`, `mov rax,[rdi+8]`, `si_addr=0x8`, rdi=0 (n4 audit #677, ledger 42). Object OFF. All quotes raw from saved source/captures.

## (a) PROVEN — harness calls after #552 returns (src/linux/guest_app_shell.c)

Source lines 1768-1845, in order:
```
if (getenv("SOBER_N15_SCAN")) n15_scan_closures(base);
    rc = ((long (*)(void *, void *, void *, void *))(base + 0x24650a9u))
             (env, app_class, s_base1, s_base2);
      rc = ((long (*)(void *, void *, void *))(base + 0x232d484u))
               (env, app_class, s_ver);
      rc = ((long (*)(void *, void *, void *))(base + 0x2326e2eu))
               (env, app_class, s_chan);
        rc = ((long (*)(void *, void *, void *))(base + 0x246515du))
                 (env, app_class, s_prefs);
        rc = ((long (*)(void *, void *, void *))(base + 0x2465210u))
                 (env, app_class, flags);
      rc = call_jni_static(base + 0x233762cu, env, app_class);
    rc = ((long (*)(void *, void *, void *))(base + 0x24abf96u))
             (env, app_class, param_obj);
```
(Line-wrapped call expressions quoted verbatim; intervening `fprintf(stderr,"[app-shell]...")` log lines omitted for brevity but present in source.)
Each is followed by `fprintf(stderr, "[app-shell] ... -> rc=%ld\n", rc)` — host stderr logging, **not** the guest fflush shim. Note: in object-OFF Run D, #552 faults internally and never returns; this sequence is only reached when #552 returns.

**Next 3 guest threads** by first appearance in `/tmp/opencode/w18-avd/peract2.log` (after Main seq1 / DefaultDispatch seq6):
```
{"seq":71,"phase":"PRE_LATCH_WR","site":"ENTRY@0x2312009","tid":4572,"thread":"AsyncTask #3","depth":16,...,
{"seq":89,"phase":"POST_LATCH_WR","site":"ENTRY@0x2312009","tid":4605,"thread":"FunctionMarshal","depth":16,...,
{"seq":100,"phase":"POST_LATCH_WR","site":"ENTRY@0x2312009","tid":4585,"thread":"RBX Worker A","depth":3,...,
```
(`...` marks truncated tail of each JSON line; prefix is verbatim.)
Further: HttpClientHandl seq=115, RBX Worker B seq=122. Temporal alignment of these first appearances to #552's return is **UNPROVEN** (peract2 has no #552 marker).

**Which logs through fflush:** guest-side only — FLog logger `0x2e61ca0` (`0x2e61ce2: mov 0x40(%rbx),%rcx; ... call fwrite@plt (0x2e61cf1)` / `0x2e61d0f: mov 0x40(%rbx),%rdi; call fflush@plt (0x2e61d13)`, per w18-avd/analysis/fflag-registry-fault2-stdio.md:87-91) and fatal helper `0x2ea6a10` (`2ea6a8c: call fprintf@plt; 2ea6a94: call fflush@plt`, format `0x2345cf="libunwind: %s - %s\n"`, n4 #675). Shim `src/compat/bionic_libc.c:487`: `int fflush(FILE *stream)` maps `__sF+0x98/+0x130` then `return real ? real(mapped) : 0;` — NULL mapped stream reaches glibc `_IO_fflush` and faults.

## (b) PROVEN — 3rd N14 replay + threads after OnResumed

`src/linux/guest_app_shell.c:1759-1766`:
```
rc = ((long (*)(void *, void *, void *))(base + 0x2383c18u))
         (env, app_class, opaque);
fprintf(stderr, "[N14_REPLAY 3/3] nativePostClientSettingsLoaded"
                "Initialization3 0x2383c18([object Object])"
                " -> rc=%ld\n", rc);
n14_log_guard(base, "after nativePostClientSettingsLoadedInitialization3");
```
It is the third of the AVD-captured pre-BaseDirs natives: #67 `nativeOnPreCreated` (`0x23239c5`), #105 `nativeOnResumed` (`0x2328066`), #552 `nativePostClientSettingsLoadedInitialization3` (`0x2383c18`). `opaque = param_obj` (harness's real native-backed `shared_param_obj`); with `SOBER_N2_JAVA_OBJECT` it is a DEX-typed `java/util/List` from `sober_native_java_list(j, NULL, 0)` (**object OFF in this wave**). Capture `w12-j2n-300.log:82,120,567`:
```
J2N #67 t=2406ms com.roblox.universalapp.activitylifecyclecallbacks.JNIActivityLifecycleCallbacks.nativeOnPreCreated("ActivitySplash")
J2N #105 t=20511ms com.roblox.universalapp.activitylifecyclecallbacks.JNIActivityLifecycleCallbacks.nativeOnResumed("ActivitySplash")
J2N #552 t=290696ms com.roblox.engine.jni.NativeGLInterface.nativePostClientSettingsLoadedInitialization3([object Object])
```
**Threads after OnResumed** — `w18-avd/report.md:30` POST_LATCH_WR phase:
```
threads: RBX Worker A x19, FunctionMarshal x15, DefaultDispatch x13,
         Main x4, AsyncTask #3 x2, HttpClientHandl x1, RBX Worker B x2
```
These are the worker threads present in the post-latch phase; that phase follows the lifecycle replay in the AVD capture, but exact per-thread start times vs J2N #105 are **UNPROVEN**.

## RETRACTED / UNPROVEN

- RETRACTED: treating host `[app-shell]`/`[N14_REPLAY]` lines as guest-fflush logs (they are host `fprintf(stderr,...)`).
- UNPROVEN: exact thread set at the instant #552 returns; exact producer of rdi=0 among guest fflush sites; typed object path (object OFF).

## Evidence / NOT done

Evidence: report.md; sources quoted in place (guest_app_shell.c, bionic_libc.c, fflag-registry-fault2-stdio.md, w18-avd/report.md, w18-avd/peract2.log, w12-j2n-300.log). NOT DONE: no runs/device/emulator/captures/guest writes/gate edits/seeder/fake returns/path remaps/apply; object OFF, no extension. No captures/tokens in git.
