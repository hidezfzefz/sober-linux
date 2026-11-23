# n5 #679 — fatal helper format/callers + Bionic FILE layout

Read-only; cores 2-3; no runs/device. Every success claim quotes a raw line.
Evidence: `dis_fatal.txt`, `raw_messages.txt`, `raw_install_sf.txt`,
`raw_sf_layout.txt`, `raw_sf_full.txt`, `raw_sf_refs.txt`.

Faulting instruction first (n4 harness): host libc
`7c8ff: 48 8b 47 08 mov rax,[rdi+0x8]` = `_IO_fflush+0x9f`, NULL rdi.

## (a) Fatal helper `0x2ea6a10` — format string and callers — PROVEN

Quoted disassembly:
> `2ea6a66: mov $0x130,%ebx`
> `2ea6a6b: add 0x72281d8(%rip),%rbx`     (`rbx = __sF + 0x130` = stderr)
> `2ea6a72: lea … # 2345cf,%rsi`
> `2ea6a79: lea … # 517ec0,%rdx`
> `2ea6a80: lea … # 2a4d76,%rcx`
> `2ea6a8c: call 6d31aa0 <fprintf@plt>`
> `2ea6a91: mov %rbx,%rdi`
> `2ea6a94: call 6d32020 <fflush@plt>`
> `2ea6a99: call 6d320d0 <abort@plt>`

Raw rodata strings (quoted):
> `0x2345cf 'libunwind: %s - %s\n'`
> `0x517ec0 '_Unwind_Resume'`
> `0x2a4d76 "_Unwind_Resume() can't return"`

So the helper prints exactly **`libunwind: _Unwind_Resume - _Unwind_Resume() can't return`** to `__sF+0x130`, flushes that stream, then aborts.

Callers: a direct-call scan found the helper invoked from the `0x1dba..–0x1dcf..` region (libunwind fatal/unwind-failure paths), e.g. quoted:
> `1dba6b7: call 2ea6a10`, `1dceb4f: call 2ea6a10`, `1dcf4c8: call 2ea6a10`

**No caller lies inside the `#552` body (`0x2383c18..0x23c1200`)** — the scan over that range returned none. So the fatal helper is not directly reachable from #552 code; it is an unwind-failure path reachable from whichever thread throws (logger/other threads). Direct reachability from #552 is **UNPROVEN**; attribution to any specific thread is **UNPROVEN** from static data.

## (b) Bionic FILE layout / `_lock` / streams — PROVEN

Quoted source:
> `bionic_libc.c:443 unsigned char sober_libc_obj___sF[0x200];`
> `gen_bionic_libc.py:97 # __sF must cover Bionic's FILE __sF[3] (0x198 bytes): the guest derives`
> `:98 # stdout/stderr as __sF+0x98 / __sF+0x130.`
> `bionic_sf_full.c:4-7 The guest derives its stdout/stderr FILE* as __sF+0x98 and __sF+0x130 ... glibc's _IO_fflush faults reading the NULL _lock at FILE+0x88.`

So the exported layout is `FILE __sF[3]`, Bionic stride `0x98`, object widened to `0x200` bytes. **Each Bionic FILE does NOT have a valid `_lock` at +0x88** — it is zeroed, so glibc's lock path (`_IO_fflush` reading `_lock+0x88`) faults. PROVEN from the quoted source comment; no per-FILE lock initializer was found.

`install_sf_full` (quoted `guest_app_shell.c:1089-1102`) resolves `sober_bionic_sf_full_install` from the dlopened `libc.so` stub and calls it with `__sF`; it is opt-in via `SOBER_BIONIC_SF_FULL` (quoted `bionic_sf_full.c`: `if (!getenv("SOBER_BIONIC_SF_FULL")) return;`). With it enabled, `host_stream_for` quoted maps:
> `if (addr == base + 0x098u) return stdout;   /* Bionic stdout */`
> `if (addr == base + 0x130u) return stderr;   /* Bionic stderr */`

and rebinds the two stdio PLT GOT slots (quoted log format `fflush 0x7229750 …`). With object OFF / default OFF, no mapping is installed and the zeroed `__sF+0x130` FILE reaches glibc.

**Which stream the fatal helper flushes:** `__sF+0x130` (**stderr**) — PROVEN by `2ea6a66/6b/91` above. Other guest logger flush sites load FILE* from object fields (`mov 0x30(%rbx),%rdi` at `237d743`, `mov 0x98(%rbx),%rdi` at `232b8d2`), so a global claim that all logger flushes use only +0x98 or only +0x130 is **UNPROVEN**.

## What NOT done
No apply, no build, no runs/gdb/perf, no device/AVD/Frida/phone, no new
captures, no guest writes, gate edits, seeder, fake returns or path remaps, no
credentials, no captures/logs/tokens in git.

## First divergence + next step
First divergence: zeroed Bionic `_lock` at FILE+0x88 (no full stdio bridge)
vs real host streams on a genuine boot. Next: harness `SOBER_BIONIC_SF_FULL`
Run D x3 with the shim ring (owned by harness in this wave).
