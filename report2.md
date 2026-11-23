# n1-2 #648 static-B — fn224 writer (READ-ONLY)

Engine SHA 3e498d64, taskset -c 4,5, worktree /tmp/opencode/n1-static-b.
Faulting instruction first: `6ca800e: ff 90 20 07 00 00 call *0x720(%rax)` — the fn[224] slot of the JNIEnv-style table in `rax` is dereferenced.

## PROVEN — writer is GENUINE GUEST CODE (not a host patch)

Quoted from dis_fn224_writer.txt (guest engine):
```
6c8e4cc: 48 8d 05 9f 9a 01 00  lea 0x19a9f(%rip),%rax  # 6ca7f72
6c8e4d3: 49 89 86 20 07 00 00  mov %rax,0x720(%r14)
```
Guest function `0x6c8e…` (the JNIEnv-style vtable builder, called from
`227bde8: e8 33 27 a1 04 call 6c8e520` in JNI_OnLoad+0xe8e and from
`6c8e5f2: call 6c8e691`) writes the address of the wrapper prologue `0x6ca7f72`
into table offset `0x720` (= fn[224] of a JNINativeInterface_-sized table with
writes at 0x0..0x740). This is a **SELF-REFERENCE**: the wrapper at 0x6ca7f72
calls `call *0x720(%rax)`, and the table it reads holds 0x6ca7f72 itself.
The neighbour writes confirm it is a function table:
```
6c8e4c5: mov %rax,0x718(%r14)   # 6ca7e40
6c8e4da: lea ... # 6ca80a6
6c8e4e1: mov %rax,0x728(%r14)
```
Host source check: native_jni.c only installs `SETFN(*t, 224, sr_ExceptionCheck)`
(line 1560) plus an unrelated slot-165 safety override; there is NO host patch
to fn[224] after setup. javavm_semantics.c:435 puts `fj_ExceptionCheck` in its
own table. So the harness flip host+0x2e690 -> guest+0x6ca7f72 is the guest's
own table-builder writing its wrapper, seen by the harness breakpoint.

## Real boot / ART self-reference

Real boot receives JNIEnv from ART: fn[224] = ART's ExceptionCheck in ART's
table (guest cannot patch ART memory). The guest's separately-built wrapper
table (0x6c8e4cc builder) holds 0x6ca7f72 in real boot too — this is genuine
guest behaviour. ART's ExceptionCheck itself cannot self-reference the guest
wrapper; the self-reference exists only in the guest-built wrapper table.

## Evidence / NOT done

Evidence: dis_fn224_writer.txt, stores_720_all.txt, writes_720.txt,
callers_2a13370.txt, /tmp/opencode/n1-harness/report.md lines 32-37.
NOT DONE: no runs/device/captures/guest writes/gate/seeder/apply; no fix.
UNPROVEN: exact containing-function entry and real-boot ART pointer value
(no capture). Next smallest step: read-only confirm in harness which table
(rax) is held at 0x6ca800e hit 100 vs hit 1 (already N1_CALL720 evidence).
