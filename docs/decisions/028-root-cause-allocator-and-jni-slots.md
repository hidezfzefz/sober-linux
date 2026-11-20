# Decision 028 — Root cause: unpatched calloc bridge + wrong JNI slots

Status: accepted (lead). Two independent parallel findings.

## 1. Guest allocation site (dispatch subagent, APS2 decode)

Allocator table in `.data`:

| Slot | Addend | Target | Patched? |
| --- | --- | --- | --- |
| `0x726c0b0` | `0x227aafa` | reallocate bridge | yes |
| `0x726c0b8` | `0x1dbe559` | free bridge | yes |
| `0x726c0c0` | `0x253225e` | **guest calloc** | **NO** |
| `0x726c0c8` | `0x1dbad62` | small-allocate bridge | yes |

The `0xc0dedbad` handle is created at `0x231f33d`:
`lea 0x726c0c0(%rip),%rax; mov $1,%edi; mov $0x1100,%esi; call *(%rax);
movl $0xc0dedbad,(%rax)`. A second creation/copy path is `0x6a35edf` with
`call *0x726c0c0` at `0x6a35f12`/`0x6a36160`. Neither writes `[handle+0x68]`.

`0x253225e` reads TLS arena key `0x7282000`; for large blocks it has a
partial-zero path that clears only the first QWORD when a page flag
(`cmpb $0,0x39(%page)`) is set. That leaves `+0x68` with stale code bytes,
explaining the invariant `0xffffff3085894cff`.

Fix: SEVENTH native bridge — trampoline at `0x253225e` (or slot rewrite at
`0x726c0c0`) to a zeroing `host_calloc(nmemb,size)`.

## 2. Our JNI table is miswired (code-review subagent)

`src/jni/native_jni.c:728-731` installs string functions at wrong indices.
Canonical Android `JNINativeInterface_` indices: `NewStringUTF=163`,
`GetStringUTFChars=165`, `ReleaseStringUTFChars=166`; slot 159 is `NewString`,
161 `GetStringChars`, 162 `ReleaseStringChars`. Our code puts the UTF variants
at 159/161/162 and leaves 163/165/166 as NULL stubs. Synthetic jstrings passed
to startup RVAs therefore cannot be read by the guest. Fix all six slots.

## 3. Test harness bug

`tests/app-shell.sh:22` only checks `guest_app_shell.c` when deciding to
rebuild, so source-level fixes to other files may not be linked into the
binary under test. Fix the guard to depend on all phase-3/4 sources/headers.
