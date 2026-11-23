# n7w1 #688 static-B — ctor replay + boot-only + dl_iterate_phdr (READ-ONLY)

FAULTING INSTRUCTION FIRST: guest `int3` at `0x6caef66` (reported RIP `0x6caef67`), HardAssert `RBXCRASH: HardAssert (Expect to have mimalloc working, but it was not detected!)`. All lines quoted raw from source/logs.

## PROVEN — 3618-ctor replay (src/linux/guest_dlopen_host.c)

```
819:    const char *range_env = getenv("SOBER_CTOR_RANGES");
820:    int native_ranges = range_env && !strcmp(range_env, "native");
821:    int all_ranges = range_env && !strcmp(range_env, "all");
823:    uintptr_t *array = (uintptr_t *)g_init_array_addr;
824:    const unsigned std_ranges[][2] = { {2, 3}, {5, 3618} };
825:    const unsigned native_range[][2] = { {2, 3618} };
826:    const unsigned all_range[][2] = { {0, 3618} };
827:    const unsigned (*ranges)[2] = all_ranges ? all_range
828:                                  : (native_ranges ? native_range : std_ranges);
829:    size_t nranges = (native_ranges || all_ranges) ? 1 : 2;
833:        for (unsigned i = ranges[r][0]; i < ranges[r][1] && i < elf->init_array_count; ++i) {
861:            uintptr_t fn = array[i];
875:            ((void (*)(void))fn)();
```
Default (`std`) replays indices **2 and 5..3617**; skips 0,1,3,4. `g_init_array_addr = base + elf->init_array_rva` (`:810`), count from DT_INIT_ARRAYSZ; `android_relocs.h:8` notes the 3618-entry array at RVA `0x7220e58` is zero-filled by plain dlopen — the harness decodes `DT_ANDROID_RELA` and writes the entries (android_relocs.c:258-261) before driving them.

**ctor3602 IS in the replayed set** (index 3602 lies in `[5,3618)`), confirmed in log `n5_1.err:3652`: `[harness] ctor[3602] rva=0x2279a47` — that is the function containing `2279a8f: mov %al,0x7cef650` (n5/n6 static-B finding).

**Order vs scheduler guard** (n5_1.err raw):
```
3652:[harness] ctor[3602] rva=0x2279a47
3687:[fflag-default] replayed guest ctor 0x2e62390 (FFlag registry): ... ctors 1/3/4 not required ...
3689:[app-shell] scheduler guest install -> rc=1
3706:[W32_LATCH_RO] before=0 after=1
3708:[app-shell] AVD boundary nativeInitClientSettings 0x2359277 -> rc=0 ... latch=1 ...
```
So ctor3602 runs **before** the ctor[0] replay, scheduler install, and the latch/guard flip (`0x7cdd149` 0->1) — the guard is written by the AVD-boundary native replay, not by ctor3602.

## PROVEN — real boot runs what harness never runs

- The Android linker invokes **every** init_array entry automatically, including indices **0,1,3,4** that std replay skips (`guest_dlopen_host.c:824`). Log names them: ctor[0]=`0x2e62390` FFlag registry (decision 008 skips genuine call; harness instead does a host-side replay at `guest_app_shell.c:1411-1419`), ctor[1]=protobuf once-guard `0x1dba8f3` (`:850-857` only under `SOBER_CTOR1`), ctor[3]=allocator thread init `0x1e47927` (`:851-854` only for native/all ranges), ctor[4]=time baseline (named in log as "time baseline=lazy").
- `android_relocs.h:8-12`: "a plain dlopen() leaves the 3618-entry DT_INIT_ARRAY ... zero-filled and the first constructor call is a NULL call (SIGSEGV, rip=0x0)" — the harness must decode APS2 relocs itself; a real boot has the Android linker doing this natively.
- Real boot has genuine Bionic libc + the engine's loaded allocator, so the mimalloc integration probe is true; harness uses glibc-backed bridges (n6 finding) and the probe is DFLog-only (static-A #683).

## PROVEN — dl_iterate_phdr shim return

- Exported to guest by `src/compat/bionic_libc.map:348: dl_iterate_phdr;`
- `src/compat/bionic_libc.c:1356: TRAMP(dl_iterate_phdr);` with the macro at `:459-470`:
```
459:#define TRAMP(name) \
460:    __asm__ volatile ( \
...
464:        "movq sober_libc_fn_" #name "(%%rip), %%rax\n\t" \
465:        "testq %%rax, %%rax\n\t" \
466:        "jz 1f\n\t" \
467:        "jmp *%%rax\n\t" \
468:        "1: xorl %%eax, %%eax\n\t" \
469:        "ret\n\t" \
```
- The function pointer is filled at `bionic_libc.c:919: sober_libc_fn_dl_iterate_phdr = native_lookup("dl_iterate_phdr");` (resolver `:32-37` uses `real_dlsym_fn(h, name)` with `h = g_glibc ? g_glibc : RTLD_DEFAULT`).

**Return semantics:** if resolved (normal case), the shim tail-jumps to the real glibc `dl_iterate_phdr` — it does **not** synthesize guest link-map entries; the callback sees the host link map. If resolution failed (pointer NULL), the TRAMP returns **0 in eax** without ever invoking the callback — an unwinder then sees zero object/frame callbacks, which is consistent with the `_Unwind_Resume() can't return` HardAssert text (strings `RBXCRASH` `0x3a30df`, `HardAssert` `0x3a30e8`, `_Unwind_Resume` in n5 static-A evidence). Which branch actually runs in n7 is **UNPROVEN** from static evidence; no new runs done.

## UNPROVEN / NOT done

- UNPROVEN: exact RVA of ctor[3]/[4] from a replayed run (not present in std logs); whether the shim's function pointer is NULL at fault time; whether zero callbacks is sufficient to cause the unwind failure.
- RETRACTED: none here. NOT DONE: no runs/device/guest writes/gate edits/seeder/fake returns/remaps; object OFF. No captures/tokens in git.
