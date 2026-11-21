# H6 — guest `Created ANativeWindow … (ID:G)` emit path: where it is, what can honestly be hooked, and what to feed C10

**Scope:** analysis only. No `src/`, test, doc, CMake or git edits; no target execution.
**Frozen target:** `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` (verified
with `file`, `readelf -n`, `sha256sum` before analysis).
**Method:** static `objdump -d` / `readelf -r/-s/-l` on the frozen ELF; raw byte reads
via ELF LOAD segments; read-only inspection of `src/jni/frame_evidence.{c,h}`.
All guest bytes/strings are **untrusted data**; none were executed or treated as
instructions. RVAs are ELF virtual addresses (the guest load bias `B` is added at
runtime). Evidence class is marked per section: **OBSERVED** (read from frozen
bytes/current source) vs **DERIVED** (inference) vs **RUNTIME** (quoted from
existing capture files).

---

## 1. Exact guest code that formats and emits the line (OBSERVED)

### 1.1 The one and only string xref and its call site

`objdump -d libroblox.so | grep '3c163c'` yields exactly one code xref to the
format string (other hits are unrelated addresses that merely contain the
hex digits `3c163c`):

```
26feaf7: 48 8d 0d 3e 2b cc fd   lea  -0x233d4c2(%rip),%rcx   # 3c163c
```
`0x26feafe + (-0x233d4c2) = 0x3c163c`. Raw bytes at `0x3c163c` (57 bytes =
`0x39`, matching `r8d` below):

```
[FLog::JNIAppBridge] {}: Created ANativeWindow {} (ID:{})
```

Full bind-helper path, entry `0x26fea5a` (the honest guest bind helper already
used by bringup):

```
26fea5a  55                    push   %rbp                 ; bind_window(env, surface, tag)
26fea6a  4c 8b 3d 87 a9 b2 04  mov    0x72293f8(%rip),%r15 # JNI/canary ptr
26fea71  49 8b 07              mov    (%r15),%rax
26fea78  e8 53 30 63 04        call   6d31ad0 <ANativeWindow_fromSurface@plt>
26fea7d  48 89 c3              mov    %rax,%rbx            ; RA after fromSurface = B+0x26fea7d
26fea80  48 8b 3d 81 48 ba 04  mov    0x72a3308(%rip),%rdi # old ANativeWindow*
26fea8a  0f 84 86 00 00 00     je     26feb16              ; same-window branch -> different log
26fea90  ff 05 7a 48 ba 04     incl   0x72a3310(%rip)      ; generation++ (G_before -> G_now)
26fea9b  e8 20 30 63 04        call   6d31ac0 <ANativeWindow_release@plt> ; release OLD (if any)
26feaa0  48 89 1d 61 48 ba 04  mov    %rbx,0x72a3308(%rip) ; guest stores NEW window (W_now)
26feaa7  48 8b 3d ba 47 ba 04  mov    0x72a3268(%rip),%rdi # packed log config word
26feab0  c1 e8 08              shr    $0x8,%eax
26feab3  40 80 ff 06           cmp    $0x6,%dil            ; low byte must be >= 6
26feaba  3c 05                 cmp    $0x5,%al             ; high byte must be >= 5
26feabf  08 c8                 or     %cl,%al
26feac1  0f 85 bb 00 00 00     jne    26feb82              ; *** SKIP LOG when gate fails ***
26feac7  48 8b 35 a2 47 ba 04  mov    0x72a3270(%rip),%rsi # -> "JNIAppBridge" (0x3c15fa)
26feace  8b 05 3c 48 ba 04     mov    0x72a3310(%rip),%eax # ID = G_now
26fead4  48 8d 4d b0           lea    -0x50(%rbp),%rcx      # vararg array
26fead8  4c 89 31              mov    %r14,(%rcx)          # {} #1 = tag (3rd arg to helper, rdx)
26feadb  48 89 59 10           mov    %rbx,0x10(%rcx)      # {} #2 = NEW window pointer (fromSurface ret)
26feadf  48 89 41 20           mov    %rax,0x20(%rcx)      # {} #3 = generation from 0x72a3310
26feae3  48 c7 45 a0 ec 01 00 movq   $0x1ec,-0x60(%rbp)    # stack arg: category id 0x1ec
26feaeb  48 89 4d a8           mov    %rcx,-0x58(%rbp)     # stack arg: vararg array pointer
26feaef  0f 10 45 a0           movups -0x60(%rbp),%xmm0
26feaf3  0f 11 04 24           movups %xmm0,(%rsp)
26feaf7  48 8d 0d 3e 2b cc fd  lea    0x3c163c(%rip),%rcx  # format string
26feafe  41 b8 39 00 00 00     mov    $0x39,%r8d           # len = 57
26feb04  ba 05 00 00 00        mov    $0x5,%edx            # requested log level = 5
26feb09  41 b9 01 00 00 00     mov    $0x1,%r9d
26feb0f  e8 fc f2 c2 ff        call   232de10              # *** FLog emitter (direct, no GOT) ***
26feb14  eb 6c                 jmp    26feb82              # RA after FLog call = B+0x26feb14
```

Call-site bytes `e8 fc f2 c2 ff` at `0x26feb0f` decode as
`0x26feb14 + (-0x3d0d04) = 0x232de10`. This is exactly the shape
`frame_evidence_verify_call_site()` already checks for C2 (5-byte `e8 rel32`),
so the same helper can verify this call site with `ra = B+0x26feb14` and
`target = B+0x232de10`.

**Argument summary at `0x26feb0f` (SysV AMD64):**

| Register/stack | Value | Meaning |
| --- | --- | --- |
| `rdi` | `*(uint32_t*)(B+0x72a3268)` (normally `0x00000406`) | packed verbosity/level config |
| `rsi` | `*(void**)(B+0x72a3270)` | pointer to `JNIAppBridge` string (`0x3c15fa`) |
| `edx` | `5` | requested log level |
| `rcx` | `B+0x3c163c` | format string pointer (the genuine string) |
| `r8d` | `0x39` | format string length (57) |
| `r9d` | `1` | flag |
| `[rsp+0]` | `0x1ec` | category id |
| `[rsp+8]` | vararg array `{tag, window, gen}` | the three `{}` values |

The same helper has a **different** log at `0x26feb58`
(`0x46f987`, "Same native window … No need to do anything!") on the
same-window branch; that is a no-op rebind and must **not** be accepted as H6.

### 1.2 The FLog emitter `0x232de10` and the real RA of its sink dispatch

```
232de10  55                    push   %rbp
232de24  4d 89 c6              mov    %r8,%r14          ; len
232de27  49 89 cf              mov    %rcx,%r15         ; format string pointer
232de2a  41 89 d4              mov    %edx,%r12d        ; level (5)
232de2d  48 89 fb              mov    %rdi,%rbx         ; config word (0x0406)
232de42  80 fb 06              cmp    $0x6,%bl
232de45  0f 83 e6 00 00 00     jae    232df31           ; low byte >= 6 -> taken
...
232df31  89 d8                 mov    %ebx,%eax
232df33  c1 e8 08              shr    $0x8,%eax         ; high byte = 4 (default)
232df36  44 38 e0              cmp    %r12b,%al         ; 4 < level 5
232df39  0f 82 48 01 00 00     jb     232e087           ; *** SKIP: no message formatted ***
232df3f  48 83 3d d9 18 a6 05  cmpq   $0x0,0x7d8f820    # sink data pointer
232df47  0f 84 88 00 00 00     je     232dfd5           ; NULL -> async ring-buffer path
232df57  48 8d bd d0 fb ff ff  lea    -0x430(%rbp),%rdi  # 0x400-byte message buffer
232df65  e8 96 3d a0 04        call   6d31d00 <memset@plt>
232df9e  48 8d 45 10           lea    0x10(%rbp),%rax    # {0x1ec, varargs}
232dfa2  0f 10 08              movups (%rax),%xmm1
232dfa5  0f 11 4c 24 10        movups %xmm1,0x10(%rsp)
232dfaa  0f 10 8d b0 fb ff ff  movups -0x450(%rbp),%xmm1 # {format string, len}
232dfb1  0f 11 0c 24           movups %xmm1,(%rsp)
232dfb5  48 8d bd d0 fb ff ff  lea    -0x430(%rbp),%rdi
232dfcb  e8 d7 00 00 00        call   232e0a7             # formatter -> message at -0x430
232dfd0  e9 89 00 00 00        jmp    232e05e
232e052  43 c6 04 2c 00        movb   $0,(%r12,%r13,1)   # NUL-terminate message
232e05e  48 8b 05 bb 17 a6 05  mov    0x7d8f820(%rip),%rax # genuine sink pointer
232e065  48 85 c0              test   %rax,%rax
232e071  48 8d 8d d0 fb ff ff  lea    -0x430(%rbp),%rcx  # 4th arg = FORMATTED MESSAGE
232e078  48 89 df              mov    %rbx,%rdi          # 1st arg = config word (0x0406)
232e07b  44 89 e2              mov    %r12d,%edx        # 3rd arg = level (5)
232e07e  ff d0                 call   *%rax              # *** sink dispatch, 2 bytes ***
232e080  48 8b 0d 71 b3 ef 04  mov    0x72293f8(%rip),%rcx  # RA after sink call = B+0x232e080
232e087  ...                                            # stack-check / epilogue
232e0a1  c3                    ret
```

**Decisive RA fact:** the sink dispatch is `ff d0` (2 bytes) at `0x232e07e`,
so a host function chained into the `0x7d8f820` data pointer sees
`__builtin_return_address(0) == B + 0x232e080` — **not** `B+0x26feb14`.
`0x26feb14` is the RA of the *earlier, different* direct call to the emitter;
using it for the GUEST_LOG observation fabricates provenance.

Sink pointer initialization (only write found in the whole image):

```
6c14353  48 8d 05 36 6b 71 fb  lea    0x232ae90(%rip),%rax  # guest fast-log sink
6c1435a  48 83 3d be b4 17 01  cmpq   $0x0,0x7d8f820
6c14362  48 89 05 b7 b4 17 01  mov    %rax,0x7d8f820        # sink = base+0x232ae90
```

`0x7d8f820` is writable `.bss`; the same slot is also used by sibling emitters
(`0x6caf56c`, `0x231bb94`, `0x232738a`) with the same 4-argument signature, so a
wrapper must filter by RA **and** message shape.

### 1.3 Logging GOT / PLT inventory (OBSERVED)

* The call at `0x26feb0f` is **direct** (`e8 rel32`). There is no GOT/PLT slot
  holding `0x232de10` (`readelf -rW` has no relocation with addend `232de10`).
  Hooking that call site would require patching guest code bytes — forbidden.
* Real GOT slot for Android logging exists:
  `__android_log_print@plt` at `0x6d319f0`, `R_X86_64_JUMP_SLOT @ 0x7229438`.
  It is **not** on this emitter's dispatch path (no call from `0x232de10..0x232e0a1`).
  The fast-log sink `0x232ae90` reaches it only via its NULL-object fallback
  writer at `0x232d467` (`__android_log_print(4, "Roblox", "%s", message)`), or
  via vtable methods on the log object at `0x7943930` (not statically proven).
* Guest config globals: `*(uint16_t*)(B+0x72a3268) = 0x0406` and
  `*(void**)(B+0x72a3270) = B+0x3c15fa` (`"JNIAppBridge"`) are set once by the
  standard JNI-guard initializer at `0x1dd6921..0x1dd695b`; no other write to
  these addresses was found in the image.

---

## 2. Can a genuine observer be installed? (OBSERVED facts + DERIVED judgement)

### 2.1 Recommended: chain-wrap the guest sink *data pointer* at `B+0x7d8f820`

This is the only place where the genuine emitter hands us the **real,
guest-formatted message** as an argument, and it requires no guest code-byte
patch — only a writable data-pointer slot, exactly as the four existing
`fe_got_write()` hooks do.

Correct C signature (derived from `0x232e078/0x232e07b/0x232e071`, also
`0x6caf566/0x6caf569/0x6caf55c/0x6caf55f`):

```c
typedef void (*fe_flog_sink_fn)(uintptr_t category, void *ctx,
                                unsigned level, const char *text);
/* rdi=config word, rsi=JNIAppBridge tag, edx=level(5), rcx=NUL-terminated text */
```

Chain semantics: read old pointer, store wrapper, forward each call unchanged
to the old pointer **if non-NULL**, and record at most one immutable GUEST_LOG
observation when (a) `ra == B+0x232e080`, and (b) the text matches the exact
format-string shape of `0x3c163c`. Nothing is fabricated: the string, the
window pointer and the ID all come from guest registers/formatter output; the
RA comes from the real call instruction.

### 2.2 Secondary: `__android_log_print` GOT `B+0x7229438`

A true JUMP_SLOT and already implemented by our compat (`src/compat/android_compat.c:120`),
so a wrapper is easy and byte-safe. But it sees only the final logcat text
(`tag="Roblox"`, `fmt="%s"`, message in `rcx`) and only if the fast-log object
path reaches `0x232d467`; it cannot prove the emitter call site as tightly.
Use only as a corroborating source, never as the sole C10 producer.

### 2.3 Alternative: execute hardware breakpoint at `B+0x26feb0f`

An HW breakpoint via `perf_event_open` + signal delivery does not patch bytes
and would expose the raw arguments (`rcx = B+0x3c163c`, vararg array with
`window`/`gen`, RA `B+0x26feb14`). Strongest provenance, but per-thread debug
register setup, `perf_event_paranoid` permissions, signal-handler safety and
re-arming make it fragile and heavyweight. Not recommended for this producer.

### 2.4 Two hard blockers found (fail-closed; do not paper over them)

1. **The guest gate suppresses this log at the default config.** The single
   write to `0x72a3268` sets `0x0406` (low=6, high=4); both the caller
   (`0x26feab3..0x26feac1`) and the emitter (`0x232df36..0x232df39`) skip the
   message because the requested level is `5` and the configured high byte is
   `4` (`4 < 5`). Therefore **no `Created ANativeWindow` text is formatted at
   all** in this configuration; hooking the sink cannot manufacture one.
   Raising the high byte to `>=5` would enable it, but that is a guest-global
   write: permissible only for lead-owned setup code, explicitly declared as
   seeded guest state, never from the observation-only evidence module, and
   never touching `0x72a3308`/`0x72a3310`.
2. **The sink slot is NULL in the current harness** (per the WIP comment in
   `frame_evidence.c` / `frame_evidence.h` and absent `H6 FLog sink observer
   installed` lines in captured `*.out`). Writing our wrapper into a NULL slot
   flips the guest's `cmpq $0,0x7d8f820` check and makes the guest call the
   sink where it previously would not. The *text*, if it ever reaches us, is
   still genuinely formatted by guest code, but turning on a previously
   absent dispatch is a state change that must be declared (the header already
   asks callers to OR `frame_evidence_flog_installed()` into seeded state).
   If no genuine message arrives, C10 must stay REJECT.

**RUNTIME note:** captured bringup lines currently read
`G8 window verdict=0 reason=R8_NO_ACQUIRE` (e.g. `/tmp/opencode/*.out`), i.e.
C8 fails before C10 is even evaluated. Implementing H6 alone will **not** flip
the verdict; C8/C9 producers are still required.

---

## 3. Exact evidence fields to feed frame_evidence for C10 (fail-closed)

One `SoberFrameWindowObservation`, kind `SOBER_FRAME_WOBS_GUEST_LOG` (`7`),
allocated only when the real text passes every check:

| Field | Required value | Provenance / rationale |
| --- | --- | --- |
| `kind` | `SOBER_FRAME_WOBS_GUEST_LOG` (7u) | existing kind; no header change needed |
| `status` | `1u` only after full parse; otherwise do not submit (or submit 0 -> global R0) | never a partial/speculative record |
| `seq` | `fe_wobs_next_seq()` | ledger's monotonic sequence |
| `tid` | `fe_wobs_tid()` | must match the C2/C5/C7/C8/C9 observations; the log call is synchronous on the bind thread |
| `ra` | `B + 0x232e080` | **true** RA of `call *%rax` at `0x232e07e` (`ff d0`, 2 bytes). Do **not** use `B+0x26feb14`. |
| `window` | parsed `{}` #2 from the real message text | must equal `snap->window` (`W_now`) for C10 |
| `g_now` | parsed `{}` #3 ("ID:") from the real message text | must equal `snap->g_now`; must equal `fs->g_before + 1` (C5) |
| `surface`, `mnative`, `w_before`, `g_before`, `width`, `height`, `host_kind` | `0` | unused by C10 |

Parsing rules (all mandatory, else no event):
* exact literal prefix from string `0x3c163c` — `[FLog::JNIAppBridge] ` — plus
  the literal separators `Created ANativeWindow ` and ` (ID:` and a closing
  `)`; reject the "Same native window" string (`0x46f987`) explicitly;
* bound the length (`0x39` format + substitutions, use a fixed cap e.g. 256)
  and require NUL-termination;
* parse the window token from between the two separators, accepting `0x…`
  hex **and** decimal (the current WIP parser only accepts `0x`); reject
  trailing garbage, overflow (`ERANGE`) and zero;
* parse the ID as decimal after `ID:` with a strict end check (digits then
  `)`), reject overflow and zero;
* optionally cross-check the parsed `window`/`g_now` against read-only loads of
  `B+0x72a3308` / `B+0x72a3310`, but the **evidence value must come from the
  message**, not from those globals (otherwise C10 is circular).

Generation-counter behavior (OBSERVED): guest `incl` at `0x26fea90`
increments `0x72a3310` **before** the store at `0x26feaa0` and before the
format at `0x26feace`; therefore `G_now = G_before + 1 (mod 2^32)` and the
logged ID equals `G_now` naturally. The host must only read the counter; no
host writes, no re-rolls, no synthetic "catch-up" events.

**Strengthen C10 at the same time** (currently only checks
`glog && glog->window == snap->window && glog->g_now == snap->g_now`):
require exactly one GUEST_LOG record (no duplicates/mixed), `glog->status==1`,
`glog->ra == base + 0x232e080`, `frame_evidence_ra_is_guest(glog->ra)`, and a
read-only verification that `B+0x232e07e..0x232e07f == ff d0` and that
`B+0x26feb0f` is `e8 rel32` -> `B+0x232de10`. Reject with `R10_NO_LOG`
otherwise. Keep `host_publish=0` and the three integrity flags unchanged.

---

## 4. Where an H6 producer plugs into `src/jni/frame_evidence.c` (read-only)

The file is being edited concurrently by Worker-1, so line numbers below are
**as read during this task**; function names are stable.

* Existing WIP H6 block: comment/state at ~lines 151–167, parser
  `fe_parse_created_window()` ~208–251, hook `fe_flog_sink_hook()` ~253–281,
  kill switch `frame_evidence_flog_killed()` ~285–290, counters/install/uninstall
  ~292–330. This is the natural place for the corrected signature, parser and
  true-RA fix — no new file needed.
* Installation: `frame_evidence_install_window_observers()` ~925 (currently
  calls `fe_install_flog_sink()` at ~953); restoration in
  `frame_evidence_uninstall_window_observers()` ~957 (call at ~964). Keep the
  existing `fe_got_write()` style mprotect/restore and idempotence.
* Verdict: `fe_window_check()`, C10 block ~1250–1251; strengthen as in §3.
* Bringup: `sober_render_evidence_bringup()` ~996–1120; the genuine guest bind
  call `sober_frame_surface_bind_guest_window()` at ~1069 is the trigger; the
  H6 observer must be installed before it (it is, via ~1048). Update the
  stale "C10 guest Created-log has no H6 producer yet" diagnostic at ~1122.
* `frame_evidence.h`: kind 7 already exists (line 169), env var
  `SOBER_FRAME_EVIDENCE_FLOG_ENV` (line 112) and H6 API (lines ~393–400) are
  already declared. New RVA constants (`0x7d8f820`, `0x232e080`, `0x26feb14`,
  `0x26feaf7`) should be added there if the lead permits; otherwise define
  them locally in `frame_evidence.c` and document them.

---

## 5. Fail-closed recommendation and concrete defects in the current WIP

1. **Fix the hook signature/argument order.** Current
   `typedef void (*fe_flog_sink_fn)(int level, void *category, unsigned len, const char *text)`
   swaps `rdi`/`rsi` and mislabels `edx`: with the guest's actual call,
   `level` receives the low 32 bits of the config word (`0x406`), `category`
   receives the tag pointer, and `len` receives the *log level* `5`, not a
   string length. Use the 4-argument signature in §2.1.
2. **Do not fabricate the RA.** Current `obs.ra = base + 0x26feb14` is a
   different call's RA; use `__builtin_return_address(0)` and verify it equals
   `base + 0x232e080`. A fabricated RA is exactly the false-green pattern C2
   was written to prevent.
3. **Tighten the parser** to the full `0x3c163c` shape and strict token bounds
   (§3); reject the `0x46f987` "Same native window" line, decimal/hex malformed
   tokens, overflow and trailing text.
4. **Guard the ledger** against duplicate/mixed GUEST_LOG records and add the
   RA/call-site checks to C10 (§3). Never derive `window`/`g_now` from host
   globals.
5. **Resolve the two blockers in §2.4 honestly.** If the guest level gate is
   not enabled, the correct outcome is `R10_NO_LOG` / REJECT — **not** a
   synthesized message, a hand-built vararg array, or a host call into the
   sink. If lead-owned setup chooses to raise the guest log threshold, record
   that as seeded state and keep `host_publish=0`/integrity declarations
   unchanged.
6. **Keep the default REJECT** (integrity 0, host_publish 1 until proven),
   leave the kill switch default-ON but honour `SOBER_FRAME_FLOG_SINK=0`, and
   restore the original sink pointer on uninstall. Note that C8 currently
   fails first in captured runs: H6 is necessary but not sufficient for ACCEPT.

---

## 6. Evidence classification / limitations

| Claim | Class |
| --- | --- |
| Format string bytes, length, sole xref, bind-path disassembly, FLog emitter/sink disassembly, RA values, GOT relocation for `__android_log_print`, config-word write `0x0406`, `"JNIAppBridge"` string | **OBSERVED** (frozen ELF) |
| Sink pointer is NULL in this harness; C8 is current first failing condition | **RUNTIME** (WIP source comments / captured `*.out`) |
| Correct hook signature, RA filtering, parser rules, C10 strengthening | **DERIVED** from the observed calling convention, deliberately conservative |
| Whether the fast-log object at `0x7943930` routes FLog to logcat in this build | **UNKNOWN** statically; not relied on by the recommendation |

Limitations: no guest code was executed; no clean genuine
`Created ANativeWindow` line exists in the collected log corpus (see
`analysis/R10_render_log_evidence.md §3`), so the formatter's exact pointer
rendering (hex vs decimal) is inferred from the `{}` formatter at `0x6caf3e0`
and must be accepted in both forms. Writes to `0x72a3268` through a computed
pointer rather than a direct RIP-relative reference cannot be excluded by
static disassembly; the only direct write found is `movw $0x406` at `0x1dd694b`.

**Bottom line:** the honest observer is a chained host wrapper on the writable
guest sink data pointer `B+0x7d8f820`, filtered by real RA `B+0x232e080` and by
the exact `0x3c163c` message shape, feeding one GUEST_LOG observation whose
`window`/`g_now` come only from the guest-formatted text. Until the guest's
level-5 gate actually produces that text, the correct C10 result is REJECT
(`R10_NO_LOG`). Fail closed.
