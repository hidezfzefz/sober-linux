# n7w4 #700 static-B — message table 0x721fbd0 (READ-ONLY)

FAULTING INSTRUCTION FIRST: guest `int3 0x6caef66` (RIP `0x6caef67`), HardAssert mimalloc, `[0x7cdd149]=1 [0x7cef650]=0` 3/3. No runs/writes.

## PROVEN — dispatcher indexing (verbatim)

```
6caed5d: 55                    push   %rbp
6caed5e: 48 89 e5              mov    %rsp,%rbp
6caed61: 83 ff 0a              cmp    $0xa,%edi
6caed64: b8 0a 00 00 00        mov    $0xa,%eax
6caed69: 0f 42 c7              cmovb  %edi,%eax
6caed6c: 48 8d 0d 5d 0e 57 00  lea    0x570e5d(%rip),%rcx   # 721fbd0
6caed73: 48 8b 3c c1           mov    (%rcx,%rax,8),%rdi
6caed77: e8 00 00 00 00        call   6caed7c
```
`eax = (edi < 10) ? edi : 10` — the input index is **clamped to 0..10**, so the table has 11 eight-byte entries at `0x721fbd0 .. 0x721fbe8`. `table[index]` becomes `%rdi` (first argument) to reporter `0x6caed7c`.

## PROVEN — call site passing string 0x4c0d2b and index 0

```
23a1460: e8 ab c9 f8 ff        call   232de10
23a1465: 48 8d 35 bf f8 11 fe  lea    -0x1ee0741(%rip),%rsi  # 4c0d2b
                                      ; "Expect to have mimalloc working, but it was not detected!"
23a146c: 31 ff                 xor    %edi,%edi
23a146e: e8 ea d8 90 04        call   6caed5d
```
- **Index = 0**, set by `xor %edi,%edi` at `0x23a146c`; the dispatcher therefore selects `table[0]` at `0x721fbd0`.
- The string `0x4c0d2b` is **not in the table** — it is loaded into `%rsi` at `0x23a1465` and passed through the dispatcher as the reporter's **second argument**. The clamped table entry is the first argument (a tag/severity object). So the premise "which table index selects the string" is imprecise: the index selects the tag; the call site selects the message. The two meet only at this call site.
- `0x23a146x` sits in `nativePostClientSettingsLoadedInitialization3+0x1d848` (same #552 native family); the dispatcher has many other call sites (`0x1dbac46`, `0x1dbe613`, `0x1dd6016`, `0x1e47895`, `0x230cd38`, …) that pass other indices/messages.

## PROVEN / UNPROVEN — table contents

The 11 file bytes at `0x721fbd0` are **zero** (checked directly) — the table is filled by `DT_ANDROID_RELA` (APS2) `R_X86_64_RELATIVE` entries at load. The stream is present at RVA `0x15e00` with the `APS2` magic (`41 50 53 32` verified), but a static SLEB128 decode of its header produced an implausible negative count, so entry values remain **UNPROVEN**; no runtime read was performed (read-only task).

## RETRACTED / NOT done

- RETRACTED: describing `0x721fbd0` as a "message table" whose index selects `0x4c0d2b` — the message is in `%rsi` from the call site; the table supplies `%rdi`.
- NOT DONE: no runs/device/guest writes/gate edits/seeder/fake returns/remaps; object OFF; no captures/tokens in git.
