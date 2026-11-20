# Decision 033 — Zero the stale curl holder at caller B

Status: accepted (lead). This supersedes the 0x6a26e78 caller patch.

## Independent caller trace

- Caller A `0x6a26e78`: the only path reaching `call 0x23330e7` passes
  `RDI == 0` (the `je` at `0x6a26ea4` is taken otherwise), so it cannot produce
  the bogus code pointer.
- Caller B on the live `0x24abf96` app-shell path:
  ```
  0x234dd7e: cmpq $0,0x460(%rbx)      ; stale curl easy-handle holder
  0x234ddc0: mov 0x460(%rbx),%rsi     ; load stale value
  0x2457709: mov %rsi,%rbx
  0x245771e: mov %rbx,%rdi            ; 48 89 df
  0x2457721: call 0x23330e7           ; F_inner
  ```
  The holder is never initialized on this path, so residue
  `0x233263f` (epilogue of curl_easy_getinfo wrapper `0x2332577`) passes the
  non-zero test and is consumed as an object pointer.

## Patch

At guest RVA `0x245771e`, verify original 3 bytes `48 89 df` and replace with
`31 ff 90` (`xor edi,edi; nop`). `F_inner` then takes its own
`test rdi,rdi; jz 0x2333183` safe bail at `0x2333104/0x2333107`. No stub, no
object substitution, no allocator interaction. Opt-in
`SOBER_CURL_HOLDER_FIX=1`; all other patches off; strict DECISION 013 gate.
