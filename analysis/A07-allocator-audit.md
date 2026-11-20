# A7 — Allocator bridge audit: guest malloc / calloc / host_calloc / free thunks

Worker: **A7** (logical Team A). Owner scope: **this file only**. No `src/`,
`CMakeLists.txt`, `tests/`, gate or engine edits were made. Analysis only.

Target: `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
Engine bytes **not modified** (all Ghidra/objdump work was read-only; the one
`open_binary` attempt with the stale snapshot was rejected on profile mismatch
and opened no session).

Strict decision 013 is untouched: nothing here creates a callback or an
`app shell rendered` marker. All proposed fixes below are **default-OFF opt-ins**
for the lead to enact in `src/`.

---

## 0. Executive summary

The guest has **two incompatible allocation contracts**, selected by a runtime
rebind of its allocator function-pointer table. Our bridge set implements both,
but with three deviations:

| ID | Finding | Severity | Start/game path impact |
| --- | --- | --- | --- |
| **M1** | `host_calloc` writes `total` (`n+0x10`) into the 16-byte block header; the guest's own calloc writes **0** there for normal sizes. The matched calloc-free thunk `0x231f53a` branches on that header. | **Medium** (latent SIGSEGV; one-line fix) | The rebound free slot used by the helper cluster is exactly `0x231f53a`; any run where the two header-gated feature flags are set dereferences `0x1110`-class values as a handle. |
| **M2** | The 7th bridge at `0x253225e` is a **plain-pointer** calloc, but it is redirected to the same `host_calloc` that returns `base+0x10`. | Low now (zero hits after rebind), real if the secondary install path runs | Off-by-`0x10` object pointer handed to guest logic. |
| **M3** | `host_free`, `host_reallocate`, `host_usable_size` never apply the `-0x10` that the calloc free thunk applies. A calloc-returned pointer passed to any of them is an **interior glibc pointer**. | **Medium–High** | Same failure family as the already-observed `realloc(): invalid pointer` abort (`analysis/tsd-static-buffer-abort.md`); would surface as `free(): invalid pointer` / `realloc(): invalid pointer` on the start/game path. |
| **M4** | `host_allocate` returns plain pointers; after the guest rebind, the free slot holds `0x231f53a`, which subtracts `0x10` unconditionally before reaching our `host_free`. | Conditional — needs runtime confirmation | Any plain host-allocated object freed through the rebound slot becomes `p-0x10`. |
| **M5** | (corroborating, previously reported) `SOBER_STUB_TSD_SEED` seeds a static BSS buffer into a slot the guest later `realloc`s. | Known | `realloc(): invalid pointer` + SIGABRT; confirms the "wrong-pointer-family into host allocator" failure mode. |

**Bottom line:** the calloc/free pair itself is *structurally* matched after the
guest's rebind (`0x726c0c0 → 0x231f0e2` alloc, `0x726c0b8 → 0x231f53a` free),
so the common 0x1100-handle path does **not** currently prove a free/alloc
mismatch. The real, concrete deviations are the **header value (M1)** and the
**missing `-0x10` awareness in the other three host bridges (M3)**, plus the
mis-typed 7th bridge (M2). Minimal opt-in fixes are listed in §5.

---

## 1. Method and evidence provenance

All guest RVAs below were read directly from the frozen engine with
`objdump -d --no-show-raw-insn --start-address=... --stop-address=...` (LOAD#0 has
`offset == vaddr`, so RVA == virtual address). Cross-checked against the prior
static artifacts: `analysis/dispatch/dispatch-map.md` §9,
`analysis/trace/trace-report.md`, `analysis/handle/refbase-layout.md`,
`analysis/bootstrap/bootstrap-map.md`, `docs/decisions/028…/029…`.

Evidence is **static observation** of shipped bytes plus **code-level** review of
`src/compat/bionic_bootstrap/bionic_bootstrap.c` and `bootstrap_profile.h`. Where
I infer runtime reachability I say so explicitly. No claim here comes from target
execution.

---

## 2. The guest's two allocation contracts

### 2.1 Contract A — plain block pointer (no header)

- `allocate` wrapper `0x1dbab02` → `call 1dbab44` (arena allocator core).
- `small-allocate` `0x1dbad62` → TLS arena, returns the arena block pointer
  directly (`1dbadba..1dbadbc` ret, no `add $0x10`).
- Raw calloc `0x253225e` → arena block, `memset@plt` tail-jump at `0x25322ea`;
  **returns the plain block pointer**, no header.
- Matched free: `0x1dbe559` (arena free). Observed prologue:
  ```
  1dbe559: mov %rdi,%rsi          ; rsi = p (used verbatim)
  1dbe563: mov %rdi,%rcx ; shr $0x1d,%rcx   ; arena = table[p>>29]
  1dbe590: mov 0x18(%rdi),%rax ; mov %rax,(%rsi) ; mov %rsi,0x18(%rdi)
  ```
  There is **no `sub $0x10`** here. `free core 0x1dbe5be` tail-calls `0x1dbe559`
  at `0x1dbe61f` and calls it at `0x1dbe660`.

### 2.2 Contract B — `n+0x10` block, 16-byte header, return `base+0x10`

Runtime-rebound calloc `0x231f0e2` (`JNIWebLoginProtocol_init+0x743`) is a
wrapper: `n = nmemb*size` with overflow check, `call 0x231f13a`, then
`memset(ptr, 0, n)` (`0x231f12c` tail-jump). The core `0x231f13a` is:

```
231f19d: add    $0x10,%r14          ; r14 = n + 0x10
231f1a1: mov    %r14,%rdi
231f1a4: call   1dbab44             ; allocate(n+0x10)
231f1a9: mov    %rax,%r12           ; r12 = base
231f1ac: xor    %r14d,%r14d         ; <=== header value = 0 (normal sizes)
231f1af: jmp    231f23e
...
231f23e: test   %r12,%r12
231f241: je     231f24d
231f243: mov    %r14,(%r12)         ; [base] = 0
231f247: add    $0x10,%r12          ; return base + 0x10
...
231f261: mov    %r12,%rax
```

For the two special sizes (`cmp $0x4000,%r14` at `0x231f17c`,
`cmp $0x10000,%r14` at `0x231f185`) the guest uses a custom allocator and stores
its **handle** in the header instead of 0. For the ordinary 0x1100 handle size,
the header is **0**.

Matched free thunk `0x231f53a` (`JNIWebLoginProtocol_init+0xb9b`):

```
231f53a: test   %rdi,%rdi
231f53d: je     231f56c
231f53f: mov    %rdi,%rsi
231f542: add    $0xfffffffffffffff0,%rsi     ; <=== base = p - 0x10
231f546: cmpb   $0,0x7cdd149                 ; feature flag A
231f54d: je     231f564
231f54f: cmpb   $0,0x7839dc8                 ; feature flag B
231f556: je     231f564
231f558: mov    (%rsi),%rdi                  ; header value
231f55b: test   %rdi,%rdi
231f55e: jne    6a28bf4                      ; nonzero -> special free (treats it as a handle)
231f564: mov    %rsi,%rdi
231f567: jmp    1dbe5be                      ; zero -> free base via 0x1dbe559 (our host_free)
```

So the guest's header is **a 0/custom-handle discriminator**, not a size field.
When both flags are 0 (the normal harness case), the header is ignored and
`base` is freed correctly. When a flag is set, a nonzero header is consumed as a
handle by `0x6a28bf4`, which does `add $0x10,%rdi` (`0x6a28c1e`) and
`call 24bc480` (`0x6a28c22`) on it — i.e. it dereferences the value as an address.

### 2.3 The runtime rebind (why the two contracts are swapped mid-startup)

`0x231e69a..0x231e6c2` loads a 7-function family and calls registrar
`0x231e88c`, which writes (confirmed disassembly at `0x231e8f9..0x231e915`):

| Slot | Value installed at RVA `0x231e8f9..` | Role |
| --- | --- | --- |
| `0x726c0c8` | `0x231f13a` | calloc **core** (Contract B allocation) |
| **`0x726c0b8`** | **`0x231f53a`** | **calloc free thunk (`-0x10`)** — replaces `0x1dbe559` |
| `0x726c0d0` | `0x234df2a` | (other) |
| `0x726c0b0` | `0x234f716` | realloc-family |
| **`0x726c0c0`** | **`0x231f0e2`** | **calloc wrapper (Contract B allocation)** |

This happens **after** `apply()` (bridges + `patch_calloc_slot()`) in
`run()`: `bionic_bootstrap.c:1330-1331` install the bridges and write `&host_calloc`
into slot `0x726c0c0`; `run()` at `:1395-1402` then calls
`sober_bionic_bootstrap_replay_constructors()`, where the guest's rebind runs and
**overwrites our slot write**. Our separate 12-byte trampoline at `0x231f0e2`
still catches every call through the rebound slot, so this is benign — but it
means the guest's **free slot is the `-0x10` thunk `0x231f53a`**, not the
`0x1dbe559` code we patched.

A concrete matched pair on the start/game path (helper cluster,
`analysis/dispatch/helper_cluster.dis`):

```
2333ac5: lea    rax,[rip+...]        # 726c0c0   (calloc slot)
2333acc: shl    rsi,0x3
2333ad0: mov    edi,0x1
2333ad5: call   QWORD PTR [rax]      ; calloc(1, count*8) -> host_calloc -> base+0x10
2333ad7: mov    r14,rax
...
2333b87: lea    rax,[rip+...]        # 726c0b8   (free slot)
2333b8e: mov    rdi,r14
2333b91: call   QWORD PTR [rax]      ; free via 0x231f53a -> base -> host_free
```

This pair is consistent **in the guest's design**; it is the proof that the
rebound free thunk, not the plain free bridge, is the live free path in the
helper cluster.

---

## 3. What our bridges actually do (source-level, `bionic_bootstrap.c`)

| Bridge RVA (`bootstrap_profile.h`) | Host fn | Layout returned | Adjustment on free/realloc/size |
| --- | --- | --- | --- |
| `0x1dbab02` allocate | `host_allocate` `:271-277` | plain `malloc(n)` + zeroed | none (Contract A ✔) |
| `0x1dbad62` small-allocate | `host_allocate` | plain | none (Contract A ✔) |
| `0x1dbe559` free | `host_free` `:319-330` | — | **`free(p)`, no `-0x10`** |
| `0x1dbdfba` usable_size | `host_usable_size` `:357-368` | — | **`malloc_usable_size(p)`, no `-0x10`** |
| `0x227aafa` reallocate | `host_reallocate` `:332-342` | — | **`realloc(p,n)`, no `-0x10`** |
| `0x1dd60e5` aligned | `host_aligned_allocate` `:344-355` | plain aligned | none |
| `0x253225e` raw calloc (7th) | `host_calloc` `:284-317` | **`base+0x10`, header=`total`** | ✘ wrong contract (Contract A site) |
| `0x231f0e2` calloc (8th) | `host_calloc` | **`base+0x10`, header=`total`** | partially right (Contract B site) |

`host_calloc` key lines:

```c
size_t total = n + 0x10u;
unsigned char *base = calloc(1, total);
*(size_t *)base = total;        /* :299 — GUEST STORES 0 HERE */
void *p = base + 0x10u;         /* :300 — correct for Contract B only */
```

---

## 4. Findings in detail

### M1 — header value mismatch (confirmed deviation, latent crash)

- **Concrete RVA / line:** `bionic_bootstrap.c:299` `*(size_t *)base = total;`
  vs guest `0x231f1ac` `xor r14d,r14d` + `0x231f243` `mov %r14,(%r12)`.
- For the 0x1100 handles, guest header = `0`; ours = `0x1110`.
- When flags `0x7cdd149` **and** `0x7839dc8` are both nonzero, `0x231f53a:0x231f558`
  loads the header and `0x231f55e jne 6a28bf4` hands `0x1110` to the special-free
  path, which dereferences it (`0x6a28c1e`/`0x6a28c22`) → near-zero-address
  SIGSEGV. With flags zero, the header is ignored and `free(base)` is correct, so
  this is **latent**, not proven-fired.
- `host_calloc`'s own comment (`:290-294`) claims the guest "stores the total
  size in a 16-byte header". Direct disassembly shows that is **incorrect** for
  the size ranges our bridges serve; the header is a 0/custom-handle flag.

### M2 — 7th bridge mis-typed (confirmed deviation, currently dormant)

- `SOBER_BRIDGE_CALLOC_RVA = 0x253225e` is a **plain-pointer** calloc
  (`0x25322ea` `jmp memset@plt`, returns the arena block directly). Redirecting it
  to `host_calloc` (which returns `base+0x10`) shifts every object by `0x10`.
- Dormant because slot `0x726c0c0` is rebound to `0x231f0e2`
  (`analysis/dispatch/dispatch-map.md` §9; three 0x1100 allocations observed at
  caller `0x231f33f`). The secondary install site `0x6a35e30` can write
  `0x253225e` back into the slot — if that path runs on the game path, M2 becomes
  active. Needs a runtime hit counter on `0x253225e` to close.

### M3 — the three generic host bridges lack the `-0x10` (real risk)

- `host_free` `:329` `free(p)`, `host_reallocate` `:338` `realloc(p, n)`,
  `host_usable_size` `:367` `malloc_usable_size(p)`.
- If any of them receives a Contract-B pointer (`base+0x10`), glibc sees an
  interior pointer: `free(): invalid pointer` / `realloc(): invalid pointer` /
  garbage usable size. The guest's own `0x1dbe559` also takes p verbatim, but it
  is paired with Contract-A allocations; after the rebind the Contract-B free
  goes through `0x231f53a` (-0x10), so the guest never mixes the two the way our
  bridge set can.
- Corroboration: `analysis/tsd-static-buffer-abort.md` documents exactly this
  failure family (`realloc(): invalid pointer` via `0x227aafa → host_reallocate`)
  when a pointer from the wrong allocation family reaches a host bridge. That
  instance was a static buffer; a calloc `+0x10` pointer would abort identically.

### M4 — inverse mismatch for plain host_allocate pointers (conditional)

- `host_allocate` returns plain `malloc` pointers (no header). After the rebind,
  slot `0x726c0b8` holds `0x231f53a`, which computes `p-0x10` before reaching
  `host_free`. A plain host-allocated object freed through that slot is freed as
  an interior pointer.
- Evidence for the *mechanism* is complete (rebind at `0x231e900`; thunk at
  `0x231f542`), but I found **no start/game-path allocation that pairs a direct
  `0x1dbab02` call with a `0x726c0b8` free**: the helper cluster pairs the calloc
  slot with the free slot (§2.3), and the direct `0x1dbab02` callers in
  `analysis/bootstrap/bootstrap-map.md` (empty string, registry, render context)
  are long-lived singletons. Marked conditional pending a runtime free-pointer
  trace (`SOBER_TRACE_FREE_SLOT` / `SOBER_TRACE_USABLE`).

### M5 — static TSD buffer (already reported, included for completeness)

`SOBER_STUB_TSD_SEED` writes `static unsigned char state[65536]` from
`guest_app_shell.c:64` into the guest TSD slot; the guest grows it through
`0x227aafa → host_reallocate → realloc()` → SIGABRT. Fix belongs to that opt-in,
not to the bridges; see `analysis/tsd-static-buffer-abort.md` §5.

---

## 5. Minimal opt-in fix list (all default-OFF; lead owns `src/`)

Ordered by value/effort. None change the default path, emit a callback, or touch
decision 013. Each should be a separate env gate so A/B is possible.

1. **F1 — correct the calloc header (addresses M1; one line).**
   At `bionic_bootstrap.c:299`, write `0` instead of `total`:
   `*(size_t *)base = 0;` — gated by `SOBER_ALLOC_FIX_CALLOC_HEADER0=1`.
   This makes both flag states correct: header 0 → `0x231f53a` takes the
   `free(base)` path; it also matches the guest core at `0x231f1ac/0x231f243`
   for the 0x1100 and small-array sizes we actually serve. Keep the `total`
   computation only for the overflow guard.

2. **F2 — stop mis-typing the raw calloc bridge (addresses M2).**
   Gate the 7th bridge entry `SOBER_BRIDGE_CALLOC_RVA (0x253225e)` behind
   `SOBER_ALLOC_FIX_RAW_CALLOC_PLAIN=1`: install a **plain zeroing** host calloc
   (plain `calloc(1, n)`, no header, return the base) at `0x253225e` while
   leaving `host_calloc` (header form) at `0x231f0e2`. This restores Contract A
   at that site. Alternative if minimal footprint matters: skip installing the
   7th bridge at all — it currently has zero hits — and rely on the slot patch
   plus the 8th bridge.

3. **F3 — make the generic bridges header-aware (addresses M3, M4).**
   Gate with `SOBER_ALLOC_FIX_HEADER_AWARE=1`. Record the `base` pointers returned
   by `host_calloc` in a small registry (e.g. open-addressed set of returned
   `base` values, or a page-granular bitmap). In `host_free`, `host_reallocate`
   and `host_usable_size`, if `p-0x10` is a registered base, use `p-0x10` as the
   glibc pointer; otherwise use `p` unchanged. This preserves the guest's
   `0x231f0e2` contract for the 0x1100 handles while making it safe for any of
   our generic bridges to see those pointers. Keep the registry writes atomic;
   the three bridges can be called from guest worker threads.

4. **F4 — heap-backed TSD seed for the opt-in that faults (addresses M5).**
   In `guest_app_shell.c` `seed_stub_tsd()`, replace the static buffer with the
   guest allocator (`guest_base + 0x1dbab02`, as `gseed` already does) or glibc
   `calloc`, and pre-fill `*(uint64_t*)(buf+0)=1`, `*(uint64_t*)(buf+8)=capacity≥512`
   so `0x2ea6470` takes the no-grow branch. Gate by the existing
   `SOBER_STUB_TSD_SEED` (fix in place) or a new `SOBER_ALLOC_FIX_TSD_HEAP=1`.
   Do not simply disable the seed — that loses the TLS-state opt-in.

**Recommended rollout:** F1 + F2 first (two tiny patches, no data structures),
then A/B F3 with `SOBER_TRACE_CALLOC=1 SOBER_TRACE_USABLE=1` and
`SOBER_TRACE_FREE_SLOT=1` to decide whether the header-aware registry is needed.
F4 is independent and can ship with F1. Keep every gate OFF in the default test
matrix (decision 013 / `app-shell.sh` / `first-frame.sh`), and re-run the strict
gate after each fix.

---

## 6. Verification recipe (no decision-013 impact)

- Static re-check: `objdump -d --start-address=0x231f13a --stop-address=0x231f250`
  (header = 0) and `--start-address=0x231f53a --stop-address=0x231f570`
  (`-0x10` + header branch), plus `--start-address=0x231e8f9 --stop-address=0x231e91c`
  (slot rebind).
- Runtime A/B with opt-ins on: `SOBER_TRACE_CALLOC=1 SOBER_TRACE_USABLE=1
  SOBER_TRACE_FREE_SLOT=1 MALLOC_CHECK_=3`, then compare with
  `SOBER_ALLOC_FIX_CALLOC_HEADER0=1` / `SOBER_ALLOC_FIX_RAW_CALLOC_PLAIN=1`.
  Success criteria: no `free(): invalid pointer` / `realloc(): invalid pointer`,
  no SIGSEGV at the `0x6a28c22` dereference, engine SHA unchanged, default gate
  unchanged.
- Add a one-shot hit counter at `0x253225e` to close the M2 "dormant" question;
  record whether the `0x6a35e30` secondary install path runs on the game path.

---

## 7. Limits / unknowns

- Feature flags `0x7cdd149` and `0x7839dc8` were not traced at runtime; M1 is a
  **latent** crash, not an observed one. If both are proven permanently 0 in the
  harness, M1 downgrades to a correctness/documentation fix only.
- I did not run the guest (`binary_session` showed no open target; the Ghidra
  snapshot in `analysis/ghidra/work/` is for a different analysis profile and was
  correctly rejected). All reachability claims for the helper cluster rest on
  prior static artifacts plus the disassembly quoted here.
- M4 is mechanism-proven but occurrence-unproven; a free-pointer trace is the
  decisive evidence.
- The build-ID gate in `sober_bionic_bootstrap_verify_build()` means every RVA
  here is valid for `204ad049...` only; the Flatpak build must not inherit them.

## 8. Evidence index

- Direct disassembly (this worker): `0x1dbab02`, `0x1dbab44`, `0x1dbad62`,
  `0x1dbe559`, `0x1dbe5be`, `0x1dbdfba`, `0x227aafa`, `0x1dd60e5`, `0x231f0e2`,
  `0x231f13a`, `0x231f53a`, `0x253225e`, `0x6a28bf4`, `0x231e660`, `0x231e88c`;
  plus `/tmp/opencode/libroblox.dis` xref scan (only one reference to
  `0x231f53a`: `0x231e6a1`).
- `src/compat/bionic_bootstrap/bionic_bootstrap.c:271-368, 588-635, 1330-1331, 1395-1402`.
- `src/compat/bionic_bootstrap/bootstrap_profile.h:62-97`.
- `analysis/dispatch/dispatch-map.md` §9 (rebind, 0x1100 allocs, 37 free sites).
- `analysis/dispatch/helper_cluster.dis` (0x2333ac5 → 0x2333b91 pair).
- `analysis/trace/trace-report.md`, `analysis/handle/refbase-layout.md` §4,
  `analysis/bootstrap/bootstrap-map.md`, `docs/decisions/028…/029…`.
- `analysis/tsd-static-buffer-abort.md` (M5 corroboration).
