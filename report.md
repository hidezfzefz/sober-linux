# n4 #676 — fflush fault, import/GOT, rdi source, shim

Read-only; cores 2-3; no runs/device. Every success claim quotes a raw line.
Evidence: `raw_harness_fault.txt`, `raw_fflush_plt.txt`, `raw_shim_fflush.txt`,
`raw_fflush_callsites.txt`, `raw_sections.txt`.

## FAULTING INSTRUCTION FIRST — PROVEN

Quoted harness raw line (lead-relayed from n4 harness):
> `[W40_RIP_MAP] rip0x00007fa51a6518ff offset=0x00000000000588ff maps='7fa51a5f9000-7fa51a749000 r-xp 00024000 00:17 482353 /usr/lib64/libc.so.6'`

Host libc ELF offset `0x7c8ff`; `objdump` quoted by harness:
> `7c8ff: 48 8b 47 08 mov rax,QWORD PTR [rdi+0x8]`

`nm -D` quoted: `_IO_fflush@@GLIBC_2.2.5 at 0x7c860` ⇒ fault = **`_IO_fflush+0x9f`, NULL rdi** (`si_addr=0x8`). RET64 was NOT exercised (si_addr far from rsp), so **there is no first-guest-RVA call site** — PROVEN.

The first guest-mapped stack qword `guest+0x00000000072a0818` is **DATA, not a
call site**: quoted `readelf -SW` `.bss` = `0000000007282500 … b1ad1c`, which
contains `0x72a0818`. PROVEN.

## Guest import / GOT slot — PROVEN

Quoted `.plt` disassembly:
> `6d32020 <fflush@plt>: ff 25 2a 77 4f 00 jmp *0x4f772a(%rip) # 7229750`

So the guest imports symbol **`fflush`** via PLT `0x6d32020`, whose GOT slot is
`0x7229750`. Call sites found (quoted): `232b8d9`, `237d747`, `237d9d3`,
`2a26665`, `2b5b3e8`, `2cb81a7`, `2e61d13`, `2ea69d1`, … all indirect via that
PLT.

## rdi just before the call — PROVEN at sampled sites

Quoted raw disassembly:
> `232b8d2: mov 0x98(%rbx),%rdi` → `232b8d9: call 6d32020 <fflush@plt>`
> `237d743: mov 0x30(%rbx),%rdi` → `237d747: call 6d32020 <fflush@plt>`
> `237d9d0: mov %r15,%rdi` → `237d9d3: call 6d32020 <fflush@plt>`
> `2e61d0f: mov 0x40(%rbx),%rdi` → `2e61d13: call 6d32020 <fflush@plt>`

So rdi is loaded from an **object field / register**, never a global or TLS
holder. A NULL field/register yields the NULL FILE* that reaches glibc.

## Which shim/trampoline forwards the symbol — PROVEN

Quoted source:
> `/home/user/sober-linux/src/compat/bionic_libc.c:487: int fflush(FILE *stream)`
> `:490: sober_fflush_fn real = (sober_fflush_fn)sober_libc_fn_fflush;`
> `:653: sober_libc_fn_fflush = native_lookup("fflush");`

`gen_bionic_libc.py:262` quoted: `if s["name"] in ("fflush", "fclose", "fwrite"):` — these are **skipped** by the TRAMP generator, so the stub exports its own C `fflush` directly (not a generated trampoline). Forwarding chain: guest `fflush@plt` → GOT `0x7229750` → stub `fflush@@LIBC` (our `bionic_libc.c:487`) → `sober_libc_fn_fflush` → glibc `_IO_fflush`.

`sober_stdio_map` quoted:
> `:471: if (!p) return NULL;`
> `:497: return real ? real(mapped) : 0;`

So a NULL guest stream maps to NULL and is passed straight to glibc — the
mechanism for the NULL rdi at `_IO_fflush+0x9f`. The exact guest call site on
the #552 path that supplies NULL is **UNPROVEN** (no `fflush@plt` call inside
the `0x2383c18` body; the sampled sites load rdi from FILE*-bearing fields).

## What NOT done
No apply/build, no runs/gdb/perf, no device/AVD/Frida/phone, no new captures,
no guest writes, gate edits, seeder, fake returns or path remaps, no
credentials, no captures/logs/tokens in git.

## First divergence + next step
First divergence: NULL FILE* from a guest FILE*-bearing field reaches host
`_IO_fflush`; real boot supplies a valid stream. Next: harness logs the guest
flush call site and its FILE* field on the #552 path.

## CORRECTION (audit #677) — symbol offset

RETRACTED my `_IO_fflush+0x3f` wording: `nm -D` gives `_IO_fflush@@GLIBC_2.2.5 =
0x7c860` and the faulting address is `0x7c8ff`, so
`0x7c8ff - 0x7c860 = 0x9f`. Correct designation: **`_IO_fflush+0x9f`**. All
other findings unchanged.
