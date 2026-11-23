# WAVE27 #626 — post-return predicate + native sequence table (static)

Read-only; no runs/device/phone/emulator. Engine SHA `3e498d64…`.
No fault-time register is used as evidence (wave rule).
Evidence: `dis_postreturn.txt`, `dis_6ca21c6.txt`, `dis_lambda_6ca22ee.txt`,
`capture_lines.txt`, `harness_lines.txt`.

## (a) Post-return path — PROVEN

At helper `0x2315532`, after `call *0x548(%rax)` (`GetStringUTFChars`, fn[165]):
```
2315556  call *0x548(%rax)
231555c  test %rax,%rax          ; <-- NULL test on the returned chars pointer
231555f  je 2315583              ; NULL -> zero the 24-byte out struct and return
2315561  mov %rax,%r12
231556a  call 0x1dc1208          ; build std::string from chars
231556f  mov (%r15),%rax
231557b  call *0x550(%rax)       ; ReleaseStringUTFChars
```
So the post-return test is a plain **NULL check**, not a length check, not
string equality, not a file/dir check. PROVEN.

`0x6ca229c` is in function **`0x6ca21c6`** (prologue at `0x6ca21c6`, canary
from `0x72293f8`; full body `dis_6ca21c6.txt`). It is another **scheduler-gated
JNI wrapper**, this time for `GetStringUTFChars`:
```
6ca21f4  call 0x2a13350          ; predicate
6ca21f9  test %al,%al
6ca21fb  je 6ca2282              ; FALSE -> direct path
...      TRUE: closure lambda 0x6ca22ee, call 0x2a13560 (dispatcher)
6ca2282  call 0x6c89e50 / 0x6ca228b call 0x6c89e70  (JNIEnv TLS getters)
6ca2290  mov (%rax),%rax
6ca2296  mov %r14,%rsi ; 6ca2299 mov %rbx,%rdx
6ca229c  call *0x548(%rax)       ; GetStringUTFChars (fn[165])
```
Predicate `0x2a13350` = `*(obj+0x28) != 0` (obj from `0x2a0de60`), the same
scheduler-installed-callback word as the FindClass/ExceptionCheck wrappers.
The TRUE-path lambda `0x6ca22ee` also calls `*0x548(%rax)` at `0x6ca2340`.

**Assert with a message string? NO — RETRACTED.** Every RIP-relative reference
in `0x6ca21c6` and its lambda is either code (`0x6ca22ee`) or the canary global
`0x72293f8`; no string address is loaded. The harness label "guest assert/fault
path" for `0x6ca229c` is a misnomer: it is a JNI `GetStringUTFChars` call behind
the `*(obj+0x28)!=0` scheduler gate. Generic `assertion failed: …` strings exist
elsewhere in the binary but none is referenced by this function.

## (b) Native sequence table — PROVEN

Harness order from `runC.err` (entry/call-site log lines, not fault registers):
| # | Harness native | Args (entry log) | Capture (w12 #/args) | Verdict |
|---|---|---|---|---|
| 1 | `nativeInitClientSettings 0x2359277` (:3706, AVD-boundary replay) | JSON/null/app tag | #551 `(JSON, null, "GoogleAndroidApp")` t=290317 | MOVED (capture 4th) |
| 2 | `JNI_OnLoad 0x227af5a` (:3715) | `(vm, NULL)` | no J2N entry (not Java-called) | EXTRA |
| 3 | `nativeSetBaseDataDirectories 0x24650a9` (:3724) | `(env, jclass, "/tmp/opencode/sober-apk/split" ×2)` | #612 two Android data dirs t=290866 | DIFFERS (value+order) |
| 4 | `nativeSetRobloxVersion 0x232d484` (:3725) | `(env, jclass, "2.740.931")` | #534 `"2.740.931"` t=290179 | MATCH value, order swapped |
| 5 | `nativeSetRobloxChannel 0x2326e2e` (:3726) | `(env, jclass, "LIVE")` | #70/#542 `""` t=290171/290179 | DIFFERS (value+order) |
| 6 | `nativeGameGlobalInit 0x233762c` (:3727) | `(env, jclass)` | #541 no Java args t=290202 | MATCH value, MOVED last |
| — | `nativeSetPreferencesFile` | not called | #613 `"prefs"` t=290868 | MISSING |
| — | `nativeInitializeNativeFlags` | not called (opt-in) | #614 t=290891 | MISSING |

**Deviation summary:** capture order is Channel → Version → GameGlobalInit →
InitClientSettings → BaseDirs → PreferencesFile → InitFlags; harness order is
InitClientSettings → JNI_OnLoad → BaseDirs → Version → Channel → GameGlobalInit.
Three ordering inversions (BaseDirs/Version/Channel, GameGlobalInit,
InitClientSettings), two missing natives, one extra JNI_OnLoad, and two value
mismatches (base paths, channel). No username paths in captures or harness
args; no redaction needed.

## RA (entry/call-site based)
`0x24650a9 -> 0x2315532 -> 0x2315556 GetStringUTFChars -> 0x231555c NULL test`;
separate wrapper `0x6ca21c6 -> 0x2a13350 predicate *(obj+0x28)!=0 -> FALSE
0x6ca229c GetStringUTFChars / TRUE 0x6ca22ee -> *0x548 @0x6ca2340`.

## What NOT done
No diff proposed/applied, no harness edit, no runs/gdb/perf, no device/AVD/
emulator/Frida/phone, no new captures, no guest writes, no gate edits, no
ledger edits, no credentials. No fault-time register used as evidence. No
dynamic confirmation of the predicate value.

## First divergence + next step
First divergence: native call ORDER/VALUE deviation plus the scheduler predicate
`*(obj+0x28)!=0` gate on the post-return JNI wrapper. Next: harness (entry/call
breakpoints only) logs `*(obj+0x28)` at `0x6ca21f4` on the Run D thread.
