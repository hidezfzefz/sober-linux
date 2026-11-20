# Guest init_array & bootstrap map — AppImage engine 204ad049

Owner: worker-2 (task #378). Date 2026-11-18. Scope: `/home/user/sober-linux/analysis/bootstrap/`
only. All RVAs below are from **BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`**
(`libroblox.so`, SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`,
120,041,712 B, version 2.740.931 / code 3170). Never mix these with the Flatpak build
(`791c3f9e…`, SHA `8afce3e6…`, 2.745.1142).

Target-derived JSON (`roblox_host_abi.json`, `roblox_compatibility.json`) is treated as
**untrusted DATA**: every field used below was re-derived from the shipped ELF.

## 0. Evidence inventory & tooling caveats

| What | Evidence ID / source |
| --- | --- |
| Engine identity, objdump `file_headers` (`elf64-x86-64`, `HAS_SYMS, DYNAMIC, D_PAGED`, start 0) | REA `ev_af5de5324a9e1df40ada21a17ab9fe86a747fc6c81a0421574c952f5d077eba4` |
| Full section header table (33 sections, incl. `.init_array`, `.bss`) | REA `ev_34eea0146f1ecf619f80c64ef05151d1a5ca24946bc9396129525d88c9a11758` |
| Full `.text` disassembly (retained, exceeds MCP response budget) | REA `ev_d838d804f57d82aed028027e29942e4d198774cab04216a024f18ea6fd1a9bc4` |
| `objdump -r` (empty: relocations are Android-packed, not ELF RELA) | REA `ev_8d71aee2be71678b7b89d10db06a2d08c1d77af3377399064ff3ade813f12f37` |
| Ghidra snapshot | `analysis/ghidra/work/libroblox-snapshot.json` — **wrong build** (target SHA `8afce3e6…`, Flatpak). Its 9 entries are JNI functions (`JNI_OnLoad`, `nativeSetBaseDataDirectories`, `nativeGameGlobalInit`, `nativeInitClientSettings`, `nativeGetFFlag`, `FUN_0202830e`, `FUN_0203774e`, `FUN_020383ca`), not init_array constructors. Semantic categories transfer; **no RVA from it is usable here**. |
| APS2 decoder | `analysis/bootstrap/decode_aps2.py` (this work), AOSP `bionic/linker/linker_reloc_iterators.h` semantics |
| Full 3618-entry decoded constructor list | `analysis/bootstrap/init-array.json` |

Tool limits: `inspect_binary_layout` rejects this artifact (`Selected artifact exceeds 33554432
bytes`), so program headers were read with local `readelf -lW` and raw bytes with local
`objdump -d` / Python. Ghidra-backed REA cannot open this target usefully; REA objdump
evidence above is the attestation path.

## 1. ELF layout (facts needed by the loader)

Program headers (`readelf -lW`):

| PT_LOAD | file offset | VMA | FileSiz | MemSiz | flags |
| --- | --- | --- | --- | --- | --- |
| 1 | `0x0` | `0x0` | `0x6d33b90` | `0x6d33b90` | R E — offset == VMA |
| 2 | `0x6d33bc0` | `0x6d37bc0` | `0x4f2948` | `0x4f3440` | RW — offset = VMA − `0x4000` |
| 3 | `0x7226540` | `0x722e540` | `0x053f78` | `0xb6ecdc` | RW — offset = VMA − `0x8000`; BSS tail |

Sections (from REA `ev_34eea014…` / `readelf -SW`):

| Section | VMA | file off | size |
| --- | --- | --- | --- |
| `.rela.dyn` (**ANDROID_RELA / APS2**, ES=1) | `0x15e00` | `0x15e00` | `0x1eb1f2` |
| `.rela.plt` (normal RELA, 542 JUMP_SLOT) | `0x200ff8` | `0x200ff8` | `0x32d0` |
| `.text` | `0x1db9a40` | `0x1db9a40` | `0x4f77a1a` |
| `.data.rel.ro` | `0x6d37bc0` | `0x6d33bc0` | `0x4e9280` |
| `.fini_array` | `0x7220e40` | `0x721ce40` | `0x18` (3 entries) |
| `.init_array` | `0x7220e58` | `0x721ce58` | `0x7110` = **3618 × 8** |
| `.data` | `0x722e540` | `0x7226540` | `0x53f60` |
| `.bss` | `0x7282500` | — (NOBITS) | `0xb1ad1c` (ends `0x7d9d21c`) |

Dynamic tags: `DT_INIT_ARRAY=0x7220e58`, `DT_INIT_ARRAYSZ=28944`,
`DT_ANDROID_RELA=0x15e00`, `DT_ANDROID_RELASZ=0x1eb1f2`, `DT_FLAGS=BIND_NOW`.
Every data seed below lands inside PT_LOAD 3 / `.bss` (zero-filled at load, no file bytes).

## 2. Packed RELA decode — the actual root cause (decision 008)

`DT_ANDROID_RELA` is AOSP's **APS2** packed relocation stream. glibc ignores this dynamic
tag; the 3618 `.init_array` u64 slots are all zero in the file, so a plain `dlopen` calls a
NULL ctor[0] (`signal 11, rip=0`, worker-3's probe). The pointers exist **only** as APS2
addends and must be applied before ctor dispatch.

Exact format (AOSP `bionic/linker/linker_reloc_iterators.h::for_all_packed_relocs`,
`linker_sleb128.h`):

- Bytes `0x15e00..0x15e03` = ASCII `APS2`; the stream starts at offset **+4** (`0x15e04`).
- Everything after the magic is an **SLEB128** (signed LEB128) stream
  (`sleb128_decoder::pop_front`).
- Stream: `num_relocs`; `r_offset` (=0). Then groups until `num_relocs` consumed:
  `group_size`; `group_flags`; if flag `0x2` then `group_r_offset_delta`; if flag `0x1`
  then `group_r_info`; if flags `0x4|0x8` (both) then `group_r_addend`.
- Flags: `0x1` = GROUPED_BY_INFO, `0x2` = GROUPED_BY_OFFSET_DELTA,
  `0x4` = GROUPED_BY_ADDEND, `0x8` = HAS_ADDEND.
- Per relocation: `r_offset +=` (delta if flag `0x2`, else its own SLEB);
  `r_info =` group value if flag `0x1`, else its own SLEB;
  `r_addend +=` SLEB if flag `0x8` (grouped-addend groups add once at group level).
- **`r_offset` and `r_addend` are running accumulators**, not independent values — a naive
  decoder that resets them produces plausible-looking but wrong garbage (my first pass did;
  corrected in `decode_aps2.py`).

Verified decode result:

| Field | Value |
| --- | --- |
| Declared relocations | `565390` (SLEB `8e c1 22`) |
| Decoded relocations | `565390` |
| Bytes consumed | `2011634` = exactly `0x1eb1f2` (whole section, no slack) |
| `r_info` low-32 histogram | `565308 × 8` = `R_X86_64_RELATIVE`; `60 × 6` = `R_X86_64_GLOB_DAT`; `22 × 1` = `R_X86_64_64` |
| Relocations targeting `0x7220e58..0x7227f60` | **3618**, all `R_X86_64_RELATIVE`, one per init_array slot |
| Their `r_addend`s | the constructor RVAs (unique: 3618 distinct pointers, range `0x1db9a40..0x6c58900`, all inside `.text`) |

Cross-check against untrusted profile — exact match, so the profile's constructor anchors
are trustworthy for this build:

| index | slot VMA | decoded RVA | profile anchor | first bytes |
| --- | --- | --- | --- | --- |
| 0 (SKIP) | `0x7220e58` | `0x2e62390` | — | `55 48 89 e5 53 50` (std prologue) |
| 1 (SKIP) | `0x7220e60` | `0x1dba8f3` | — | `e9 00 00 00 00` — 5-byte **jmp thunk** to `0x1dba8f8` |
| 2 (RUN) | `0x7220e68` | `0x1dbe2b1` | `0x1dbe2b1` ✓ | `55 48 89 e5 41 57…` |
| 3 (SKIP) | `0x7220e70` | `0x1db9a40` | `0x1db9a40` ✓ | `55 48 89 e5 41 57…` (= `.text` base) |
| 4 (SKIP) | `0x7220e78` | `0x1dba6c4` | `0x1dba6c4` ✓ | `e9 5f 41 00 00` — **jmp thunk** to `0x1dbe828` |
| 5 (RUN) | `0x7220e80` | `0x1dba834` | `0x1dba834` ✓ | `48 8d 05 …` (LEA-first, no frame pointer) |

Constructor body statistics (all 3618): 1580 begin `55 48 89 e5` (standard frame-pointer
prologue), 71 begin `e9 00 …` (jmp thunks), 347 `80 3d …` (cmp byte global), 343 `c7 05 …`
(mov qword global), 324 `c6 05 …` (mov byte global), 301 `31 c0 …` (xor eax), 255
`66 c7 …` (mov word global), remainder misc. So most entries are tiny C++ static-init
stubs, not full functions — they must still be replayed in order, because they set vtables
and globals.

**Hand-off to worker-3:** apply **all** packed relocations (including the 82 non-relative
ones — they populate GOT entries such as `__cxa_finalize@LIBC`), not just the init_array
ones. Order: map with ctor suppression → apply APS2 → worker-1 seeds/bridges → dispatch
ctors `[2,3)` then `[5,3618)`.

## 3. Data seed slots — exact sizes and layouts

All addresses are **VMAs/RVAs**; all are in zero-filled `.bss` and need no file bytes.

| Slot (RVA) | Size to seed | Evidence (disassembler, REA `ev_d838d804…`) |
| --- | --- | --- |
| `allocator_object_slot` `0x7834570` | **8 bytes**: a pointer field (the `rax` returned by call `0x1dd55f2`) inside a ~**156-byte** allocator state block `0x7834554..0x78345ef`. Initializer `0x2251b2a` writes: `0x7834554` dword, `0x7834558` 16 B, `0x7834568` 8 B, `0x7834570` 8 B (named slot), `0x7834590`/`0x78345a0`/`0x78345c0`/`0x78345d0` 16 B each, `0x78345b0`/`0x78345e8` 8 B each, `0x78345e0` 8 B. | `0x2251b2a`: `55 48 89 e5`, call `0x1dd55f2`, `mov %rax,0x…(%rip) # 7834570`, ret at `0x2251b8c`. |
| `arena_guard_slot` `0x7cd8fb0` | **8 bytes**, atomic lock word, zero-initialized. `lock cmpxchg %rcx(1),[0x7cd8fb0]` with expected 0; short-circuits if nonzero. | `0x1dbb178` (arena initializer): `mov 0x…(%rip),%rax # 7cd8fb0`, `test`, `lock cmpxchg`. |
| `arena_table_slot` `0x7cd9040` | **8 bytes**: a pointer to the runtime-allocated arena table. The table itself is allocated by `call 0x1dbbad9` with size `rbx + 0x10000` and zeroed with `memset(ptr,0,rbx)` at `0x1dbb9e2`; `0x1dbb1ec` reads the entry at `table+0x10`; the profile declares `arena_table_slot_count = 4194304` (4,194,304). **Do not hand-size this table** — the computed size depends on runtime page geometry (`0x1dbb8bb`); let the guest allocator satisfy it via the bridge by replaying `0x1dbb178`. | `0x1dbb9a2`: `mov %rax,0x…(%rip) # 7cd9040`; `0x1dbb9d6`/`0x1dbb9e2`; `0x1dbb1ec`: `cmpq $0,0x…(%rip) # 7cd9050`. |
| `empty_string_slot` `0x79413b8` | **8-byte pointer field** in a wider string-state block. Initializer `0x225dd2a` calls guest `allocate(0x50)` = **80 bytes** at `0x1dbab02`, zeroes the object at offsets `0x10,0x20,0x30,0x3a`, then stores the pointer at `0x7941360`, stores `%rcx` at `0x79413b8` and `0x7941380`, stores `%rdx` at `0x79413c8`, and `ptr+0x49` at `0x79413b0`. Seed the 80-byte object through the allocator bridge, not a fixed address. | `0x225dd2a`: `mov $0x50,%edi; call 1dbab02; movups …; mov %rcx,0x… # 79413b8`. |
| `jni_singleton_slot` `0x7d9cf40` | **1024 bytes** (profile value, consistent with BSS layout). First ~92 bytes are initialized by `0x1dba834`: an **atomic pointer exchange** at `0x7d9cf40` (head ⇄ `&0x7d9cf48`), then object fields at `0x7d9cf48` (8 B old head), `0x7d9cf50/0x7d9cf60/0x7d9cf70` (16 B each), `0x7d9cf90` (8 B zero), `0x7d9cf98` (dword `0x3f800000` = 1.0f). Zero the 1024 B, then replay `0x1dba834` (it is also ctor[5]). | `0x1dba834`: `lea 0x… # 7d9cf48; xchg %rax,0x… # 7d9cf40; ret at 0x1dba87d`. |
| `pre-JNI registry_slot` `0x7cdd118` (extra seed from profile `native_pre_jni_bootstrap`) | **8-byte pointer** to a **0x338 = 824-byte** object: `0x227be14` calls guest `allocate(0x338)` at `0x1dbab02`, `memset(0x338)`, writes float 1.0f at offsets `0x20,0x88,0xb0,0xd8,0x100,…`, `0xffffffff` at `0x60`, and stores the object pointer at `0x7cdd118` (`mov %rbx,0x… # 7cdd118` at `0x227c092`). | `0x227be14` (std prologue); `0x227c092` store site. |

Additional seed-related entry points (from profile, verified to land in `.text`):
`allocator_thread_initializer 0x1e47927` (std prologue; reads TLS key at `0x7282000`,
calls `pthread_getspecific`, returns a pointer at `0x721db80`) and `arena_initializer
0x1dbb178`. Neither appears in the init_array — they are called from allocator code
(`0x1dbadfd`, `0x1dbe475`, `0x1dbc49f`) and must be reachable through the allocator bridge.

Bridge entry prologues (all valid instruction boundaries):
`allocate 0x1dbab02`, `small-allocate 0x1dbad62`, `reallocate 0x227aafa`,
`aligned-allocate 0x1dd60e5`, `usable_size 0x1dbdfba` (leaf, `mov rsi,rdi`),
`free 0x1dbe559` (leaf, `mov rsi,rdi`) — match worker-1's list exactly; no size
disagreements on the bridge RVAs themselves.

**Correction to `analysis/rea/findings.json`** (confirmed independently by worker-1): the
`bridge_entries` fingerprint for small-allocate @ `0x1dbad62` is transcribed as
`554889e5890d202a`, but a direct byte read gives `554889e553504889`; the former bytes
actually belong to allocator-object init `0x2251b2a`. **The RVA is correct; only the
fingerprint string is wrong.** Verified fingerprints:
`0x1dbab02 = 554889e55350e837`, `0x1dbad62 = 554889e553504889`,
`0x1dbdfba = 4889fe488b057cb0`, `0x227aafa = 554889e541565348`,
`0x1dd60e5 = 554889e541565348`, `0x1dbe559 = 4889fe488b05ddaa`.

**Seed slots are NOT relocation targets.** I queried all 565,390 decoded APS2 relocations:
zero relocations target any of `0x7834570`, `0x7cd8fb0`, `0x7cd9040`, `0x79413b8`,
`0x7d9cf40`, `0x7cdd118` (nor within ±0x20 of any of them). They are pure BSS slots;
worker-1's `apply()` can write them without any ordering interaction with packed-RELA
application (which only fills init_array + GOT).

## 4. Initialization sequence (recommended)

1. **Map** `libroblox.so` with ctor suppression; apply ELF RELA/REL/JUMP_SLOT normally
   (glibc does this for `.rela.plt`, 542 entries).
2. **Apply APS2 packed RELA** from `0x15e00` (565,390 relocations) *in full* — this fills
   all 3618 init_array pointers and 82 GOT entries (worker-3, `src/linux/`).
3. **Zero BSS** (PT_LOAD 3 tail) so every seed slot starts at 0.
4. **Install allocator bridges** (worker-1): the six entry points in §3 must be callable by
   guest code before any guest initializer runs.
5. **Seed / replay in this order**:
   `0x2251b2a` (allocator object) → `0x1dbe2b1` (ctor[2], allocator/arena state) →
   `0x1e47927` (allocator thread initializer, profile says "after constructor 2") →
   `0x227be14` (pre-JNI registry) → `0x225dd2a` (empty string) →
   `0x1dba834` (ctor[5], JNI singleton) → ctors `[6,3618)`.
   `0x1dbb178` (arena init) is self-guarded and is invoked from `0x1dbe475` inside
   ctor[2]; replaying it explicitly is safe (the `cmpxchg` short-circuits on a nonzero guard).
6. **Skip** ctor indices `0` (`0x2e62390`), `1` (`0x1dba8f3`), `3` (`0x1db9a40`),
   `4` (`0x1dba6c4`); these are the profile's excluded range `[0,2)+[3,5)`.
   Note ctor[3] calls the allocator bridge (`0x1dbad62`, `0x1dbdfba`) directly and would
   reinitialise mimalloc; ctor[4] is a jmp thunk into that same allocator subsystem.
7. Emit "guest init complete" only after ctor 3617 returns.

## 5. Ranked list — patch / replay / skip

| Rank | Function / RVA | Action | Why |
| --- | --- | --- | --- |
| 1 | APS2 decoder, `0x15e00` (565,390 relocs) | **must implement** (worker-3) | Root cause: without it every init_array slot is NULL. |
| 2 | `allocate` bridge `0x1dbab02` (+ 5 sibling entries) | **must patch/bridge** (worker-1) | Called by nearly every seed initializer (empty string, registry, arena table) and by ctor[3]. |
| 3 | ctor[2] `0x1dbe2b1` | **replay** (first ctor) | Initializes allocator/arena globals (`0x7cd8fa8`, `0x7cd9078`, `0x7cd9080`) and calls arena init via `0x1dbe475`. |
| 4 | allocator object init `0x2251b2a` | **replay or seed 8 B at `0x7834570`** | Supplies the allocator object pointer all later allocations depend on. |
| 5 | `0x1e47927` allocator thread init | **replay after ctor[2]** | Profile directive; relies on TLS key `0x7282000` and `pthread_getspecific`. |
| 6 | `0x227be14` pre-JNI registry init | **replay or seed 8 B pointer + 824 B object at `0x7cdd118`** | Pre-JNI registry is referenced by a long chain of JNI helpers (`0x227c168…`). |
| 7 | `0x225dd2a` empty-string init | **replay or seed 80 B object + pointers around `0x79413b8`** | Requires guest allocator; C++ string state used by most constructors. |
| 8 | ctor[5] `0x1dba834` | **replay** (or seed 1024 B + atomic-head pattern) | JNI singleton; first ctor in the `[5,3618)` range. |
| 9 | ctors `[6,3618)` | **replay in ascending order** | 3612 mostly-tiny static init stubs; skipping any leaves vtables/globals unset. Watch for any that touch Android/JNI/NDK and would need a stub — iterate with the crash harness. |
| 10 | arena init `0x1dbb178` | **replay via ctor[2], self-guarded** | Allocates the arena table through the bridge; do not pre-size by hand. |
| — | ctor[0] `0x2e62390`, ctor[1] `0x1dba8f3`, ctor[3] `0x1db9a40`, ctor[4] `0x1dba6c4` | **SKIP** | Explicit profile exclusion; ctor[3]/[4] reinitialize/replace the native allocator, ctor[1]/[4] are jmp thunks. |

## 6. RVA / prologue audit — flags for worker-1 and lead

Every RVA from the validated profile lands inside an executable segment and on a decoded
instruction boundary. Five do **not** start with the canonical `55 48 89 e5`
(`push rbp; mov rbp,rsp`) prologue — these are *valid but unusual* entries and must not be
"fixed" by patching a prologue into them:

| RVA | bytes | assessment |
| --- | --- | --- |
| `0x1dba834` (jni singleton init / ctor[5]) | `48 8d 05 0d 27 fe 05` | LEA-first, no frame pointer; valid function, ret at `0x1dba87d`. |
| `0x225dd2a` (empty string init) | `bf 50 00 00 00 e8 …` | `mov edi,0x50; call allocate` — valid function start, no prologue. |
| `0x1dba6c4` (ctor[4] anchor) | `e9 5f 41 00 00` | 5-byte jmp thunk to `0x1dbe828`. |
| `0x1dba8f3` (ctor[1]) | `e9 00 00 00 00` | 5-byte jmp thunk to `0x1dba8f8`. |
| `0x326743e` (fmod input_count) | `48 8b 47 60 48 2b 47 58` | leaf getter, ends `ret`; valid. |
| `0x326789a` (fmod input_current) | `48 8b 47 58 48 8b 4f 60` | leaf getter; valid. |

`0x1dbdfba` (usable_size) and `0x1dbe559` (free) start `48 89 fe` (`mov rsi,rdi`) — valid
leaf entries, also flagged so nobody mistakes them for bad RVAs. All other 20+ profile
RVAs (six bridges, `0x2251b2a`, `0x1dbe2b1`, `0x1db9a40`, `0x1e47927`, `0x1dbb178`,
`0x227be14`, and the FMOD/setter methods) start with `55 48 89 e5` and are clean.
**No profile RVA is invalid**; the earlier "plausible prologue" cross-check was too coarse
but its conclusion (all in the R E PT_LOAD) holds.

One size caveat: the profile's `arena_table_slot_count = 4194304` is a *count of slots*, not
a byte size, and the byte stride is not fixed in the static code (see §3). Treat it as
profile DATA and let `0x1dbb178` allocate the table through the bridge.

## 8. Follow-up: ctor[6] fault analysis (integration debugging)

**Status: the protobuf/ctor[1] hypothesis in this section was tested and DISPROVEN by
worker-3's exhaustive replay of schedules `[2,3)+[5,…)`, `[2,3618)` and `[0,3618)` — all
three fault identically at ctor[6]. Skipped ctors are not the cause; the common factor is
the NULL arena table (§9). The disassembly below is retained as evidence of the faulting
instruction and as a fallback if seeding the table does not clear it.**

Reported chain: ctor[6] `0x1dbeece` → `0x1dbef38` → `0x1dbef9a` → `0x1dc1b19` →
`0x1dc1ee1` → `0x1dc2392` → fault at `0x1dc207f` (`mov 0x8(%r15),%rax`, r15 = NULL).
Evidence: REA `ev_d838d804…` (full objdump disassembly) plus local `objdump -d`.

- ctor[6] `0x1dbeece` is a **once-guard initializer for global 0x7239570**:
  `+0` done flag, `+4` id `0x357`, `+8` pointer filled by an APS2 RELATIVE relocation at
  `0x7239578` with addend `0x1184cc0` (serialized proto path
  `openTelemetry/proto/common/v1/common.proto`), `+0x2c = 5`. It calls `0x1dbef52`, which
  calls `0x1dc1ac4` (protobuf descriptor registration) with that proto path and id `0x357`.
- `0x1dc207f` is a **virtual method**: its address is stored as a RELATIVE-relocation
  addend in 56 vtable slots in `.data.rel.ro` (e.g. `0x72175c8`, `0x723a3b0`,
  `0x72621d0`). The caller `0x1dc2390` is `call *%rax` with `rdi = rbx` (NULL) — a
  protobuf object created lazily by `0x1dc24ec` came back NULL.
- The lazy protobuf globals in this path are **BSS flags/objects around
  `0x783d960..0x783dab0`**: `0x783d968` (flag `0x783d970`, read in `0x1dbf00a`),
  `0x783d980` (flag `0x783d988`, read in `0x6b1b75c`), `0x783daa8` (ctor[6]'s flag),
  and `0x783dab0` (ctor[1]'s flag). None of them is a GOT slot (libm is ruled out) and
  none is in the data-seed set (those are at `0x7834xxx`).
- **Primary suspect: skipped ctor[1] (`0x1dba8f3` → thunk `0x1dba8f8`).** That function is
  itself a once-guard initializer for flag `0x783dab0` and calls `0x6b90794` and
  `0x2a0d7d0` — i.e. it initialises protobuf/descriptor runtime state in the *same*
  `0x783daxx` neighbourhood as ctor[6]'s flag `0x783daa8`. Replaying ctor[1] before
  ctor[6] is the cheapest diagnostic (and may be required because we are not running
  mocktail's Python host that the profile's skip decisions were derived against).
  Secondary suspect: ctor[0] `0x2e62390`, which once-initialises `0x72a07a0` via
  `0x2e618b0`. Test the two skipped ctors individually; do **not** blanket-enable
  ctor[3]/[4] (they reinitialise mimalloc through the bridge).

## 9. Follow-up: arena table uses mmap, not the allocator bridges

**Resolution (DECISION 009):** the NULL/hanging arena path described here was a *downstream
symptom*. Root cause was in the harness's versioned Bionic `libc.so` stub: its constructor
called `dlsym()` while the stub's own `dlsym` slot was still NULL, leaving all 411
`sober_libc_fn_*` slots unset; guest `sysconf()` then returned a bogus value (observed
`0x3e8`), arena init bailed and `arena_table_slot` stayed NULL. Fix that stub first; the
mmap/size analysis below remains valid documentation of the path but no host-side arena
pre-seed should be needed once `sysconf` returns 4096.



Fault-time dump from worker-3: `arena_guard@0x7cd8fb0 = 1` but `arena_table@0x7cd9040 =
NULL`, proving arena init `0x1dbb178` ran and its table allocation failed (REA
`ev_d838d804…`):

- `0x1dbb98a` / `0x1dbb99d` call helper **`0x1dbbad9`** with
  `rdi = rbx + 0x10000` (size), `esi = 1`, `edx = bool`, `rcx = 0x7cd9000`;
  `0x1dbb9a2` stores its result into `arena_table_slot 0x7cd9040`.
- `0x1dbbad9` → `0x1dbbddc` (pure size/alignment rounding, no allocation) →
  `0x1dbbe50` → `0x1dbbfc9` → `0x6c81699` → **`call mmap@plt` (0x6d31c80)**;
  `mprotect@plt` / `prctl@plt` also used. **None of the six bridged allocator entries is
  on this path.**
- Size expression: `rbx = align_down((8 << (cl − 29)) + page_size − 1, page_size)` from
  globals `0x7281f40` (clamped to ≤ `0x40`, default `0x2f`) and `0x7281fe0`
  (`sysconf(_SC_PAGESIZE)`); default `cl=0x2f` gives `rbx = 2 MiB`, so the mapping
  request is about `2 MiB + 64 KiB` (the profile's `arena_table_slot_count = 4194304`
  is a *slot count*, not this byte size).
- Therefore a NULL table means `mmap` returned `MAP_FAILED`. Check the guest's `mmap`
  JUMP_SLOT in `.rela.plt` (542 JUMP_SLOTs; the "82/82 symbolic" check covers only
  GLOB_DAT). If anonymous mapping cannot work under the custom loader, pre-seed
  `0x7cd9040` with a zeroed mapping and set guard `0x7cd8fb0 = 1` before ctor[2] so
  `0x1dbb178` short-circuits.

## 7. Reproduction

```sh
# sections / identity (REA evidence ev_34eea014… / ev_af5de532…)
# decode packed relocs and dump all 3618 constructor RVAs:
python3 analysis/bootstrap/decode_aps2.py
# machine-readable full list: analysis/bootstrap/init-array.json
# spot-check disassembly (local objdump; full form is REA ev_d838d804…):
objdump -d --start-address=0x2251b2a --stop-address=0x2251b90 <engine>
objdump -d --start-address=0x1dba834 --stop-address=0x1dba880 <engine>
objdump -d --start-address=0x1dbb178 --stop-address=0x1dbb1f8 <engine>
```

`<engine>` = `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` (verify SHA first).
