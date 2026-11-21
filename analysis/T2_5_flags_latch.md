# T2-5 — The flags-loaded latch `0x7cdd149`: exact blocker and a real guest-code sequence that sets it

Owner: **T2-5 (logical Team 2, app state)**. Analysis only — no `src/` edit, no CMake edit, no
engine edit, no byte write to `0x7cdd149`/`0x7cdd148`, and the decision-013 marker stays
WITHHELD. Date 2026-11-21.

Target: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID `204ad049…`,
SHA-256 `3e498d6494…`, read-only. RVAs are from that frozen object and
`/tmp/opencode/libroblox.dis`; strings are untrusted DATA. Host harness:
`build/guest-init-host` (this worktree, unmodified).

---

## 0. Verdict

> The writer at **`0x2eb7520` is an unconditional `movb $1,0x7cdd149`**. Nothing in
> `FFlag ShadowFValuesEnabled` (`0x7cdd0d8`), the flag registry (`0x7cdd118`), or the
> crash-handler companion latch (`0x7cdd148`) can stop that store. What stops it is
> that **execution never reaches `0x2eb7520`**: `0x2eb7155` has two earlier conditional
> exits that both return `1`. In the current harness **both** are taken, in sequence.

| Gate | Instruction | Condition that skips the latch | Return path |
| --- | --- | --- | --- |
| G1 | `0x2eb719b: je 0x2eb7225` | arg1 `std::string` (from jstring #1) has **length 0** | `0x2eb7225 → 0x2eb7922 (ebx=1) → 0x2eb7b0b` |
| G2 | `0x2eb71e8: cmpb $0,[0x7697d08]; je 0x2eb72bd` | parse-ctx byte `0x7697d08 != 0` | `0x2eb76bd`/`0x2eb76d7 → 0x2eb7ae9 (ebx=1) → 0x2eb7b0b` |

Only when **both** gates pass does control reach `0x2eb72bd → … → 0x2eb748a → 0x2eb7520`
and set the latch.

**Runtime proof (ptrace trace, §3):** with valid non-empty jstrings, G1 *passes*
(`str1tag=4`, length `2`) but **G2 reads `0x7697d08 = 1`**, so `0x2eb7520` is never hit and
`0x2eb7b0b` returns `rax=1` with `0x7cdd149=0`. With the harness's current
`SOBER_CANONICAL_JSTRINGS=1` jstrings, G1 *also* fails because `GetStringUTFChars` hands the
guest an empty C string — so both defects are real and independent.

**Recommended legitimate fix:** do **not** try to repair the fussy `nativeInitClientSettings`
settings-parse gate. Use the guest's own crashpad installer instead —
`CrashpadHandlerMain` **`0x2ec9c30`** with a real `char **argv` containing `--channel` and a
non-empty value. That reaches the sibling writer **`0x2ec9fae`** with only one gate (a
non-empty native string), no JNI-string conversion and no settings-JSON parse (§5).

---

## 1. `0x2eb7520` is unconditional; the usual suspects are not the blocker

Verbatim from `libroblox.dis`:

```
2eb74f1: ...                              ; last log/refcount block
2eb74fb: je  2eb7520
2eb750d: jne 2eb7520
2eb7520: movb $0x1,0x4e25c22(%rip)        # 7cdd149   <<< THE LATCH WRITE
2eb7527: cmpb $0x0,0x4e25baa(%rip)        # 7cdd0d8   (ShadowFValuesEnabled)
2eb752e: je  2eb7543
2eb7530: mov  0x4e25be1(%rip),%rdi        # 7cdd118   (flag registry)
2eb7537: call 6c8323c                     ; install shadow-F-values crash handler
2eb753c: movb $0x1,0x4e25c05(%rip)        # 7cdd148   (companion latch)
2eb7543: ...
```

Consequences:

* `0x7cdd0d8` (`ShadowFValuesEnabled`, cleared by `ctor[3594]` `0x2279055` in this build)
  is tested **after** the `0x7cdd149` store. It controls only whether `0x6c8323c` runs and
  whether the *companion* latch `0x7cdd148` becomes 1. **It cannot prevent `0x7cdd149=1`.**
  The "ShadowFValuesEnabled is why the latch stays 0" hypothesis is false.
* The flag registry `0x7cdd118` is used at `0x2eb748a` (`call 0x236d85a`) on the path that
  *reaches* the latch, not as a branch predicate. A NULL registry would fault there, not
  return `1`. In the harness `0x7cdd118` is already seeded non-NULL (log:
  `pre_jni_registry @0x7cdd118 = …`), so registry state is not the blocker either.
* There is **no** `movb $0,0x7cdd149` anywhere in the object (only six `movb $1` writers:
  `0x2eb7520`, `0x2ec9fae`, `0x56d31a3`, `0x56d3386`, `0x56d6a4b`, `0x56d6b80`). The byte
  is never reset, so a zero latch means the store was never executed.

---

## 2. The two gates in `0x2eb7155` (the "conditional writer")

### 2.1 G1 — arg1 must be a non-empty `std::string`

`nativeInitClientSettings` `0x2359277` (export, size `0x133`) converts three jstrings with
the genuine helper `0x2315532` and calls `0x2eb7155` at `0x235933d`:

```
23592e4: lea -0x80(%rbp),%rdi ; mov %r14,%rsi(env); mov %r12,%rdx(jstring#1)
23592eb: call 2315532          ; jstring -> std::string  -> arg1 (rdi at 2eb7155)
...
2359327: lea -0x80(%rbp),%rdi  ; arg1 = std::string(jstring#1)
235932b: lea -0x68(%rbp),%rsi  ; arg2 = std::string("") [literal 0x2dd083]
235932f: lea -0x98(%rbp),%rdx  ; arg3 = std::string(jstring#2)
2359336: lea -0xb0(%rbp),%rcx  ; arg4 = std::string(jstring#3)
235933d: call 2eb7155
```

`0x2315532` is `GetStringUTFChars` (JNIEnv `+0x548`, fn[165]) → `std::string(const char*)`
`0x1dc1208` → `ReleaseStringUTFChars` (`+0x550`). Critically, when `GetStringUTFChars`
returns NULL **or the bridge's "unknown handle" empty string**, `0x2315532` builds a
**zero-length** std::string (`xorps/movq $0` at `0x2315583-0x2315589`).

In `0x2eb7155`:

```
2eb7188: movzbl (%rdi),%eax        ; libc++ std::string tag: bit0=1 long(len @+8);
2eb718b: test $0x1,%al             ; bit0=0 short(len = byte0>>1)
2eb718d: je  2eb7196
2eb718f: mov 0x8(%r12),%rax
2eb7194: jmp 2eb7198
2eb7196: shr $1,%eax
2eb7198: test %rax,%rax
2eb719b: je  2eb7225               ; G1: len==0 -> bypass, NO latch write
```

`0x1dc1208` confirms the encoding: `len<0x18` → `byte0 = len<<1` (SSO, bit0 clear). So a
real `"{}"` yields `byte0=0x04`, len `2`, G1 passes. The bypass path `0x2eb7225` runs the
large platform-init blob `0x56d654a` and returns `ebx=1` at `0x2eb7922` → `0x2eb7b0b`
**without** touching `0x7cdd149`. This is the path the earlier note
(`analysis/lua-rbxcrash-flags-loaded.md` §4) meant by "pass non-empty jstrings".

### 2.2 G2 — parse-context byte `0x7697d08` must be 0

If G1 passes:

```
2eb71a1: movb $0x0,0x7697d08(%rip)   ; clear the byte up front
2eb71a8: lea  0x7697cb0(%rip),%rbx   ; parse context (also used by JSON driver 0x237dc30)
2eb71de: call 235a58c                ; settings/flag-filter logger + JSON parse
2eb71e8: cmpb $0x0,0x7697d08(%rip)   ; G2
2eb71ef: je  2eb72bd                 ; byte==0 -> latch path
2eb71f5: testb $0x1,0x7697cf0(%rip)
2eb71fc: jne 2eb76bd
2eb7220: jmp 2eb76d7                 ; byte!=0 -> NO latch path
```

`0x7697d08` is `parse_ctx+0x58` (`0x7697cb0 + 0x58`). The settings logger `0x235a58c`
parses arg1 with the guest's SimpleJson parser (`0x235aff0`, called via `0x235aefa`) and
writes that parse result / flag-filter state through a `+0x58` pointer slot — the relevant
sites in `0x235a58c` are `0x235af5d` (`mov %eax,0x58(%rbx)`, the parser return),
`0x235a6b6` and `0x235ab85` (`movb $1,0x58(%reg)`). When that byte ends up `1`, the
`je 0x2eb72bd` is not taken and control dies at `0x2eb7ae9 (ebx=1) → 0x2eb7b0b` with the
latch still 0. So **settings content is a real second gate** — but via this one byte, not
via `0x7cdd0d8`.

Honesty note: the gate address (`0x2eb71e8`), its runtime value (`1`), and the fact that
`0x2eb7520` is skipped are proven (§3). The exact sub-instruction of `0x235a58c` that
supplied the `1` could not be isolated with certainty — a hardware watchpoint on
`0x7697d08` did confirm `0x235af5d` as a writer, but guest SIGTRAP/threading made repeated
watchpoint runs nondeterministic. The downstream conclusion is unaffected.

---

## 3. Empirical proof (unmodified harness + read-only ptrace observation)

A minimal ptrace tracer (`/tmp/t25_trace.c`, outside the repo) attached during the harness's
built-in `SOBER_GDB_PAUSE_MS` window, set software breakpoints on guest RVAs and logged
registers/globals. Full opt-in stack, jstrings built with `sober_jni_new_string_utf8`
(i.e. `SOBER_CANONICAL_JSTRINGS` **unset** so they are valid bridge objects):

```
TRACER base=0x7f30f4090000
TRACE entry_check  rax=0xabcdef0123456789  g7697d08=0  str1tag=4      # 0x2eb7188, SSO len2
TRACE len_check    rax=0x2                 g7697d08=0  str1tag=4      # 0x2eb7198 -> G1 PASSES
TRACE parse_ret    rax=0                   g7697d08=0                 # 0x235af5d, parser ok
TRACE logger_chk   rax=0x557d…             g7697d08=0                 # 0x235a60e
TRACE gate         rax=0x5531…             g7697d08=1                 # 0x2eb71e8 -> G2 FAILS
TRACE func_ret     rax=0x1                 latch=0  g7697d08=1        # 0x2eb7b0b, rc=1
# breakpoint at 0x2eb7520 was NEVER hit
```

Complementary evidence with the **current harness default** (`SOBER_CANONICAL_JSTRINGS=1`,
`SOBER_JNI_STRING_DEBUG=1`), `/tmp/opencode/t25_dbg.err:3682-3685`:

```
[jni-str] GetStringUTFChars(0x…) -> o=(nil) payload='(null)'    # jstring #1
[jni-str] GetStringUTFChars(0x…) -> o=(nil) payload='(null)'    # jstring #2
[jni-str] GetStringUTFChars(0x…) -> o=(nil) payload='(null)'    # jstring #3
[app-shell] nativeInitClientSettings 0x2359277 -> rc=1, flags-loaded latch=0
```

Root cause of this second symptom: `src/jni/jni_strings.c:sober_jstr_create()` builds a
`JstrHandle` that is **not** registered in `SoberJni.objects[]`, and
`sober_jstr_install()` (the function that would repoint JNIEnv fn[165] at a handler that
understands those handles) is **never called** anywhere in `src/`. So `sr_GetStringUTFChars`
(`native_jni.c:676`) fails `sr_validate`, returns its static `""`, and all three std::strings
are empty → G1 bypass. With that knob unset, `sober_jni_new_string_utf8` produces real,
registered `SrObject`s (log shows `payload='{}' / 'LIVE' / '2.740.931'`), G1 passes — but G2
still fails, exactly as the trace shows.

So the observed `rc=1, latch=0` is **two stacked defects**, and fixing only the jstring one
does not set the latch.

---

## 4. What is *not* the cause (explicitly ruled out)

| Candidate | Ruling |
| --- | --- |
| `FFlag ShadowFValuesEnabled` `0x7cdd0d8` | Tested at `0x2eb7527`, **after** the latch store; only gates `0x7cdd148` + `0x6c8323c`. |
| Flag registry `0x7cdd118` | Used at `0x2eb748a` on the latch-reaching path, not as a branch; non-NULL in harness. |
| `nativeInitializeNativeFlags` `0x2465210` | Feeds provider IDs only; contains no `movb $1,0x7cdd149` (prior A03/C2 conclusion, unchanged). |
| Concurrent reset of the latch | No `movb $0,0x7cdd149` exists in the object. |
| `nativePostClientSettingsLoadedInitialization3 0x2383c18` / Lua start | These are *consumers* of the latch (`0x23862b0: cmpb $0,[0x7cdd149]`); they fail because it is 0, not vice versa. |

---

## 5. Concrete opt-in module design (real guest code, no byte writes)

All RVAs below are real guest code in the frozen object. Suggested knobs are additions the
lead owns; this worker edits none. Keep the whole existing full-opt-in stack unchanged
(TSD heap seed, no neutralize, FFlag ctor replay `0x2e62390`, full `__sF`, faithful JavaVM,
controller/delegate seeds). All new knobs default OFF.

### 5.1 Primary — `CrashpadHandlerMain(0x2ec9c30)` with a native `char **argv` (recommended)

Why: the writer `0x2ec9fae` has **one** gate — a non-empty native string at global
`0x72a3710` — and needs no JNI string conversion at all:

```
2ec9f15: movzbl [0x72a3710] / 2ec9f2b: test %rax,%rax
2ec9f2e: je 2eca08a                  ; empty -> error path, no latch
2ec9f34..2ec9fa9:                    ; straight line, no early return
2ec9fae: movb $0x1,[0x7cdd149]       ; LATCH WRITE
2ec9fb5: cmpb $0,[0x7cdd0d8]         ; only the companion latch (as before)
```

`0x72a3710` is populated inside `CrashpadHandlerMain` itself:

```
2ec9cbe: call 2ec9dff  ; rdi=&argc, rsi=argv, rdx="--channel" (0x610b99) -> value char*
2ec9d09: call 2e57c20  ; store that value into global 0x72a3710
2ec9db9: call 2ec9eed  ; -> 0x2ec9fae writes the latch
```

`0x2ec9dff` is a plain `strncmp`-based argv scanner (`2ec9e80: call strncmp@plt`): it finds
the `"--channel"` token and returns the *following* element. Required argument shape:

```c
/* export: void CrashpadHandlerMain(int argc, char **argv) — nm: 0x2ec9c30 T */
char *argv[] = { "crashpad", "--channel", "LIVE", NULL };
int   argc   = 3;                       /* non-NULL argv, argc > 1 */
((void (*)(int, char **))(base + 0x2ec9c30u))(argc, argv);
```

Sequence (one new opt-in, e.g. `SOBER_FLAGS_LATCH_CRASHPAD`):
1. Run after the existing full-opt-in stack and **before** any TaskScheduler-initialising
   JNI call (`0x2383c18`, `0x26fe80f`, `0x25448f6`, `0x2eba91c`).
2. Call `0x2ec9c30` as above. `"LIVE"` is a benign, non-empty channel string; any non-empty
   token works. `"--channel"` is the exact key literal at `0x610b99`.
3. **Verification (read-only):** `*(uint8_t *)(base + 0x7cdd149) == 1`.

Fallback with the same writer: JNI export `nativeRunCrashpadHandler` **`0x2eca720`**
(`Java_com_roblox_engine_jni_NativeSettingsInterface_nativeRunCrashpadHandler`, export size
`0xb87`). It populates `0x72a3710` itself at `0x2ecae43-0x2ecae48` from a jstring converted
via `0x2315532`, then calls `0x2ec9eed` at `0x2ecb12e`. Call it as
`(JNIEnv*, jclass, jstringArray)` with a real array whose element 0 is a **non-empty**
jstring created by `sober_jni_new_string_utf8` (NOT `sober_jstr_create`, see §3). Heavier
than the `char**` route because it also walks the crashpad handler table at `0x7cdeb50`;
prefer `0x2ec9c30`.

### 5.2 Secondary — repairing the `nativeInitClientSettings` route (not yet achieved)

To make `0x2359277` itself set the latch, **both** gates must be cleared:
1. Use valid registered jstrings (`sober_jni_new_string_utf8`) or actually call
   `sober_jstr_install(env_fn_array, base)` so `SOBER_CANONICAL_JSTRINGS` objects reach the
   correct accessor. This clears G1.
2. Supply settings content that leaves `0x7697d08 == 0` at `0x2eb71e8`. The byte is the
   settings/flag-filter parse result written by `0x235a58c`; a parse that produces no
   flag-filter state leaves it 0. This is **not empirically solved** — with `"{}"` the byte
   was still 1 in every run — so do not claim this route works. A lead-owned opt-in could
   A/B real captured `ClientSettings` payloads against the `0x235af5d`/`0x235ab85` path,
   but the crashpad route of §5.1 should be used for the latch in the meantime.

### 5.3 Ordering and scope

* The latch must be set **before** the `TaskScheduler` guard `0x23862b0` runs (Lua start,
  post-settings init, `setTaskSchedulerBackgroundMode 0x2eba91c`).
* No guest byte/vtable/JNI method is patched or invented. Every address is shipped guest
  code. No direct write to `0x7cdd149`/`0x7cdd148` and no synthetic callback is used.
* Companion latch `0x7cdd148` will stay 0 unless the guest also fetches
  `ShadowFValuesEnabled` true via thunk `0x227e2c6`; that is expected and is **not** required
  to clear the TaskScheduler guard.

---

## 6. Proof criterion

A module is proven iff, after the §5.1 call and before TaskScheduler init:

1. `(uint8_t)(*(volatile unsigned char *)(guest_base + 0x7cdd149u)) == 1`
   (read-only check; do **not** write it), and
2. the `RBXCRASH: FatalRuntimeError (Can't initialize the TaskScheduler before flags have
   been loaded)` line (literal `0x36a389`) no longer appears, and the guard at
   `0x23862b0` falls through, and
3. the run reaches the next genuine guest state/fault — capture that RVA honestly.

Negative control: with the opt-in OFF, the same run must still show `latch=0` and the
`0x6caef66` int3 / `0x23862b0` trap. If toggling the knob changes a guest byte only because
the module wrote it directly, the proof is void.

---

## 7. Honesty / decision-013 implications

1. This supplies **missing real initialization** (the guest's own crash-handler install
   ran). It is *not* a crash fix by itself: setting the latch only changes which trap the
   reporter takes (`0x6caef4f`: A=B=1 → `abort()` at `0x6caef61`; A=1/B=0 → still `int3`).
2. Do **not** set `0x7cef650` (the `DebugForceTerminateOnRbxCrash` FFlag value byte) as a
   substitute: that only switches `int3` → `abort()` and is equally non-recovering.
3. No guest→Java callback is emitted by this path. Per decision 013 the
   `app shell rendered` marker stays WITHHELD; no gate/test change is implied.
4. The empirical observations here come from read-only disassembly, existing harness logs,
   and a ptrace observer that only *read* guest registers/memory and set software
   breakpoints. The engine SHA `3e498d6494…` is unchanged.

---

## 8. Evidence index

* `libroblox.dis`: `0x2359277`–`0x23593a9` (nativeInitClientSettings, call @ `0x235933d`),
  `0x2315532` (jstring→std::string, `GetStringUTFChars` @ `0x2315556`, empty fallback
  `0x2315583`), `0x1dc1208` (std::string ctor, SSO `byte0=len<<1` @ `0x1dc1230`),
  `0x2eb7155`–`0x2eb7b0b` (writer, G1 `0x2eb719b`, G2 `0x2eb71e8`, latch `0x2eb7520`,
  returns `0x2eb7922`/`0x2eb7ae9`/`0x2eb7b0b`), `0x235a58c`/`0x235adac`/`0x235aefa`/
  `0x235aff0` (settings logger/parser: `+0x58` writes `0x235af5d`, `0x235a6b6`,
  `0x235ab85`), `0x2ec9c30`–`0x2ec9dd5` (CrashpadHandlerMain: `--channel` scan
  `0x2ec9cbe`, store `0x2ec9d09`, call `0x2ec9db9`), `0x2ec9eed`–`0x2ec9fae` (latch
  `0x2ec9fae`, gate `0x2ec9f2e`), `0x2eca720`/`0x2ecae43`/`0x2ecb12e`
  (nativeRunCrashpadHandler), `0x23862b0` (TaskScheduler guard), `0x36a389` (panic string),
  `0x6caef4f`–`0x6caef66` (reporter).
* `str_at.py`: `0x610b99="--channel"`, `0x2dd083=""`, `0x4c3243="set_flag_filters_begin"`,
  `0x36a389="Can't initialize the TaskScheduler before flags have been loaded"`.
* `nm -D`/exports: `0x2ec9c30 T CrashpadHandlerMain`, `0x2eca720 T …nativeRunCrashpadHandler`,
  `0x2359277 T …nativeInitClientSettings`.
* Runtime logs: `/tmp/opencode/t25_dbg.err:3682-3685` (empty-string symptom),
  `/tmp/opencode/t25_fix.err:3682-3685` (valid strings, latch still 0),
  `/tmp/opencode/jstr.log:3683`, `/tmp/opencode/final-ab.log:3680`.
* ptrace trace tool (analysis-only, outside repo): `/tmp/t25_trace.c`; results quoted in §3.
* Prior analyses (read only): `A03-fflag-loaded-state.md`,
  `lua-rbxcrash-flags-loaded.md`, `fflag-registry-trigger.md`, `fflag-registry-fault2-stdio.md`.
  Harness source: `src/linux/guest_app_shell.c:953-980`,
  `src/jni/native_jni.c:284,334,671-688,976,1197`, `src/jni/jni_strings.c:78-81,265-278,357-376`.
