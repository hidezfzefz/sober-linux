# REVIEW A7 — Genuine guest→Java callback path audit

Auditor: **A7** (full-project review). Scope: **this file only** — analysis, no
`src/`, `CMakeLists.txt`, `tests/`, gate or engine edits. Strict marker
**WITHHELD**: nothing in this report asserts or promotes the `app shell rendered`
marker. All RVA/GOT facts below are from the single frozen AppImage object
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(AppImage 2.740.931 / code 3170); the SHA was re-checked unchanged during this
audit. RVA == file offset in the first `R E` PT_LOAD. Target-derived strings are
untrusted DATA. This is an audit of the cited analysis reports plus **independent
byte-level re-verification of the decisive RVAs** (objdump over the frozen `.so`);
no new runtime, REA or debugger capture was performed here.

---

## 0. Bottom line

- **The static path map is PROVEN, end to end.** The genuine path is
  `nativeAppBridgeV2InitWithParams 0x24abf96 → 0x24ad699 → 0x253cb56 → 0x253e126`
  (registration) → MessageBus subscription node at singleton `+0x288` → node
  dispatch **`0x27eaf04`** → publish call **`0x27eaf2f: call [rax+0x30]`** →
  descriptor invoke **`0x27eaf4c`** → slot resolution `0x7234520 → 0x6dbd860 →
  0x27eaf6e` → **`jmp rcx` at `0x27eaf6b`** → notifier **`0x27eaf6e(placeId)`** →
  thunk `0x25848e0` → `CallStaticVoidMethodV` (JNIEnv `+0x470`, slot 142) →
  `gameLoadedCallback(J)V` (class `0x72a2f48`, method `0x72a2fc0`), returning to
  `base+0x27eafb3` and logging `SessionReporterState_GameLoaded placeId:%lld`
  (`0x36c7c9`) for `placeId != 0`. I re-disassembled every one of these sites this
  audit; all match the reports byte-for-byte.
- **The three alternatives are all correctly disqualified.** The ALT thunk is a
  harness direct call into `0x25848e0` (returns into host code, never enters
  `0x27eaf6e`) and is the *only* observed `gameLoadedCallback` in any log.
  `onAppBridgeNotification` (`0x72a3028`, name string `0x3dea91`) is genuinely
  guest-driven but is JNI plumbing, explicitly rejected by the lead ruling and
  unable to satisfy the notifier-provenance criterion. `onAppShellReloadNeeded`
  (`0x72a3020`) has **no native reader at all** and cannot fire in a native-only
  harness.
- **A genuine notifier-driven callback has NEVER been observed at runtime.**
  No hit/counter/breakpoint at `0x27eaf6e` (or `0x27eaf4c`/`0x27eaf04`/
  `0x27eaf2f`), no captured `RSI = placeId != 0`, no thunk return address
  `base+0x27eafb3`, no `SessionReporterState_GameLoaded` log line, and no
  opted-in provenance recorder line exists in any captured log. The
  game-load path faults at guest `0x31a1360` first. The default gate log is
  `marker withheld: callback=0 render=1 (… last='isVrDevice')`.

**Domain % (this audit's scope, weighted): ≈ 76 % proven.** The mechanism and the
disqualifications together are ≈ 93 % of the non-runtime weight; the single
decisive fact the gate needs — a *runtime* notifier-driven `gameLoadedCallback` —
is at **0 %**.

| Sub-domain | Weight | Proven | Evidence status |
| --- | --- | --- | --- |
| Notifier→Java endpoint (`0x27eaf6e` → `0x25848e0` → `gameLoadedCallback`, class `0x72a2f48` / method `0x72a2fc0`, thunk ABI) | 18 % | ~98 % | re-disassembled this audit; strings verified raw |
| Descriptor invoke `0x27eaf4c` + slot chain `0x7234520 → 0x6dbd860 → 0x27eaf6e` + tail `jmp` `0x27eaf6b` | 17 % | ~95 % | re-disassembled; APS2 addend uniqueness (C01/C05/C11) |
| MessageBus publish `0x27eaf04` / `0x27eaf2f` + registration `0x24ad699 → 0x253cb56 → 0x253e126` into `+0x288` | 17 % | ~90 % | re-disassembled; publish call site is a direct fact; list-walk edge is virtual (no static caller) |
| placeId provenance `StartGameParams.placeId()J` → `0x72a31c0` → RSI of notifier | 15 % | ~88 % | getter `()J` read/store proven (C12); final Lua→signal edge inferred, not single-stepped |
| Disqualification of ALT thunk / `onAppBridgeNotification` / `onAppShellReloadNeeded` | 15 % | ~95 % | static + runtime-confirmed for the first two; statically dead natively for the third |
| **Runtime occurrence of a genuine notifier-driven callback** | **18 %** | **0 %** | no notifier hit, no RA capture, no SessionReporter log; only rejected alternatives observed |

---

## 1. The genuine chain — exactly what the static evidence establishes

### 1.1 Registration (precondition, proven)

`nativeAppBridgeV2InitWithParams 0x24abf96`, at **`0x24ad699`**:
`lea rsi,[rip] # 0x7234520; mov edi,1; call 0x253cb56` — re-verified this audit.
`0x253cb56` builds a family of 0x28-byte event descriptors; the GameLoaded one
(`0x253d06e..0x253d09d`, re-verified):
`vptr=0x6dbbf98`, `+0x08=event_id 1`, `+0x10=delta 0`, `+0x18=&0x7234520`,
`+0x20=self`; registered by `call 0x253e126`, which allocates a 0x60-byte
subscription node in the MessageBus singleton's `+0x288` list and writes the
node dispatch pointer **`0x27eaf04`** at `node+0x08` (site `0x253e235`,
re-verified). Descriptor vtable `0x6dbbf98 + 0x30 = 0x27eaf4c` (invoke).

### 1.2 Slot / notifier materialisation (proven)

APS2 relocation decode (565,390 `R_X86_64_RELATIVE` addends; C01/C05/C11/C12):
```
off 0x6dbd860  -> addend 0x27eaf6e   (notifier; unique reference in image)
off 0x7234520  -> addend 0x6dbd860   (address of slot; unique reference)
off 0x6dbbfc8  -> addend 0x27eaf4c   (descriptor invoke; unique reference)
```
So `*(0x7234520) == base+0x6dbd860` and `[0x6dbd860] == base+0x27eaf6e`.
`0x6dbd860` is vtable slot[0] of `DataModelBindingsAndroid`; base class
`RBX::DataModelBindings` slot[0] is a bare `ret` (`0x1dd72a0`) — the notifier is
the override. RTTI at `0x60dca2` is `std::function<void(long)>` (boost::bind of
`DataModelBindings::method, this, _1`), i.e. `void(placeId)`.

### 1.3 MessageBus publish (proven call site, virtual upstream)

`0x27eaf04` is the subscription-node dispatch (re-verified this audit):
```
27eaf1b: mov [rbp-0x18], rsi          ; save placeId (arg2, jlong)
27eaf1f: mov rdi, [rdi+0x50]          ; node+0x50 = embedded id=1 descriptor
27eaf28: mov rax, [rdi]               ; descriptor vptr = 0x6dbbf98
27eaf2b: lea rsi, [rbp-0x18]          ; &placeId
27eaf2f: call QWORD PTR [rax+0x30]    ; *** publish: descriptor invoke 0x27eaf4c ***
```
Ghidra xrefs to `0x27eaf04` are only the six node-creation `lea` sites — there is
no static caller: the MessageBus walks the `+0x288` list and calls `node+0x08`
virtually. That absence of a direct caller is a property of the guest's virtual
dispatch, **not** a gap in the chain.

### 1.4 Descriptor invoke → slot resolution → tail jump (proven)

`0x27eaf4c` (re-verified byte-for-byte this audit):
```
mov rax,rdi ; mov rcx,[rdi+8] (id=1) ; mov rdi,[rdi+0x18] (&0x7234520)
mov rsi,[rsi] (placeId) ; add rdi,[rax+0x10] (+0) ; test cl,1
mov rax,[rdi] (=0x6dbd860) ; mov rcx,[rax+rcx-1] (=0x27eaf6e)
27eaf6b: jmp rcx            ; tail-call notifier, RSI = placeId
```
Discriminator: a genuine firing enters `0x27eaf6e` **from `0x27eaf6b`** with
`RSI = placeId`; the ALT path never executes `0x27eaf4c`/`0x27eaf6b`/`0x27eaf6e`.

### 1.5 Notifier → thunk → Java (proven)

`0x27eaf6e` (re-verified): `rbx=rsi` (placeId); `call 0x227b9e2` (JNIEnv);
`rsi=[0x72a2f48]` (class `NativeGLJavaInterface`, string `0x3dea65`);
`rdx=[0x72a2fc0]` (`gameLoadedCallback`,`(J)V` — name `0x4fcdc1`, sig `0x433046`,
both strings verified raw); `rcx=rbx`; `xor eax,eax` (AL=0);
`call 0x25848e0` at `0x27eafae`; **returns to `0x27eafb3`**; for `placeId != 0`,
`__android_log_print(3,"rbx.jni",0x36c7c9,placeId)` — string verified raw as
`onGameLoaded() SessionReporterState_GameLoaded placeId:%lld`.
Thunk `0x25848e0` (re-verified): manual SysV va_list (`gp_offset=0x18`,
`fp_offset=0x30`, `movabs 0x3000000018` at `0x258494a`), `call [rax+0x470]` at
`0x258495a` (`CallStaticVoidMethodV`), return site `0x2584960`.
This is the **only** textual xref to `0x36c7c9` in the 1.1 GB `.text`, so the
notifier is the single GameLoaded→Java emitter in this build.

### 1.6 placeId / StartGameParams (proven data flow; final edge inferred)

`nativeAppBridgeV2StartGameWithParam 0x2ebd51a` calls getter-by-name helper
`0x6c60980(env,class,obj,"placeId")` at `0x2ebd865` → `GetMethodID(name="placeId"
@0x2a4e3f, sig="()J" @0x33061d)` + `CallLongMethodV` (both strings verified raw),
and stores the jlong into BSS global **`0x72a31c0`** at `0x2ebd86a`. That global
has exactly two xrefs: the write and the read at `0x2ec1370` inside the
`StartGameWithParam::$_0::operator()` closure (`0x2ec1340`, vtable `0x6dbcd20`),
which copies it (placeId at `+8`) into the struct handed to the AppBridge virtual
`+0x40`, starting the game load. The VM-side DataModel-load-completion →
`SessionReporterState_GameLoaded` → MessageBus event-id-1 publication edge is
**strongly supported** (RTTI `onGameLoaded…DataModel` at `0x6881cc`,
`gameLoadedSignal` at `0x2c3165`, log semantics, id→table encoding `8k+1`) but
was never single-stepped; a runtime hit at `0x27eaf4c`/`0x27eaf6e` remains the
decisive proof. A fidelity trap: the default harness `game_params_get` returns a
**jstring** for `placeId()J`, so `0x72a31c0` would receive pointer-shaped garbage;
only `SOBER_STARTGAME_V2` / `SOBER_GAME_PARAMS` return a real jlong (C12 §5).

---

## 2. Runtime record — exactly what HAS and HAS NOT been observed

### 2.1 OBSERVED at runtime (from the retained logs / verification reports)

| Observation | Concrete evidence | Qualifies for the gate? |
| --- | --- | --- |
| Default path: phase-2/3 green, no callback | `guest init complete`, `first frame rendered`, `marker withheld: callback=0 render=1 (env calls=33 callbacks=0 last='isVrDevice')`; zero `app shell rendered` emissions | N/A — this is the correct WITHHELD state |
| Guest swap RVA `0x649d422` executed once | `[render] guest call 0x649d422 … guest-driven swaps observed: 1` (D12 §2) | Render leg only; synthetic context, no RA capture (A4) |
| Full opt-in stack reaches a genuine, counted `onAppBridgeNotification(String,String)` | cached ID `0x72a3028` (name string `0x3dea91`, sig `(String,String)V`); `callbacks=3`/`4`, `last='onAppBridgeNotification'`; gate `marker withheld: callback=0 render=1 …` and/or `marker withheld: diagnostic guest-state seed active` | **NO** — explicitly rejected; only counted under `SOBER_LOG_APP_BRIDGE_NOTIFICATION` (a withhold knob) |
| `gameLoadedCallback` fired once — **harness-triggered ALT thunk only** | `lead-gate.log:3690-3698`: `ALT PATH: skipping 0x247dc8d`, `ALT PATH: gameLoadedCallback method=…`, `guest->Java callback: gameLoadedCallback`, `ALT PATH guest call 0x25848e0 gameLoadedCallback(env,class,method,0,0,0) -> rc=-6066930334832433271`, `callbacks=1 last=gameLoadedCallback`, then `marker withheld: diagnostic guest-state seed active` and `callback=0 render=1` | **NO** — direct host call to `0x25848e0` with placeId 0, returns into harness code; HONESTY NOTICE / decision 037/038 diagnostic |
| `SOBER_GAME_LOADED_PATH` faults before the notifier | `game.log:3723-3739`: `signal=11 … rip maps to …+0x31a1360`, `si_addr=0x28`, `faulting guest RVA: 0x31a1360`; PROGRESS Phase 9/10 record `app+0x488 == NULL` (`0x320b0ba`) | no callback at all |
| Opt-in A/B matrix forces withhold | 17/17 documented opt-ins emit zero markers (11 print `marker withheld: diagnostic guest-state seed active`, 6 fault pre-gate); 13 out-of-chain guest mutators are a robustness gap (verification 471 §3b) | no marker |
| Engine integrity | SHA-256 `3e498d64…`, BuildID `204ad049…` unchanged before/after all runs | invariant |

### 2.2 NOT OBSERVED at runtime (the decisive gaps)

- **No hit/counter/breakpoint at `0x27eaf6e`** (notifier), nor at `0x27eaf4c`
  (invoke), `0x27eaf04` / `0x27eaf2f` (publish), or `0x27eaf6b` (tail jump). No
  log in `/tmp/opencode/*.log` contains any of these RVAs as an event.
- **No capture of `RSI = placeId != 0` at notifier entry**; no verification that
  `*(0x7234520) == base+0x6dbd860` / `[0x6dbd860] == base+0x27eaf6e` *at
  runtime*, and no check of non-NULL `0x72a2f48` / `0x72a2fc0` at call time.
- **No thunk return-address / stack-frame provenance.** The only observed
  `gameLoadedCallback` returns into host code near the ALT call site; no run has
  shown the required pair `0x2584960` (thunk `call *0x470` return) over
  `base+0x27eafb3` (notifier→thunk return). The opt-in recorders that *could*
  capture it — `src/jni/callback_provenance.c` (`SOBER_CALLBACK_PROVENANCE`) and
  `src/jni/notifier_probe.c` — are default-OFF, withhold-listed, and **never
  produced a line in any captured log** (grep for `callback-provenance`,
  `GENUINE`, `HARNESS-THUNK` across all logs: zero hits).
- **No `SessionReporterState_GameLoaded` / `onGameLoaded() … placeId:%lld` log**
  (string `0x36c7c9`) in any log — i.e. no evidence the guest's own state machine
  reached GameLoaded.
- **No genuine MessageBus event-id-1 publication observed**, and no proof the
  guest reached `SessionReporterState_GameLoaded` → `+0x288` list → `0x27eaf04`.
- **No `onAppShellReloadNeeded` firing** — statically impossible natively (§3.3).
- **No guest-owned app state proven at runtime**: `app+0x408`/`app+0x440` written
  by guest RVAs `0x25d7e75`/`0x24ad50a`, app singleton `0x72cb5e0` populated by
  real guest code (`0x2383c18`), `SingleSurfaceApp::userDidLogin 0x2771f6c`
  completing without the `0x2772403` fault — none observed; the only working
  start-app path uses synthetic seeds.
- **No `app shell rendered` emission** on any default or clean run.

---

## 3. The disqualified alternatives — precise, with the correction on `0x3dea91`

### 3.1 ALT thunk (`SOBER_ALT_CALLBACK_PATH`, direct `call 0x25848e0`)

`guest_app_shell.c` host code calls `base+0x25848e0` directly with class
`0x72a2f48`, method `0x72a2fc0` and placeId `0`, logging `ALT PATH guest call
0x25848e0`; it skips `nativeAppBridgeAppStart 0x247dc8d`. It returns to host code
near the call site (observed `rc=-6066930334832433271`, a meaningless value from
a `void` call), **never** through `0x27eaf4c → 0x27eaf6b → 0x27eaf6e`, never to
`base+0x27eafb3`. It increments the same name-only counter as a genuine callback,
so a name/count alone cannot discriminate. Decision 037 introduced it as an
opt-in diagnostic with a mandatory HONESTY NOTICE; decision 038 ruled it
diagnostic-only, never proof, and made it the first `seeded_state` term. The
observed run (`lead-gate.log`) is the labelled false-green control — the gate
correctly withheld. **Disqualified.**

### 3.2 `onAppBridgeNotification(String,String)` — genuinely guest-driven, still rejected

Cached ID **`0x72a3028`**, name string **`0x3dea91`** (verified raw:
`onAppBridgeNotification`), signature `(Ljava/lang/String;Ljava/lang/String;)V`.
I verified this audit that its guest reader at `0x2eb4b96` passes class
`0x72a2f48` and method `0x72a3028` into the **same generic thunk `0x25848e0`**
(`call` at `0x2eb4bac`, guest return site `0x2eb4bb1`). *Precision correction:*
several reports (B10/C10) write "`0x72a3028` → thunk `0x3dea91`"; `0x3dea91` is
the **method-name string RVA**, not a thunk — the thunk is `0x25848e0`.
It *was* observed at runtime under the full opt-in stack (`callbacks=3`/`4`,
`last='onAppBridgeNotification'`), so it genuinely fires from guest code — but:
(1) the lead's phase-8/9 ruling (decision 013 note, reaffirmed in 038) explicitly
rejects it as JNI plumbing, not an app-shell frame; (2) it is logged only under
`SOBER_LOG_APP_BRIDGE_NOTIFICATION`, itself a withhold knob; (3) its thunk return
site is `base+0x2eb4bb1`, **not** `base+0x27eafb3`, so it cannot satisfy the
notifier-provenance criterion in any case. **Disqualified.**

### 3.3 `onAppShellReloadNeeded()V` — statically dead natively

Cached ID **`0x72a3020`** (name `0x4a702d`, sig `()V` `0x288017`), written once
at `0x227b432` in `JNI_OnLoad`. Exhaustive search (appshell-reload-trigger.md):
one textual occurrence in the whole 20.6 M-line disassembly (the write), no
native reader, no second `GetStaticMethodID`, no APS2 addend `0x4a702d`, no Java
`invoke-static` caller. The Java path (listener `WeakReference` in
`NativeGLJavaInterface`, `ActivityNativeMain.n()` → `runOnUiThread` → real
`Surface`) is real but needs ART, a live Activity, a UI Looper and a Surface —
none exists in this native-only harness. Any harness log of that name would be
fabricated. **Disqualified** (and its absence is a correct `callback=0` state).

---

## 4. Proof criteria — what would actually lift the marker

These are the ANDed conditions from C09 §5–§7 / C03 §5 / decision 038 §2. All
must hold in **one run on the frozen engine with every opt-in unset**:

1. **Notifier hit (primary):** breakpoint/hit-counter at `0x27eaf6e` with
   `RSI = placeId != 0`, cached class `base+0x72a2f48` and method
   `base+0x72a2fc0` non-NULL; hit after `0x24ad699` registration, with
   `*(0x7234520) == base+0x6dbd860` and `[0x6dbd860] == base+0x27eaf6e`.
2. **Guest dispatch path:** hits at `0x27eaf04` / `0x27eaf2f` / `0x27eaf4c` /
   `0x27eaf6b` in that order, with `RSI = placeId` at the notifier — proving the
   guest's own MessageBus published event id 1 rather than a host-built call.
3. **Return-address discriminator:** thunk `call *0x470` return `base+0x2584960`
   with the frame above it `base+0x27eafb3` (not a host address); bridge logs
   `guest->Java callback: gameLoadedCallback`, `last='gameLoadedCallback'`,
   `callbacks` increased ≥ 1 over the pre-event baseline; dispatch name/sig/arg
   match `(J)V` with the captured placeId, after introducers `0x2ebd51a` and
   `0x2ebca9e`.
4. **Corroborating guest log:** `rbx.jni / onGameLoaded() SessionReporterState_GameLoaded
   placeId:<placeId>` (`0x36c7c9`) emitted around the same time (requires
   `placeId > 0`).
5. **Render leg:** guest `0x649d422` executed, `g_guest_swaps >= 1`,
   `sober_render_phase3_done() == 1`, phase-2/3 ancestors green — paired with
   (1)–(4), never sufficient alone.
6. **Clean state:** `seeded_state == 0` (every guest-mutating `SOBER_*` knob
   unset, including `SOBER_ALT_CALLBACK_PATH`, `SOBER_CALLBACK_PROVENANCE`,
   `SOBER_LOG_APP_BRIDGE_NOTIFICATION` and the notifier-probe knob); no
   `ALT PATH`, `HONESTY NOTICE`, seed-withhold or `DIAGNOSTIC ONLY` lines; engine
   SHA/BuildID unchanged.
7. **Gate:** exactly one `emit_marker("app shell rendered\n", …)` from
   `shell_cb && render_ok`. Criterion 1–4 evidence must be captured by the gate
   itself, not by an opt-in diagnostic; a name-only counter is necessary but
   **not sufficient**.

None of criteria 1–4 has ever been observed. Until such native provenance
instrumentation exists and fires on a clean run, the marker stays WITHHELD.

---

## 5. Runtime evidence consulted

- `/tmp/opencode/lead-gate.log:3690-3698` — sole observed `gameLoadedCallback`,
  ALT thunk, HONESTY/withhold lines.
- `/tmp/opencode/game.log:3723-3739` — `SOBER_GAME_LOADED_PATH` SIGSEGV at
  `0x31a1360`, `si_addr=0x28`.
- `/tmp/opencode/*.log` aggregate — `last=` distribution: `isVrDevice`
  (default, `callbacks=0`), `onAppBridgeNotification` (opt-in, `callbacks=3/4`),
  `gameLoadedCallback` (ALT only); zero notifier RVAs, zero provenance-recorder
  lines, zero `SessionReporterState_GameLoaded` lines.
- `tests/verification-report-471.md §2-3` — strict gate, 17 in-chain opt-ins,
  13 out-of-chain gaps; `tests/D12-final-invariants.md §2`;
  `analysis/REVIEW_A4_render.md` (sibling render audit).
- `PROGRESS.md` Phase 9/10 — no genuine gate callback observed; fault baseline;
  decision 038 accepted; name-only counting insufficient.
- Static basis: `analysis/C01-gameload-trigger.md`, `C03-appshell-frame-evidence.md`,
  `C05-notifier-object.md`, `C06-session-reporter.md`, `C09-marker-evidence.md`,
  `C11-messagebus-publish.md`, `C12-placeid-source.md`,
  `analysis/game-loaded-path.md`, `analysis/callback/callback-map.md`,
  `analysis/jni/appshell-reload-trigger.md`, `analysis/decision-memo-alt-thunk-vs-013.md`,
  `docs/decisions/013/037/038`.

---

## 6. Honesty limits

- Static source/log review plus independent byte-level re-verification of the
  decisive RVAs; **no new runtime, REA or debugger capture** was performed for
  this audit. "Proven" above means the static mechanism is established by the
  frozen binary and corroborating relocation/xref analysis, not that the app-shell
  frame or a genuine callback was reached.
- The `SessionReporterState_GameLoaded → MessageBus event-id-1 → 0x27eaf04` edge
  is strongly supported but is a virtual/data-flow inference, not an isolated
  direct-call proof; the notifier has no static caller by design.
- The domain-% figures are this auditor's weighted judgement over the sub-domains
  listed in §0, not a measured quantity; the 0 % row is a factual statement about
  the absence of runtime evidence in the consulted logs.
- The precision correction in §3.2 (`0x3dea91` = name string, thunk = `0x25848e0`)
  does not change any disqualification conclusion.
- Marker remains **WITHHELD**; this report does not weaken decision 013/038,
  propose any gate/test/source/engine edit, or claim a genuine guest→Java
  app-shell callback has occurred.
