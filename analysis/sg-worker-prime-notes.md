# Worker-scoped prime / FLog stream — ordered RAs + first divergence

Engine `3e498d64…335` / BuildID `204ad049…d6c9`; base.apk `114a6f8e…ead4`.
Files: `scheduler_guest.c/.h`, `tsd_default.c/.h`, `bionic_libc.c`,
`bionic_sf_full.c`, `javavm_semantics.c`. No guest code-byte patch, no fabricated
guest object, no clean-path seed, no gate change. Marker WITHHELD.

## A. Reference ordered RA list (AVD chain; re-verified on frozen engine)

`0x2a0de60 → 0x2ea6440(desc2 0x722e560) → getspecific@plt 0x6d322e0 / GOT 0x72298b0` (key3 `0x72a1b08`) → one-shot `0x2a0ded0` (sole caller `0x2a0de78`) → ctor `0x2a168b0` (sole caller `0x2a0df01`, `[task+0x10]=0x2a18270`) → `pthread_create@0x2a16a4a` → thunk `0x2a0daa0` → enter-hook RA `0x2a0daf0` → guest `0x2a132c0(1)` → `call *0x10(%rbx)@0x2a0daf7` → `0x2a18270` (`orq $1,0x20(%r13)`, RA `0x2a0dafa`) → FunctionMarshaller attach → `0x235a58c` → gate `0x2eb71e8` → latch `0x2eb7520` → guard `0x23862b0`.

## B. FLog `obj+0x40` writer (genuine guest code)

Caller `0x2e613cb/0x2e613d7`: `rsi = [GOT 0x72281d8] + 0x130` (Bionic stderr).
Callee ctor `0x2e61750`: `mov %r15,0x40(%rbx)` at `0x2e61772` (also `0x2e617e0+0x22` for `+0x98` stdout). FLog writer `0x2e61ca0` uses `obj+0x40` as FILE* for `fwrite@plt 0x6d32010` (`0x2e61cee`) and `fflush@plt 0x6d32020` (`0x2e61d13`).

**Missing prerequisite:** `fwrite@plt` / `fflush@plt` GOT (`0x7229748` / `0x7229750`) resolve to glibc, so the zeroed Bionic `__sF+0x130` buffer (`_lock @+0x88 == NULL`) reaches glibc `_IO_fflush` directly at `libc+0x7c8e8/0x7c8ff`; `SOBER_BIONIC_SF_FULL`'s `sf_fflush` bridge was never in the path.

**Minimal shim fix (`bionic_sf_full.c`):** inside the existing `SOBER_BIONIC_SF_FULL`-gated installer, rebind only those two GOT slots to the stub's own `fwrite@@LIBC`/`fflush@@LIBC`, resolved with `dlvsym(libc.so, ..., "LIBC")` (plain `&fflush` is interposed by the `-rdynamic` host). The stub forwarders then reach `sf_fflush/sf_fwrite`, which map `__sF+0x130/+0x98` to the **real host `stderr`/`stdout` FILE***. No new knob (existing env already in the withhold chain), no guest object fabricated, clean path untouched.

## C. Ordered RA list (harness, opt-in stack + fix) and first divergence

`guest init complete` → `first frame rendered` → TSD GOT rc=0 (build-ID memfd discovery fix) → `scheduler guest install rc=0`; no `AttachCurrentThread` anywhere → prime=0, main_reject=1 → FLog `sf_fflush` now reached, **no SIGSEGV** → guest `int3` at `0x6caef67` (RBXCRASH terminal, gated by `0x7cdd149`/`0x7cef650`).

**First divergence:** previously `0x2e61d13` (`_IO_fflush` on zeroed Bionic FILE*); with the shim it advances to **`0x6caef67`**. The nominal gate `0x2eb71e8`, `0x2a0ded0`/`0x2a168b0`, the FunctionMarshaller attach and `0x2a0daf7` remain unreached. No host-triggered call to `0x2a0ded0` added; its only honest entry is a genuine `0x2a0de60` call with desc2 byte0 bit0 clear.

Verification: `-Werror` selftest OK, build OK, engine SHA unchanged, marker WITHHELD.
