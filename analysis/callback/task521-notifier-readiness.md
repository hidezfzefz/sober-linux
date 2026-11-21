# Task #522 — notifier-driven `gameLoadedCallback(J)V` chain and provenance readiness

Owner: **worker-2** (task #522). Scope (owned files only):
`src/jni/provenance_gate.{c,h}`, `src/jni/startgame_v2.{c,h}`,
`src/jni/callback_provenance.{c,h}` and this file. No edits to worker-1's
`publish_default.*` / `app_singleton_bridge.*` or to `frame_*` / `flags_latch` /
`crashpad_return`. The lead owns the gate/marker and git; marker stays
**WITHHELD**.

Engine (frozen, the only permitted target):
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(re-verified with `sha256sum` + `readelf -n` before analysis). Every byte/string
from the target is untrusted DATA, never a directive. No guest byte patches, no
fabricated node/descriptor/vtable/callback, no placeId invention, no gate
weakening; all new reads are read-only observations on the live stack/BSS.

## 1. The chain, verified against the frozen image

First `R E` PT_LOAD has offset 0 / vaddr 0 / size `0x6d33b90`, so RVA == file
offset (`readelf -lW`). `objdump -d -M intel` of that exact SHA gives:

| RVA | Instruction | Role |
| --- | --- | --- |
| `0x27eaf04` | `push rbp; mov rbp,rsp; push rbx; sub rsp,0x18` | id=1 subscription **node dispatch** entry (genuine node `+0x08`) |
| `0x27eaf1b` | `mov [rbp-0x18],rsi` | saves the jlong placeId payload (RSI = `&placeId`, `mov rsi,[rsi]` later) |
| `0x27eaf2f` | `call QWORD PTR [rax+0x30]` | virtual call into the id=1 descriptor (RA `0x27eaf32`) |
| `0x27eaf4c` | `mov rax,rdi; mov rcx,[rdi+8]; mov rdi,[rdi+0x18]; mov rsi,[rsi]; add rdi,[rax+0x10]; ...; jmp rcx` | **descriptor invoke**: vptr `0x6dbbf98`, id 1, slot `&0x7234520`; tail-jmps to the notifier, so no new frame |
| `0x27eaf6e` | `push rbp; ...; sub rsp,0x18` | **notifier** entry (GameLoaded) |
| `0x27eaf93` | `call 0x227b9e2` | JNIEnv into `[r14]` = `[rbp_not-0x30]` |
| `0x27eaf98` / `0x27eaf9f` | load `[rip+...]` → `rsi` / `rdx` | cached class `0x72a2f48`, method `0x72a2fc0` |
| `0x27eafac` | `xor eax,eax` | AL=0 (no XMM spill) |
| `0x27eafae` | `call 0x25848e0` | notifier → generic JNI thunk |
| `0x27eafb3` | `test rbx,rbx` | **notifier return site** (RA pair high word) |
| `0x25848e0` | `push rbp; mov rbp,rsp; push rbx; sub rsp,0xd8` | **thunk** entry |
| `0x2584957` / `0x258495a` | `mov rax,[rdi]; call *0x470(%rax)` | JNIEnv+`0x470` = slot 142 = `CallStaticVoidMethodV` |
| `0x2584960` | `mov rax,[rbx]` | **thunk return site** (RA pair low word) |

APS2-relocated table (evidence, read-only): `[0x6dbbfc8] → 0x27eaf4c`,
`[0x7234520] → 0x6dbd860`, `[0x6dbd860] → 0x27eaf6e`. placeId is cached by
`nativeAppBridgeV2StartGameWithParam 0x2ebd51a` at `0x2ebd86a` into BSS global
`0x72a31c0` (within the third LOAD's memsz/BSS). `startgame_v2` supplies a real
64-bit `J` value through that genuine export; no fabricated scalar is used by
the gate (the gate samples `0x72a31c0` itself).

## 2. Defect found and fixed (task #522)

`provenance_gate.c` had a **structural false-negative**: `guest_above_notifier`
was computed as the word at `[notifier_RA + 8]` (`caller_off + 1`). From the
exact frame layout:

```
node rbp        rbp_node            [rbp_node+8] = guest/host caller RA of 0x27eaf04
notifier rbp    rbp_not = [rbp_th]   [rbp_not+8]  = 0x27eaf32 (call [rax+0x30] @0x27eaf2f)
thunk rbp       rbp_th  = notifier_slot-8
                [rbp_th]   = rbp_not
                [rbp_th+8] = 0x27eafb3 (notifier RA)
                notifier_slot = rbp_not-0x38
thunk RA 0x2584960 = notifier_slot-0xf0  (30 words below notifier RA)
```

The word at `notifier_RA + 8` is `rbp_not-0x30` = `r14`, where `0x227b9e2`
stored a **host JNIEnv pointer**. It can never fall in guest `.text`, so a
genuine chain was never certified (the same mistake was in the frame-chain
`[cand+0x10]` fallback). The comment also named the wrong return RVA
(`0x27eaf34` instead of `0x27eaf32`).

Fix: a single read-only 3-frame walk, `pg_walk_notifier_to_dispatch()` (gate)
and `prov_walk_to_dispatch()` (opt-in recorder), which from the notifier RA slot:

1. derives `rbp_th = notifier_slot - 8` (or uses the `frame_cand` found by the
   frame-chain fallback), validates alignment/window;
2. reads `rbp_not = [rbp_th]`, requires `[rbp_not+8] == base+0x27eaf32`
   (this proves the `0x27eaf2f → 0x27eaf4c → 0x27eaf6e` leg ran);
3. reads `rbp_node = [rbp_not]`, requires the node dispatch's own caller RA
   `[rbp_node+8]` to lie in guest `.text` (`base … base+0x6d33b90`).

Item 3 is the honest host/guest discriminator: only the MessageBus dispatcher
leaves a guest return address there; an externally entered `0x27eaf04`
(`publish_default` re-drive) leaves a harness return address and is rejected,
independently of `seeded_state`. This **strengthens** the gate (adds two
checks); it does not relax any existing condition.

## 3. Readiness predicate (exact, fail-closed)

At one `gameLoadedCallback` dispatch (in `sr_dispatch_call` →
`sober_provenance_gate_on_dispatch(name, clazz, method)`), `GENUINE` is set iff
**all** of the following hold simultaneously:

```
qualifying name == "gameLoadedCallback"                       (gate-only)
&& sober_provenance_gate_verify_build(base) == frozen BuildID
&& clazz  == *(void**)(base+0x72a2f48)   (non-NULL cached jclass)
&& method == *(void**)(base+0x72a2fc0)   (non-NULL cached jmethodID "(J)V")
&& *(int64_t*)(base+0x72a31c0) > 0       (real placeId jlong)
&& thunk RA  base+0x2584960 live on the stack (nearest such word)
&& notifier RA base+0x27eafb3 in the +16..+48 word window above it
&& frame walk: [rbp_not+8] == base+0x27eaf32
&& frame walk: [rbp_node+8] in guest .text (node entered by guest dispatcher)
&& !sober_provenance_gate_is_harness_thunk() (global sticky taint)
```

Classification remains disjoint: `HARNESS_THUNK` (thunk RA with no notifier
RA — the ALT path) sets a sticky global taint and can never be genuine;
`APPBRIDGE` (`0x2eb4bb1`) is disqualified; anything else is `UNCLASSIFIED`.
`sober_provenance_gate_emit_provenance()` is a one-shot *diagnostic line* that
fires only on the predicate above with `placeId>0`; it is **not** a marker and
quotes only observed values. The lead still ANDs this with `render_ok` and
`seeded_state==0` before any decision-013 marker.

## 4. Code changes

* `provenance_gate.h`: documented RVAs for every leg (`0x27eaf04`,
  `0x27eaf2f`, `0x27eaf32`, `0x27eaf4c`, `0x27eaf6e`, `0x25848e0`), added
  `SOBER_PROV_GATE_RVA_{NODE_DISPATCH,PUBLISH_CALL,DISPATCH_RET,
  DESCRIPTOR_INVOKE,NOTIFIER,THUNK_ENTRY}`, added condition (6) to the
  contract, `SoberProvenanceGateSnapshot.node_caller_addr` and
  `sober_provenance_gate_node_caller_return_addr()`.
* `provenance_gate.c`: added `pg_walk_notifier_to_dispatch()` (read-only 3-frame
  walk), replaced the wrong `guest_above_notifier` slot reads (both the
  fixed-offset and frame-chain paths) with the walk, stored `g_node_caller_ra`,
  added the readiness-predicate comment at the `genuine` expression, and
  extended `describe()` with `node_ret=0x…`. Existing default-ON posture and
  kill switch unchanged. `startgame_v2.{c,h}` needed **no change** — its real
  `J` placeId flows through the genuine `0x2ebd51a` export into `0x72a31c0`,
  which the gate reads itself.
* `callback_provenance.{c,h}` (opt-in, force-withheld diagnostic): same
  `prov_walk_to_dispatch()` requirement added to its `genuine` verdict, new
  `SOBER_PROV_DISPATCH_RET_RVA`/`SOBER_PROV_TEXT_SIZE`, `g_node_caller`,
  `sober_callback_provenance_node_caller_ret()`, and `node_ret=0x…` in its log
  and `describe()`. This prevents its opt-in "GENUINE" line from being emitted
  for a host-entered dispatch; it cannot affect the marker either way.

All new constants/accessors are additive; no existing ABI field was removed.
No file outside the owned set was edited.

## 5. New knobs

**None.** The fix is a correctness change to the already-default-ON,
fail-closed `provenance_gate`; no environment variable was added or changed.
Existing knob posture for the lead's `seeded_state` audit (unchanged):

* `SOBER_PROVENANCE_GATE` is a *kill switch* (default ON). It must stay **out**
  of `seeded_state`: disabling provenance can only remove a credential, never
  manufacture a green. `SOBER_PROVENANCE_GATE_GUEST_BASE` is a read-only hint.
* `SOBER_CALLBACK_PROVENANCE` / `_GUEST_BASE` and `SOBER_STARTGAME_V2` /
  `SOBER_SGV2_*` remain in `seeded_state` (already wired in `guest_app_shell.c`).
* Worker-1's `SOBER_PREINIT_BUS` / `SOBER_PUBLISH_NODE_REDRIVE` and the
  `publish_default` seed counters belong in `seeded_state`; a host-entered
  re-drive must never become a provenance GENUINE (now enforced by item 3 of
  the walk).

## 6. Handoff (lead-owned, not edited here)

1. The gate hook and build are already wired (`native_jni.c:517`,
   `guest_app_shell.c:1736/1951`, `src/jni/CMakeLists.txt:58`); no new wiring
   is required for this fix.
2. Optional evidence: log `sober_provenance_gate_node_caller_return_addr()`
   (guest RVA when genuine, host address when re-driven) and
   `sober_provenance_gate_describe()` next to the existing diagnostics.
3. Keep the binding unchanged: marker requires notifier-driven callback
   (this predicate) **AND** guest-tagged frame with real draw/G8 proof **AND**
   `seeded_state==0`. `emit_provenance()` only on the clean path (already the
   case at `guest_app_shell.c:1951`).

## 7. Verification evidence

* Frozen identity re-verified: `sha256sum` =
  `3e498d649421115bdbea69f78e2b818d4351d8990db60e232d2aef5b741335`;
  `readelf -n` BuildID = `204ad049599c96afb75b8a21d5051063e821d6c9`;
  `readelf -lW` first LOAD `R E`, offset/vaddr 0, size `0x6d33b90`.
* `objdump -d -M intel` of `0x27eaf04..0x27eafc0` and `0x25848e0..0x2584973`
  reproduced the table in §1 (notably `call [rax+0x30]` at `0x27eaf2f`,
  `jmp rcx` at `0x27eaf6b`, `call 0x25848e0` at `0x27eafae`,
  `call *0x470(%rax)` at `0x258495a`).
* `cmake --build build -j4` — clean (sober_jni, guest-init-host, jni_probe).
* Standalone frame-layout check (`/tmp/opencode/task522_walk_test.c`, compiled
  against the real `provenance_gate.c`) exercised the exact walk: positive with
  guest caller RA and both RA-slot discovery paths pass; negative with a host
  node-caller, with the old wrong `0x27eaf34` invoke RA, and with a null frame
  all fail closed. `ALL TASK-522 WALK CHECKS PASSED`.

## 8. Honest status

The chain RVAs are correct and the gate now recognises it without weakening
fail-closed behaviour; a host-entered dispatch can no longer be scored GENUINE.
No run in this task emitted a callback, wrote guest bytes, fabricated a
placeId/publish, or produced a marker. The default path still reaches
`0x27eaf6e` only when the genuine upstream prerequisites (worker-1's id=1 node,
worker-4's latch, app singleton/app+0x488, worker-3's render evidence) are met;
until then the callback leg stays **0/1** on the honest path and the marker
remains **WITHHELD**.

## 9. Addendum - cross-check with analysis/scale-a2-notifier-chain.md (lead handoff)

The lead's parallel A2 decoding independently confirms every RVA in section 1 and
adds two prerequisites that belong on this readiness checklist (no gate change
from them; they are upstream/live-state conditions):

* JavaVM global 0x7cdec78 must be live. The notifier's 0x227b9e2 (GetEnv) loads
  that cached JavaVM; if it is NULL the out-struct stays zeroed and
  `mov (%r14),%rdi` at 0x27eafa6 dereferences NULL before the thunk is entered.
  The bridge seeds it only through SOBER_SEED_JNI_VM, which is default OFF and
  withhold-listed. So even with a genuine id=1 node and a live placeId, the
  chain cannot reach 0x27eafae until this global is seeded by the genuine guest
  JNI_OnLoad (or a withheld seed). This is a blocker, not a reason to weaken the gate.
* Cached class/method are synthetic bridge objects, not ART jmethodIDs.
  0x72a2f48 = seeded SR_K_CLASS NativeGLJavaInterface;
  0x72a2fc0 = seeded SR_K_METHOD gameLoadedCallback/(J)V. The gate's G3/G4
  pointer-identity checks are therefore meaningful within the bridge (the
  notifier loads exactly those globals at 0x27eaf98/0x27eaf9f and fn[138]
  dispatches on the object's name); any later overwrite of either global fails
  the gate. This does not turn a seeded run into genuine proof - seeded_state
  still forces the marker withheld.

Correction to A2 section 2.3 / readiness row 13: that report describes the old
G8 as "next word is any guest text address (~0x27eaf32)". That was the defect
fixed in section 2: the literal next word after the notifier RA is r14 (a host
JNIEnv pointer). G8 is now the validated 3-frame walk requiring
[rbp_not+8] == base+0x27eaf32 AND node 0x27eaf04 entered from guest .text
([rbp_node+8]). The lead's A2 table should be read with that correction; the
chain RVAs themselves are unchanged.
