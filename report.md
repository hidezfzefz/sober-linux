# WAVE29 #633 — TLS key writers, TLS object type, missing native RVAs

Read-only; no runs/device/phone. Engine SHA `3e498d64…`. No guest writes.
FAULTING INSTRUCTION (named first): **`0x6ca229c: call *0x548(%rax)`**,
bytes `ff 90 48 05 00 00`; stack RA `0x231555c` is not the fault site.
Evidence: `dis_tls_key3.txt`, `dis_tls_destructor.txt`, `dis_key_other.txt`,
`dis_accessor_return.txt`, `dis_accessor_ret2.txt`, `symbols.txt`,
`capture_missing_natives.txt`, `jni_onload_range.txt`.

## (a) Guest writers of TLS key `0x72a1b08` (= Bionic key 3) — PROVEN

Exactly **two** `pthread_setspecific@plt (0x6d31d10)` calls use this key (all
six references to the global `0x72a1b08` were enumerated: getspecific ×2 at
`0x2ea6455/0x2ea650c`, setspecific ×2 below, key_delete `0x2ea661a`,
key_create `0x2ea6641`):

| RVA | Function | Value stored in `rsi` | When |
|---|---|---|---|
| `0x2ea656e` | TLS descriptor accessor `0x2ea6440` (lazy branch) | `r14` = freshly `calloc`'d/`memset` state block; `0x2ea6555 movq $1,(%r14)` (refcount/marker +0), `0x2ea6561 mov %r12,0x8(%r14)` | first access per thread when `getspecific` returns NULL |
| `0x2ea6695` (tail jmp from `0x2ea668e`) | destructor `0x2ea6670` | `rbx` = same slot pointer, after `dec` at `0x2ea667f` | TLS destruction on that thread |

Key creation: `pthread_once` callback `0x2ea6640` calls
`pthread_key_create(&key@0x72a1b08, destructor=0x2ea6670)` at `0x2ea664f`.
Excluded: `0x2ea61b6` sets a **different** key from `0x72a1b00`. PROVEN.

**Thread** of each writer: **UNPROVEN** statically (no tid in disassembly).
**JNI_OnLoad** (`0x227af5a..0x227b8ef`) contains no `setspecific` or
`0x2ea6440` call — PROVEN. No capture-order native sets the key directly
(they reach the accessor indirectly via the 463 call sites of `0x6c89e50`/
`0x6c89e70`, e.g. `0x6c8d86a`, `0x6c8f3f7`, `0x6ca228b`) — indirect
population UNPROVEN without a dynamic trace. Which real-boot-only paths
populate it that the harness never runs is therefore **UNPROVEN**; the lazy
accessor is shared by the FindClass/GetStringUTFChars/ExceptionCheck wrappers.

## (b) What `[TLS value+0]` is — PROVEN expectation

`0x6ca228b call 0x6c89e70` → `0x2ea6440(0x7282068)` returns the per-thread
slot object; `0x6ca2290 mov (%rax),%rax` loads its **+0x00**, and the
faulting call uses that rax as a **JNI function-table pointer**
(`call *0x548(%rax)`). So the wrapper expects `[TLS value+0]` to be the JNI
function table — i.e. the TLS value is a **holder/indirection whose +0 is the
functions pointer** (same layout as the bridge `env_obj {functions, owner}`),
not a raw scalar and not the scheduler `obj`. Which guest code installs the
table into that field: **UNPROVEN** (the accessor initialises `+0` to `1` at
`0x2ea6555`; the indexed slot is read at `0x2ea6573`, return `%r15,%rax`).

## (c) Missing natives — PROVEN

| Native | Entry RVA (nm -D) | Capture args (w12) |
|---|---|---|
| `nativeSetPreferencesFile` | `0x246515d` | #613 t=290868ms `("prefs")` — one jstring |
| `nativeInitializeNativeFlags` | `0x2465210` | #614 t=290891ms `(EnableAndroidBinaryChannelDownloadTiming, PgsTreatmentActive, EnableLazyWorkManagerInit, EnableAppPlatformQoSEmergencyOnAnd…)` — String[] of flag names (log truncated) |

Harness calls `0x2465210` only under the `SOBER_NATIVE_FLAGS_INIT` opt-in and
never calls `0x246515d` — both are capture natives absent from the Run C order.

## What NOT done
No diff proposed/applied, no harness edit, no runs/gdb/perf, no device/AVD/
emulator/Frida/phone, no new captures, no guest writes, no gate edits, no
ledger edits, no credentials. Writer thread identity and real-boot-only
populating paths are UNPROVEN (needs harness breakpoint data).

## First divergence + next step
First divergence: TLS key-3 slot never populated on the harness path before
`0x6ca229c`, plus the two missing natives. Next: harness logs key-3
`getspecific` result at `0x6ca228b` and `[value+0]` at `0x6ca2290` (faulting
instruction `0x6ca229c` named first).
