# Guest indirect-dispatch map — 0xc0dedbad handle creation site

Owner: **worker-4** (task #392). Scope: `analysis/dispatch/` only (new dir). Static analysis, no target execution, no code changes.

Target: AppImage `libroblox.so` — BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`,
120,041,712 bytes, path `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`.
**No Flatpak RVAs are used.** All RVAs below = file offsets (first `R E` PT_LOAD maps offset == RVA; see E2).

## 0. Evidence IDs and method

| ID | Source |
| --- | --- |
| **E1** | `ev_d838d804f57d82aed028027e29942e4d198774cab04216a024f18ea6fd1a9bc4` — REA `inspect_with_objdump` **full `.text` disassembly**, SHA-bound to the exact artifact above (authority `shipped-artifact`, confidence `observed`). Re-generated this task; retained in session. |
| **E2** | `ev_34eea0146f1ecf619f80c64ef05151d1a5ca24946bc9396129525d88c9a11758` — REA `inspect_with_objdump` **section headers**, same SHA; confirms VMA == file offset for the image (`.dynsym` VMA `0x318` / file off `0x318`, etc.). |
| **E3** | `ev_af5de5324a9e1df40ada21a17ab9fe86a747fc6c81a0421574c952f5d077eba4` — engine identity (prior REA pass, `analysis/rea/report.md` §5). |
| **E4** | `ev_12b97ce17ac58a5e2de32bd40589deb2ac7d32100927239855d0d834e02873e6` — REA runtime capture of the failing app-shell harness (worker-1). |
| **E5** | Local, reproducible, SHA-bound static derivations: `objdump -d -M intel` (E1 mirrors this), raw-byte scan of `ad db de c0`, and APS2 decode with `analysis/bootstrap/decode_aps2.py` (blob at file offset `0x15e00`, length `0x1eb1f2`, magic `APS2`, **565,390 / 565,390 relocations decoded**, `r_info & 0xffffffff == 0x8` = `R_X86_64_RELATIVE`, addend = linked function VMA). Local dumps retained at `analysis/dispatch/helper_233319a.dis` and `analysis/dispatch/helper_cluster.dis`. |

Malformed/sectionless-ELF caveats from prior passes do not apply here: this artifact has a full section table (E2). REA's Ghidra deep-provider calls on this 120 MB image were interrupted by the transport budget, so all disassembly claims are backed by E1/E2 plus local `objdump` (E5); no Ghidra-only evidence is claimed.

## 1. Headline answer

> **The 0xc0dedbad handle type is allocated through function-pointer slot `0x726c0c0`.** Its **APS2 static addend is `0x253225e`** (a raw `calloc(count,size)`), but **at runtime the slot is rebound to `0x231f0e2`** — a zeroing `calloc` wrapper installed at `0x231e915` (full evidence in §9). The known constructor is `0x231f31f`: `call QWORD PTR [0x726c0c0]` at RVA **`0x231f33d`** with `(rdi=1, rsi=0x1100)`, immediately followed by the magic writer `mov DWORD PTR [rax],0xc0dedbad` at RVA **`0x231f34b`**. The constructor never writes `[obj+0x68]`. **Correction after team runtime evidence (§9): this constructor is NOT reached in the app-shell startup path (hit counter = 0); it establishes the object type/layout, but the object actually consumed at `0x23331ae` is a different instance — the fault value is not any of the three zeroed 0x1100 handles' `+0x68`.**

The ~37 `call *(%r15)` sites in the helper cluster at `0x233319a..0x2333e00` are **all the guest `free` bridge** (slot `0x726c0b8` → `0x1dbe559`, patched to native `host_free` by phase-2 bootstrap). None of them allocate. They are the *consumer/teardown* side, not the creator.

## 2. Function-pointer table around 0x726c0b8 (APS2 addends, E5)

Decoded `R_X86_64_RELATIVE` slots in `0x726c000..0x726c500` (115 slots). The allocator/helper dispatch block is `0x726c080..0x726c0d0`:

| Slot VMA | APS2 addend | Identification |
| --- | --- | --- |
| `0x726c080` | `0x6a33e2c` | guest allocator helper |
| `0x726c088` | `0x6a3419b` | guest allocator helper |
| `0x726c090` | `0x6a3458a` | guest allocator helper |
| `0x726c098` | `0x6a346db` | guest allocator helper |
| `0x726c0a0` | `0x6a2db1d` | guest allocator helper |
| `0x726c0a8` | `0x6a3480e` | guest allocator helper |
| `0x726c0b0` | `0x227aafa` | **reallocate** bridge |
| **`0x726c0b8`** | **`0x1dbe559`** | **free** bridge — the `r15` slot in the helper cluster |
| **`0x726c0c0`** | **`0x253225e`** | **`calloc(count,size)` — THE 0xc0dedbad ALLOCATOR (see §5)** |
| `0x726c0c8` | `0x1dbad62` | small-allocate bridge |
| `0x726c0d0` | `0x2a0d56d` | guest allocator helper |
| `0x726c300`, `0x726c310` | `0x720d1d0` | global vtable pointer; consumer calls `[[0x726c300]+0x78]` at `0x23332be` |

Secondary allocator table (same decode): `0x726b3c8` → `0x1dbab02` (**allocate** bridge); `0x726bb20` → `0x1dbad62`, `0x726bb30` → `0x227aafa`, `0x726bb38`/`0x726bb40` → `0x1dbe559`, `0x726bb48` → `0x1dbad62`, `0x726bb50` → `0x227aafa`.

### Runtime (re)installation of these slots — E1/E5

APS2 supplies the initial values, and guest code rewrites the five live bridge slots during allocator bootstrap:

```
231e8f9: mov [0x726c0c8], rsi     ; small-allocate
231e900: mov [0x726c0b8], rdx     ; free
231e907: mov [0x726c0d0], r8
231e90e: mov [0x726c0b0], rcx     ; reallocate
231e915: mov [0x726c0c0], r9      ; allocate/calloc  <-- creator slot installed here
```

A second writer re-affirms the same functions:

```
6a35e14: mov [0x726c0b0], rax
6a35e22: mov [0x726c0d0], 0x2a0d56d
6a35e30: mov [0x726c0c0], 0x253225e   ; exactly the APS2 addend for the creator slot
```

So the addend-to-slot mapping is not just a static artefact: the guest installs `0x253225e` into `0x726c0c0` at runtime.

## 3. The `call *(%r15)` sites in helper 0x233319a and sibling helpers (E1/E5)

`r15` is loaded **four** times in the cluster `0x233319a..0x2333e00`, every time from the same slot:

```
2333255: lea r15,[rip+0x4f38e5c]  # 0x726c0b8
2333827: lea r15,[rip+0x4f38e...] # 0x726c0b8
23338ff: lea r15,[rip+0x4f38e...] # 0x726c0b8
2333d33: lea r15,[rip+0x4f38e...] # 0x726c0b8
```

**37 `call QWORD PTR [r15]` sites** (all → `0x726c0b8` → free bridge `0x1dbe559`):

```
2333263 23332c7 233331a 2333342 2333353 2333364 2333388 23333e0
2333461 233347c 233348d 233349e 23334af 23334c0 23334d1 23334e2
23334f3 2333504 2333515 2333526 2333537 2333566 2333585   (23 in 0x233319a itself)
2333848 233390d 2333921 2333932 2333943 2333954 2333965 2333976 2333987 23339a8
2333d42 2333d65 2333de8 2333df5                            (14 in the sibling helpers)
```

Plus one more load/call of the **same slot** via `rax`:
```
233323d: lea rax,[rip+...] # 0x726c0b8
233324b: call QWORD PTR [rax]      ; also the free bridge
```

**Other indirect dispatch inside 0x233319a (not the allocator table) — E1:**

| Call site | Target source | Meaning |
| --- | --- | --- |
| `0x23332be` | `rax = [[0x726c300] + 0x78]` | global vtable callback (slot `0x726c300` → `0x720d1d0`) |
| `0x23332f9` | `rax = [rbx+0xea8+0]` (vtable set to `0x2457797` at `0x6a36261`) | per-object virtual callback |
| `0x233341e` | `r8 = [[rbx+0x78] + 0x10]` | per-object virtual callback |
| `0x2333443` | `rcx = [[rbx+0x78] + 0x18]` | per-object virtual callback |

None of these are allocation sites. This resolves the earlier question (decision 025 / `refbase-layout.md`): the `call *(%r15)` sites are allocator calls, there is no RefBase vtable at `0x726c0b8`, and DECISION C's conclusion stands.

## 4. The creation → consumption chain (all E1/E5 unless noted)

```
231e915         install 0x253225e into slot 0x726c0c0            (bootstrap)
231f294         singleton getter  [callers: 0x231f0ac, 0x23200cd, 0x234c377, 0x6a26e73]
231f2e1  ->  call 0x231f31f  (first-use construction)
  231f32c  lea  rax,[rip+...]  # 0x726c0c0         ; allocator slot
  231f333  mov  edi,1                              ; count
  231f338  mov  esi,0x1100                         ; size
  231f33d  call QWORD PTR [rax]                    ; *** ALLOCATE: 0x253225e(1,0x1100) ***
  231f348  mov  r14,rax
  231f34b  mov  DWORD PTR [rax],0xc0dedbad        ; *** MAGIC WRITE: object IS the handle ***
  231f351..  initialise +8,+0x960,+0x949,+0x998,+0x80,+0x88,+0x90,+0x98,
             +0xa0,+0xa8,+0x970,+0x980,+0x988,+0xe90/+0xea0/+0xeb0,
             +0xb0..+0x180 (memset 0xd0), +0x1008,+0x1010,+0x1018,+0x1058 ...
             -- NOTE: +0x68 is NEVER WRITTEN here -> uninitialised
  231f430  mov  [rbx],r14                          ; return handle to singleton holder
23200cd      call 0x231f294 ; 23200d2 store handle at [rbx+0x90]
2320039      mov  rax,[rbx+0x90]                   ; reload handle
232004f      mov  QWORD PTR [rax+0x68],0x0        ; *** guest ZEROES +0x68 on THIS path ***
2320057      call 0x233319a                       ; teardown consumer (clean path)
23330e7      consumer entry (other callers: 0x2457721, 0x6a26ef2)
2333109      cmp  DWORD PTR [rdi],0xc0dedbad      ; validate handle magic
2333168      call 0x233319a
  23331ae  mov  rbx,[rdi]                          ; rbx = handle
  23331ca  mov  rdi,[rbx+0x68]                     ; *** READ the uninitialised field ***
  23331d6  call 0x245605d                         ; validator -> fault with garbage pointer
  2333585  call QWORD PTR [r15]                   ; free bridge (after the damage)
```

### Key consequences

1. **Allocation site:** slot `0x726c0c0` / addend `0x253225e`, call at `0x231f33d`, size `0x1100`, magic writer `0x231f34b`, singleton constructor `0x231f31f`.
2. **The uninitialised field:** `0x231f31f` writes `+0` (magic) and many other fields but **not `+0x68`**. The value read at `0x23331ca` is whatever the guest allocator left in that word — consistent with worker-1's finding that it is stale code bytes, ASLR-invariant, and already non-zero when the lazy-link guard `0x2350044` runs (so the linker skips).
3. **The guest already knows how to fix it on one path:** at `0x232004f` it explicitly writes zero to `[handle+0x68]` before calling the consumer at `0x2320057`. The failing app-shell path reaches the consumer through `0x23330e7` (callers `0x2457721`, `0x6a26ef2`) and does **not** pass through `0x232004f`. This is a path-specific missing initialisation/cleanup, on top of the uninitialised allocation — worth checking with the runtime trace before deciding between "zero at allocation" and "zero at the app-shell handoff".
4. The lazy-link writer `0x23500c6` (`mov [A+0x68],B`) has **zero static callers** (prior worker-1 backward trace; re-confirmed: no direct `e8`, pointer, or APS2 addend to `0x234fff8`), so it is not the fix path for this object.

## 5. Address of the allocator target 0x253225e (E1/E5)

```
253225e: push rbp ; mov rbp,rsp ; push r14 ; push rbx
2532265: mov rbx,rsi                         ; size
2532268: mov r14,rdi                         ; count
253226b: mov edi,[0x7282000]                 ; arena key
253227a: call pthread_getspecific@plt        ; thread-local arena (fallback 0x721db80)
253228d: cmp r14,1
2532293: ... mul rbx ...                     ; count * size (overflow-checked)
253229e: cmp rbx,0x400                       ; small-size bin
25322a7: ... lea rax,[rbx+7] ; and rax,~7 ; mov rcx,[rdi+rax+0xe8] ; ...
```

This is a `calloc`-style allocator (thread-local arena, count×size, size-class bins). It returns zeroed memory *if* the arena path zeroes; the observed stale bytes show that, on this build/path, the returned `+0x68` word is **not** reliably zero. This is the concrete allocation entry the runtime tracer (worker-1) should breakpoint to capture the allocating stack.

## 6. Secondary magic writer (not the creator) — E5

`0x6a36295: mov DWORD PTR [rbx],0xc0dedbad` lives inside a teardown/re-init routine that immediately frees fields via the `0x726c0b8` bridge (`0x6a362bc`, `0x6a362c9`, `0x6a362ec`) and calls `0x2333d22`; it restores the magic rather than allocating the object. Do not target it as the creator.

## 7. Handoff summary for the team

- **worker-1 (runtime trace):** set the allocation-site watchpoint/breakpoint at `0x231f33d` (or on target `0x253225e`) when the handle is created, plus a data watchpoint on `[handle+0x68]`; expect the creating stack rooted at `0x231f31f` ← `0x231f2e1` ← `0x231f294`. gdb is now installed in this environment (`/usr/bin/gdb`), unlike the earlier passes.
- **worker-2 / worker-3:** the creator slot is `0x726c0c0` (addend `0x253225e`), size `0x1100`, magic writer `0x231f34b`, field `+0x68` left uninitialised. A zero-fill at the allocation bridge or an explicit zero of `[handle+0x68]` at the app-shell handoff are both statically supported; prefer the narrowest change. Do not patch the free-bridge `call [r15]` sites (DECISION C).
- **lead:** DECISION C is reinforced — slot `0x726c0b8` is the free bridge, not a vtable; the allocating entry is the adjacent slot `0x726c0c0`.

## 8. Limits

- Static analysis only; no runtime capture of the allocation stack in this task (worker-1 owns that). The claim "slot `0x726c0c0` allocates the 0xc0dedbad handle" is based on the direct `allocate → magic write` adjacency at `0x231f33d`/`0x231f34b`, the matching field layout with the consumer, and the runtime installation of `0x253225e` into that slot.
- `0x231e8f9..0x231e915` installs bridge pointers from registers; their exact provenance (which host shim supplies r9) is not resolved statically.
- Ghidra deep-provider evidence ids were not obtained (transport budget on the 120 MB image); all findings are backed by REA objdump evidence E1/E2 and local E5.

## 9. Runtime corrections and one-byte-misalignment check (added on lead's request)

### 9.1 The allocator slot is rebound at runtime — `0x726c0c0` holds `0x231f0e2`, not the APS2 addend (E1/E5 + team runtime dumps)

- Static APS2 addend for `0x726c0c0`: `0x253225e` (raw `calloc`). **Runtime dump at the `0x2456079` fault (worker-3, `SOBER_DUMP_HANDLE=1`): slot `0x726c0c0` = `guest_base + 0x231f0e2`.**
- `0x231e915` installs `r9` into `0x726c0c0`; at app-shell time that value is `0x231f0e2`. Disassembly of `0x231f0e2` (E1) shows a **zeroing calloc wrapper**:

```
231f0e2: push rbp; mov rbp,rsp; push rbx; push rax
231f0e8: test rsi,rsi                 ; size == 0 -> fail
231f0ed: mov rbx,rsi                  ; rbx = size
231f0f0: mov eax,0x4001
231f0f5..231f10b: overflow-checked count*size -> rbx
231f10d: imul rbx,rdi
231f111: mov rdi,rbx
231f114: call 0x231f13a               ; internal allocator (calls 0x1dbab44 at 0x231f1a4)
231f119: test rax,rax ; je 0x231f131
231f11e: mov rdi,rax; xor esi,esi; mov rdx,rbx
231f12c: jmp memset@plt               ; tail-jump: memset(ptr, 0, count*size)
```

- Therefore the **real app-shell allocation path is `0x231f33d`-style calls through the rebound slot → `0x231f0e2` → `0x231f13a` → `0x1dbab44`, then zero-filled**; `0x253225e` is the *pre-rebind* target and is never the live slot value in our harness. Worker-2's 7th bridge at `0x253225e` consequently saw zero hits; their 8th bridge at `0x231f0e2` is the one that is hit (three 0x1100 allocations reported at guest RVA `0x231f33f`, e.g. `ptr=0x7fe8f406f890`).
- **Creation-site reachability:** worker-3's opt-in hit counter at the magic writer `0x231f34b` recorded **0 hits** in the app-shell startup path. `0x231f31f` lives in `JNIWebLoginProtocol_init` — it documents the handle layout but is not the creator of the object consumed at `0x23331ae` in our run. Do not treat `0x231f33d`/`0x231f34b` as a patch target for the strict gate.

### 9.2 Magic-writer inventory (E5) — two sites, only one allocates

| Writer RVA | Instruction | Role |
| --- | --- | --- |
| `0x231f34b` | `mov DWORD PTR [rax],0xc0dedbad` | primary creator (`0x231f31f`), right after the `0x1100` allocation; **not reached in app-shell path** |
| `0x6a36295` | `mov DWORD PTR [rbx],0xc0dedbad` | secondary re-initialisation inside a teardown routine (free-bridge calls at `0x6a362bc`, `0x6a362c9`, `0x6a362ec`, then `call 0x2333d22`); restores the magic, does not allocate |

The other 15 `0xc0dedbad` immediates in the image are comparisons/flag tests (`0x2331bdf`, `0x23325e6`, `0x2333109`, `0x234ad27`, `0x2350034`, `0x2456091`, `0x257cf49`, `0x25828cc`, `0x6a35efe`, `0x6a36339`, `0x6a365a4`, `0x6a399e4`, `0x6a3dab0`, `0x6a3db66`, `0x6a3e51c`).

### 9.3 Free sites (E1/E5)

`0x233319a` itself contains **23** `call QWORD PTR [r15]` sites (list in §3, first row group); the sibling helpers `0x2333827..0x2333e00` add 14 more (**37 total** in the cluster). Every one of them resolves to slot `0x726c0b8` → free bridge `0x1dbe559` (DECISION C). Plus the `call [rax]` at `0x233324b`, same slot.

### 9.4 One-byte-misalignment check (lead's request) — negative result, with exact counts

- Search for the 7-byte core `4c 89 85 30 ff ff ff` (`mov [rbp-0xd0],r8`): **192 occurrences** in the SHA-bound image; first at RVA `0x1dbfb80`, last at `0x6c85e06`.
- The full stale 8-byte value `ff 4c 89 85 30 ff ff ff` occurs **42 times** (first `0x1dbfb7f`, last `0x6c85e06`); in each case the value begins at the `0xff` byte immediately preceding a core occurrence (the tail of `push -1` / `6a ff`), consistent with worker-1's decode.
- For every occurrence and shifts `-1 / 0 / +1`, I tested whether the object base implied by `stale_offset - 0x68` holds the magic `0xc0dedbad` at `+0`: **zero matches**. No static image location has both magic at `+0` and the stale bytes at `+0x68`, so there is no recoverable one-byte-shifted static base.
- This agrees with lead's independent scan and with the team's zero-signal field log (worker-2/worker-3): the three zeroed 0x1100 handles have `+0x68` = `0`, `0`, or a valid guest-heap pointer — **never** `0xffffff3085894cff`. The fault value is therefore not a post-allocation copy into those objects; the consumer dereferences a **different, bogus object pointer** (possibly a pointer into guest code, which is why its `+0x68` reads as code bytes). The decisive remaining question is who supplies the object pointer at `0x23331ae`, not which slot allocates it.

### 9.5 Net correction to this map

The dispatch table facts (§§2–4) stand, with the following amendments:
1. Slot `0x726c0c0` — static addend `0x253225e`, **runtime target `0x231f0e2`** (zeroing wrapper). Cite both.
2. `0x231f33d` / `0x231f34b` are type-defining but **not app-shell-reachable** (hit counter 0).
3. The `+0x68` stale bytes are **not** explained by an uninitialised allocation in this run; they are read from a wrong/bogus object pointer. Do not patch allocation/zeroing on that basis (consistent with DECISION C and the strict gate).

## 10. The function that zeroes `[handle+0x68]` at 0x232004f (lead's follow-up)

**Enclosing function: entry `0x231fd7e`, ends `0x23200cc` (`ret`).** Prologue `push rbp; mov rbp,rsp; push r15/r14/r13/r12/rbx; push rax; sub rsp,0x28`; epilogue `add rsp,0x28; pop rbx/r12/r13/r14/r15/rbp; ret`. Exactly one static caller: `call 0x231fd7e` at `0x231e70e` (E1/E5).

**Argument contract (SysV, at entry):** 5 arguments — `rdi`, `rsi`, `rdx`, `rcx`, `r8`. Saved as `rbx=rdi`, `r14=rsi`, `r15=rdx` (also `[rbp-0x50]`), `r12=rcx`, `r13=r8`. `rdi` is the **container object** that receives the new backing object; the handle pointer is later read from `[rbx+0x90]`.

**What it initialises (E1), in order:**
1. `0x231fd9f`–`0x231fdb0`: `lea rax,[rip] # 0x726c0c0; mov edi,1; mov esi,0x2e0; call [rax]` — allocates a **0x2e0-byte object through the same (rebound) calloc slot** (note: a *different* object from the 0x1100 handle).
2. `0x231fdc2`: `mov DWORD PTR [rax],0xbab1e` — this new object is the **backing object B** (magic `0xbab1e` from worker-1's lazy-link analysis).
3. Initialises ~40 fields of B: `+0xd8=0`, `+0xe0=0x6a37110`, `+0xe8=0x2358a78`, `+0xf0=0x6a37821`, `+0xf8=rcx`, `+0x100=0`, `+0x158/+0x160/+0x168/+0x170 = 0/0x6a40587/0x6a40593/0x6a4059d`, `+0x178=rsi`, `+0x180=0`, `+0x188/+0x1a8` zeroed, `+0x1b8=0`, `+0x10..+0x30` zeroed with `+0x18=0`, `+0x28=-1`, `+0x30..+0x60` zeroed, `+0x1c0=rax`, `+0x1c8=rdx`, `+0x1d0=r8`, `+0x1d8=0x1dd72a0`, `+0x1e0=0x17`, `+0x1e8=rax`, `+0x70..+0x80` zeroed, `+0x2d8 |= 2`, `+0x2d0=0x64`, `+0x2b0=-1`, `+0x40..+0x60` zeroed.
4. `0x231ff12`–`0x231ff65`: five calls to `0x2320211` and one to `0x232029e` (registration helpers); failure falls through to `0x23200cd`.
5. `0x23200cd`: `call 0x231f294` (the 0xc0dedbad singleton getter); `0x23200d2`: `mov [rbx+0x90],rax` — stores handle **A** into the container.
6. `0x2320039`: `mov rax,[rbx+0x90]` (reload A); `0x2320045`: `rdi = rbx+0x90` (address of the holder that the consumer expects); **`0x232004f`: `mov QWORD PTR [rax+0x68],0x0`** — detaches/clears A's back-pointer; `0x2320057`: `call 0x233319a` with `rdi = &container.handle_slot`.
7. `0x232005c`–`0x23200b6`: frees B's sub-objects via the `0x726c0b8` free bridge, zeroes their slots, returns 0.

**Why the failing app-shell path skips it:** the consumer `0x233319a` has two independent callers. The safe path is `0x231fd7e → 0x2320057`, which performs step 6's zero. The app-shell path is `0x23330e7 → 0x2333164/0x2333168` (`lea rdi,[rbp-0x48]; call 0x233319a`), which reaches the same consumer **without** passing through `0x231fd7e`, so `[handle+0x68]` is never cleared and the validator at `0x23331d6 → 0x245605d` runs on the stale value.

**Can we call/replicate it?** Do **not** call `0x231fd7e` wholesale — it allocates a 0x2e0 object and performs global registrations/step-3 initialisation with side effects. Replicate only step 6's writer-side seed at the app-shell call site (as worker-1 proposed): at `0x2333164`, preserve the original `lea rdi,[rbp-0x48]`, compute `obj = [rdi]`, write `QWORD 0` to `[obj+0x68]` (and `[obj+0x20]`), restore `rdi`, then continue into the original `call 0x233319a` at `0x2333168`. Argument contract for worker-3: `rdi` = address of the handle slot (`rbp-0x48` in `0x23330e7`'s frame); the handle object is `*(rdi)`; all other registers must be preserved. This mirrors the guest's own `0x232004f` exactly and is a writer-side seed, not a reader-side skip.

## 11. Static provenance of the object pointer consumed at 0x23331ae (E1/E5)

`0x233319a` reads `rbx = [rdi]` at `0x23331ae`. Its two direct callers pass a *holder* whose word 0 is the handle:

- `0x2320057` (safe path): `rdi = rbx+0x90`, where `[rbx+0x90]` = result of singleton getter `0x231f294` (the 0xc0dedbad object A).
- `0x2333168` (failing path): `rdi = rbp-0x48` in `0x23330e7`, whose word 0 was validated as magic `0xc0dedbad` at `0x2333109`. So `0x23330e7`'s **first argument** is the object pointer directly (it validates `[rdi]`), and it is re-wrapped into a local holder for the consumer.

Callers of `0x23330e7` (only two, E5):
- `0x2457721` inside `0x2457702`: `rbx = rsi` (second argument) and `rdi = rbx` at `0x245771e`. Its sole caller is `0x234ddc7`, where `rsi = [rbx+0x460]` (`0x234ddc0`) — i.e. the handle is a QWORD field at offset **`0x460`** of a large guest object (`rbx`), and that field is explicitly cleared at `0x234ddf4` after the call.
- `0x6a26ef2` inside `0x6a26ed9`: passes `r14` (no direct/static caller found — likely reached via an indirect/jump-table dispatch).

If the consumed object is a wrong/bogus pointer (worker-1's current theory), the two concrete static sources to inspect are the `[big_object+0x460]` slot loaded at `0x234ddc0` and the `r14` value at `0x6a26ef2`. This is a pointer-provenance question, not an allocation/zeroing one.

**Runtime confirmation (worker-3) and identification of the bogus value:** their opt-in writer-side stub at `0x2333164` faulted writing `[obj+0x68]` with `obj = guest_base + 0x233263f` — i.e. the "object pointer" stored in the app-shell slot is a **pointer into guest `.text`**, not heap memory. Static identification of that exact RVA (E1/E5): `0x233263f` is the **common bail-out/return block of function `0x2332577`**, reached by `je 0x233263f` at `0x23325e2` (null handle) and `jne 0x233263f` at `0x23325ea` (magic `0xc0dedbad` mismatch at `0x23325e4`); it contains `mov rcx,[rbx]; cmp rcx,[rbp-0x10]; ... ret`. Function `0x2332577` has 20+ callers (`0x2331417`, `0x245469f`, `0x245486e`, `0x2456c04`, `0x2457320`, `0x245739d`, `0x24574c1`, `0x24574ec`, `0x2457510`, `0x2457534`, `0x2457558`, `0x245757c`, `0x24575ae`, `0x24575d5`, `0x2581848`, `0x2581a4b`, `0x6a270b5`, `0x6a27767`, `0x6a28224`, `0x6a282e1`, `0x6a2a995`, `0x6a2a9cf`). There is **no static data reference / APS2 addend / `lea` to `0x233263f`** in the image, so the value in the slot is a runtime-generated code pointer — most plausibly a **stale return address** from one of those call sites being stored where an object pointer belongs. Refined (E1): `0x233263f` is precisely the **return address of `call 0x2332657` at `0x233263a`** (`0x233263a + 5 = 0x233263f`); `0x2332657` is a large switch dispatcher (jump table at `0xd371d8`, switch on `(esi & 0xf00000) + 0xfff00000 >> 20`, 6 cases). The value is therefore stack residue from this helper, captured into a data slot at runtime. The fix must correct the pointer written into the app-shell holder (the `[big_object+0x460]` slot at `0x234ddc0`, or `r14` at `0x6a26ef2`), not any `+0x68` field.

**Live app-shell call chain resolved (E1 + lead's call-chain trace):** lead's full trace ends at `call 0x23330e7` at **`0x6a26ef2`**. Disassembly of that enclosing function (entry `0x6a26e5c`) shows:

```
6a26e73: call 0x231f294          ; *** the 0xc0dedbad singleton getter ***
6a26e78: mov  r14,rax            ; r14 = getter's return value
6a26e7b: mov  [rbx],rax
6a26e9d: mov  [rbx+8],<0x20 wrapper with vtable 0x720b938, +0x18 = r14>
6a26ea1: test r14,r14
6a26ea4: je   0x6a26ed9          ; cold path
6a26ed9: (cold path, no new prologue - r14 still live)
6a26ee2: call 0x277884e
6a26eea: call 0x2ea4f90          ; both preserve r14 (push r14 in their prologues)
6a26eef: mov  rdi,r14
6a26ef2: call 0x23330e7          ; F_inner -- the failing consumer wrapper
```

Both intervening callees preserve `r14`, so **the bogus object pointer passed to `0x23330e7` is exactly the return value of the guest's own singleton getter `0x231f294`** (captured into `r14` at `0x6a26e78`). `0x231f294` returns `[rbp-0x18]`, the output slot written by constructor `0x231f31f` (`mov [rbx],r14` at `0x231f430`) from the allocation through slot `0x726c0c0`. Therefore the corrupted state is in the **singleton's cached handle / the value the getter returns** — not in a random caller slot. This is why substituting a synthetic object at the call site cannot be sufficient: the guest subsequently frees/queries it through bridges expecting the original guest-arena object.

**One-line fix at the call site (E1):** `0x23330e7` stores its RDI into `[rbp-0x48]` at `0x2333100`; that slot is the bad pointer. At `0x2333164`, instead of touching `[obj+0x68]`, **replace the slot itself**: obtain a canonical handle from the nullary guest singleton getter `0x231f294` (called exactly that way at `0x23200cd`, result stored as a handle at `0x23200d2`) and write `mov [rbp-0x48], rax` before the original `lea rdi,[rbp-0x48]; call 0x233319a` at `0x2333164/0x2333168`. This injects the same valid object the safe path uses and leaves all other guest logic intact. The alternative (worker-1) is to store a host-side synthetic `0xc0dedbad` object into the slot from the bridge, but the guest getter needs no host dependency.
