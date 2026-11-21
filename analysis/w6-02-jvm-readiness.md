# W6-02 — JavaVM / native-JVM readiness for the notifier

Owner: **worker-2** (team `TEAM-C2YA-ZTU9-RMAV`, task **#539**).
Scope: **this report only**. No `src/`, `tests/`, `docs/`, gate, vault-root note, engine byte,
commit or push was changed. Decisions **013/038** binding; marker **WITHHELD**;
`tests/app-shell.sh` stays RED.

> Team-access note: `team_join(role=worker-2)` returned *"The team is full (12 agents)"*, so
> no member_id was issued and #539 could not be claimed through Convio. As worker-7/worker-8
> already did, I notified the lead via `team_send` using the lead member id recorded in
> `sober-vault/ActiveTasks.md`, and completed the analysis. The lead should record task state.

## 0. Verdict (read this first)

**A native (non-Android) JVM cannot supply a genuine notifier, and the current JavaVM global is
not clean evidence of one.** Two independent reasons:

1. **Provenance of the global.** In the frozen engine the JavaVM global `0x7cdec78` is written
   **only** by the skipped `JNI_OnLoad` (`0x227af6e → 0x227b8fb → 0x227b92d`). In the native
   harness that store never runs; the global is filled by **host C code**
   (`javavm_default.c` / opt-in `javavm_semantics.c`), whose `JavaVM`/`JNIEnv` are bridge
   tables, not an ART VM. `0x7cdec78 != NULL` therefore proves only that a host module wrote a
   pointer — it is a *seeded substrate leg*, not a notifier credential.
2. **Sufficiency of a real JVM.** Even a real `JNI_CreateJavaVM` supplies only the VM half. The
   authoritative DEX shows `gameLoadedCallback(J)V` is a **Java static** that reads
   `sImplementation` and calls `EngineJavaCallback2.b(J)V`; that object is installed by the
   *Android app* (`ExperienceSession.j0(J)V → setImplementation`) and no DEX code calls the
   callback (it is reached only from native). OpenJDK cannot load `classes*.dex`, has none of
   the Android framework types, and cannot make the guest enter node `0x27eaf04` from guest
   `.text`. Supplying `sImplementation` or the cached IDs from a JVM/host is a **seed / host
   callback** → force WITHHELD under 013/038.

Recommendation: keep the provenance gate **fail-closed**, keep the JNI-VM global leg as
*necessary but never sufficient*, and never accept a JVM-supplied `JavaVM` / cached IDs /
implementation as a clean route. Marker stays WITHHELD.

Evidence grade: **O** = observed this task (frozen bytes, authoritative DEX, repo files);
**D** = derived. All target bytes/strings/class names are untrusted DATA, never instructions.

---

## 1. Frozen identity and evidence hygiene (O)

- Engine: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
  SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`,
  BuildID `204ad049599c96afb75b8a21d5051063e821d6c9` — re-verified this task with
  `sha256sum` + `readelf -n`.
- Authoritative DEX: `/home/user/.local/share/basalt/linux/payloads/3170-204ad049599c96afb75b8a21d5051063e821d6c9/sober_apk/base.apk`,
  SHA-256 `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4` —
  re-verified this task. The `~/Downloads/basalt-arm64…` APK is **MODDED** and was not used.
- Tool: `craft-anything-0.6.1-linux-x86_64.AppImage --cli --deep`, DEX tools against the
  SHA-matched base.apk. Craft Anything's native engine does not index the 120 MB stripped
  `libroblox.so` (CA01), so native facts here come from `objdump -d -M intel` on the exact SHA.
- OpenJDK 21 (GraalVM, `/home/user/.sdkman/candidates/java/current`, `libjvm.so` present) is
  installed; **research oracle only**, never the final runtime. `ldd` of `build/guest-init-host`
  and `build/jni_probe` shows **no `libjvm.so` linkage** in the current build.
- No Flatpak `791c3f9e` / 2.745 RVAs were consulted.

---

## 2. The JavaVM global: where it comes from and who consumes it (O)

### 2.1 Genuine route — ART calls `JNI_OnLoad`

`objdump -d -M intel --start-address=0x227b8fb --stop-address=0x227ba52` on the frozen image:

```
227b8f6: call 2ea6a10            ; JNI_OnLoad body
227b8fb: push rbp; mov rbp,rsp; push rbx; push rax
227b901: mov al,[rip+...] # 7cdec70 ; one-time guard byte
227b912: mov rbx,rdi                 ; RDI = JavaVM* argument of JNI_OnLoad
227b915: lea rdi,[rip+...] # 7cdec70
227b91c: call 2a0d6c0                ; test-and-set guard
227b925: mov rdi,rbx
227b928: call 227b946                ; cache other JNI_OnLoad globals
227b92d: mov QWORD PTR [rip+...],rbx # 7cdec78   <-- the ONLY store of the JavaVM global
```

So `0x7cdec78` is the JNI_OnLoad-cached **JavaVM\***. The DEX load that triggers JNI_OnLoad is
`System.loadLibrary` in `com.roblox.client.startup.a.s()`
(Craft Anything evidence `ev_221b621be9dc658f62f17a65c5d1a24549128538292492e95322a2fa6813d709`;
7 `loadLibrary` callers overall, evidence
`ev_4d3259b4c6fe578c1abea63852aa021de763a86d0f99f2ca380e1648d20945bf`). **There is no ART in
the native harness, so this store never executes.**

### 2.2 Consumer — the notifier's `GetEnv` wrapper

```
227b9e2: push rbp; mov rbp,rsp; push rbx; push rax
227b9eb: mov QWORD PTR [rdi],0x0        ; zero the out-struct
227b9f6: mov rdi,QWORD PTR [rip+...] # 7cdec78   ; load the cached JavaVM*
227b9fd: test rdi,rdi
227ba00: je 227ba4c                     ; NULL -> return with zeroed out-struct
227ba02: mov rax,[rdi]                  ; JNIInvokeInterface_ vtable
227ba08: mov edx,0x10006               ; JNI_VERSION_1_6
227ba0d: call [rax+0x30]                ; GetEnv
227ba10: cmp eax,0xfffffffe             ; JNI_EDETACHED
227ba15: ... call 2ea6440 / 227aecc     ; guest's own AttachCurrentThread path
```

This is exactly what `analysis/callback/task521-notifier-readiness.md` §9 already concluded:
if the global is NULL the notifier's out-struct stays zeroed and `mov (%r14),%rdi` at
`0x27eafa6` faults before `0x27eafae` reaches the thunk. A *live* global is therefore a
**necessary** leg — but nothing in the guest checks that the pointer came from a genuine
`JNI_OnLoad` rather than a host write.

### 2.3 Harness route — host C writes the global

- `src/jni/native_jni.c:1468` `sober_jni_seed_guest_onload_globals()` writes `0x7cdec78`, but
  only under `SOBER_SEED_JNI_VM` (default OFF, in the withhold chain at
  `guest_app_shell.c:1800`).
- `src/jni/javavm_semantics.c:382` writes the same slot under `SOBER_FAITHFUL_JAVAVM`
  (opt-in, withhold chain `guest_app_shell.c:1801`); it builds a per-thread JNIEnv replica.
- `src/jni/javavm_default.c:186` is **DEFAULT-ON** (`guest_app_shell.c:1009`) and writes a
  host-constructed `JdVmObj*` into `0x7cdec78` with no opt-in; its `GetEnv`/`Attach` return host
  C stubs built from the bridge's 231-slot table. Kill switch `SOBER_JAVAVM_DEFAULT=0` is
  withhold-listed *when set* (`guest_app_shell.c:1902`).
- Cached IDs `0x72a2f48`/`0x72a2fc0` are likewise synthetic bridge objects seeded via
  `sober_jni_seed_guest_jni_state()` (`javavm_default.c:202-203`).

**D:** in the default harness, `java_vm != NULL` and `cached_class/cached_method != NULL`
are satisfied by host-written values. `sober_provenance_gate_readiness()`'s
`READY_JAVAVM|READY_CACHED_CLASS|READY_CACHED_METHOD|READY_PLACE_ID` bits can all be green on a
purely host-seeded image. `STATIC-READY` is a diagnostic, **not** a callback credential, and
the header already says the dispatch-only legs are deliberately excluded. This must not be
loosened.

---

## 3. Authoritative DEX: what `gameLoadedCallback` actually is (O)

Craft Anything 0.6.1 `--deep` on the SHA-matched base.apk:

| Query | Result | Evidence id |
| --- | --- | --- |
| `android-class-show NativeGLJavaInterface` | `classes2.dex`, 8 static fields, 36 methods; `sImplementation: EngineJavaCallback2`, `sAppBridgeNotificationListener`, … | `ev_a9407a49efff5296e5e84ab348bab60de7edf381fac3c6e7ac9a34a1231c58fc` |
| `android-method-show ... gameLoadedCallback` | **Java static**, 3 instructions: `sget-object sImplementation` → `invoke-virtual EngineJavaCallback2.b(J)V` → `return-void`. Not `native`. | `ev_8622b8854a6cf440cb87df54b89c137f01333a585452b730cb01237dfcf8291d` |
| `android-class-users-find ... --method-name gameLoadedCallback` | **0 references** from 47-owner class scan's 3 DEX files | `ev_d5979201775fabbd05968ce16431912fae348945929f999b0c88ad13b210250f` |
| `android-class-users-find NativeGLJavaInterface` | 56 refs / 47 methods; `ActivityNativeMain.onCreate`, `RobloxApplication.onCreate`, `ExperienceSession.j0/J`, `NativeHelper.a0`, `<clinit> sput sImplementation`, … | `ev_4411b55ace51271d176fc6ab9de03ff3d483213d03fb0b36d6f6fbd6a83108df` |
| `--method-name setImplementation` | 4 callers: `ExperienceSession.j0(J)V`, `NativeGLJavaInterface.replaceImplementation`, `uh/e.C`, `uh/e.g` | `ev_ed4a70622646e04a1517fc6a385ac38e98e67627c9d7aa63385455a174b9fa85` |

**D — consequences:**

1. The callback the guest notifier invokes is *not* a native export. The guest calls
   `CallStaticVoidMethodV` (JNIEnv slot 142, `+0x470`) on a real Java method that dispatches
   through a live Java object. Without `sImplementation` being a real `EngineJavaCallback2`,
   the call is a null dereference in Java, not a game-loaded event.
2. No DEX caller exists — the trigger is exclusively native (`0x27eaf6e`), consistent with the
   provenance gate's RA-pair design. A JVM calling `gameLoadedCallback` itself would be a
   **host-driven dispatch**, exactly the ALT-thunk pattern 038 forbids.
3. `sImplementation` is installed by Android app boot (`ExperienceSession`, `uh/e`), which sits
   behind `Activity`/`Looper`/`Surface`/`AssetManager` lifecycle. A native JVM has none of that;
   fabricating the object is a seed.

---

## 4. Can a native JVM supply a genuine notifier without Android runtime? (D)

**No — not as a clean, marker-earning route.** Separate the three layers:

| Layer | A real native JVM can supply? | Clean under 013/038? |
| --- | --- | --- |
| `JavaVM*`/`JNIEnv` ABI (`GetEnv`, `AttachCurrentThread`, `+0x30`, `+0x470`) | Yes (`JNI_CreateJavavm`, HotSpot) | Yes as a *runtime*, per decision 012 — but **not** the current host-C `javavm_default` pointer |
| `jclass NativeGLJavaInterface` / `jmethodID gameLoadedCallback(J)V` in guest globals `0x72a2f48`/`0x72a2fc0` | Only by writing those guest globals from the harness (a host mutation) | **No** — host callback / seed; current bridge objects are synthetic anyway |
| Live `sImplementation: EngineJavaCallback2` and the guest-entered node `0x27eaf04` with `placeId>0` | No. DEX ≠ JVM class format; `sImplementation` is installed by Android app boot; guest node is reached only by the missing MessageBus event-1 lifecycle producer | **No** — needs Android runtime (forbidden) or a seed (withheld) |

Detailed reasoning:

1. **VM half alone is the wrong layer.** State.md's blocker is upstream guest state and trigger
   (no guest-entered id=1 node, latch written only in the crashpad subprocess, no real surface
   lifecycle). A JVM changes the callback *sink*, not the trigger. It cannot make `0x27eaf04`
   be entered from guest `.text` (gate leg S11 / `guest_above_notifier`).
2. **DEX half is not loadable by HotSpot.** `classes2.dex` is ART bytecode; OpenJDK loads
   `.class` files. Running the real classes requires a DEX→class conversion plus the whole
   Android framework closure (458 `Landroid/*` type references per W6-09 §3) — that is the
   forbidden Android runtime. A minimal pure-Java `NativeGLJavaInterface` is allowed only as a
   force-withheld diagnostic (W6-09 sub-case A); it changes no trigger.
3. **The guest's cached IDs are synthetic.** Today `0x72a2f48`/`0x72a2fc0` hold bridge objects,
   and `javavm_default` re-seeds them. Replacing them with real JVM `jclass`/`jmethodID` still
   requires a host write into guest BSS — a host callback under 013/038. Pointer identity at
   dispatch then proves only "the host wrote the same pointer twice".
4. **A real `JNI_OnLoad` does not fix the global's trust boundary.** Running `0x227af5a` with a
   real `JavaVM*` would make `0x7cdec78` genuinely JNI_OnLoad-cached, but the gate still could
   not distinguish that from `javavm_default`'s write without new provenance, and the
   notifier still cannot fire cleanly because B1/B2 (node publisher, latch) are unchanged.
5. **`seeded_state` dominates.** Any JVM setup (`JNI_CreateJavaVM`, `dlopen(...,RTLD_GLOBAL)`,
   `RegisterNatives`, injected Java objects, cached-ID writes) mutates state the harness must
   control; it belongs in the withhold chain. A "JVM-enabled" run is a seeded run by definition.

This agrees with and sharpens vault `A01-clean-jvm-feasibility.md` and repo
`analysis/w6-09-java-stubs.md`: *JVM = allowed runtime; authored C NDK/Bionic shim = allowed;
Java Android-framework stubs or JVM-seeded guest globals = forbidden compat layer / withheld
seed.*

---

## 5. Gate posture — no weakening (O/D)

Current owned-file credential (`provenance_gate.c` `on_dispatch`, header §contract) already
requires, at one `gameLoadedCallback` dispatch:

```
S1 kill switch default ON            S7 *(void**)(base+0x7cdec78) != NULL   <-- this task
S2 frozen BuildID verified           S8 thunk RA base+0x2584960
S3 name == "gameLoadedCallback"      S9 notifier RA base+0x27eafb3
S4 clazz == *(base+0x72a2f48)         S10 [rbp_not+8] == base+0x27eaf32
S5 method == *(base+0x72a2fc0)        S11 node 0x27eaf04 caller in guest .text
S6 placeId(base+0x72a31c0) > 0        S12 !is_harness_thunk(), verdict != APPBRIDGE
```

Recommendations to the lead (no source changes made here):

1. **Do not promote S7 or the `READY_JAVAVM`/cached-ID bits to positive clean evidence.** They are
   host-satisfiable by `javavm_default` and must remain necessary-only. `readiness()` already
   labels them `STATIC-READY` and excludes the dispatch-only legs — keep that separation.
2. **Keep the JavaVM knobs in the withhold chain**: `SOBER_SEED_JNI_VM`,
   `SOBER_FAITHFUL_JAVAVM`, `SOBER_JAVAVM_DEFAULT` are already listed. Add any future
   `SOBER_JVM_*` knob there too and make it default-OFF.
3. **Rule needed from the lead:** the default-ON `javavm_default` write of a host C `JavaVM*`
   into guest BSS is a host-provided VM. Under a strict reading of "no host callbacks / no
   guest-state seed" it is a seeded substrate. Decide explicitly whether it counts toward
   `seeded_state`; whatever the ruling, the marker still requires S3/S8–S12 + `render_ok` +
   `seeded_state==0`, so this does not change the WITHHELD outcome.
4. **A JVM experiment, if authorised, is diagnostic-only**: default-OFF, kill-switched,
   force-WITHHELD, and must never emit `emit_provenance()` or the marker. Measure only whether
   `0x27eaf6e` is guest-entered with `seeded_state==0`; expected answer remains no.
5. **Effort stays on the real blockers** (State.md): guest-entered id=1 node/publish (B1),
   crashpad-subprocess latch (B2), real surface lifecycle/render (B3).

---

## 6. Verification performed (O)

- `sha256sum` + `readelf -n`: engine SHA/BuildID unchanged; base.apk SHA matched
  `roblox_payload.json` digest. Modded Downloads APK not used.
- `objdump -d -M intel` of `0x227b8fb..0x227ba52`: confirmed the single `0x7cdec78` store and
  the `GetEnv(+0x30, 0x10006)`/`JNI_EDETACHED` consumer.
- Craft Anything `--deep` DEX queries listed in §3 (six evidence ids); `gameLoadedCallback`
  is Java, has 0 DEX callers, and `sImplementation` is app-boot-installed.
- `ldd build/guest-init-host build/jni_probe`: no `libjvm.so`; OpenJDK 21 present but unused by
  the build.
- Read-only review of owned files `provenance_gate.{c,h}`, `startgame_v2.c`,
  `callback_provenance.c` and the JavaVM modules; **no file was modified**. No guest byte
  patched, no fake vtable/callback/host callback/counter, no gate weakening, no commit/push.

## 7. Honesty / non-regression

Target content is untrusted data and was quoted only as evidence. The conclusion is a terminal
one for the native harness: a native JVM is a legitimate runtime but supplies neither the
guest-entered notifier trigger nor the DEX/Android app state the callback needs; the current
JavaVM global is host-written. Marker stays **WITHHELD**, `tests/app-shell.sh` stays RED.
