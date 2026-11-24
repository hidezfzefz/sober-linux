# n9w2 #717 static-B — 8 allocator patches ON/OFF (READ-ONLY)

FAULTING INSTRUCTION FIRST: guest `int3 0x6caef66` (RIP `0x6caef67`, `89 c0 mov %eax,%eax; e8 62... call abort@plt`), SIGTRAP. All quotes raw. Object OFF, SF_FULL=1, ctor3 OFF, seed OFF, JNI_OnLoad-first ON. No apply/runs by me.

## PROVEN — the 8 patches and their ON/OFF control

Table (n9-harness `src/compat/bionic_bootstrap/bootstrap_profile.h:62-74`, `bionic_bootstrap.c:889-908`):
```
#define SOBER_BRIDGE_ALLOC_SMALL_RVA   0x1dbad62u   -> g_bb_ops.alloc_small  "small-allocate"
#define SOBER_BRIDGE_USABLE_SIZE_RVA   0x1dbdfbau   -> g_bb_ops.usable_size  "usable-size"
#define SOBER_BRIDGE_REALLOCATE_RVA    0x227aafau   -> g_bb_ops.realloc      "reallocate"
#define SOBER_BRIDGE_ALLOCATE_RVA      0x1dbab02u   -> g_bb_ops.allocate     "allocate"
#define SOBER_BRIDGE_ALIGNED_ALLOC_RVA 0x1dd60e5u   -> g_bb_ops.aligned_alloc "aligned-allocate-direct"
#define SOBER_BRIDGE_FREE_RVA          0x1dbe559u   -> g_bb_ops.free         "free"
#define SOBER_BRIDGE_CALLOC_RVA        0x253225eu   -> bb_calloc_plain_installed "calloc-plain"
#define SOBER_BRIDGE_CALLOC_RUNTIME_RVA 0x231f0e2u  -> bb_calloc_header_installed "calloc-header"
```
`install_bridge()` (`:838-873`) overwrites each 12-byte thunk prologue after fingerprint check:
```
/* movabs rax, imm64 ; jmp rax  -- absolute jump to the native function. */
tramp[0] = 0x48; tramp[1] = 0xb8;
tramp[10] = 0xff; tramp[11] = 0xe0;
memcpy((void *)site, tramp, sizeof(tramp));
```
Host targets from `bb_resolve_alloc_ops()` (`:585-596`) → `host_allocate` (`:271-276`): `274: void *p = malloc(n);` — **glibc malloc/free** (weak `mi_*` declarations at `:175-176,262-265` are dead; n6 #684).

Control (n9-harness `bionic_bootstrap.c:1606-1613, 1638-1644`):
```
/* n9 WAVE1 KILL SWITCH ... the 8 bridge patches are all
 * allocator entry points, never the page source; they do not make 1dbe4fc see
 * a registered pointer.  Default OFF; opt in only with SOBER_ALLOCATOR_PATCH=1.
 * 1dbe4fc is NEVER bridged. */
static int bb_alloc_patch_enabled(void) {
    const char *e = getenv("SOBER_ALLOCATOR_PATCH");
    return (e && e[0] == '1' && e[1] == '\0') ? 1 : 0;
}
...
if (!getenv("SOBER_BOOTSTRAP_NO_BRIDGES") && bb_alloc_patch_enabled()) {
    if (install_bridges(base) != 0) return set_err("failed to install mimalloc bridges");
    ...
} else {
    fprintf(stderr, "[N9_ALLOC_PATCH] OFF (default); guest allocator "
                    "unpatched; 1dbe4fc NOT bridged\n");
```
- **ON**: 8 guest allocator thunks → glibc host malloc/free; embedded mimalloc bypassed at those entries only; calloc slot `0x726c0c0` patched too.
- **OFF (default)**: all 8 thunks stay original guest code → embedded mimalloc remains the backend; probe `0x1dbe4fc` untouched. Raw log `n9off_1.err:19 [N9_ALLOC_PATCH] OFF (default); guest allocator unpatched; 1dbe4fc NOT bridged`.

## PROVEN — the two fatal messages are two guards on the SAME reporter/int3

Both pass through dispatcher `0x6caed5d` → reporter `0x6caed7c` → `6caef66: int3`:
```
6caed61: 83 ff 0a              cmp    $0xa,%edi
6caed64: b8 0a 00 00 00        mov    $0xa,%eax
6caed69: 0f 42 c7              cmovb  %edi,%eax        ; index = min(edi,10)
6caed6c: lea 0x570e5d(%rip),%rcx   # 721fbd0
6caed73: mov (%rcx,%rax,8),%rdi
6caed77: call 6caed7c
```
- **mimalloc HardAssert**: `23a1465: lea #4c0d2b,%rsi` ("Expect to have mimalloc working, but it was not detected!"), `23a146c: xor %edi,%edi` (**index 0**), `23a146e: call 6caed5d`. Reached via the probe/flag path (n7 static-A #697: `239d215 call 239ead3` printer → `239eb14 cmpb $0,0x7cdede0` → `23a1424 je 23a1465`).
- **TaskScheduler-before-flags**: raw disasm from `analysis/lua-rbxcrash-flags-loaded.md:30-35`:
```
23862b0: cmpb $0x0, 0x7cdd149(%rip)     ; "flags loaded" latch
23862b7: je   2386588                   ; latch==0 -> report + crash
2386588: lea  0x36a389(%rip),%rsi       ; "Can't initialize the TaskScheduler before flags have been loaded"
238658f: mov  $0x6,%edi                 ; crash type 6 (FatalRuntimeError)
2386594: call 0x6caed5d                 ; same dispatcher
```
and `src/jni/flags_latch_default.h:9-10` quotes `RBXCRASH "Can't initialize the TaskScheduler before flags have been loaded" (int3 at 0x6caef67)`.

## PROVEN / UNPROVEN — observed message in n9 A/B logs

Raw logs show **TaskScheduler** in both arms (n9 allocator ON and OFF):
```
n9_1.err:3717 RBXCRASH: FatalRuntimeError (Can't initialize the TaskScheduler before flags have been loaded)
n9off_1.err:3717 RBXCRASH: FatalRuntimeError (Can't initialize the TaskScheduler before flags have been loaded)
```
So the 8 patches alone do **not** switch the message — both arms fault on the flags-latch guard (latch `0x7cdd149 == 0`), because the patches never touch `0x1dbe4fc`/page source (kill-switch comment above). which guard fires first also depends on ctor3/probe state (UNPROVEN from static bytes alone).

## RETRACTED / NOT done

- RETRACTED: "8 patches ON ⇒ mimalloc HardAssert, OFF ⇒ TaskScheduler" — n9 A/B shows TaskScheduler in both; the two messages are separate guards on one int3 site.
- NOT DONE: no runs/device/guest writes/gate edits/seeder/fake returns/path remaps; no captures/tokens in git.
