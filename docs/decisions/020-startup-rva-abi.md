# Decision 020 — Verify startup RVA call ABI

Status: accepted (lead).

## Observation

Decision 019's allocator zero-fill is present and builds clean, but the
app-shell fault is unchanged at `0x2456079` with
`rdi=0xffffff3085894cff`.

worker-2 observed that the harness calls every startup RVA with only two
arguments `(env, clazz)`, while `analysis/jni/jni-map.md` §2/§3 documents:

- `nativeSetBaseDataDirectories` (`0x24650a9`) needs two path-string args;
- `nativeSetRobloxVersion` / `nativeSetRobloxChannel` need string args;
- `nativeAppBridgeV2InitWithParams` (`0x24abf96`) takes a large param struct.

The three current calls return garbage RAX (`-6066930334832433271`) and leave
`rdx`/`rcx` with caller residue. A wrong SysV ABI call can easily leave a
sign-extended 32-bit value in a guest object field that is later read as a
pointer at `+0x68`.

## Ruling

- worker-1 to supply exact signatures and expected values for each startup RVA.
- worker-2 to provide jstring/param-struct construction helpers in `src/jni/`.
- worker-3 to fix the harness calls and rerun `tests/app-shell.sh` with the
  strict DECISION 013 gate.
- All experimental guest patches remain reverted by default; no false marker.
