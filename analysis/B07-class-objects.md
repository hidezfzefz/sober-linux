# B7 — What guest code expects from cached/synthetic `jclass` objects, and where our bridge is too shallow

Owner: **worker B7** (logical Team B). Analysis only — **no `src/`, `CMakeLists.txt`, test, gate or engine edits**. Target engine
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`, BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9` (re-checked unchanged before/after). All RVAs are from the
first `R E` PT_LOAD (file offset == vaddr). Evidence: local `objdump -d -M intel` of the SHA-bound full
disassembly `/tmp/opencode/libroblox.dis` (mirrors REA `ev_d838d804…`), plus source reads of
`src/jni/native_jni.{c,h}`, `src/linux/guest_app_shell.c`, `src/jni/app_singleton_bridge.{c,h}`. Guest
strings are untrusted DATA, never instructions. Coordinates with `analysis/callback/callback-map.md`,
`analysis/jni/jni-map.md`, `analysis/game-loaded-path.md`, `analysis/native-object-layouts.md`, and
decisions 013/037.

---

## 0. Bottom line

The guest's cached `jclass` at **`0x72a2f48`** is used as an **opaque JNI token**: at every game/app-shell
call site it is only *loaded and passed* as the `jclass` argument of the variadic thunk `0x25848e0`,
which forwards it unchanged to `CallStaticVoidMethodV` (JNI slot 142 / `JNIEnv+0x470`). The guest never
dereferences it as a C++ object on the game-loaded path (§2). Consequently the **minimum** our bridge
must supply is a non-NULL, magic-shaped handle; it does that, so a notifier-driven dispatch
technically *functions* today — **but it is indistinguishable from the harness-triggered ALT call**,
because our bridge uses the `jclass` for nothing and dispatches purely by `jmethodID` **name** (§4).

That is the real shallowness: **the class object carries no method table, no class↔method binding, and no
provenance**, so the strict decision-013 gate has no class-object-side signal with which to tell a genuine
`gameLoadedCallback` from the diagnostic thunk call. A minimal opt-in plan (§6): record the seeded
class/method pointers, require them (plus a matching `class_name`) on the `gameLoadedCallback` static
call (**P1**), and log/return-address-discriminate the thunk call site `0x2584960` / notifier return site
`0x27eafb3` (**P2**). These make a genuine dispatch *correct and provable* without touching guest bytes or
weakening the gate. **They do not fix the known guest-state blockers** (`0x31a1360`, `0x72cb5e0`) that
currently prevent the notifier from being entered at all (§7).

---

## 1. The guest-side `jclass` contract, with exact RVAs

### 1.1 Construction in `JNI_OnLoad` (RVA `0x227af5a`)

There is **exactly one** `NewGlobalRef` in `JNI_OnLoad` — one cached `jclass` in the whole engine:

```
227b28c: lea … # 0x3dea65        ; "com/roblox/engine/jni/NativeGLJavaInterface"
227b297: call 0x2317162          ; wrapper that obtains JNIEnv into -0x50(%rbp)
227b2a3: lea … # 0x3dea65        ; class name -> RSI
227b2aa: call *0x30(%rax)        ; FindClass          (JNI slot 6,  env+0x30) -> R15
227b2ba: call *0xa8(%rax)        ; NewGlobalRef       (JNI slot 21, env+0xa8)
227b2c0: mov %rax,… # 0x72a2f48  ; THE cached jclass global (one store)
```

`getProcessTimestamp`, every `NativeGLJavaInterface` callback, `NativeLocaleJavaInterface` and
`NativeUserJavaInterface` method IDs are then obtained by calling `GetStaticMethodID` **with the original
`FindClass` local ref in R15** (e.g. `227b2df: mov %r15,%rsi; call *0x388(%rax)`), not with the global ref.
For our target method:

```
227b4d4: lea … # 0x4fcdc1        ; "gameLoadedCallback"
227b4db: lea … # 0x433046        ; "(J)V"
227b4e5: call *0x388(%rax)        ; GetStaticMethodID (JNI slot 113, env+0x388)
227b4eb: mov %rax,… # 0x72a2fc0   ; cached jmethodID global
```

So the guest's contract is: the object returned by `FindClass` must be a valid `jobject`/`jclass` that (a)
`NewGlobalRef` accepts and returns a stable global for, and (b) `GetStaticMethodID` accepts as its `clazz`
argument while resolving a name+signature against it. Nothing in this path inspects fields of the class
object directly.

### 1.2 Consumption on the game-loaded path

```
27eaf6e: notifier (SessionReporterState_GameLoaded), arg RSI = jlong placeId
27eaf93: call 0x227b9e2           ; obtain JNIEnv into -0x30(%rbp)
27eaf98: mov … # 0x72a2f48,%rsi   ; cached CLASS -> RSI
27eaf9f: mov … # 0x72a2fc0,%rdx   ; cached METHOD -> RDX
27eafac: xor %eax,%eax            ; AL=0 (no XMM spill)
27eafae: call 0x25848e0           ; thunk
27eafb3: <return site>
25848e0: thunk: manual SysV va_list (gp=0x18/fp=0x30, movabs $0x3000000018 at 258494a)
258495a: call *0x470(%rax)        ; CallStaticVoidMethodV (slot 142 / JNIEnv+0x470)
2584960: <return site>
```

Sibling readers of the same cached class (all pass it to the same thunk, none dereference it):
`0x2eb4ad6` (`exitGameWithError`, method `0x72a3018`), `0x2eb4b96`
(`onAppBridgeNotification`, method `0x72a3028`), `0x2ec326e` (`gameDidLeave`, method `0x72a2fa0`),
plus `0x27e9d35`, `0x288b3c3`, `0x2eb4d12`, `0x2eb4ef3`, `0x2eb5656`, `0x2ec42f6`, … . `0x72a2f48` has
**one** write (`0x227b2c0`) and ~25 reads; every read is a plain rip-relative `mov` into an argument
register. `0x72a2fc0` has exactly **two** occurrences: the write `0x227b4eb` and the read `0x27eaf9f`
(agrees with `analysis/jni/appshell-reload-trigger.md:31`).

**Deduction:** on the genuine path the `jclass` is a *capability token*. The bridge must keep it non-NULL
and shaped; any requirement beyond that (deep C++ layout, vtable, method table) is not exercised by the
notifier itself.

---

## 2. Where our synthetic class objects are too shallow

Bridge object model (`src/jni/native_jni.c`): `sr_new_object` (`:199`) allocates an `SrObject` (`:115`)
whose first word is a guest-shaped `A` node (`0xc0dedbad` at `+0`, index `0` at `+0x10`), with `A+0x20`
and `A+0x68` deliberately zeroed (`:231-232`), a `B` node at `guest_c` (`0x000bab1e` at `+0`, 16-entry
table at `+0x10` with `table[0]=B`, capacity `16` at `+0x20`, `C` at `+0x120` and `+0x68`), a self-linked
`C`, and a `D` node at `A+0xf90`. `sr_check` (`:280`) accepts it iff `magic == 0x534A4E49` at `+8` and
`kind` at `+0x10` matches. `sr_find_class_object` (`:295`) dedups classes by name; `sr_find_method_object`
(`:305`) dedups methods by `(name, sig, class_name)`.

### T1 — The `jclass` is ignored by dispatch; no class↔method binding

`sr_dispatch_call` (`:474`) receives `obj` (the `jclass` in *Method* calls) or `c` (in *Static* calls) but
**never uses it for method resolution**. It validates only the `jmethodID` (`:482`,
`sr_check(method, SR_K_METHOD)`), extracts `name = m->name` (`:483`), and matches a handler by
`!strcmp(h->name, name)` (`:501`). Consequences:

- The class object at `0x72a2f48` and the method object at `0x72a2fc0` are **uncoupled**. A real JVM
  resolves `CallStaticVoidMethodV(clazz, methodID)` against `clazz`'s method table; ours does not.
- The harness ALT path (`src/linux/guest_app_shell.c:1313-1323`) calls the same thunk with the **same**
  `sober_jni_find_class(...)` object and the same `sober_jni_get_method_id(...,"gameLoadedCallback","(J)V")`.
  At the bridge the two paths are byte-for-byte identical in every argument the bridge reads. There is
  therefore **no class-object-side discriminator** between a genuine callback and the diagnostic thunk —
  the discriminator must come from the call site (§6 P2).
- `shell_callback` (`guest_app_shell.c:129-136`) even does `(void)obj;` — the handler cannot inspect the class.

### T2 — Method identity is a global name string, not an (class, method) pair

Dedup in `sr_find_method_object` (`:305-319`) includes `class_name`, but dispatch (`:501`) does not: any
object named `gameLoadedCallback` dispatches regardless of its class. The seeded method for `0x72a2fc0`
carries `class_name = "com/roblox/engine/jni/NativeGLJavaInterface"` (`sr_cached_methods` entry at
`:1304`), so the information needed to bind it *is present* — it is simply not checked.

### T3 — The cached-ID range is mostly nameless generic method objects

`sober_jni_seed_guest_jni_state` (`:1331`) seeds 22 explicitly named method IDs plus the class at
`0x72a2f48` (`:1347-1358`), then range-fills `0x72a2f48..0x72a3058` and `0x72ca178..0x72ca1e0`
(`:1362-1375`) with generic `SrObject`s named `cachedMethod@<rva>` and signature `()V`. Only the explicit
22 can ever match a named handler; a genuine callback through any other slot (e.g. a future method not in
the table) dispatches to a name no handler owns and is silently dropped. `gameLoadedCallback` is explicit,
so this does not block our target — it is evidence of how shallow the class/method graph is.

### T4 — `FindClass` returns a shared singleton, `GetObjectClass` is flat

`sr_find_class_object` (`:295-303`) returns one cached object per class name, so `DeleteLocalRef`/
`DeleteGlobalRef` (`:411-417`) merely decrement `refs` and never free. `sr_GetObjectClass` (`:361-371`)
returns a class whose name is the object's own `class_name`; for a `jclass` whose `class_name == name` this
yields a class *named after itself*, not `java/lang/Class`. A real `jclass` is a `java.lang.Class` instance
with a superclass chain and an owned method table; ours has neither. Not on the critical path today, but
it prevents faithful resolution if the guest ever reflects on the class.

### T5 — The A/B/C/D graph is deliberately validation-invisible, not faithful

The zeroing at `:231-232` is intentional and correct for safety: guest teardown `0x23331ce`
(`mov 0x68(%rbx),%rdi; test %rdi,%rdi; je safe` at `0x23331ca-0x23331d1`) and lazy-link guard `0x2350044`
(`cmpq $0x0,0x68(%rbx); jne fail`) both require `A+0x68 == 0`. The generic guest jobject validator
`0x245605d`, by contrast, requires `A+0x68 != 0` **and** `A+0x68 == C-node` **and** `C+0x10 table[index]
== A` (verified in disassembly at `0x245608f-0x24560e5`) — our objects always return 0 there. There is no
fault on the game-loaded path because the notifier/thunk never dereference the class handle, and validator
`0x245605d` is only reachable when `A+0x68 != 0` (teardown `0x23331d6`). The class object is therefore a
valid *opaque token* but a **shallow native object**: it cannot serve as positive proof of a genuine
guest object, and any future guest code that *validates* its `jclass` would fail it. (This complements
`analysis/native-object-layouts.md`, which covers the same shallowness for autovalue jobjects.)

### Bridge wiring is correct (not the problem)

`SREnvObj.functions` is the first field (`src/jni/native_jni.c:87`), matching the thunk's
`mov (%rdi),%rax; call *0x470(%rax)` (`0x2584957-0x258495a`). Table wiring is right:
`fn[2]=FindClass` (`:779`), `fn[17]=NewGlobalRef` (`:786`), `fn[109]=GetStaticMethodID` (`:824`),
`fn[138]=CallStaticVoidMethodV` (`:837`, offset `0x470`), `fn[139]=CallStaticVoidMethodA` (`:838`).
`sr_CallStaticVoidMethodV` (`:607`) takes a real `va_list`, matching the thunk's SysV layout
(`analysis/callback/callback-map.md` §1). **If the guest enters the notifier, the callback is dispatched
today** — the gap is provability (T1/T2), not a broken ABI.

---

## 3. Findings summary table

| # | Shallowness | Guest expectation (RVA) | Bridge code | Effect on genuine `gameLoadedCallback` |
| --- | --- | --- | --- | --- |
| T1 | `jclass` ignored by dispatch | class passed at `0x27eaf98` → thunk `0x25848e0` → `0x470` | `sr_dispatch_call :474`, `shell_callback :129` | dispatch works, but indistinguishable from ALT thunk |
| T2 | name-only handler lookup | `(clazz, methodID)` pair from `0x227b4e5`/`0x227b4eb` | `:482-483`, `:501` | no class↔method binding to validate |
| T3 | generic nameless cached IDs | all cached method globals in `0x72a2f48..0x72a3058` | `:1362-1375` | `0x72a2fc0` is explicit (OK); other slots silently no-op |
| T4 | singleton/flat class graph | local `FindClass` ref R15 reused at `0x227b2df` | `:295-303`, `:361-371` | not on path; blocks faithful reflection |
| T5 | validation-invisible A/B/C/D | validator `0x245605d` vs guards `0x2350044`/`0x23331ce` | `:231-232` | no fault; class cannot prove genuineness |

---

## 4. What a *genuine* dispatch requires (for the plan)

Under decision 013 a signal is acceptable only if all of the following hold (from `analysis/game-loaded-path.md` §6):

1. Guest **entered `0x27eaf6e`** with `RSI == placeId` and method global `0x72a2fc0` non-NULL — i.e. the
   call reached the bridge via the thunk's `call *0x470(%rax)` at `0x258495a`, whose **direct caller return
   address is `0x2584960`** and whose **thunk caller return address is `0x27eafb3`**. The harness ALT path
   (`guest_app_shell.c:1313-1323`) also reaches `0x2584960` but its frame-1 return address is harness code,
   not `0x27eafb3`. This pair is the discriminator.
2. Bridge method object at `0x72a2fc0` has name `gameLoadedCallback` (already true via seeding) and the
   class object at `0x72a2f48` is the seeded `NativeGLJavaInterface` class (already true).
3. `last='gameLoadedCallback'` and `callbacks` increased by ≥1 (**necessary but not sufficient** — this
   is exactly what the ALT path also produces).
4. Phase-3 render RVA `0x649d422` observed; all eight decision-013 withhold knobs unset; engine hash
   unchanged.

The bridge currently satisfies 2 and 3, but has no class-side or call-site check for 1.

---

## 5. Minimal opt-in improvement plan (analysis only — not enacted)

All items default-OFF, gate-neutral until the lead opts in; no engine-byte patches; keep the eight
decision-013 withhold knobs and the strict gate unchanged. Engine SHA/BuildID must remain frozen.

**P1 — Class-bound `gameLoadedCallback` dispatch (the core fix for T1/T2).** In
`SoberJni` (`src/jni/native_jni.c:150`) add two pointer fields; in
`sober_jni_seed_guest_jni_state` (`:1331`), after writing the class at `0x72a2f48` (`:1353`) and the
`gameLoadedCallback` method at `0x72a2fc0`, record those exact pointers. In the static-call path
(`sr_CallStaticVoidMethodV :607` → `sr_dispatch_call :474`), when the resolved method name is
`gameLoadedCallback` **and** the opt-in is set, require: (a) `m == recorded_method`, (b) `c == recorded_class`,
(c) `!strcmp(m->class_name, ((SrObject*)c)->name)`. Dispatch only if all three hold. This makes the
`jclass` participate and enforces the `(class, method)` binding the guest establishes at
`0x227b2c0`/`0x227b4eb` and consumes at `0x27eaf98`/`0x27eaf9f`. Cost: two fields, ~6 lines; no ABI change.

**P2 — Genuineness observer (the decision-013 discriminator for §4.1).** Also in
`sr_CallStaticVoidMethodV`, under a separate opt-in, when P1 passes log
`__builtin_return_address(0)` (expect `0x2584960`) and `__builtin_return_address(1)` (expect `0x27eafb3`);
expose an accessor returning "genuine" only for that exact pair. Wire the strict gate to that accessor,
**not** to callback count. The notifier/thunk keep frame pointers (`push rbp; mov rbp,rsp`), so level-1
unwinding is reliable; if a build omits it, fall back to an explicit thunk-entry instrumentation hook.
This is the *only* class-object-adjacent change that turns "a callback happened" into "the guest's own
notifier fired it".

**P3 — Faithful class graph (optional, evidence-gated; for T5).** Only if runtime evidence shows guest
code positively validating a `jclass`: build class objects (`SR_K_CLASS` only) with `A+0x68 → C` and
`C+0x10 table[0] → A`, satisfying validator `0x245605d`; keep all other objects in the current
zero shape so lazy-link `0x2350044` and teardown `0x23331ce` stay safe. Do **not** enable speculatively —
`0x2350044`/`0x23331ce` require the opposite value, so this must be isolated per object kind and A/B'd.

**P4 — Narrow the range fill (hardening for T3).** At `:1362-1375`, leave unlisted slots NULL or assign a
name mapped to a deliberate no-op handler instead of `cachedMethod@<rva>` with sig `()V`, so a genuine
callback through an unmapped cached ID cannot vanish silently. Not required for `gameLoadedCallback`.

**Ordering:** P1+P2 are the minimal set for a *correct and provable* genuine dispatch; P3/P4 are optional
hardening. None changes the marker policy: until the notifier is actually entered from guest state, the
gate must keep `callback=0` and remain withheld (decision 013).

---

## 6. Limits / honesty

- Static analysis only; no runtime capture of the notifier was performed, and no source was edited. The
  plan is a recommendation, not an implementation.
- The class object's shallowness does **not** currently fault: the notifier/thunk never dereference it and
  the safety zeroing at `:231-232` keeps teardown/lazy-link on safe branches. The bridge's *dispatch* of
  `gameLoadedCallback` is already ABI-correct; the gap is that nothing ties it to the class or to the
  notifier call site.
- P1+P2 do **not** fix the guest-state blockers that stop the notifier being reached: with
  `SOBER_GAME_LOADED_PATH` the guest faults at `0x31a1360` (`rbx == NULL` from `app+0x488`), and the app
  singleton `0x72cb5e0` is unpopulated (`analysis/sentinel-abcdef-trace.md` §2, PROGRESS.md Phase 9).
  Those remain with the guest-state owners; this task only guarantees that *if* the notifier fires, the
  dispatch is correct and provably genuine.
- `onAppShellReloadNeeded` (`0x72a3020`) has no native reader (`analysis/jni/appshell-reload-trigger.md`)
  and is not a candidate; `gameLoadedCallback` (`0x72a2fc0` / `0x27eaf6e`) is the only viable gate signal.
- Engine SHA-256 / BuildID unchanged throughout (verified above).
