# Native Bionic bootstrap patcher (worker-1, task #375)

Owner: **worker-1**.  All files in this directory are owned exclusively by
worker-1; cross-cutting changes (CMake, tests, exports.map, stubs) are handed
off to worker-3 via `team_send`.  Do not edit outside this directory.

## Purpose

Make the **unmodified AppImage `libroblox.so`** reach and complete its
`init_array` natively on Linux — no `basalt`/`mocktail`/Android runtime, no
translation layer.  This library performs, in native code, the runtime patching
the Android loader would have done before guest constructors run.

Target (and **only** target):

| Field | Value |
| --- | --- |
| BuildID | `204ad049599c96afb75b8a21d5051063e821d6c9` |
| SHA256 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` |
| Version | 2.740.931 (version_code 3170), Android 26, NDK r28c, x86-64 |

The Flatpak engine (`791c3f9e...` / 2.745, SHA `8afce3e6...`) is a **different
binary** — none of its RVAs appear here.  Every call into this library gates on
the mapped object's `NT_GNU_BUILD_ID`; a mismatch returns
`SOBER_BIONIC_ERR_BUILD_MISMATCH` and patches nothing (verified below).

## Files

- `bionic_bootstrap.h` — public C interface (publish this path in handoffs).
- `bionic_bootstrap.c` — implementation: build gate, six bridge installs, four
  data seeds, optional init_array replay, guest discovery via `dl_iterate_phdr`.
- `bootstrap_profile.h` — every validated RVA/size with inline evidence notes.
- `README.md` — this file (evidence + integration contract).

## Validated profile (evidence-backed)

Sources: `docs/interfaces/abi-map.md`; `analysis/rea/findings.json`
→ `decision_003_004_extension.bridge_profile` (from
`native/mocktail/roblox_host_abi.json`, whose consumption by basalt is REA-
evidenced by `ev_d7ca1d41...` and engine identity by `ev_af5de532...`).

**Six mimalloc bridges** (guest RVA → native host function):

| label | RVA | host |
| --- | --- | --- |
| allocate (small) | `0x1dbad62` | `malloc`/`mi_malloc` |
| usable_size | `0x1dbdfba` | `malloc_usable_size`/`mi_usable_size` |
| reallocate | `0x227aafa` | `realloc`/`mi_realloc` |
| allocate | `0x1dbab02` | `malloc`/`mi_malloc` |
| aligned_allocate_direct | `0x1dd60e5` | `aligned_alloc`/`mi_aligned_alloc` |
| free | `0x1dbe559` | `free`/`mi_free` |

Each bridge thunk is patched with a 12-byte `movabs rax,<host fn>; jmp rax`
after its 8-byte prologue fingerprint is verified against bytes read directly
from the AppImage engine.  mimalloc is used if the process already exports it;
otherwise native glibc malloc is used.  Both are host SysV-AMD64 code.

**Four data seeds** (slot RVA ← initializer RVA):

| object | slot | initializer | notes |
| --- | --- | --- | --- |
| allocator object | `0x7834570` | `0x2251b2a` | 8-byte pointer inside ~156 B state block `0x7834554..0x78345ef` |
| JNI singleton | `0x7d9cf40` | `0x1dba834` (= ctor[5]) | zero 1024 B here; the initializer runs as `ctor[5]` during replay, NOT in `apply()` (its `xchg` is not idempotent) |
| empty string | `0x79413b8` | `0x225dd2a` | |
| pre-JNI registry | `0x7cdd118` | `0x227be14` | 8-byte pointer only; 0x338-byte object is a separate guest allocation — zeroing 0x100 here would clobber adjacent BSS |

All four slots are confirmed by worker-2 (`analysis/bootstrap/bootstrap-map.md`)
to receive **zero** packed relocations (nor within ±0x20), so per-slot zeroing
is safe.  The guest initializers allocate their objects through the bridges;
we never hand-size an object.

**Constructors / init_array**: 3618 entries at **VMA `0x7220e58`**
(`DT_INIT_ARRAY` tag 0x19 = `0x7220e58`, `INIT_ARRAYSZ` = 28944 = 3618×8;
section `.init_array` file offset `0x721ce58`).  `0x7220e58` is a VIRTUAL
ADDRESS — add it directly to the mapped load base; never treat it as a file
offset.  Validated replay ranges (half-open):
`[2,3)` and `[5,3618)`.  Cross-check anchors: ctor RVAs
`2=0x1dbe2b1, 3=0x1db9a40, 4=0x1dba6c4, 5=0x1dba834`.

## Static cross-check evidence (2026-11-18, local shell + probe)

Read-only harness `gcc ... test_bootstrap_probe.c bionic_bootstrap.c -ldl`,
mapping the actual AppImage `libroblox.so` with `MAP_PRIVATE`:

```
verify_build (correct build) = 0  []
bridge small-allocate     @0x1dbad62: FP OK
bridge usable-size        @0x1dbdfba: FP OK
bridge reallocate         @0x227aafa: FP OK
bridge allocate           @0x1dbab02: FP OK
bridge aligned-allocate   @0x1dd60e5: FP OK
bridge free               @0x1dbe559: FP OK
DT_INIT_ARRAY VMA = 0x7220e58 (INIT_ARRAYSZ=28944, file off 0x721ce58) -- MATCH
init_array file off 0x721ce58 -> vaddr 0x7220e58 -- MATCH
init_array_info rc=0 rva=0x7220e58 count=3618 []   (segment-mapped probe)
verify_build (corrupted build) = -1  [build ID mismatch: guest is not AppImage
                                      204ad049... (refusing to patch ...)]
```

- `readelf -nW` → GNU `NT_GNU_BUILD_ID` = `204ad049...` (matches gate).
- `readelf -lW` → LOAD#0 `offset==vaddr`; `.init_array` at file offset
  `.init_array` VMA `0x7220e58` / file offset `0x721ce58` lives in the RW/RELRO
  LOAD whose `VirtAddr - Offset = 0x4000`;
  all four data slots live in the third RW (BSS-like, memsz 0xb6ecdc ≫ filesz
  0x053f78) LOAD, matching the profile's cross_check note.
- **Ordering proof (objdump)**: `0x225dd2a` begins `mov $0x50,%edi; call
  0x1dbab02` (allocate via bridge); `0x227be14` does `mov $0x338,%edi; call
  0x1dbab02`; `0x2251b2a` calls `0x1dd55f2` then stores the result to
  `0x7834570`.  Therefore **bridges must be installed before seed
  initializers run** — this implementation does exactly that.  `0x2251b2a`
  also stores `ECX`, so initializer calls zero all six argument registers.
- **Profile transcription correction**: `findings.json` lists small-allocate's
  `first_bytes` as `554889e5890d202a`, but those are the bytes at the
  *allocator initializer* `0x2251b2a`.  The real bytes at `0x1dbad62` are
  `554889e553504889`.  The RVA is correct; we verify against the bytes measured
  from the binary.  Flagged to worker-2/lead for `analysis/bootstrap/` and any
  profile update.

## Integration contract for worker-3 (DECISION D)

Header: `src/compat/bionic_bootstrap/bionic_bootstrap.h`
Entry point: `int sober_bionic_bootstrap_apply(void *guest_base)` → `0` = OK.

**DECISION 008 canonical order (supersedes the dlopen-only Mode A below).**
The root cause of the "init_array crash" is now known: the guest's
`DT_INIT_ARRAY` is all-zero in the file and is filled by **Android packed
relocations** (`DT_ANDROID_RELA` @ `0x60000011`, ptr `0x15e00`, size
`0x1eb1f2`) which glibc ignores.  The agreed order is:

1. map the guest with constructors suppressed (worker-3, `src/linux/`);
2. worker-3 applies packed RELA / fills the 3618 init_array entries;
3. **worker-1 calls `sober_bionic_bootstrap_apply(guest_base)`** — bridges +
   seeds only, no constructors; this is safe after relocations are applied;
4. worker-3 drives ctor ranges `[2,3)` + `[5,3618)` from `guest_base+0x7220e58`;
5. worker-3 emits the "guest init complete" signal.

**Replay order (`sober_bionic_bootstrap_replay_constructors()`).** init_array
indices `[2,3)`, then allocator-thread init `0x1e47927` (profile directive:
"after constructor 2"), then `[5,3618)`.  `ctor[5]` (`0x1dba834`) performs the
JNI singleton `xchg` exactly once.  `0x1dbb178` (arena init) is self-guarded and
is reached through `ctor[2]`, so we do not call it explicitly.

**RVA correction (lead, DECISION 008 follow-up).** An earlier draft of this
README/handoff incorrectly converted `0x7220e58` from a file offset to
`0x7224e58`. That was wrong: `0x7220e58` is already the **VMA**
(`readelf -dW`: DT_INIT_ARRAY `0x7220e58`, DT_INIT_ARRAYSZ 28944;
`readelf -SW`: `.init_array` addr `0x7220e58`, file offset `0x721ce58`).
All occurrences of `0x7224e58` were removed. `replay_constructors()` now reads
`DT_INIT_ARRAY`/`DT_INIT_ARRAYSZ` from `PT_DYNAMIC` at runtime and cross-checks
them against the profile; a segment-mapped probe returns
`rva=0x7220e58, count=3618`. Worker-3 drives ctors at
`guest_base + 0x7220e58`. All four seed-slot RVAs and the six bridge RVAs are
VMAs and are added directly to the load base (bridge RVAs also happen to equal
file offsets because they live in LOAD#0 where `offset == vaddr`).

**New safe helper:** `int sober_bionic_bootstrap_init_array_info(void *base,
unsigned long long *out_rva, unsigned int *out_count)` — build-ID gated,
executes no guest code, returns `0x7220e58 / 3618`. Useful for worker-3's
harness to locate the ctor array.

**Fault localisation (worker-3 request).** `replay_constructors()` publishes
two default-visible globals immediately *before* each `init_array` call:

```c
extern volatile unsigned sober_bootstrap_ctors_done;            /* index */
extern volatile unsigned long long sober_bootstrap_ctors_current_rva; /* guest RVA */
```

A SIGSEGV handler can read these to name the exact index / constructor that
faulted.  Verified present in the dynamic symbol table when built with
`-fvisibility=hidden` (all 11 `sober_bionic_bootstrap_*` functions are likewise
exported via `SOBER_BOOTSTRAP_EXPORT`, so CMake/exports.map binding works).

**Interposition shim.** There is no `dlopen` interposer in this library.
`sober_bionic_bootstrap_from_stub_constructor()` (the optional Mode A helper)
is now a **no-op unless `SOBER_BOOTSTRAP_INTERPOSE=1`** is set, so worker-3's
explicit weak-ref/dlsym call path cannot double-apply.

**Packed-RELA confirmation (worker-2).** `DT_ANDROID_RELA` @ file offset
`0x15e00`, size `0x1eb1f2` is AOSP APS2; 565,390 relocs decode (565,308
RELATIVE, 60 GLOB_DAT, 22 R_X86_64_64) and consume the whole table.  Exactly
3618 RELATIVE relocs target VMA `0x7220e58..0x7227f60`; their addends are the
real init_array function pointers (ctor[2]=0x1dbe2b1, [3]=0x1db9a40,
[4]=0x1dba6c4, [5]=0x1dba834 — match this profile).  The 3618 file bytes are
zero by design.  Confirms worker-3 must apply packed RELA before seeding.

`apply()` deliberately does **not** zero the seed slots: a packed relocation
may have just written a value into or near them.  The guest initializer
functions write every field they use.  My writes are confined to the four
documented slot ranges plus the 12-byte bridge prologues.  I do **not** apply
packed relocations — worker-3 owns that.  If any seed slot turns out to be a
relocation target, it is overwritten by the *guest's own initializer* (the same
thing mocktail does), not by stray scrubbing here.

Helper entry points worker-3 may use instead of `apply()`:
`sober_bionic_bootstrap_run(guest_base)` = apply + internal replay of
`[2,3)+[5,3618)` (only for a fully custom loader that will not drive ctors
itself).  Under decision 008 worker-3 drives ctors, so call **`apply()` only**.

The older dlopen-only Mode A below is kept for reference but is not sufficient
on its own because plain dlopen never applies the packed relocations.

Two supported modes:

**Mode A — dependency constructor + ordinary dlopen (reference only; see
DECISION 008 — insufficient alone).**  Stub DSO constructors run before the guest's own
`init_array`, after map+relocation.  Add to one NDK stub (e.g. `ndk_stub.c`
built into `liblog.so`):

```c
extern void sober_bionic_bootstrap_from_stub_constructor(void)
    __attribute__((weak));
__attribute__((constructor(101))) static void bootstrap_guest(void) {
    if (sober_bionic_bootstrap_from_stub_constructor)
        sober_bionic_bootstrap_from_stub_constructor();
}
```

Worker-3 also needs to: add `bionic_bootstrap.c` to the `sober_compat` static
library and its include dirs; add `sober_bionic_bootstrap_*` to
`src/compat/exports.map` so the host executable publishes them (`-rdynamic`)
for the stub's late binding.  Do **not** replay constructors separately in this
mode — glibc runs `init_array` itself.

**Mode B — custom native loader / manual replay.**  After map+relocation call
`int sober_bionic_bootstrap_run(guest_base)`; this applies seeds+bridges and
replays `[2,3)+[5,3618)` (mocktail schedule).  Do not use `dlopen()` in this
mode or constructors run twice.

All entry points are idempotent and build-ID gated.  `guest_base == NULL` means
"discover via `dl_iterate_phdr()` and verify".

## Runtime debugging log (2026-11-18, local reproduction)

Reproduced the harness locally with a standalone build of
`src/linux/guest_dlopen_host.c` + `android_relocs.c` + `android_compat.c` +
`gl_egl_forward.c` + this patcher (`-ldl -lpthread -lm -rdynamic`,
`LD_LIBRARY_PATH=build/ndk-stubs`).  Three real bugs in **this** patcher were
found and fixed:

1. **dlsym from a bridge context** — `host_allocate()` originally called
   `dlsym(RTLD_DEFAULT, "mi_malloc")` on every allocation.  That `dlsym` crashed
   glibc (`si_addr=0`) on the first guest allocation made by seed initializer
   `0x225dd2a`.  Fixed: mimalloc is now bound with load-time **weak symbols**
   (`extern ... __attribute__((weak))`), never dlsym inside a bridge.  Host
   allocator is glibc malloc when mimalloc is absent — still fully native.
2. **init_array entries are absolute, not relative** — worker-3's packed RELA
   writes `base + VMA` into each slot.  `replay_range()` was adding the base a
   second time (double relocation), so `ctor[2]` jumped to garbage.  Fixed: a
   value already inside the guest mapping is used directly; only a true
   relative VMA gets the base added.  Verified `ctor[2] = 0x1dbe2b1`,
   `ctor[5] = 0x1dba834` now dispatch correctly.
3. **`DT_INIT_ARRAYSZ=0` false failure** — the harness zeroes that tag to
   suppress loader-driven ctors; `read_init_array_vma()` used to reject it.
   Fixed: a zero/mismatched tag is accepted and the validated count 3618 is
   used.

Remaining obstacle (after the three fixes): `apply()` **completes**, and
`ctor[2]` + `ctor[5]` run; `ctor[6]` (`0x1dbeece`) faults at guest RVA
`0x1dc207f` on a `mov 0x8(%r15),%rax` with a NULL table pointer.  Root cause is
outside the four seeds/six bridges (likely the 17 unresolved libm `GLOB_DAT`
symbols worker-3 reported, or an allocator callback table).  Handed to
worker-3/lead with the exact fault data.

To get `apply()` past seed `0x225dd2a`, the empty-string seed now reproduces the
initializer's **data effects** directly (allocate 0x50 through the installed
bridge, zero it, write the six pointer fields at `0x79413b0/3b8/3c0/3c8/380/360`)
instead of executing the guest initializer, whose `strlen@plt`/`memmove@plt`
path faulted through the versioned stub libc.  Opt out with
`SOBER_NO_DIRECT_SEED_EMPTY=1`.  Debug trace: `SOBER_BOOTSTRAP_DEBUG=1`.

## Final verification — DECISION 006 done condition PASSED (2026-11-18)

With worker-3's regenerated `build/ndk-stubs` (proper fix for the DECISION 009
self-hosting-dlsym bug in the versioned libc stub) and this file in its final
state, the official harness completes:

```
$ LD_LIBRARY_PATH=build/ndk-stubs timeout 100 <guest-init-host>
[harness] calling bootstrap apply(...)
[harness] bootstrap seeds/bridges applied
[harness] delegating ctor replay to worker-1's ...replay_constructors()
guest init complete
exit=0
```

Final patcher state: the temporary stub fn-slot repair and 32 MiB arena
pre-seed were **removed** after the proper stub fix landed; the only
belt-and-braces measure kept is writing `0x1000` to guest globals `0x7281fe0`
and `0x7281fe8` (plain BSS) at the end of `apply()`.  `clamp_cl` at `0x7281f40`
is left at its valid default 0.  The four seeds, six bridges, build-ID gate,
absolute init_array handling and `[2,3)+[5,3618)` replay are unchanged.

## Status / open items

- Patchers, gate, discovery, replay, evidence, and this README: **done**.
- Awaiting worker-2 `analysis/bootstrap/bootstrap-map.md` for exact seed slot
  sizes; defaults in `bootstrap_profile.h` are conservative bounds taken from
  the disassembly (allocator ≈0x100, JNI 1024, empty string 0x40, registry
  0x100).  If worker-2 reports different sizes, update the `*SLOT_SIZE` macros.
- End-to-end "init_array completes" is jointly owned: worker-3 wires Mode A
  into `tests/guest-dlopen.sh` + CMake; lead records the PASS in decision 005.
  This directory does not modify any of those files.
