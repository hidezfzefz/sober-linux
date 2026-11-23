# n6 #684 static-B — weak mi_malloc + malloc/new resolution (READ-ONLY)

FAULTING INSTRUCTION FIRST (n5 new divergence): `guest+0x6caef67` SIGTRAP, real trap `6caef66: cc int3` in HardAssert reporter `0x6caed7c`; log `RBXCRASH: HardAssert (Expect to have mimalloc working, but it was not detected!)`. All quotes raw.

## (a) PROVEN — "host binds weak mi_malloc" is a README claim, not an active code path

Exact declarations (all in the **compat bootstrap shim**, `src/compat/bionic_bootstrap/bionic_bootstrap.c`, NOT the harness app-shell `guest_app_shell.c` and NOT the ELF loader `android_relocs.c`):
```
175:extern void *mi_malloc(size_t) __attribute__((weak));
176:extern void *mi_calloc(size_t, size_t) __attribute__((weak));
262:extern void  mi_free(void *) __attribute__((weak));
263:extern void *mi_realloc(void *, size_t) __attribute__((weak));
264:extern void *mi_aligned_alloc(size_t, size_t) __attribute__((weak));
265:extern size_t mi_usable_size(const void *) __attribute__((weak));
```
**RETRACTED as an active mechanism:** these six weak symbols are *declared but never referenced* in any compiled source — repo-wide grep finds only these declaration lines plus prose in `README.md:46,49,51,242`. The actual bridge ops are bound in `bb_resolve_alloc_ops()` (`bionic_bootstrap.c:585`):
```
591:    g_bb_ops.allocate      = host_allocate;
593:    g_bb_ops.free          = host_free;
594:    g_bb_ops.usable_size   = host_usable_size;
595:    g_bb_ops.realloc       = host_reallocate;
596:    g_bb_ops.aligned_alloc = host_aligned_allocate;
```
and `host_allocate` (`bionic_bootstrap.c:271-276`) is plain glibc:
```
271:static void *host_allocate(size_t n)
274:    void *p = malloc(n);
274:    if (p) memset(p, 0, n);
```
So the n5 phrase "host binds weak mi_malloc" refers to **documentation intent** (`README.md:55`: "mimalloc is used if the process already exports it; otherwise native glibc malloc is used"; `README.md:246`: "allocator is glibc malloc when mimalloc is absent"), not to a live weak-symbol call. The library truly has **zero** `mi_*` dynamic symbols (`nm -D` over all 1132 dynsyms returns none) — the weak refs would only bind if the *host process* exported mimalloc; it does not, and they are unused anyway.

**Where guest malloc/new resolve:** the engine's allocator is **embedded** (statically linked mimalloc); guest allocator calls are direct internal calls, not PLT/dynsym. The harness patches **eight** bridge thunks (README says "six", code has eight) via `install_bridges()` at `bionic_bootstrap.c:875-908`; RVAs from `bootstrap_profile.h:62-74`:
```
62:#define SOBER_BRIDGE_ALLOC_SMALL_RVA   0x1dbad62u
63:#define SOBER_BRIDGE_USABLE_SIZE_RVA   0x1dbdfbau
64:#define SOBER_BRIDGE_REALLOCATE_RVA    0x227aafau
65:#define SOBER_BRIDGE_ALLOCATE_RVA      0x1dbab02u
66:#define SOBER_BRIDGE_ALIGNED_ALLOC_RVA 0x1dd60e5u
67:#define SOBER_BRIDGE_FREE_RVA          0x1dbe559u
71:#define SOBER_BRIDGE_CALLOC_RVA        0x253225eu
74:#define SOBER_BRIDGE_CALLOC_RUNTIME_RVA 0x231f0e2u
```
i.e. guest embedded allocator wins for all ordinary calls; shims (glibc malloc/free) win **only** at those eight patched thunks.

## (b) PROVEN — premise false: no library-defined malloc/new symbol has a GOT slot

For **each** of `malloc, free, calloc, realloc, posix_memalign, memalign, aligned_alloc, _Znwm, _Znam, _ZdlPv, _ZdaPv, _Znwj`:
- `nm -D --defined-only libroblox.so` → **no match** (none defined/exported);
- `readelf -rW libroblox.so` → **no relocation** mentioning any of them (all 542 dynamic relocs are `R_X86_64_JUMP_SLOT`, none for these names).

Therefore there is **no per-symbol loader code to quote** — no guest GOT slot exists for any of these symbols. The only generic symbolic-GOT resolution in the loader is `src/linux/android_relocs.c:262-286`:
```
262:                const char *name = symidx ? symbol_name(elf, symidx) : NULL;
263:                void *value = name ? dlsym(RTLD_DEFAULT, name) : NULL;
...
276:                    *target = (uint64_t)(uintptr_t)value +
277:                              (type == 1 ? (uint64_t)r_addend : 0);
278:                    ++symbolic;
```
This would make a **host shim win** over a library definition for any symbolic relocation (dlsym RTLD_DEFAULT searches the host first), but it never applies to malloc/new here because none are relocated. `src/trace/native_trace.c:389-393` (`real_malloc = sr_resolve("malloc", "__libc_malloc")` etc.) is an **optional LD_PRELOAD tracer**, not the guest loader.
Verdict: **the library's own embedded allocator wins** for guest malloc/operator-new; shims only via the eight patched bridge RVAs.

## UNPROVEN / NOT done

- UNPROVEN: whether the eight bridge RVAs are the *only* allocator indirection (full static enumeration of direct calls to embedded allocator not done); exact property that makes the guest's mimalloc detection false (static-A scope).
- RETRACTED: n5's "weak mi_malloc binding" as the cause of the HardAssert — the weak refs are dead declarations.
- NOT DONE: no runs/device/guest writes/gate edits/seeder/fake returns/path remaps; no captures/tokens in git.
