# Task #509 — genuine clean return from crashpad `0x2ec9eed`

Engine: frozen `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
Target content is untrusted data. All RVAs below are from that object.
Worker-2 owns `src/jni/crashpad_return.c/.h` and this note only.

## 1. Exact fault (re-derived, not assumed)

Reproduction (real repo, route `crashpad-valid` with the existing `@SOCK@`
socketpair), `guest-init-host` → SIGSEGV/139. The harness's own SIGSEGV handler
did not print (the fault is reached through crashpad's log flush on a Bionic
`FILE*`), so the exact chain was captured from a core with `eu-stack -b`:

```
#0  libc.so.6+0x7d225              _IO_fputs            <-- SIGSEGV
#1  libroblox.so+0x2a26661         guest fputs@plt call site (0x2a2665d)
#2  libroblox.so+0x2a264ad         log-buffer flush (0x2a26480)
#3  libroblox.so+0x2a84d84         crashpad::UnixCredentialSocket::RecvMsg
                                   ("unexpected msg name", "truncated msg",
                                    "incorrect payload size", util/linux/socket.cc)
#4  libroblox.so+0x2a3a1c0         ReceiveClientMessage (call @0x2a3a1bc)
#5  libroblox.so+0x2a3a032 / #6 0x2a39da3 / #7 0x2a32fdb
                                   crashpad::HandlerMain / InitializeWithClient
#8  libroblox.so+0x2eca569         writer 0x2ec9eed tail: call 0x2a31a50 @0x2eca565
#9  libroblox.so+0x2ecb12e         nativeRunCrashpadHandler 0x2eca720
#10 host route_crashpad_valid
```

Lathe writer `0x2ec9eed` sets the latch at `0x2ec9fae` (`movb $1,0x7cdd149`)
*before* the crash; the crash is strictly in the post-latch crashpad launch.

### Root cause is two stacked host-side gaps, not a guest logic fault

1. **IPC**: `RecvMsg` (crashpad `util/linux/socket.cc`) requires
   `SCM_CREDENTIALS` on the received message and an exact-size payload. The
   old `@SOCK@` path closed the peer immediately, so the guest got no
   credentials and logged `unexpected msg name`; with a raw message it logged
   `incorrect payload size 21`. The call site `0x2a3a1b1` passes
   `buf_size = 0x20` (32 bytes) to `RecvMsg`: the engine's
   `ExceptionHandlerProtocol::ClientToServerMessage` is
   `{int32 version=1; uint32 type=0 (kTypeCheckCredentials); uint64 stack;
   uint64 exc_info; uint64 sanitization}` = 32 bytes.
2. **stdio**: the guest computes stderr as Bionic `__sF+0x130` (stride 0x98).
   `crashpad_return` log flush calls `fputs@plt` at `0x2a2665d` and `fflush@plt`
   at `0x2a26665` with that pointer. The compat shim maps fwrite/fflush only
   under `SOBER_BIONIC_SF_FULL` and never maps `fputs`; glibc's `fputs`/`fflush`
   on a Bionic-layout `FILE*` faults in `_IO_fputs`/`_IO_fflush`.

## 2. Viable route (validated)

New opt-in module `src/jni/crashpad_return.c/.h`, kill switch
`SOBER_CRASHPAD_RETURN` (default off). It performs, with no guest byte write,
no fake callback/vtable and no host write to guest state:

1. `socketpair(AF_UNIX, SOCK_SEQPACKET, 0)` + `SO_PASSCRED` on both ends — the
   kernel attaches `SCM_CREDENTIALS` to the peer's send. (SOCK_STREAM also
   validated; SEQPACKET matches crashpad's own `CreateCredentialSocketpair`.)
2. A real detached server-peer thread sends the genuine 32-byte
   CheckCredentials message (`version=1, type=0`, zeros), then closes after
   200 ms. `RecvMsg` succeeds; the handler's epoll sees EPOLLERR, drops the
   client and returns 0.
3. A host-side Bionic `__sF` bridge exported from the module maps `fputs` and
   `fflush` on `__sF+0x098/+0x130` to host stdout/stderr. Wrappers are exact
   pass-throughs to glibc when the module is disabled, so the default run is
   unchanged. Root CMake uses `-rdynamic` and `-fvisibility=hidden`, hence the
   explicit `visibility("default")` on the two wrappers.
4. Channel string seeded via the guest's own setter `0x2e57c20` at
   `0x72a3710`; argv handed over as a faithful JNI `String[]`
   (`sober_jni_new_string_array`) to `nativeRunCrashpadHandler` `0x2eca720`.

### Evidence — scratch wiring of the exact module (real repo code copied)

```
[crashpad-return] server peer sent 32-byte CheckCredentials message (fd=16)
[ERROR exception_handler_server.cc:52] EPOLLERR: Error ... (104)
[INFO/rbx.JNIRobloxSettings] crashpad handler: succeed
[crashpad-return] 0x2ec9eed returned, argv=5 latch@0x7cdd149=1 withhold=1
[flags-latch] installed route=crashpad-return latch@0x7cdd149=1 installs=1 withhold=1
[app-shell] flags-latch module -> rc=0 latch=1
```
Exit status 13 (harness's seeded-state app-shell diagnostic), **not 139**:
no SIGSEGV. Earlier A/B in the same scratch, in order:
- closed peer, no stdio bridge → fault #0 `_IO_fputs` @ guest `0x2a2665d`;
- raw 21-byte peer, fputs bridge only → `incorrect payload size 21`;
- 32-byte CheckCredentials, fputs bridge only → `EPOLLERR`, then fault
  #0 `_IO_fflush` @ guest `0x2a26665`;
- 32-byte CheckCredentials + fputs+fflush bridge → `crashpad handler: succeed`,
  latch=1, no fault (module-wired run above).

## 3. Scope / gate

The route is seeded state and forces marker WITHHELD (decision 013/038).
`sober_crashpad_return_should_withhold()` must be ORed into `seeded_state` in
`guest_app_shell.c` (lead-owned) together with the four env knobs listed in
`crashpad_return.h`. No route sets a callback, emits the marker or patches a
guest byte. `flags_latch_default.*`, compat, CMake, tests and docs are untouched
in the real repo.
