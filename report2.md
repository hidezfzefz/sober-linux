# n9w4 #722 static-B — TaskScheduler init prerequisites (READ-ONLY)

FAULTING INSTRUCTION FIRST: guest `int3 0x6caef66` (RIP `0x6caef67`), SIGTRAP; `[0x7cdd149]=0`, `[0x7cef650]=0` 6/6 (harness #721). No runs/writes.

## PROVEN — TaskScheduler guard and its prerequisites (raw disasm)

TaskScheduler setup lives in `nativePostClientSettingsLoadedInitialization3+0x238e`, entry `0x2385fa6`; it initializes a config struct and then tests the flags-loaded latch:
```
2385fa6: 55                    push   %rbp
2385fcd: 48 89 1f              mov    %rbx,(%rdi)     ; zero field +0
2385fd2: 88 47 08              mov    %al,0x8(%rdi)   ; +8 = 1
2386008: e8 59 e3 b1 ff        call   1ea4366
2386269: e8 79 fc a4 ff        call   1dd5ee7        ; allocator-backed object -> +0x1c8
23862a0: e8 2b fb 68 00        call   2a15dd0
23862b0: 80 3d 92 6e 95 05 00  cmpb   $0x0,0x5956e92(%rip)  # 7cdd149
23862b7: 0f 84 cb 02 00 00     je     2386588         ; latch==0 -> reporter
2386588: 48 8d 35 fa 3d fe fd  lea    -0x201c206(%rip),%rsi  # 36a389
                                      ; "Can't initialize the TaskScheduler before flags have been loaded"
238658f: bf 06 00 00 00        mov    $0x6,%edi       ; index 6
2386594: e8 c4 87 92 04        call   6caed5d         ; shared dispatcher -> int3 6caef66
```
**Prerequisite 1 — flags-loaded latch `0x7cdd149 != 0`.** static-A #719 n9w3 enumerated exactly six `movb $0x1,0x7cdd149` writers: `0x2eb7520` (in `0x2eb7155`, called from `nativeInitClientSettings 0x2359277` at `0x235933d`, gated by parse-ctx `0x7697d08==1` → skipped), `0x2ec9fae` (Crashpad, gated channel `0x72a3718`), and `0x56d31a3/0x56d3386/0x56d6a4b/0x56d6b80` (dismissAll family; static-A #724 corrected #719's "no direct callers" — they do have static callers `0x2eb8296→0x56d2d35`, `0x56d6765→0x56d6779`, but no genuine CppProxy route). Harness log `n9_1.err:3695 [flags-latch] route=none (inert, latch=0)`; fault bytes show latch 0 in all six runs — hence the guard fires.

**Prerequisite 2 — scheduler nesting prime on `FunctionMarshaller`.** `src/linux/guest_app_shell.c:1176-1177`:
```
/* Scheduler guest nesting prime: install now; the actual prime is fired
 * from on_attach("FunctionMarshaller") at the JavaVM attach site. */
int sg_rc = sober_scheduler_guest_install((void *)base);
```
and `:1897-1901`: `sober_scheduler_guest_on_attach("FunctionMarshaller");` — opt-in (`SOBER_SCHEDULER_GUEST=1`), harness-side call is "the earliest point available to us". Guest-side worker primitives `0x2a132c0(1)` nesting write, thunk `0x2a0daa0`, vtable call `0x2a0daf7` were documented in `analysis/A06-scheduler-state.md`, which is **no longer on disk** (audit #718) — UNPROVEN from surviving files. Surviving `analysis/A08-surface-delegate.md:463` only confirms the independent symptom: `RBXCRASH: Can't initialize the TaskScheduler before flags have been loaded`, faulting RVA `0x6caef67`.

**Prerequisite 3 — MessageBus prime before V2InitWithParams.** `analysis/callback/task507-preinit-ordering.md:32,59-70` (raw): `nativeAppBridgeV2InitWithParams 0x24abf96`; "The guest reaches this function exactly once: `0x24ad699: lea rsi,[0x7234520]; mov edi,1; call 0x253cb56`"; "the first step after spending the guard is the MessageBus service getter `0x24a8e44(4)`" and "the prime is safe as soon as the guest image is mapped and relocated" — so it must run before `0x24abf96` to avoid the one-shot window.

## PROVEN / UNPROVEN — capture natives around the guard

- **#727 AppStart = `0x247dc8d`**: `guest_app_shell.c:1919-1931` call `(env, app_class, s_url, s_ua, 0, s_aid, s_plat, s_ext)` with log `"[app-shell] guest call 0x247dc8d nativeAppBridgeAppStart..."`.
- **#734 V2InitWithParams = `0x24abf96`**: `guest_app_shell.c:1871` `"[app-shell] guest call 0x24abf96 nativeAppBridgeV2InitWithParams(param_obj) -> rc=%ld"`.
- **#736 StartLuaAppDM = `0x25448f6`**: `guest_app_shell.c:914-918` `(env, clazz)` + log; `:1940-1946` `real_appstate_start_lua(...)` then `"after StartLuaAppDM"`.
- UNPROVEN: capture numbering/timestamps for #727/#734/#736 and their exact order relative to the guard — the authoritative `w12-j2n-*.log`, `w18-avd/`, and `n8*` logs are **gone** (audit #718/#720: missing, not deleted by us), so no raw capture line can be quoted. Harness source order is V2InitWithParams → AppStart → StartLuaAppDM; capture numbering suggests a different order — cannot resolve statically.

## RETRACTED / NOT done

- RETRACTED: any claim of verified latch=1/mimalloc-HardAssert in n9 (audit #720 — unverifiable, logs gone).
- NOT DONE: no runs/device/guest writes/gate edits/seeder/fake returns/path remaps; object OFF; no captures/tokens in git.
