# G7 — Concrete `frame_evidence` design for R6's six-part swap discriminator

Author: **G7** (group D). Scope: **this file only** — `analysis/G7_swap_discriminator.md`.
Analysis-only: no `src/`, gate, CMake or test file was edited. Engine frozen:
AppImage `libroblox.so`, BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
SHA-256 `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`.
Decisions **013/038 stay WITHHELD**; this design adds no false green.

> **Purpose.** `analysis/R6_swap_provenance.md §5.2` states six necessary
> conditions for accepting an `eglSwapBuffers` traversal at guest GOT `0x722a1a8`
> as a *guest-originated frame swap*. This document turns those six sentences
> into a buildable, testable module — **`frame_evidence`** — with exact byte
> patterns, state variables, algorithms, a C API, fail-closed rules and test
> vectors. It is a design for the lead to wire in, not an implementation and not
> a gate change. `render_ok` remains an additive AND; the sole marker site is
> untouched.

---

## 0. Mapping R6's six parts to implementation

| # | R6 §5.2 condition | `frame_evidence` artifact | Section |
| --- | --- | --- | --- |
| 1 | RA interval `[base, base+0x6d33b90)` | `sfe_in_guest_ra()` + identity guard | §2 |
| 2 | 5-byte indirect-call shape `call *0x18(%reg)` through vtable slot `0x7191300` | `sfe_decode_call_18()` + `sfe_vtable_ok()` | §3 |
| 3 | same RA recurs on ≥2 tagged swaps | `(RA,count)` recurrence table | §4 |
| 4 | ≥1 guest-RA GLES draw between two tagged swaps | RA-gated draw wrappers + `draws_between` | §5 |
| 5 | `0x72a3308` guest-written (gen delta at `0x72a3310`), not the host store | host-baseline + five-GOT observer + window credential | §6 |
| 6 | mixed synthetic + guest ⇒ `MIXED -> WITHHELD` | verdict logic + fail-closed list | §7 |

A swap is a **candidate** only after parts 1+2 (+ the vtable tie of part 2).
Parts 3–5 are evaluated over the accumulated candidate set. Part 6 can demote
any apparent pass. The final `GUEST-TAGGED` verdict requires all six.

---

## 1. Frozen constants and identity guard (all from R6 / R2 / T3-5)

```c
#define SFE_BUILDID32        0x204ad049u      /* read from ELF note at init      */
#define SFE_TEXT_SIZE        0x6d33b90u       /* first PT_LOAD, flags R E        */
#define SFE_GOT_SWAP         0x722a1a8u       /* eglSwapBuffers JUMP_SLOT        */
#define SFE_VTABLE           0x71912e8u       /* render vtable (APS2-populated)  */
#define SFE_VT_SWAP_OFF      0x18u             /* slot[3] @ 0x7191300             */
#define SFE_SWAP_WRAPPER     0x649d422u       /* vmethod: mov/jmp, tail jmp PLT  */
#define SFE_SWAP_PLT         0x6d334d0u       /* eglSwapBuffers@plt              */
#define SFE_G_WINDOW         0x72a3308u       /* global ANativeWindow*           */
#define SFE_G_WINDOW_GEN     0x72a3310u       /* window generation counter       */

/* three OBSERVED guest call sites of ANativeWindow_fromSurface@plt 6d31ad0 */
#define SFE_ANW_FROM_APPSTART 0x26fea78u      /* helper 0x26fea5a (Java-driven)  */
#define SFE_ANW_FROM_CREATED  0x2a2087cu      /* onSurfaceCreatedNative thunk    */
#define SFE_ANW_FROM_CHANGED  0x2a20907u      /* onSurfaceChangedNative thunk    */
```

Five ANativeWindow GOTs, installed **as a set** (C08 §4 Phase A step 3; R2 §2):
`{0x72294a8 (fromSurface), 0x72294a0 (release), 0x72297c0, 0x72297c8,
0x72297d0 (acquire/getWidth/getHeight)}`. Hooking only `0x72294a8` sends the
object into compat `ANativeWindow_release` at `0x26fea9b` and corrupts it.

**Identity guard (fail-closed F1).** At `init`, read the guest ELF note / verify
the SHA-256 out of band and require BuildID32 == `0x204ad049`. A mismatch makes
every RVAs above untrustworthy, so `frame_evidence` becomes permanently inert
and `sober_frame_evidence_render_ok()` returns 0. No constant is ever "guessed"
from a different build (Flatpak `791c3f9e…` is out of scope).

---

## 2. Part 1 — guest RA interval

Authoritative (R6 §4.1): the **first `PT_LOAD`**, `vaddr 0`, `filesz 0x6d33b90`,
flags `R E` → `base ≤ ra < base + 0x6d33b90`. This includes `.text`
`[0x1db9a40,0x6d3145a)` and `.plt` `[0x6d319a0,0x6d33b90)`.

```c
static int sfe_in_guest_ra(uintptr_t base, uintptr_t ra) {
    return base != 0 && ra >= base && ra < base + (uintptr_t)SFE_TEXT_SIZE;
}
```

- The host Route-B swap (R6 §2/§3) has RA in the host PIE `0x5637a6…`, ~0x29
  bits *below* `base=0x7f0556700000`; it fails this test. Native first-frame
  swap S1 never traverses the GOT at all. The interval check alone rejects both
  host swaps — that is why R6 calls it the unforgeable core's first leg.
- `base` is the same load bias used by `render_guest`; read it once at init and
  freeze it. If `base == 0`, fail closed (F2).
- Optional *secondary* tightening (not authoritative): require RA to fall in
  `.text` proper rather than PLT; this is redundant with the opcode check in §3
  and must not replace it. Do **not** use `dladdr()` to promote an RA — it is
  label-only; an RA outside the interval is never accepted.

---

## 3. Part 2 — exact byte patterns for `call *0x18(%reg)`

### 3.1 Decoder contract

The return address captured at the GOT hook is the address **immediately after**
the `call` instruction that dispatched through the render vtable. Because
`0x649d422` is a tail-`jmp` (no new frame), the RA is the guest return site of
`call QWORD PTR [vtable+0x18]`, not of the wrapper. The decoder receives `ra`
and scans backwards up to 8 bytes for an `FF /2` (CALL r/m64) instruction that
ends exactly at `ra`, has a non-zero displacement form, and whose displacement
is exactly `0x18`.

Verdict values: `CALL18_ACCEPT`, `CALL18_REJECT`, `CALL18_UNDECODABLE`.

### 3.2 Accepted encodings (bytes **verified with `objdump -D -b binary -m
i386:x86-64` against the frozen toolchain this task**)

`FF /2` = CALL r/m64. `mod` must be `01` (disp8) or `10` (disp32) — `mod=00`
means no displacement (or RIP-relative for `rm=101`) and is rejected.

| Bytes (hex) | Length | Disassembly | Note |
| --- | --- | --- | --- |
| `ff 50 18` | 3 | `call *0x18(%rax)` | canonical disp8 |
| `ff 51 18` | 3 | `call *0x18(%rcx)` | |
| `ff 52 18` | 3 | `call *0x18(%rdx)` | |
| `ff 53 18` | 3 | `call *0x18(%rbx)` | |
| `ff 55 18` | 3 | `call *0x18(%rbp)` | mod=01,rm=101 = `[rbp+disp8]`, valid |
| `ff 56 18` | 3 | `call *0x18(%rsi)` | |
| `ff 57 18` | 3 | `call *0x18(%rdi)` | common: `this` in RDI |
| `ff 54 24 18` | 4 | `call *0x18(%rsp)` | rm=100 ⇒ SIB byte `24` |
| `41 ff 50 18` | 4 | `call *0x18(%r8)` | REX.B |
| `41 ff 56 18` | 4 | `call *0x18(%r14)` | REX.B |
| `41 ff 57 18` | 4 | `call *0x18(%r15)` | REX.B |
| `41 ff 54 24 18` | 5 | `call *0x18(%r12)` | REX.B + SIB |
| `ff 90 18 00 00 00` | 6 | `call *0x18(%rax)` | canonical disp32 |
| `ff 94 24 18 00 00 00` | 7 | `call *0x18(%rsp)` | disp32 + SIB |
| `41 ff 90 18 00 00 00` | 7 | `call *0x18(%r8)` | REX.B + disp32 |

An optional REX prefix `0x40..0x4f` (`REX.W` 0x48 is redundant for `FF /2` but
tolerated) may precede any of these; `REX.B` extends `rm` to `r8..r15` and
`REX.X` extends the SIB index. The decoder records the effective base register
for diagnostics only.

### 3.3 Rejected encodings (equally exact)

| Bytes | Disassembly | Why rejected |
| --- | --- | --- |
| `e8 xx xx xx xx` | `call rel32` (direct) | host Route-B `guest_render.c:354`; guest direct call to `0x649d422` is also non-vtable |
| `ff 10` | `call *(%rax)` | no `+0x18` displacement |
| `ff 50 20` | `call *0x20(%rax)` | wrong displacement |
| `ff 15 18 00 00 00` | `call *0x18(%rip)` | `mod=00` → GOT-style call, not a vtable slot load |
| `ff 90 20 00 00 00` | `call *0x20(%rax)` | wrong disp32 |
| any of §3.2 with RA outside `[base,base+0x6d33b90)` | — | part 1 fails; host-RA form of a guest-looking instruction |
| any of §3.2 when `[vtable+0x18] != base+0x649d422` | — | wrong vtable; opcode alone is not enough |

### 3.4 Backwards-decoder pseudocode

```c
/* never reads before the mapping start: ra is >= base+0x1db9a40 in practice */
static int sfe_decode_call_18(uintptr_t ra, unsigned *len_out) {
    const unsigned char *p8 = (const unsigned char *)ra;
    for (unsigned L = 3; L <= 8; ++L) {
        const unsigned char *s = p8 - L;
        const unsigned char *p = s;
        unsigned char rex = 0, modrm, mod, regop, rm, sib = 0;
        long disp = 0;
        int have_sib = 0, dlen;

        if (*p >= 0x40 && *p <= 0x4f) rex = *p++;       /* optional REX */
        if (*p != 0xff) continue;                        /* CALL r/m64    */
        modrm = *(p + 1);
        if (((modrm >> 3) & 7) != 2) continue;           /* must be /2    */
        mod = modrm >> 6;  rm = modrm & 7;
        if (mod == 0) continue;                          /* needs disp    */
        p += 2;
        if (rm == 4) { sib = *p++; have_sib = 1; }
        if (mod == 1) { disp = (int8_t)*p;     dlen = 1; }
        else          { disp = (int32_t)sfe_load32(p); dlen = 4; }
        p += dlen;
        if (p != p8) continue;                           /* must end at RA */
        if (disp != 0x18) continue;                      /* +0x18 slot     */
        (void)rex; (void)sib; (void)have_sib;
        if (len_out) *len_out = L;
        return CALL18_ACCEPT;
    }
    return CALL18_REJECT;
}
```

Notes:
- Try all lengths (3…8) rather than assuming 5: R6's "5 bytes" is a
  shorthand; the real encodings are 3–8 bytes (§3.2). The table above is the
  normative set; a candidate whose preceding bytes are not in it is rejected.
- The decoder is a **runtime necessary check**. It is deliberately not
  sufficient: the site is not pinned statically (~11,600 `call *0x18(%r..)`
  sites in the guest), so acceptance also requires the vtable tie below and an
  offline `objdump` slice at the captured RA (§10 test vector A/§11 evidence).

### 3.5 Vtable tie (part 2, second half)

At init (and again at the first candidate) verify
`*(uintptr_t *)(base + 0x7191300) == base + 0x649d422`. This is the value APS2
populates and `render_guest.c:538-543` already checks. The opcode check says
"an indirect call through a +0x18 slot"; the vtable tie says "that slot is the
render swap vmethod". Together they tie the swap to the real frame path and
reject an unrelated guest `call *0x18(%reg)` (e.g. `0x649db51` `call *0x50(%rax)`
would already fail the `0x18` test). Failure ⇒ F5, no candidates accepted.

---

## 4. Part 3 — recurrence tracking (stable-loop discriminator)

State (process-global atomics, survive render shutdown):

```c
#define SFE_RA_SLOTS 8
static _Atomic uintptr_t s_ra_slot[SFE_RA_SLOTS];   /* guest RAs, zero = empty */
static _Atomic uint32_t  s_ra_hits[SFE_RA_SLOTS];
static _Atomic uintptr_t s_ra_first;                /* for the log line        */
static _Atomic uint64_t  s_swaps_candidate;         /* parts 1+2 satisfied      */
```

`sfe_note_candidate(ra)`:

1. `atomic_fetch_add(&s_swaps_candidate, 1)`.
2. If `s_ra_first == 0`, set it to `ra` (used only for the normative log).
3. Look up `ra` in `s_ra_slot[]`; if absent, claim an empty slot. Increment
   `s_ra_hits[i]` (saturate at `UINT32_MAX`).
4. `s_loop_stable = 1` iff **any** slot has `hits ≥ 2`.

Why "any slot" rather than "the first RA": a real loop may have two nearby
`call [vtable+0x18]` sites (e.g. a front/back-buffer branch); requiring the
*same* RA on two swaps is R6's wording and is satisfied by any slot reaching 2.
A one-off guest-origin swap (hits all == 1) fails closed. The host Route-B call
never enters any slot because its RA fails part 1.

---

## 5. Part 4 — bracketing guest-RA draw requirement

### 5.1 Why the existing `draw_calls` counter cannot be reused

`render_guest.c:142-150` sums per-trampoline `incq` counters with **no RA
filter**, so any invocation counts — that is the "host-forgeable" weakness R6
§5.1 calls out. `frame_evidence` needs its own **RA-gated** observer.

### 5.2 RA-gated draw observer (data-only patching)

Replace the plain counting trampolines for the GLES draw/clear entry points with
**typed C forwarding wrappers** installed as the GOT / extension-table function
pointers. Because the wrapper is entered by a real guest `call`,
`__builtin_return_address(0)` inside it *is* the guest draw call site:

```c
/* one per needed signature; show the glDrawElements shape */
typedef void (*sfe_real_drawelements)(int, int, int, const void *);
static sfe_real_drawelements s_real_glDrawElements;

static void sfe_wrap_glDrawElements(int mode, int count, int type,
                                    const void *indices)
{
    uintptr_t ra = (uintptr_t)__builtin_return_address(0);
    if (sfe_in_guest_ra(s_base, ra)) {
        atomic_fetch_add_explicit(&s_draws_guest_ra, 1, memory_order_relaxed);
        sfe_note_draw_ra(ra);          /* expected: dispatcher 0x6496d0c sites */
    }
    s_real_glDrawElements(mode, count, type, indices);   /* tail call, same ABI */
}
```

Guest draw sites to accept (T3-5 §2.2, OBSERVED): dispatcher `0x6496d0c` with
branches `glClear@0x6494ae1`, `glDrawElements@0x6496dbc`,
`glDrawArrays@0x6496e1a`, `glDrawElementsInstanced@0x6496d75`,
`glDrawArraysInstanced@0x6496dec`, `glClearBufferfv@0x6494991/0x6494a79`,
`glClearBufferiv@0x6494aa3`, `glClearBufferfi@0x64949d2` (plus extension-table
slots `0x77a4fe0/8`, `0x77a4fa0/8/b0`, `0x77a4f98`, `0x77a4f78`). The native
first frame calls its own `dlsym`'d `glClear*`, never these slots, so a false
draw from S1 is impossible.

Alternative for signatures that cannot be typed: an assembly trampoline that
writes `last_ra` into a per-stub slot using `r10`/`r11` (SysV *non-argument*
caller-saved scratch) — `mov (%rsp),%r11; movabs r10,<slot>; mov %r11,(%r10);
incq 8(%r10); movabs rax,<real>; jmp *rax` — with the swap hook sampling the
slots. Typed C wrappers are preferred: they preserve XMM argument registers
(e.g. `glClearBufferfv`'s `const GLfloat *` is a pointer, but other draw APIs
carry floats) without hand-rolling register save/restore.

### 5.3 Bracketing algorithm

```c
static _Atomic uint64_t s_draws_guest_ra;   /* RA-gated draw count              */
static _Atomic uint64_t s_draws_snapshot;   /* value at previous candidate swap */
static _Atomic uint64_t s_draws_between;    /* max delta between two swaps      */
```

In the swap hook, **before** forwarding:

```c
uint64_t d = atomic_load_explicit(&s_draws_guest_ra, memory_order_relaxed);
uint64_t prev = atomic_exchange(&s_draws_snapshot, d);
if (is_candidate && d > prev && d - prev > atomic_load(&s_draws_between))
    atomic_store(&s_draws_between, d - prev);  /* bracket only between tagged swaps */
    /* snapshot refreshes at every swap, but only a candidate may set the gate*/
```

`part4_ok` iff `s_draws_guest_ra ≥ 1 && s_draws_between ≥ 1 && s_swaps_candidate ≥
2`: at least one guest-RA draw strictly between two tagged swaps. A single draw
before a single swap is not a frame interval (T4-4 R2, C04 §3 step 2). The
forwarding order (count before `orig()` call) keeps the counter consistent with
what the guest has already drawn at the observation point.

---

## 6. Part 5 — guest-written window detection (`0x72a3308` / `0x72a3310`)

### 6.1 The hole R6 §5.1 identifies

The current gate conjunct `*(void **)(base+0x72a3308) != NULL`
(`guest_app_shell.c:1780`) is host-forgeable: `render_guest.c:417-423`
**directly stores** its synthetic `s_anw` there and `atomic_inc`s
`0x72a3310`. A non-NULL value proves nothing about guest authorship.

### 6.2 Two-tier credential

**Tier A — decisive, always-on (no synthetic hook required).**

At `init`, snapshot:

```c
s_win_baseline = *(uintptr_t *)(base + SFE_G_WINDOW);
s_gen_baseline = *(uint32_t  *)(base + SFE_G_WINDOW_GEN);
s_host_window  = sober_render_guest_window();   /* the host's synthetic s_anw,
                                                    may be NULL if module unused */
```

The generation counter is incremented **only** by the guest helper at
`0x26fea90` (`incl 0x72a3310` inside `0x26fea5a`) and by the host install.
Therefore, after the host baseline is captured:

- `part5_ok` requires `gen_now > s_gen_baseline` (a *post-install* increment —
  the host install increment is already in the baseline),
- `win_now != NULL` and `win_now != s_host_window` (the guest replaced the
  host's window, rather than merely leaving it there),
- window shape plausible: non-zero `refs`/`magic/version` header, vtable-like
  pointer; reject a bare 12-byte compat-shim pointer (`R1` / T3-5 §3.3),
- all four values sampled **after** a candidate swap, so the window is bound in
  the same counting window as the frame, not merely at boot.

A default run (host store only) has `gen_now == s_gen_baseline` and
`win_now == s_host_window` ⇒ fail closed. This is the credential that R6's
plain non-NULL check lacked.

**Tier B — corroborating RA (when the observation hook is installed).**

A **forwarding** `ANativeWindow_fromSurface` wrapper at GOT `0x72294a8`
(installed with the other four GOTs as a set, §1) counts an RA only when it is
exactly one of `base+0x26fea78`, `base+0x2a2087c`, `base+0x2a20907`. That
ties the generation increment to the guest bind path. Per the lead's ruling
(`MODULE_INTEGRATION.md §2`), the *hook's presence* is supporting evidence, not
a hard requirement: a clean run may use the real NDK path, and a missing hook
must cause neither a false green nor a false red. Tier A is authoritative;
Tier B strengthens the log line when available.

### 6.3 Install discipline

- Five GOTs hooked together with `mprotect(READ|WRITE)` → 8-byte slot store →
  restore original protection (the `rg_got_write` pattern,
  `render_guest.c:217-235`). All forwarding, no substitution in a proof run.
- Lazy PLT binding: write the **GOT JUMP_SLOT at base+RVA**, not the PLT stub
  (R2 §1), so no resolver can overwrite the hook.
- Never call `0x26fea78` directly without a real Java Surface; a Surface-token
  driver that makes the window "bound" is a withheld diagnostic (F9).

---

## 7. Part 6 — fail-closed rules

The verdict logic returns `GUEST-TAGGED` only when every gate below passes.

| ID | Rule | Effect |
| --- | --- | --- |
| F1 | BuildID32/SHA mismatch, or base not from the frozen ELF | inert; render_ok = 0 |
| F2 | `base == 0` or observer hooks failed to install | inert / WITHHELD |
| F3 | RA outside `[base,base+0x6d33b90)` | not a candidate; counted synthetic |
| F4 | preceding bytes not a §3.2 `call *0x18(%reg)` (incl. direct `e8`) | not a candidate |
| F5 | `[base+0x7191300] != base+0x649d422` | no candidates accepted |
| F6 | recurrence: max `ra_hits < 2` | WITHHELD(RECURRENCE) |
| F7 | draws: `draws_guest_ra < 1` or `draws_between < 1` or candidates < 2 | WITHHELD(BRACKET_DRAW) |
| F8 | window: gen ≤ baseline or window NULL/equal to host synthetic/shape bad | WITHHELD(WINDOW) |
| F9 | any withhold env knob or seeded_state in the gate chain | WITHHELD(SEED), independent of evidence |
| F10 | **mixed evidence**: any `tag=synthetic` swap since init alongside ≥1 candidate | `MIXED -> WITHHELD` (never GUEST-TAGGED) |
| F11 | suppressed counters / substituting GLES driver / any non-forwarding patch | WITHHELD |
| F12 | no offline confirmation of the captured RA's preceding bytes (§11) | diagnostic only; not promoted to proof |

**Counting window for F10** = all swaps observed since `sober_frame_evidence_init()`.
The default Route-B call S2 always leaves one `tag=synthetic` swap in that
window, so a default run with a genuine guest swap is correctly `MIXED`.
Reaching a clean `GUEST-TAGGED` *diagnostic* requires an explicit
`[ev-suppress]`-style suppression of S2 (`SOBER_RENDER_SUPPRESS_SYNTHETIC` in
T3-5 §5/E5) — which is itself in the withhold chain, so no marker is emitted.
This is intentional: decision 013 accepts no false green.

**Additive-AND safety.** `frame_evidence` reads no `SOBER_*` in its proof path,
is observation-only, and is default-on. The gate keeps `seeded_state ⇒
shell_cb=0`, the `!shell_cb || !render_ok → -4` return, and the single marker
site. `render_ok` becomes `sober_render_phase3_done() &&
sober_frame_evidence_render_ok()` — phase-3 ancestry is required but never
sufficient. Every new knob must join the withhold chain **in the same change**;
no unilateral gate/test edit is proposed.

---

## 8. C-level API sketch (new files `frame_evidence.h` / `.c`)

```c
/* frame_evidence.h — analysis sketch; lead-owned wiring */
#ifndef SOBER_FRAME_EVIDENCE_H
#define SOBER_FRAME_EVIDENCE_H
#include <stddef.h>
#include <stdint.h>

enum sober_frame_verdict {
    SFE_ABSENT = 0,       /* not installed / no observations                  */
    SFE_WITHHELD,         /* partial evidence or a seed knob                  */
    SFE_MIXED,            /* synthetic + guest in counting window (F10)       */
    SFE_GUEST_TAGGED      /* all six R6 parts satisfied                       */
};

/* reason bits for the verdict/diagnostic string */
#define SFE_R_IDENTITY      (1u<<0)   /* F1  */
#define SFE_R_BASE          (1u<<1)   /* F2  */
#define SFE_R_RA_INTERVAL   (1u<<2)   /* F3  */
#define SFE_R_OPCODE_SHAPE  (1u<<3)   /* F4  */
#define SFE_R_VTABLE_SLOT   (1u<<4)   /* F5  */
#define SFE_R_RECURRENCE    (1u<<5)   /* F6  */
#define SFE_R_BRACKET_DRAW  (1u<<6)   /* F7  */
#define SFE_R_WINDOW        (1u<<7)   /* F8  */
#define SFE_R_SEED          (1u<<8)   /* F9  */
#define SFE_R_MIXED         (1u<<9)   /* F10 */
#define SFE_R_PHASE3        (1u<<10)  /* ancestry ANDed by the gate           */

struct sober_frame_evidence {
    uintptr_t base;
    uint64_t  swaps_total, swaps_candidate, swaps_synthetic;
    uintptr_t ra_first;
    uint32_t  max_ra_hits;
    uint64_t  draws_guest_ra, draws_between;
    uintptr_t win_baseline, gen_baseline, win_now, gen_now, host_window;
    uint64_t  from_surface_guest_ra;    /* Tier B corroboration */
    int       installed, identity_ok, withhold_seen;
};

/* 0 = installed; <0 = fail-closed inert. Call BEFORE render_guest_install
 * so the host window/gen publish is captured in the baseline. */
int  sober_frame_evidence_init(uintptr_t guest_base);

/* Called from the observation hooks; all are safe no-ops when inert. */
void sober_frame_evidence_observe_swap(uintptr_t ra);
void sober_frame_evidence_observe_draw(uintptr_t ra);
void sober_frame_evidence_observe_from_surface(uintptr_t ra);
void sober_frame_evidence_sample_window(void);   /* after each candidate swap */

/* Verdict + reason bitmap; re-entrant, safe after render shutdown. */
enum sober_frame_verdict sober_frame_evidence_verdict(uint32_t *reasons_out);
int  sober_frame_evidence_render_ok(void);       /* verdict==GUEST_TAGGED
                                                    (gate ANDs phase3_done) */

/* Diagnostics / built-in vector self-test (no guest needed). */
const char *sober_frame_evidence_describe(char *buf, size_t cap);
int  sober_frame_evidence_selftest(char *buf, size_t cap);

#endif
```

**Internal hook wiring** (lead-owned, new GOT write path; no engine bytes
patched): extend `rg_hook_swap` (`render_guest.c:255-264`) so it calls
`sober_frame_evidence_observe_swap(ra)` with the captured
`__builtin_return_address(0)`; install RA-gated typed draw wrappers (§5.2) over
the three PLT GOTs `0x7229fc8 / 0x722a040 / 0x722a048` and the extension-table
entries; install the forwarding fromSurface wrapper with the five-GOT set
(§6.3). All state stays in process-global atomics, never in `SoberRender *`, so
the gate can read it after render shutdown.

### 8.1 Alignment with the concurrent `frame_evidence` module (read-only observation)

While this analysis was authored, another agent produced
`src/jni/frame_evidence.{h,c}` (F7, observation-only). Its actual API is
`frame_evidence_set_guest_base()`, `frame_evidence_record_swap(ra)`,
`frame_evidence_record_draw(ra)`, `frame_evidence_record_window_bind(window,
gen, ra)`, `frame_evidence_observe_window_global(base, ra)`, immutable
`SoberFrameEvidenceEvent` records, `guest_render_ok()` and verdict codes
`ABSENT/MIXED/INCOMPLETE/GUEST_TAGGED`. That module is a sound **event recorder**
and implements parts 1 and 6 of R6 (RA interval; sticky mixed-evidence taint).
Relative to R6's six parts it does **not** yet provide:

- part 2 — no `FF /2` opcode decoder or vtable-slot tie (§3 above);
- part 3 — no `(RA,count)` recurrence table, so `swaps_guest >= 1` still accepts
  a one-off guest swap (R6 requires ≥2 with a stable RA);
- part 4 — no `draws_between` bracketing, so a draw merely counted anywhere in
  the run satisfies `draws >= 1`;
- part 5 — `frame_evidence_record_window_bind()` accepts the *first*
  generation value (`prev == 0`) and requires only non-NULL + guest RA +
  strictly increasing gen. It never snapshots a **host baseline** nor compares
  the window against the host synthetic pointer, so a host store
  (`render_guest.c:417-423`) observed under a guest-RA call site could still
  bind (§6.2 Tier A closes this);
- identity guard F1 (BuildID/SHA) is absent.

The `sfe_*` sketch in §8–§9 is the proposed **superset**: the lead can either
extend the F7 module in place (reusing its immutable record log and
mixed-evidence taint) or add a companion that supplies the four missing parts.
Either way the gate wiring stays a single additive AND; no marker change is
proposed here.

---

## 9. Reference verdict implementation

```c
enum sober_frame_verdict sober_frame_evidence_verdict(uint32_t *why) {
    uint32_t r = 0;
    enum sober_frame_verdict v = SFE_WITHHELD;

    if (!atomic_load(&s_identity_ok)) { r |= SFE_R_IDENTITY; goto out; }
    if (!atomic_load(&s_base))        { r |= SFE_R_BASE;      goto out; }

    if (atomic_load(&s_swaps_synthetic) > 0 &&
        atomic_load(&s_swaps_candidate) > 0) { r |= SFE_R_MIXED; v = SFE_MIXED; goto out; }
    if (atomic_load(&s_withhold_seen))  r |= SFE_R_SEED;
    if (atomic_load(&s_max_ra_hits)   < 2) r |= SFE_R_RECURRENCE;
    if (atomic_load(&s_draws_guest_ra) < 1 ||
        atomic_load(&s_draws_between) < 1 ||
        atomic_load(&s_swaps_candidate) < 2)  r |= SFE_R_BRACKET_DRAW;

    sfe_sample_window();
    if (atomic_load(&s_gen_now) <= atomic_load(&s_gen_baseline) ||
        atomic_load(&s_win_now) == 0 ||
        atomic_load(&s_win_now) == atomic_load(&s_host_window) ||
        !sfe_window_shape_ok(atomic_load(&s_win_now)))
        r |= SFE_R_WINDOW;

    v = (r == 0) ? SFE_GUEST_TAGGED : SFE_WITHHELD;
out:
    if (why) *why = r;
    return v;
}
```
`sober_frame_evidence_render_ok()` additionally requires
`sober_render_phase3_done()` (F/SFE_R_PHASE3) and a clean verdict. The positive
verdict string enumerates the six legs, e.g.
`render provenance: guest-tagged frame at 0x649d422 (swaps=N draws=M ra=base+R
fromSurface=observed window=guest-bound) -> GUEST-TAGGED`; the negative path
lists failed reason names `ABSENT(R1|R4|R5|MIXED) -> WITHHELD`.

---

## 10. Test vectors

### A. Decoder vectors (bytes immediately before RA; run in `selftest`)

Each vector is a fake RA pointing just past the bytes in an RWX test page;
`sfe_decode_call_18` result is asserted.

| # | Bytes before RA | Expected | Reason on reject |
| --- | --- | --- | --- |
| A1 | `ff 50 18` | ACCEPT len 3 | — |
| A2 | `ff 90 18 00 00 00` | ACCEPT len 6 | — |
| A3 | `41 ff 50 18` | ACCEPT len 4 | — |
| A4 | `ff 54 24 18` | ACCEPT len 4 | — |
| A5 | `41 ff 54 24 18` | ACCEPT len 5 | — |
| A6 | `ff 57 18` `ff 53 18` `ff 55 18` | ACCEPT ×3 | — |
| A7 | `e8 00 00 00 00` | REJECT | direct call (host Route B) |
| A8 | `ff 10` | REJECT | no displacement |
| A9 | `ff 50 20` | REJECT | displacement ≠ 0x18 |
| A10 | `ff 15 18 00 00 00` | REJECT | RIP-relative, mod=00 |
| A11 | `ff 90 20 00 00 00` | REJECT | disp32 ≠ 0x18 |
| A12 | A1 bytes but RA = host `0x5637a6…` | REJECT at part 1 | RA interval |
| A13 | A1 bytes, `[vtable+0x18]` tampered | REJECT at F5 | vtable tie |

### B. Scenario vectors (state → verdict)

`base=0x7f0556700000`, `S2 synthetic swap present` unless noted.

| # | State | Expected verdict / reasons |
| --- | --- | --- |
| V0 | init only, no events | ABSENT |
| V1 | S2 only: total=1, synth=1, cand=0, RA host | WITHHELD(RA_INTERVAL); no `tag=guest-loop` line |
| V2 | 1 candidate (RA in guest, A1 opcode), hits=1, no draw, host window | WITHHELD(RECURRENCE\|BRACKET_DRAW\|WINDOW) |
| V3 | 2 candidates same RA, hits=2, no draw | WITHHELD(BRACKET_DRAW\|WINDOW) |
| V4 | 2 candidates same RA, 1 guest draw between, but `win==host_window`, `gen==baseline+1` | WITHHELD(WINDOW) — host store only |
| V5 | as V4 + guest window (`gen>baseline`, `win!=host_window`, Tier-B RA `0x2a2087c`) **but S2 present** | **MIXED → WITHHELD(MIXED)** (F10) |
| V6 | as V5 but S2 suppressed via `[ev-suppress]` knob | GUEST_TAGGED (withhold_seen=1 ⇒ gate still withholds marker) |
| V7 | 2 candidates with *different* RAs, hits=1 each | WITHHELD(RECURRENCE) |
| V8 | 2 candidates, A7 (`e8`) preceding bytes | WITHHELD(OPCODE_SHAPE) — actually 0 candidates |
| V9 | 2 candidates, valid A1 bytes, `[0x7191300]` mismatch | WITHHELD(VTABLE_SLOT) |
| V10 | BuildID mismatch | ABSENT(IDENTITY) |

### C. Runtime A/B procedure (all `SOBER_*` off unless named; R6 §5.3)

- **E0 control (default).** Expect `swaps(total/guest/synth)=1/0/1`, draws=0, no
  `tag=guest-loop` line, `render_ok=0`, marker withheld. Assert
  `grep -q 'swaps(total/guest/synth)=[1-9][0-9]*/0/[1-9][0-9]*'`.
- **E1 RA split.** Inject a second *host* Route-B call: expect total=2,
  guest=0, synth=2. `guest` must never increment (F3). A guest-loop swap then
  yields `ra=base+0x…`, A1/A2 opcode, `tag=guest-loop`; offline `objdump`
  confirms `call *0x18(%reg)` at `ra-N..ra`.
- **E2 draw.** Guest draw sites counted with guest RA; native `glClear` not
  counted; `draws_between≥1` only when two tagged swaps bracket a draw.
- **E3 window bind** (`SOBER_EVIDENCE_SURFACE=1`, withheld). Five GOTs installed;
  fromSurface RA one of the three sites; gen delta beyond host baseline;
  window != host synthetic. A/B: hooking only `0x72294a8` corrupts the shim at
  `0x26fea9b`.
- **E4 suppression / mixed.** `SOBER_RENDER_SUPPRESS_SYNTHETIC=1`: no
  `tag=synthetic` in window; both tags present ⇒ `MIXED -> WITHHELD`.
- **E5 positive diagnostic** (all legs artificially present): normative log
  line below appears, but every driving knob is in the withhold chain ⇒ no
  marker. Marker still requires genuine notifier-driven
  `gameLoadedCallback` (`0x27eaf6e`, `placeId>0`, RA `0x27eafb3`) with zero
  seeded state.

Normative positive assertion (never produced by either host swap):

```
[render] guest-tagged swap at 0x649d422 ra=base+0x[0-9a-f]+ tag=guest-loop | guest-tagged swaps: [1-9][0-9]* | guest GLES draws between tagged swaps: [1-9][0-9]*
```

---

## 11. Honesty limits / open items

- The exact guest frame-loop `call [vtable+0x18]` site cannot be pinned
  statically (~11,600 candidates). Runtime RA capture plus **offline `objdump`
  confirmation** of the §3.2 bytes at the captured RA is required before an E1
  observation is called proof; the runtime decoder alone is necessary, not
  sufficient.
- Whether the app-shell frame consumes global `0x72a3308` or `nativeData+0x140`
  is UNKNOWN (P8 §5); both are seeded together today, so a window credential
  obtained via a seeded path stays withheld.
- Tier B's fromSurface RA observer is corroborating only, per the lead's ruling
  that the hook is not a hard requirement. Tier A (host-baseline + gen delta +
  pointer deviation + shape) is the decisive guest-write test and must not be
  weakened to "non-NULL".
- The three-vs-two GLES draw signatures need matching typed wrappers; XMM-heavy
  entry points must use the typed path, not the bare `incq` trampoline.
- **No runtime test was run for this document.** No evidence line in §10 has
  been observed. Engine SHA/BuildID unchanged; decision 013/038 unweakened;
  **marker WITHHELD; no success claimed.**

## 12. Evidence index (all read-only)

`analysis/R6_swap_provenance.md` §4–§6 (six-part discriminator, byte examples,
A/B procedure); `analysis/T3_5_render-evidence.md` §1–§6 (state, hooks, verdict,
E0–E5); `analysis/R2_got_hook_reachability.md` §1–§2 (five GOTs, three
fromSurface sites, host-written window); `analysis/C08-surface-frame.md` §4
Phase A step 3; `analysis/MODULE_INTEGRATION.md` §2 (render evidence contract,
fromSurface ruling); `analysis/P8_anw_guest_consumption.md` §2/§5;
`analysis/R1_zero_tagged_swaps.md` §4; `src/jni/render_guest.{c,h}`,
`src/linux/guest_render.c`, `src/linux/guest_app_shell.c:1777-1780`; plus
`objdump`/`readelf` over the frozen ELF. No other file was modified.
