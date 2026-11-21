# SCALE-A1 — id=1 subscription-node registration: full guard trace and root cause

Analysis only. No source, test, CMake, README, docs or git changes were made. One new
file (this one). Frozen engine:
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`,
BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(re-verified on disk). All target bytes/strings were treated as untrusted DATA.
Method: `objdump -d -M intel`, `readelf`, `nm`, source read-through; no Ghidra/REA.
For the text LOAD segment file offset == RVA, so every RVA below is a byte offset.

## 1. Verdict (up-front)

**No guest prerequisite is missing.** The reported `NO_NODE` is a *host-side node
discovery* defect, not a failed guest registration.

The guest genuinely builds and links the 0x60-byte id=1 subscription node, but it
does **not** store that node directly at `singleton + 0x288`. It first stores a
**0x18-byte intrusive-list container** at `+0x288` and links the node into that
container's `+0x08`. `sober_publish_default_find_node()` treats `+0x288` as the
node itself and compares the container's `+0x08` (a heap pointer) against the
dispatch pointer `base+0x27eaf04`, so it *always* returns NULL — even when the
guest registration succeeded. The extra indirection is at RVA `0x23172ce` /
`0x2317354` and `0x231772c` / `0x2317766`, which the existing analysis
(`analysis/C11-messagebus-publish.md` §4, `analysis/callback/task500-...` §1)
omitted.

Consequence: the diagnostic re-drive is **not** inert for the reason claimed; it
links a node the harness then fails to see. No fabricated guest state is needed —
the fix is one extra dereference in the read-only host finder. Conversely, **if
the finder is left unchanged it is impossible to produce a node that it will
accept without fabricating one**, because genuine guest code never writes the
node pointer to `+0x288` directly.

## 2. Complete guard/branch inventory from entry (0x253cb56) to insertion (0x253d09d)

`0x253cb56` is a straight-line publisher (nearest dynsym is
`nativeAppBridgeV2InitWithParams+0x90bc0`; it is an internal helper). From entry
to the id=1 insert call there are **exactly two** conditionals, both at entry:

| RVA | instruction | condition to continue |
| --- | --- | --- |
| `0x253cb7c`–`0x253cb7e` | `test edi,edi; je 0x253d1b7` | arg1 (event id) != 0 — re-drive passes 1 ✔ |
| `0x253cb84`–`0x253cb8b` | `cmp BYTE PTR [0x72a31ba],0; jne 0x253d1b7` | one-shot guard clear — harness reports 0 ✔ |
| `0x253cb91` | `mov BYTE PTR [0x72a31ba],1` | guard spent here, 13 bytes in |

Between `0x253cb91` and `0x253d09d` there is **no** conditional early exit: only
15 iterations of `mov edi,4; call 0x24a8e44` → build descriptor on stack → call a
publish helper → `call [vtable]` to destroy the temporary descriptor
(the `je/jne`/`call [rcx+rax]` sequences are only null checks on that teardown).
The id=1 block is the 13th:

```
253d064: mov edi,4
253d069: call 24a8e44          ; bus getter -> rax = base+0x72cbdd0
253d06e: lea rcx,[0x6dbbf98]   ; id=1 descriptor vptr
253d075: lea r14,[rbp-0x2a0]   ; descriptor
253d07c: mov [r14],rcx         ; +0x00 = vptr 0x6dbbf98
253d07f: mov QWORD [r14+8],1   ; +0x08 = id 1
253d087: mov QWORD [r14+0x10],0
253d08f: mov [r14+0x18],rbx    ; +0x18 = slot global (arg2 = base+0x7234520)
253d093: mov [r14+0x20],r14    ; +0x20 = self
253d097: mov rdi,rax           ; bus
253d09a: mov rsi,r14           ; descriptor
253d09d: call 253e126          ; insert into bus+0x288
```

The genuine call site is `0x24ad699` (`lea rsi,[0x7234520]; mov edi,1;
call 0x253cb56`) at the tail of `nativeAppBridgeV2InitWithParams 0x24abf96`; it is
unconditional within its basic block (`0x24ad5c0/0x24ad5d6` only branch around
refcount teardown, both converge on `0x24ad699`).

## 3. Requested checks — all negative

* **`0x24a8e44(4)` bus getter** (`0x24a8e44..0x24a8e7a`): with guard A `0x72cc270`
  and guard B `0x72cc718` armed, it executes `lea rax,[0x72cbdd0]` /
  `cmove rax,0x72cc278` on `id==3` and returns. With `id=4` it returns
  `base+0x72cbdd0` directly; it is not a service lookup that can return NULL or
  be skipped. Guards armed ⇒ the only remaining paths are constructor init
  (`0x2a0d6c0`, `0x30f9aae`, `0x2a0d7d0`), which the harness has already run.
  **Not the failure.**
* **Descriptor construction at `0x253d06e`**: vptr `0x6dbbf98`, id `1`, slot
  `&0x7234520`, self `&desc` — exactly what `sober_publish_default_descriptor()`
  validates (vptr/id/slot). **Not the failure.**
* **Stack canary** `0x72293f8` and `0x1ea9684` (called with global `0x7d8fb60`)
  appear in every helper but contain no branch that skips insertion.

## 4. Insertion chain and the missed indirection

```
253e126: lea rsi,[rdi+0x288]          ; rdi = bus object -> slot ADDRESS
253e150: call 253e180                 ; build(out=&stack, slot=&bus.field_288, desc)

253e180:
 253e191: r15 = slot address (base+0x72cbdd0+0x288)
 253e1a7: call 23172ce                ; <-- GET/CREATE LIST CONTAINER at the slot
 253e1ac: call 1dbab02(0x60)          ; r13 = subscription node
 253e1c2: call 253e21e                ; fill node
 253e1c7: rdi = [r15]                 ; = *(bus+0x288) = container
 253e1cd: call 231772c                ; intrusive insert(container, node)

23172ce (0x23172ec..0x2317357):
 23172ec: cmp QWORD PTR [rdi],0       ; slot empty?
 231731a: mov edi,0x18
 231731f: call 2317426                ; allocate 0x18-byte LIST CONTAINER
 231732e: [hdr+0x00] = 0x100000000    ; refcount
 2317331: [hdr+0x08] = 0              ; first node
 2317339: [hdr+0x10] = 0x7fffffff
 2317354: mov [r14],rax               ; *(bus+0x288) = hdr   <-- '+'0x288 now points to HDR

253e21e (0x253e232..0x253e27c) builds the 0x60-byte node:
 253e23c: node+0x08 = 0x27eaf04       ; dispatch
 253e24a: node+0x20 = [slot]          ; = hdr (intrusive next)
 253e271: node+0x28 = 0x246fb6a
 253e27c: call 226c048(node+0x30, desc);
           226c07a: [dest+0x20] = dest        ; node+0x50 = &embedded descriptor
           226c085/0x226c092: jmp [desc.vptr+0x18] ; copy descriptor into node+0x30

231772c (0x2317752..0x2317766):
 2317752: r12 = [hdr+0x08]
 2317766: mov [hdr+0x08],r14          ; hdr+0x08 = node   <-- real node linked ONE LEVEL DOWN
```

`0x27eaf04` confirms the ground truth: `27eaf1f: mov rdi,[rdi+0x50]` — the object
handed to the dispatcher is the 0x60-byte node, not the 0x18-byte container.

## 5. The host-side defect (exact code)

`src/jni/publish_default.c:243-255`:

```c
cur = *(void **)(buses[i] + SOBER_PD_OFF_BUS_LIST);   /* 0x288 -> 0x18-byte HDR */
dispatch = *(void **)(cur + SOBER_PD_NODE_OFF_DISPATCH); /* HDR+8 -> node POINTER */
if (dispatch == b + 0x27eaf04 && descriptor(b, cur) != NULL) return cur;
cur = *(void **)(cur + SOBER_PD_NODE_OFF_NEXT);       /* HDR+0x20 is OOB (hdr is 0x18) */
```

`HDR+0x08` is the node address (a heap pointer from `0x1dbab02`), never
`base+0x27eaf04`, so the dispatch test fails on the first iteration. `descriptor(b,cur)`
would read HDR+0x50, also outside the 0x18-byte container. `HDR+0x20` is out of
bounds, so the walk cannot recover. Both buses (default `0x72cbdd0`, mode-3
`0x72cc278`) use this same chain, so walking the second bus does not help.

## 6. Minimal genuine state / proof

* **Minimal fix (host, read-only, no fabrication):** in `find_node()`, after
  reading `hdr = *(bus+0x288)`, evaluate `node = *(hdr+0x08)` and validate
  `node+0x08 == base+0x27eaf04` plus `sober_publish_default_descriptor(base,node)`
  (descriptor at `node+0x50`); for the next link use the container's own forward
  chain rather than `hdr+0x20`. This changes no guest byte and calls no guest
  code. All genuine prerequisites (latch `0x7cdd149`, app shell, bus, guards
  `0x72cc270/0x72cc718`, placeId `0x72a31c0`, clear node guard `0x72a31ba`) are
  already present.
* **If `find_node()` must stay as written:** no amount of genuine guest state
  will satisfy it. `0x23172ce`/`0x231772c` unconditionally place the container at
  `+0x288` and the node at container+`+0x08`; there is no guest path that stores
  `0x27eaf04` at `bus+0x288+0x08`. Making the current test pass would require
  writing a synthetic node/descriptor into guest memory — fabrication, which is
  disallowed. Hence the current NO_NODE result is uninformative about the guest.

## 7. Caveats / honesty

* The structural claim is static and byte-exact from the frozen SHA above; I did
  not run a debugger to observe the linked node in memory.
* Local reproduction attempts with the prebuilt `build/guest-init-host` were
  inconclusive: `SOBER_MESSAGEBUS_PREINIT=1` (`0x25448f6`) faults before
  `0x24abf96`; with `SOBER_PREINIT_BUS=1` instead, `0x24abf96` returns but a later
  opt-in driver faults before the `publish_seeded` diagnostic is reached. That is
  a separate harness-stability issue and does not change the `find_node` finding.
* This analysis does **not** claim the notifier/callback chain fires; it only
  isolates why node discovery reports NO_NODE. Marker stays WITHHELD.
