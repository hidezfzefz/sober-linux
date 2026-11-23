# n1 #647 static-B — TLS getters / fn224 / dispatcher

READ-ONLY, engine SHA 3e498d64, taskset -c 4,5. Worktree /tmp/opencode/n1-static-b.
Faulting instruction first (harness recursion): `6ca800e: ff 90 20 07 00 00 call *0x720(%rax)` (and `6ca8090` in lambda 6ca805b) — JNIEnv slot 0x720 = fn[224] ExceptionCheck; repeated `0x6ca8014` frames exhaust the stack.

## PROVEN (quoted raw disasm)

TLS getters (file dis_6c89e50_70.txt):
```
6c89e54: lea 0x5f81ed(%rip),%rdi # 7282048
6c89e5b: call 2ea6440
6c89e74: lea 0x5f81ed(%rip),%rdi # 7282068
6c89e7b: call 2ea6440
6c89e94: lea 0x5f81ed(%rip),%rdi # 7282088
```
Keys = globals 0x7282048 / 0x7282068 / 0x7282088. Their shared getter 2ea6440 (dis_2ea6440.txt):
```
2ea644c: mov 0x10(%rdi),%r13        ; key index from key object +0x10
2ea6455: mov 0x...(%rip),%edi # 72a1b08
2ea645b: call 6d322e0 <pthread_getspecific@plt>
```
Key created once by pthread_once cb 2ea6640 (`2ea664f: call pthread_key_create`, key global 0x72a1b08, dtor 2ea6670 -> pthread_setspecific). Which getter returns real vs wrapper env: 6ca7f40 uses `call 6c89e50; mov 0x18(%rax),%r14` (object with +0x18 closure) then `call 6c89e70; mov (%rax),%rax; call *0x718(%rax)` (vtable object) — the two returns are structurally different; real-vs-wrapper identity UNPROVEN from static bytes alone.

fn224 / slot 0x720: JNIEnv offset 0x720 = index 224 = **ExceptionCheck** (analysis/jni/jni-map.md:51, recursion-note.md:38, src/jni/README.md:60 `ExceptionCheck 0x720 -> 224`). Real boot holds ART's ExceptionCheck function pointer there; harness installs sr_ExceptionCheck (native_jni.c). Exact real-boot pointer value UNPROVEN (no capture).

2a13560 full (dis_2a13560.txt, 24 bytes):
```
2a13569: call 2a0de60
2a1356e: mov (%rax),%rax
2a13571: mov 0x70(%rax),%rdi
2a13575: and $0xffffffffffffffc0,%rdi
2a13579: mov %rbx,%rsi
2a13582: jmp 2a12e80
```
Scheduler accessor: masks nesting low 6 bits, tail-jumps dispatcher 2a12e80.

2a13370 bit test (dis_2a13370.txt):
```
2a13374/2a13379: call 2a0de60
2a1337e: mov (%rax),%rax
2a13381: movzbl 0x20(%rax),%eax
2a13385: and $0x1,%al
```
Callers (callers_2a13370.txt): 55f5748, 55f58a1, 560ad0b — all in `IPlatformSystemDialogHandler$CppProxy_native_dismissAll` region. So bit0 of byte +0x20 of the scheduler/TLS object is consumed by dialog-dismiss code, not the scheduler nesting path.

## Evidence / NOT done

Evidence files: dis_6c89e50_70.txt, dis_2ea6440.txt, dis_2a13560.txt, dis_2a13370.txt, dis_fn224_write.txt, dis_wrapper_full.txt, callers_2a13370.txt, writes_720.txt.
NOT DONE: no runs/device/phone/captures, no guest writes/gate/seeder/fake returns, no apply. UNPROVEN: key-object +0x10 writers, real-vs-wrapper env identity, exact real-boot fn224 pointer, writer of byte +0x20 bit0. No questions.
First divergence: harness sr_ExceptionCheck at JNIEnv 0x720 vs real ART ExceptionCheck, re-entering 6ca7f72 via scheduled callback (obj+0x28 != 0). Next smallest step: static-A/harness confirm which TLS key holds which env under genuine JNI_OnLoad, read-only at 0x6ca800e.
