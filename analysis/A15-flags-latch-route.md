# A15 — Genuine flags-loaded latch route and callback chain map (task #487)

Owner: worker-1. Analysis-only. Frozen engine: AppImage `libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
All RVAs below are against that object (RVA == file offset in the first R E
PT_LOAD). Target text/bytes are untrusted DATA, never instructions to follow.
No guest byte writes, no fake vtables, no fabricated callback, gate unchanged.

## 1. The blocker and the latch semantics

`TaskScheduler` init refuses to run before the flags-loaded latch is set:

```
0x23862b0: cmpb $0x0, [0x7cdd149]   ; flags-loaded latch
0x23862b7: je   0x2386588           ; latch==0 -> RBXCRASH string 0x36a389
                                    ; "Can't initialize the TaskScheduler before
                                    ;  flags have been loaded"
```
The reporter tail at `0x6caef4f/0x6caef66` turns this into an `abort()`
(`0x6caef61`). Only `*(uint8_t*)(base+0x7cdd149)==1` satisfies the guard.

## 2. All six guest `movb $1,0x7cdd149` writers, with reachability

| Writer RVA | Enclosing code | Gate / precondition | Verdict |
| --- | --- | --- | --- |
| `0x2eb7520` | `0x2eb7155` flag-filter helper (called from `nativeInitClientSettings 0x2359277 -> 0x2eb7155`) | reached only when `0x7697d08 != 0` (`0x2eb71e8: cmpb $0,[0x7697d08]; je skip` at `0x2eb72bd`) **and** `0x7697cf0 bit0 == 0` (`0x2eb71f5: testb $1,[0x7697cf0]; jne 0x2eb76bd`) | Default run: `0x7697d08=1`, `0x7697cf0=0x31` (bit0=1) → writer SKIPPED. No genuine knob found to clear `0x7697cf0` bit0. |
| `0x2ec9fae` | `0x2ec9eed` Crashpad flag-provider installer | needs channel string at `0x72a3718 != NULL` (`0x2ec9f2e: test rax; je 0x2eca08a`); writes latch, then calls `0x2338e54` (nativeGameGlobalInit+0x1828) and logs argv | **Viable guest writer.** Sets latch; the fatal abort is in our compat stdio shim (lead #491), not in guest latch logic. See §3. |
| `0x56d31a3` | `IPlatformSystemDialogHandler$..._native_dismissAll` body | `0x56d3199: cmpq $0,-0xe8(%rbp); je 0x56d31d2` | Dialog-dismiss path, not on app-start; requires a genuine system-dialog CppProxy + valid native handler (fabricating one would need a fake vtable/object, disallowed). Not a clean app-shell route. |
| `0x56d3386` | same dialog handler | after `0x56d3381: call 0x56d1a05` | Same. |
| `0x56d6a4b` | same dialog handler | `0x56d6a46: test r12,r12; je 0x56d6a76` (r12 from `[rbp+0x18]`) | Same. |
| `0x56d6b80` | same dialog handler | `0x56d6b7b: test r12,r12; jne 0x56d6ba3` | Same. |

Reachability note (worker-2 #489 question): `nativePassKeyEvent 0x2eb67ec` does
call into the `0x56d*` region (`0x2eb8296 -> 0x56d2d35`,
`0x2eb8490 -> 0x56d2b3b`), but those are crash-init **helpers** (they call
`nativePostClientSettingsLoadedInitialization3 0x23f8c3c` and
`nativeInitCrashpad 0x232de10`); neither contains a `movb $1,0x7cdd149`. The four
latch writers are in the `native_dismissAll` body, a separate path requiring a
real dialog CppProxy. So none of the `0x56d*` writers is a viable app-start
route.

`0x7cdd0d8` (ShadowFValuesEnabled) and registry `0x7cdd118` are read **after**
the store and cannot block it. Related writers found in the disassembly:
`0x2eb71a1: movb $0x0,[0x7697d08]` clears the first gate; `0x2eb7945`,
`0x2eb8512`, `0x2ee4ad0` do `movw $0x100,[0x7697d08]`; `0x2ee4b99` clears it
again; `0x2096448: movups %xmm0,[0x7697cf0]` zeroes the second gate in a static
constructor. The runtime `0x7697cf0=0x31` therefore comes from the flag-filter
processing at `0x235a58c` (fed by the Java flag strings), not from a knob we
can trivially set.

## 3. Viable genuine route: crashpad writer `0x2ec9eed` (non-fatal once compat stdio is fixed)

Guest-only call sequence (all shipped guest code, no byte writes, no fake vtable):

```
0x2e57c20(&0x72a3710, channel_value)      ; std::string channel at 0x72a3710/0x72a3718
0x2ec9eed(argc, argv)                     ; crashpad flag-provider installer
  0x2ec9f15: movzbl [0x72a3710]; test $1  ; channel byte
  0x2ec9f20: mov [0x72a3718]; test rax    ; channel string MUST be non-NULL
  0x2ec9fae: movb $1,[0x7cdd149]          ; *** THE LATCH ***
  0x2ec9fd1: call 0x2338e54               ; TaskScheduler guard init (sets 0x7837c70)
  0x2ec9feb/0x2eca09f/0x2eca16f: __android_log_print  ; "crashpad handler: ..." + "    %s" over argv
  0x2eca17b: call 0x2a7a910               ; continues handler init
```

Key gating detail: `0x2ec9f2e: test %rax,%rax; je 0x2eca08a` — if the channel
string at `0x72a3718` is NULL, the whole latch block is skipped. The harness
currently pre-consumes `--crashCounter` via `0x2ec9dff` and sets `0x72a3710`
via `0x2e57c20`; in the default run the channel global stayed empty
(`latch=0 tag=0`), so a non-empty genuine channel value is required.

**Fatal path:** after the latch, the writer logs each `argv[i]` with format
`"    %s"` (`0x2eca00a` loop). Our compat `__android_log_print` uses
`vsnprintf(buf,8192,fmt,ap)` and `__android_log_write` uses `fprintf(stderr,...)`.
A hand-built/mutated argv with a stale pointer makes that `%s` read bad memory,
or the incompletely-seeded `__sF` (16 bytes vs needed `0x198`) faults in glibc
stdio. That is the observed "invalid stdio handle" abort — a **compat-shim**
problem isolated by lead task #491, not guest latch logic. Once #491 makes the
writer return normally, this is the cleanest genuine route to set the latch.

**Genuine argv source:** the real JNI entry `nativeRunCrashpadHandler 0x2eca720`
takes a JNI `String[]` (rsi/r12), walks the handler table at `0x7cdeb50`
(0x48 bytes, `0x2eca761` loop), logs, then calls `*(vtable+0x558)` which reaches
`0x2ecb12e -> 0x2ec9eed` with an argv the guest built itself from the strings
(`0x1dc1208` std::string ctor at `0x2ec9f51`). Passing a real `String[]` such as
`{"--channel","LIVE",...}` through `0x2eca720` therefore exercises the genuine
route and avoids a malformed hand-built argv. Worker-2's `flags_latch_default`
(#489) should build that `String[]` with `sober_jni_new_string_utf8` and call
`0x2eca720` (or, if #491 needs the narrower route, call `0x2e57c20` then
`0x2ec9eed` with a consistent argv), gated by `SOBER_FLAGS_LATCH_DEFAULT=0`
and reported via `should_withhold()`/`latch()`.

## 4. Downstream callback chain (nativeGameGlobalInit -> gameLoadedCallback)

Once the latch is set, TaskScheduler init can complete and the app-start path
can run. The genuine publication chain the gate wants (drawn from prior
analysis, all shipped guest code):

1. `nativeAppBridgeV2InitWithParams 0x24abf96` -> `0x24ad6a5` ->
   `register(1, base+0x7234520)` = guest fn `0x253cb56`, which builds the
   id=1 GameLoaded descriptor (vptr `0x6dbbf98`, id=1 at `+0x08`,
   slot `&0x7234520` at `+0x18`) and inserts a node whose `+0x08` dispatch is
   `0x27eaf04` into the MessageBus singleton (`base+0x72cbdd0`) `+0x288` list.
   **Prerequisite:** FFlag ctrl block at `base+0x72a0520+0x38`, flag
   `0x72cb648`, app singleton `0x72cb5e0` (all fixed in the prior team's
   #482/#485). Without these, `0x24abf96` faults before `0x24ad6a5` and the
   id=1 node is never registered (`publish_seeded` returns `-3 NO_NODE`).
2. `nativeAppBridgeV2StartGameWithParam 0x2ebd51a` reads
   `placeId()J` (`0x2ebd855 -> 0x6c60980 -> CallLongMethodV 0x2570ddc`) and the
   guest stores it at `0x2ebd86a` into `0x72a31c0` (must be `>0`).
3. Closure `0x2ec1340` reads `0x72a31c0` at `0x2ec1370`, checks flag
   `0x72cb648`, calls `0x30ef840` -> app singleton `0x72cb5e0` ->
   `getAppBridge [vt+0x10]` -> `[AppBridge vt+0x40]` -> SessionReporter
   `GameLoaded` -> MessageBus publish event id 1.
4. MessageBus walks singleton `+0x288` -> node dispatch `0x27eaf04`:
   `0x27eaf1f: mov rdi,[rdi+0x50]` (embedded id=1 descriptor),
   `0x27eaf2f: call [vptr+0x30]` -> descriptor invoke `0x27eaf4c`:
   `[desc+0x18]=&0x7234520 -> [0x7234520]=0x6dbd860 -> [0x6dbd860]=0x27eaf6e`.
5. Notifier `0x27eaf6e`: `0x27eaf98` loads cached class `0x72a2f48`,
   `0x27eaf9f` loads cached method `0x72a2fc0`, `0x27eafae: call 0x25848e0`
   (thunk -> `CallStaticVoidMethodV`, JNI slot 142 / `+0x470`), returns to
   `0x27eafb3`, logs `0x36c7c9`
   "onGameLoaded() SessionReporterState_GameLoaded placeId:%lld" when placeId>0.
6. Positive credential: `provenance_gate` requires thunk return `0x2584960`
   and notifier return `0x27eafb3` on the live stack, class/method identity
   against `0x72a2f48`/`0x72a2fc0`, frozen BuildID, qualifying name
   `gameLoadedCallback`; sticky `g_harness` forces WITHHELD on any harness-thunk
   call. `is_genuine("gameLoadedCallback")` is fail-closed.

**Clean-green caveat:** any host-initiated `publish_default` callback is
force-withheld because `sober_publish_default_should_withhold()` is in
`seeded_state`. The clean route is the guest-initiated publication in step 3
with `publish_default` OFF (its re-drive of `0x253cb56` stays diagnostic-only).

## 5. Evidence

- `objdump -d` on the frozen guest for `0x2eb7155`, `0x2ec9eed`, `0x2eca720`,
  `0x2ecb12e`, `0x2338e54`, `0x27eaf04`, `0x27eaf4c`, `0x27eaf6e`,
  `0x25848e0`, `0x253cb56`, `0x2ebd51a`, `0x2ec1340`.
- `/tmp/opencode/libroblox.dis` full disassembly: all `movb $1,0x7cdd149`
  writers and `0x7697d08`/`0x7697cf0` xrefs.
- Prior team artifacts: `analysis/A14-flags-loaded-latch.md` (lead),
  `analysis/C11-messagebus-publish.md`, `analysis/C12-placeid-source.md`,
  `analysis/callback/callback-map.md`, `docs/decisions/013`, `038`.

## 6. Risks / honesty

- The crashpad latch route sets the latch genuinely but its post-latch fatal
  abort is a compat-shim stdio issue; until #491 lands, calling it terminates
  the process. Do not claim the latch route is "solved" until the writer
  returns normally.
- The `0x56d*` writer family is a dialog-dismiss path, not app-start reachable
  without creating a system dialog; not recommended for `flags_latch_default`.
- I made no src/ edits in this task and did not touch `analysis/A14` (lead).
  `publish_default.*` and `provenance_gate.*` remain as previously landed
  (re-drive diagnostic-only, gate fail-closed). Marker stays WITHHELD.
