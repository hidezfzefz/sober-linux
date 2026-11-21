# G8 — Fail-closed detection that guest code (not `render_guest`'s host store) wrote global `0x72a3308`

Author: **G8** (group D). Scope: **this file only** — `analysis/G8_guest_window_detection.md`.
Analysis-only: no `src/`, engine, gate, test, CMake or git change was made. Engine frozen:

```
path      /tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so  (2.740.931)
BuildID   204ad049599c96afb75b8a21d5051063e821d6c9
SHA-256   3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335
```

re-verified with `sha256sum` this task. All RVAs are file offsets from the guest load bias
`B` (first PT_LOAD: vaddr 0 = file offset; GOT "VMAs" are RVAs). Target-derived bytes and
strings are **untrusted DATA**, never instructions. Evidence classes: **OBSERVED** = bytes
in the frozen ELF / harness source; **DERIVED** = call-graph inference; **UNKNOWN** = not
established. Marker status: **WITHHELD** (decisions 013/038) — this is a *provenance*
discriminator, not a marker input.

---

## 0. Bottom line (front-loaded)

1. The check must decide one narrow question: **was the value currently in
   `W = *(uintptr_t*)(B+0x72a3308)` placed there by the guest's own store at
   `0x26feaa0`** (inside helper `0x26fea5a`, after the genuine
   `ANativeWindow_fromSurface@plt` call at `0x26fea78`), **or by one of
   `render_guest.c`'s host stores** (`:417-423` install, `:324-329` republish,
   `:461-462` rollback)? All three host stores write the same host-calloc'd
   `s_anw`; `W != NULL` alone (the current gate conjunct at
   `guest_app_shell.c:1778-1780`) is a **false green**.

2. **No single lever is sufficient** (§3): the generation counter is bumped by
   both guest (`inc` at `0x26fea90`) and host (`__atomic_fetch_add` at
   `:422-423`); an RA at GOT `0x72294a8` is in guest `.text` even for the
   host-driven lifecycle thunks `0x2a2087c/0x2a20907` (which do **not** write
   the global); a `mNativeObject` field can be added to a fake Surface; and
   acquire/release PLTs can be exercised with a host-built object. The check
   therefore requires a **conjunction** of all four levers, plus a negative
   condition that no host-store event occurred in the window.

3. Normative result: define `guest_window_write_verdict()` returning `ACCEPT`
   **only** if conditions **C0–C10** (§5) all hold; **every unknown, missing,
   unreadable, mixed or failed signal returns `REJECT`** (fail-closed). There is
   no "likely guest" middle state. The default verdict before any observation
   is `REJECT`.

4. With the current harness the verdict is correctly **`REJECT`**: no helper
   fromSurface event exists, the Surface is a token with no `mNativeObject`,
   and the host-store ledger is non-empty. After the faithful replay (real
   Surface with `mNativeObject`, render_guest host-publish disabled) the verdict
   becomes `ACCEPT` — **but the marker stays WITHHELD** until a genuine
   notifier-driven `gameLoadedCallback` is also proven with all seeds off.

5. Two prerequisites make the check *satisfiable*; until they exist it simply
   keeps rejecting (correct fail-closed behaviour): (a) implement
   `GetFieldID`/`GetLongField` in the bridge JNIEnv (C08 §3 item 3) so
   `mNativeObject:J` can be read; (b) stop relying on `render_guest.c:326-329`
   and instrument/disable the host-publish sites (P8 §6 item 2).

---

## 1. The two write paths (verified, OBSERVED)

### 1.1 Guest store — the event we want to attribute

`objdump -d -M intel --start-address=0x26fea5a --stop-address=0x26feb90` (this task):

```
26fea78  e8 53 30 63 04       call   6d31ad0 <ANativeWindow_fromSurface@plt>   ; rdi=env, rsi=Surface
26fea7d  48 89 c3             mov    rbx,rax                 ; <-- RA captured at GOT 0x72294a8
26fea80  48 8b 3d ..          mov    rdi,[0x72a3308]         ; old window
26fea87  48 39 f8             cmp    rax,rdi
26fea8a  0f 84 86 00 00 00    je     26feb16                 ; same-window branch (NO store, NO gen bump)
26fea90  ff 05 7a 48 ba 04    inc    DWORD PTR [0x72a3310]   ; <-- generation, different-window only
26fea96  48 85 ff             test   rdi,rdi
26fea99  74 05                je     26feaa0                 ; skip release if old == NULL
26fea9b  e8 20 30 63 04       call   6d31ac0 <ANativeWindow_release@plt>       ; <-- RA 0x26feaa0
26feaa0  48 89 1d 61 48 ba 04 mov    [0x72a3308],rbx         ; <-- THE GUEST STORE (RA-tied to 0x26fea78)
26feace  8b 05 ..             mov    eax,[0x72a3310]         ; gen value -> "Created ANativeWindow ... (ID:..)"
26feaf7  lea    .. 0x3c163c   ; "[FLog::JNIAppBridge] {}: Created ANativeWindow {} (ID:{})"
26feb58  lea    .. 0x46f987   ; "Same native window. No need to do anything!"  (same-window branch)
26feb7d  e8 3e 2f 63 04       call   6d31ac0 <ANativeWindow_release@plt>       ; RA 0x26feb82
```

PLT stubs (verified): `6d31ad0: ff 25 .. -> [0x72294a8]` (fromSurface),
`6d31ac0: ff 25 .. -> [0x72294a0]` (release),
`6d32100: ff 25 .. -> [0x72297c0]` (acquire).

### 1.2 Host stores — the events to exclude

| Site | Code | Generation effect |
| --- | --- | --- |
| `render_guest.c:417-423` (install) | `*(base+0x72a3308)=s_anw` | `__atomic_fetch_add(base+0x72a3310, 1, SEQ_CST)` |
| `render_guest.c:324-329` (republish after lifecycle replay) | `*(base+0x72a3308)=s_anw` | none |
| `render_guest.c:461-462` (got_fail rollback) | `*(base+0x72a3308)=old_window` | none |

Subtlety (DERIVED, minor): the guest `inc` is a **4-byte** RMW at `0x72a3310`;
the host `__atomic_fetch_add` is an **8-byte** RMW because the address is cast
to `uintptr_t*`. Low-32 sampling therefore works, and a non-zero upper 32 bits
at `0x72a3314` would weakly indicate a host atomic — but this only becomes
visible after 2³² events, so the check **does not rely on it**.

### 1.3 The other fromSurface call sites (not stores of the global)

```
2a2087c  call 6d31ad0  -> RA 2a20881 ; stores result to [nativeData+0x140], NOT the global
2a20907  call 6d31ad0  -> RA 2a2090c ; same
```

These are the two sites the harness actually drives (`from_surface=2` in logs);
their RAs are in guest `.text`, which is precisely why **RA-in-guest alone is
not a guest-write signal**.

---

## 2. Instrumentation model (where the four levers are observed)

The engine is frozen, so observation is by GOT interposition (allowed: the
render_guest module already does this) plus read-only snapshots. Install the
five ANativeWindow GOT hooks **together** with one coherent object layout —
hooking only `fromSurface` corrupts the compat `release` at `0x26fea9b`
(C08 §4 Phase-A step 3 / P8 §6 item 3). Fail-closed rule **I0**: if any of the
five wrappers is missing or they disagree on layout, verdict is `REJECT`.

| ID | Hook / probe | Exact address | Fields captured |
| --- | --- | --- | --- |
| **H1** | fromSurface GOT observer (extend `rg_hook_from_surface`, `render_guest.c:239-252`) | `B+0x72294a8` | `ra=__builtin_return_address(0)`, `env` (rdi), `surface` (rsi), `mNativeObject` read via JNI (§2.1), returned pointer `ret`, `g_before = load32(B+0x72a3310)`, `tid`, `seq`. Forward to genuine `s_got_old[0]`; **never** short-circuit with `s_anw` for the candidate event. |
| **H2** | release GOT observer (new wrapper on the genuine slot) | `B+0x72294a0` | `ra`, `window` arg, `tid`, `seq`. Forward to original. |
| **H3** | acquire GOT observer (+ getWidth/getHeight for layout check) | `B+0x72297c0` (`0x72297d0`/`0x72297c8`) | `ra`, `window` arg, returned dims, `tid`, `seq`. |
| **H4** | host-store probe: instrument the three `render_guest.c` store sites (or a boolean `host_publish_enabled`) | host code | `seq`, `kind ∈ {INSTALL,REPUBLISH,ROLLBACK}`, `window`, `g_before/g_after`. |
| **H5** | gate-time snapshot | `B+0x72a3308`, `B+0x72a3310` | `W_now` (seq-cst load), `G_now` (32-bit seq-cst load). |
| **H6** | optional log capture of guest string `0x3c163c` | guest logger `0x232de10` | window pointer and `ID` fields of `Created ANativeWindow … (ID:…)`. |

`ra` semantics are exact because the PLT stub is `jmp *GOT(...)`: the return
address left on the stack by the guest's `call …@plt` is the instruction after
that call. Verified numerically with `B = 0x7f0556700000` (logs):

| Signal | RVA | Absolute (`B=0x7f0556700000`) |
| --- | --- | --- |
| GOT fromSurface / release / acquire | `0x72294a8` / `0x72294a0` / `0x72297c0` | `0x7f055d9294a8` / `0x7f055d9294a0` / `0x7f055d9297c0` |
| Global W / generation G | `0x72a3308` / `0x72a3310` | `0x7f055d9a3308` / `0x7f055d9a3310` |
| **RA helper (store-tied)** | `0x26fea7d` | `0x7f0558dfea7d` |
| RA release-old (in helper) | `0x26feaa0` | `0x7f0558dfeaa0` |
| RA release-same (same-window) | `0x26feb82` | `0x7f0558dfeb82` |
| RA acquire (EGL init) | `0x649c2db` | `0x7f055cb9c2db` |
| RA getWidth / getHeight (EGL init) | `0x649c308` / `0x649c314` | `0x7f055cb9c308` / `0x7f055cb9c314` |
| RA created/changed thunk (reject) | `0x2a20881` / `0x2a2090c` | `0x7f0559120881` / `0x7f055912090c` |

### 2.1 Reading and validating `mNativeObject` (H1)

This is the decisive anti-spoof lever. In H1, using the captured `env`:

```
jclass  c = GetObjectClass(env, surface);        // must succeed
jfieldID f = GetFieldID(env, c, "mNativeObject", "J");  // must be non-NULL
jlong   m = GetLongField(env, surface, f);       // must be non-zero
```

`IsInstanceOf(surface, android/view/Surface)` (or a class-name comparison)
should also pass. Then `is_real_native_window(m)` must hold: `m` is readable,
non-NULL, and carries a **recognised native-window shape** — either a real NDK
`ANativeWindow` header (magic/version/vptr/query at the documented offsets,
per C08 §4 Phase A) or a guest vptr in a whitelist — and **not** the 12-byte
compat `{refs,width,height}` prefix that `android_compat.c:498-513` returns.
Any fault, missing field, zero value or unknown shape → `mnative_valid = 0`.

> Prerequisite: the bridge JNIEnv currently leaves `GetFieldID`/`GetLongField`
> as `sr_stub` returning NULL (C08 §3 item 3). Until implemented, H1 always
> records `mnative_valid=0` and the check correctly always `REJECT`s.

### 2.2 Ledger records

```
fs_event { seq, ra, env, surface, mnative, mnative_valid, ret, g_before, tid }
rel_event{ seq, ra, window, tid }
acq_event{ seq, ra, window, dims_ok, tid }
host_event{ seq, kind, window, g_before, g_after }
gate state: W_now, G_now
```

---

## 3. Why each single signal fails (motivation for the conjunction)

| Signal alone | Spoof / false green |
| --- | --- |
| `W != NULL` (current gate) | Host store at `render_guest.c:326-329` writes `s_anw`; gate goes green with no guest binding (P8 §5 item 5). |
| `G` incremented | Host `:422-423` also does `+1`. Delta alone cannot distinguish host from `inc` at `0x26fea90`. |
| RA in guest `.text` at GOT `0x72294a8` | Lifecycle thunks `0x2a2087c/0x2a20907` have guest RAs but store only `nativeData+0x140`, not the global (§1.3). |
| `mNativeObject` present | Can be bolted onto a fake Surface while `rg_hook_from_surface` still ignores the jobject and returns `s_anw`. |
| acquire/release PLTs exercised | Can be driven with the host 12-byte shim; `refs=0x10000` keeps it alive through every release (P8 §4). |
| All of the above except host-store exclusion | Host `:324-329` can overwrite `W` *after* a genuine-looking event. |

Conclusion: accept only on the conjunction **and** an explicit "no host-store
event in the window" negative condition.

---

## 4. Fail-closed decision principle

```
verdict = REJECT                      /* default, before any observation      */
on any observer error, unreadable memory, unknown base, mixed tids, or missing
   instrumentation: return REJECT with that reason                            /* I0, C0 */
evaluate C1..C10 in order; on first failure: return REJECT(reason)             */
if all pass: return ACCEPT("guest wrote B+0x72a3308")                          */
```

There is deliberately **no** "inconclusive-but-accept" path. Any `NULL`, fault,
counter wrap inconsistency, or event with an unexpected RA is a rejection, not
a warning. The verdict function is a pure function of `(ledger, B, W_now,
G_now)` so it can be unit-tested with synthetic ledgers (§8).

---

## 5. Exact acceptance conditions

Let `E` be the candidate `fs_event`, `W_before` the window value sampled at H1
entry, `G_before = E.g_before`, and all `seq` values monotonic. **ACCEPT iff all
C0–C10 hold:**

- **C0 — integrity / instrumentation.** `B != 0`; engine SHA-256 and BuildID
  equal the frozen values in the header; all six observers active; I0 satisfied
  (five ANativeWindow GOTs hooked coherently); every event has the same `tid`;
  no read fault.
- **C1 — no host publish in window.** `host_event.count == 0` and
  `host_publish_enabled == 0` (render_guest's install/republish/rollback stores
  disabled for this run). Any host-store event with `seq >= E.seq` is an
  immediate `REJECT`.
- **C2 — exact store-tied fromSurface event, and no other.**
  (a) Exactly one `fs_event` exists in the ledger; (b) `E.ra == B+0x26fea7d`;
  (c) the 5 bytes at `E.ra-5 .. E.ra` decode as `e8 rel32` whose target is
  `B+0x6d31ad0` (the fromSurface PLT) — rejects return-into-libc RA forging;
  (d) there are **zero** `fs_event`s with any other RA (`0x2a20881`,
  `0x2a2090c`, host PIE, …). This is the mixed-evidence fail-closed rule.
- **C3 — real Surface.** `GetObjectClass`/`GetFieldID("mNativeObject","J")`/
  `GetLongField` all succeeded: `E.mnative_valid == 1`, `E.mnative != 0`,
  `is_real_native_window(E.mnative)` true, Surface class check true.
- **C4 — returned window bound to that Surface.** `E.ret != 0`;
  `E.ret == E.mnative` (or `E.ret` is a registered ANativeWindow whose backing
  handle equals `E.mnative`); and `E.ret != W_before` (different-window branch;
  the same-window branch does not publish and does not bump G).
- **C5 — generation delta from the guest `inc`.** `G_now == (E.g_before + 1)
  mod 2^32` (matches `inc [0x72a3310]` at `0x26fea90`). Optional corroboration:
  H6 `Created … (ID:G_now)` log present with `ID == G_now`. Delta 0, >1 or
  decreased → `REJECT`.
- **C6 — final window is that pointer.** `W_now == E.ret` and `W_now != 0`.
- **C7 — release PLT on the old window.** If `W_before != 0`, a `rel_event`
  must exist with `R.seq > E.seq`, `R.ra == B+0x26feaa0`, `R.window ==
  W_before` (the `0x26fea9b` call). If `W_before == 0` the release is omitted
  by `je 0x26feaa0` and no event is required.
- **C8 — acquire PLT on the published window.** An `acq_event` must exist with
  `A.seq > E.seq`, `A.ra == B+0x649c2db`, `A.window == W_now` (the `0x649c2d6`
  call in `egl_init`).
- **C9 — coherent object layout.** At least one getWidth/getHeight observation
  with guest RA `B+0x649c308`/`B+0x649c314` returned positive dimensions, or
  the window's refcount word is non-zero and all accesses fault-free; no
  incompatible 12-byte/64-bit-layout mismatch.
- **C10 — guest publish log (when H6 compiled in).** H6 observed the guest
  `0x3c163c` log with pointer `== W_now` and `ID == G_now`. If H6 is unavailable
  the verdict is still `REJECT` for gate purposes (fail-closed on reduced
  confidence).

**Rejection reasons (normative tokens):** `R0_UNVERIFIED_ENGINE`,
`R1_HOST_PUBLISH`, `R2_RA_NOT_HELPER`, `R2_MIXED_FROMSURFACE`,
`R2_OPCODE_MISMATCH`, `R3_NO_MNATIVEOBJECT`, `R4_RET_MISMATCH`,
`R5_GENERATION`, `R6_WINDOW_MISMATCH`, `R7_NO_RELEASE`, `R8_NO_ACQUIRE`,
`R9_LAYOUT`, `R10_NO_LOG`.

---

## 6. Spoof analysis — each attack is caught by a named condition

| # | Attack | Defeated by |
| --- | --- | --- |
| S1 | Host store `render_guest.c:417-423`/`:324-329` writes `s_anw`, bumps G | C1 (host_event), C2 (no helper fs_event) |
| S2 | Call helper `0x26fea5a` but keep harness `rg_hook_from_surface` returning `s_anw` | C3 (no real `mNativeObject` on token) and C4 (`ret == s_anw != m`) |
| S3 | Add `mNativeObject` to a fake Surface but keep the short-circuiting hook | C4 (`ret != mnative`) |
| S4 | Set `mNativeObject` to the 12-byte compat shim | C3 (`is_real_native_window` false) |
| S5 | Lifecycle-thunk fromSurface (RA `0x2a20881`/`0x2a2090c`) presented as "guest write" | C2b (RA must be `0x26fea7d`) |
| S6 | RA numerically `0x26fea7d` via return-into-libc | C2c (`e8 rel32` opcode/target check) |
| S7 | Host bumps G (atomic add) instead of guest `inc` | C1 + C5 (event-tied +1) |
| S8 | Host republish after a genuine-looking event | C1 + C6 (`W_now == E.ret`) |
| S9 | Both synthetic and genuine fromSurface events present | C2d (zero other RAs → `R2_MIXED_FROMSURFACE`) |
| S10 | Acquire/release driven on the wrong object | C7/C8/C9 (window arg + layout) |

---

## 7. Test vectors

Reference constants (absolute values assume `B = 0x7f0556700000` from the logs;
the checker itself uses RVAs, so vectors are base-relative in reality):

```
GOT_FS = 0x7f055d9294a8   GOT_REL = 0x7f055d9294a0   GOT_ACQ = 0x7f055d9297c0
WA     = 0x7f055d9a3308   GA     = 0x7f055d9a3310   (G=W+8)
RA_H   = 0x7f0558dfea7d   RA_REL = 0x7f0558dfeaa0   RA_ACQ = 0x7f055cb9c2db
RA_CT  = 0x7f0559120881   RA_MT  = 0x7f055912090c
S      = 0x5637a8bb1234   (real Surface jobject)
M      = 0x5637a8cfe010   (real NDK-shaped ANativeWindow, mNativeObject)
OLD    = 0x5637a8cde000   (previous window)
SHIM   = 0x5637a8cf0000   (host calloc 12-byte compat struct, s_anw)
```

| ID | Scenario / key field values | Expected | First failing rule |
| --- | --- | --- | --- |
| **V1** | Genuine rebind: one `fs_event` RA=`RA_H`, `e8`→`0x6d31ad0`, `mnative=M` valid, `ret=M`, `g_before=4`; no host events; `W_before=OLD`, `W_now=M`, `G_now=5`; `rel(OLD,RA_REL)`, `acq(M,RA_ACQ)`, dims>0, H6 `Created … (ID:5)` | **ACCEPT** | — (all C0–C10) |
| **V2** | Current harness baseline: host INSTALL+REPUBLISH of `SHIM`; two `fs_event`s with RA=`RA_CT`/`RA_MT`, `mnative=0`; `W_now=SHIM`, `G_now=1` | REJECT | `R1_HOST_PUBLISH` (also `R2_RA_NOT_HELPER`, `R3_NO_MNATIVEOBJECT`) |
| **V3** | Helper driven with 16-byte token Surface: RA=`RA_H`, `GetFieldID`→NULL, `mnative=0`, `ret=SHIM`; PLTs present | REJECT | `R3_NO_MNATIVEOBJECT` (also `R4_RET_MISMATCH`) |
| **V4** | Wrong fromSurface RA `RA_CT`, but `mnative=M`, `ret=M`, `G 4→5`, PLTs present | REJECT | `R2_RA_NOT_HELPER` |
| **V5a** | As V1 but `G_now=6` (delta 2 — host bump interleaved) | REJECT | `R5_GENERATION` |
| **V5b** | As V1 but `G_now=4` (same-window branch, no publish) | REJECT | `R5_GENERATION` (also `R4_RET_MISMATCH` if `ret==OLD`) |
| **V6** | V1 plus host REPUBLISH at `:326-329` after E, writing `W=SHIM` | REJECT | `R1_HOST_PUBLISH` / `R6_WINDOW_MISMATCH` |
| **V7** | V1 but `W_before=OLD` and no release event | REJECT | `R7_NO_RELEASE` |
| **V8** | V1 but acquire event window `= SHIM` instead of `M` | REJECT | `R8_NO_ACQUIRE` |
| **V9** | `mnative=M` valid but hook returns `SHIM` (`ret=SHIM`, `W_now=SHIM`) | REJECT | `R4_RET_MISMATCH` |
| **V10** | V1 plus an extra `fs_event` with RA=`RA_MT` in the same window | REJECT | `R2_MIXED_FROMSURFACE` |
| **V11** | `mnative=SHIM` (12-byte prefix, no magic/vptr), `ret=SHIM` | REJECT | `R3_NO_MNATIVEOBJECT` |
| **V12** | `B=0` or SHA-256 mismatch / observer missing | REJECT | `R0_UNVERIFIED_ENGINE` |
| **V13** | First bind: `W_before=0`, no release event, all else as V1, `G 0→1` | **ACCEPT** | — (C7 waiver applies) |
| **V14** | Generation wrap: `g_before=0xffffffff`, `G_now=0` | **ACCEPT** | — (mod-2³² delta 1) |
| **V15** | RA numerically `RA_H` but bytes at `RA-5` are not `e8 rel32`→`0x6d31ad0` | REJECT | `R2_OPCODE_MISMATCH` |
| **V16** | V1 but H6 log `Created … (ID:4)` (stale ID) | REJECT | `R10_NO_LOG` |
| **V17** | Two events on different `tid` | REJECT | `R0_UNVERIFIED_ENGINE` (multi-thread fail-closed) |

---

## 8. Test / integration procedure (read-only w.r.t. engine)

**Unit (no engine needed).** Compile the pure verdict function with a fake
ledger and feed V1–V17 as struct literals; assert the Expected column, and
assert that *removing any one field* (setting it to 0/NULL) flips ACCEPT→REJECT.
This is the direct proof of fail-closed behaviour.

**A/B live runs** (`tests/first-frame.sh` harness, custom `SOBER_FRAME_MARKER`
under `/tmp`; engine bytes never patched — only the documented GOT stores and
the two window globals):

1. **Negative control (default harness, `SOBER_RENDER_GUEST` default ON).**
   Expect `from_surface=2`, `swaps(total/guest/synth)=1/0/1`, `draws=0`,
   verdict `REJECT(R1_HOST_PUBLISH)`, marker withheld. This proves the current
   false-green window conjunct is now correctly excluded.
2. **Spoof controls V3/V4/V9/V10** run as injected variants; each must yield
   the named REJECT token. If any returns ACCEPT, the corresponding lever is
   broken (e.g. the fromSurface hook is still short-circuiting with `s_anw`).
3. **Positive control (faithful replay).** Disable render_guest host publish;
   supply a real Surface with `mNativeObject=M`; drive genuine helper
   `0x26fea5a(env, Surface)`; expect verdict `ACCEPT` with `W_now=M`,
   `G_now=G_before+1`, rel@`0x26fea9b` on old, acq@`0x649c2d6` on new, and H6
   `Created ANativeWindow M (ID:G_now)`. This proves *guest-write provenance*
   only — the marker remains **WITHHELD** under 013/038 (Leg C not closed).
4. **Integrity invariants (every run).** `sha256sum` before/after =
   `3e498d64…`; `readelf -n` BuildID unchanged; no guest `.text` byte patched.

**Gate integration point.** Replace the naked conjunct
`*(void**)(base+0x72a3308) != NULL` at `guest_app_shell.c:1778-1780` with
`guest_window_write_verdict(...) == ACCEPT`. If the verdict function is absent
or errors, the gate must treat it as `REJECT` (fail-closed). The module must
never emit `app shell rendered`; it contributes one diagnostic line only.

---

## 9. Limits / unknowns (honest)

- **UNKNOWN:** the exact internal shape of a real NDK `ANativeWindow` chosen for
  Phase A (C08 §4) is a design decision; `is_real_native_window()` must be
  keyed to whichever layout is actually adopted, and must explicitly reject the
  12-byte compat prefix. The guest-side contract (non-NULL returned pointer) is
  **OBSERVED** (C08 §2.3).
- **BLOCKER (must fix):** bridge JNIEnv lacks `GetFieldID`/`GetLongField`
  (C08 §3 item 3). Without them C3 can never pass and the check always rejects.
- **DERIVED:** the assumption `ret == mNativeObject` reflects Android's
  `ANativeWindow_fromSurface` returning the `android::Surface*` held in that
  field. If a faithful implementation returns a wrapper object instead, relax
  C4 to "registered ANativeWindow whose backing handle == mNativeObject" — but
  keep it an explicit whitelist, never an unconstrained pointer.
- **UNKNOWN:** whether the app-shell frame consumes `0x72a3308` or
  `nativeData+0x140` as its drawable (C08 §5); this check proves only the
  *write provenance of the global*. Pair it with the swap/draw provenance work
  (R6 §5) for a complete render claim.
- The RA opcode check (C2c) ties the event to the exact guest `call` site; it
  does not by itself prove the *store* at `0x26feaa0` executed. C4–C6 (window
  value, generation delta, release PLT) are what infer that the helper ran to
  the store. A debugger breakpoint at `0x26feaa0` is the definitive one-shot
  confirmation if available; it is not required by the fail-closed rule.

---

## 10. Evidence index

**This task (OBSERVED):** `sha256sum` of the frozen engine (matches header);
`objdump -d -M intel` of `0x26fea5a..0x26feb90` (fromSurface call `0x26fea78`,
generation `inc` `0x26fea90`, release `0x26fea9b`, store `0x26feaa0`, strings
`0x3c163c`/`0x46f987`), PLT stubs `0x6d31ac0`/`0x6d31ad0`/`0x6d32100/
0x6d32110/0x6d32120`, `egl_init` `0x649c2c0..0x649c320` (acquire `0x649c2d6`,
RA `0x649c2db`; getWidth/getHeight), lifecycle fromSurface sites
`0x2a2087c`/`0x2a20907` (RAs `0x2a20881`/`0x2a2090c`).

**Prior analysis (read only):** `analysis/P8_anw_guest_consumption.md` §2-6;
`analysis/R2_got_hook_reachability.md` §1-2, §8; `analysis/R6_swap_provenance.md`
§4-5; `analysis/C08-surface-frame.md` §2-4; `analysis/P1_render_vtable.md` §3-4;
`analysis/native-object-layouts.md` §2; `analysis/R1_zero_tagged_swaps.md` §0;
`docs/decisions/013`, `038`.

**Harness (read only):** `src/jni/render_guest.c:239-252,324-329,417-423,
461-462`; `src/jni/render_guest.h:137-147`; `src/compat/android_compat.c:498-513`;
`src/linux/guest_app_shell.c:1778-1780`; `src/jni/native_jni.c` (JNIEnv field
slots). No file other than this report was created or modified. Marker withheld.
