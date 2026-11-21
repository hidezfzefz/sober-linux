# G3 — Withhold enumeration for the `frame_*` modules (F1–F8)

Author: **G3** (group D). Scope: **this file only** —
`analysis/G3_withhold_frame_modules.md`. Analysis-only: no `src/`, CMake, gate,
test, doc, engine or git file was edited. Engine frozen: BuildID
`204ad049599c96afb75b8a21d5051063e821d6c9`, SHA-256
`3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`
(AppImage 2.740.931). All RVAs are offsets from the guest load bias;
target-derived bytes/strings are untrusted DATA. Decisions **013/038 remain
binding; the marker stays WITHHELD**. This report is a knob/withhold audit, not
a gate input and not a success claim.

> Snapshot caveat (same as G9 §0). Source files were landing from the F-agents
> while this was written. At final check `src/jni/` contains `frame_dispatch`
> (F3), `frame_draw` (F4), `frame_redraw` (F2), `frame_surface` (F5),
> `frame_thread` (F6), `frame_evidence` (F7), `frame_synthetic_tag` (F8) — all
> **untracked and in no CMake target**. F1 `frame_callbacks` has **no file**
> (F9 §3; G9 §1). All shipped `SOBER_*` env literals found in `frame_*.[ch]`
> are enumerated in §2; re-run the §7 command after any late arrival.

---

## 0. Front-loaded verdict

1. **Six canonical driver names** must join `seeded_state` per the lead's
   authoritative consolidation (`MODULE_INTEGRATION.md §4`, `F9 §6`):
   `SOBER_RENDER_FRAME_EVIDENCE`, `SOBER_EVIDENCE_SURFACE`,
   `SOBER_EVIDENCE_DEVICE`, `SOBER_EVIDENCE_REDRAW`,
   `SOBER_RENDER_SUPPRESS_SYNTHETIC`, `SOBER_FRAME_THREAD` — plus the module
   verdict `sober_frame_thread_should_withhold()`.
2. **Every shipped injector/driver module also has its own literal knob** that
   is *not* in that canonical list and is currently **absent** from the chain:
   `SOBER_FRAME_DISPATCH` (F3), `SOBER_FRAME_DRAW` (F4), `SOBER_FRAME_REDRAW`
   (F2), `SOBER_FRAME_SURFACE` (F5), `SOBER_FRAME_THREAD` (F6). If those files
   are installed (merged into `render_guest` or kept for A/B), their names must
   be in the chain **in the same change** (G9 §3 "canonical-name mismatch"):
   adding only F9's six leaves every real shipped knob un-withheld.
3. **Observation-only modules are NOT withheld**: F7 `frame_evidence`
   (`SOBER_FRAME_EVIDENCE=0`) is fail-closed and F8 `frame_synthetic_tag`
   (`SOBER_FRAME_SYNTH_TAG=0`) is fail-safe; both are default-ON, write no guest
   state and can only turn decisive counts to zero. `SOBER_PROVENANCE_GATE=0`,
   marker paths and the legitimate `SOBER_RENDER`/`SOBER_APP_SHELL` likewise
   stay out (`D02 §5–6`, `G1 §4.4`, `MODULE_INTEGRATION.md §4`).
4. The live chain at `guest_app_shell.c:1608–1765` has **122 `getenv()` terms
   and 5 prefixes, zero frame terms**. `SOBER_WINDOW_WIDTH`/`_HEIGHT` (read by
   F5 `frame_surface.c:404–405`) are already in-chain (`:1713–1714`) — parity
   satisfied, do not duplicate.
5. Disputed point, recorded honestly: `F10 §FP-5/FC-9` and `G9 §3` want
   `SOBER_FRAME_SYNTH_TAG=0` withheld or made fail-closed. From the shipped code
   (§5.2) the tag is **additive** to `render_guest`'s existing RA classifier and
   disabling it cannot manufacture a green, so the authoritative policy
   (`F9 §6`, `G1 §4.4`, `MODULE_INTEGRATION.md §4`) is **not withheld**. If the
   lead instead promotes `active()` to a gate classifier, it must be promoted
   to a withhold entry in that same change.

---

## 1. Authority / rules applied

- `MODULE_INTEGRATION.md §1` — "All new knobs and modifiers must be in the
  `seeded_state` withhold chain"; one module per bridge slot.
- `MODULE_INTEGRATION.md §4` — authoritative frame consolidation: keep F6/F7/F8,
  merge/retire F1/F2/F3/F4/F5; six names to add; do not withhold the three
  fail-closed/fail-safe kill switches.
- `F9 §3/§6` — eight concern slots, one-owner decisions, canonical knob table.
- `D02 §5` — **alias parity** (V2/legacy pair) and **prefix parity** (Tier-2
  value modifiers); `D02 §5–6` — marker paths and legitimate gates stay out.
- `G1 §4.4`, `G7 §401`, `T4-4 §4.6` — any knob that drives, suppresses, filters
  or fakes an observation joins the chain in the same change.
- `G9 §3` — shipped-vs-canonical name mismatch; `tests/D09-withhold-audit.sh`
  is the D09-style audit baseline.

---

## 2. Exact names that must join `seeded_state`

### 2.1 Authoritative (lead decision — add these regardless of file layout)

| Exact name | Concern / action | Class | Source |
| --- | --- | --- | --- |
| `SOBER_RENDER_FRAME_EVIDENCE` | E1: RA-split swap tag + diagnostic draw counters (**not** the F7 kill switch) | driver | `F9 §6`, `C04:244` |
| `SOBER_EVIDENCE_SURFACE` | E3: five-GOT window bind + lifecycle drive (F2/F5 concern) | driver | `F9 §6`, `T3_5:435` |
| `SOBER_EVIDENCE_DEVICE` | E4: `0x649c21e(window, pbuffer=0)` device path / residual draw wrappers (F4/F5) | driver | `F9 §6`, `T3_5:436` |
| `SOBER_EVIDENCE_REDRAW` | E4: drive `0x2a20a00` → `[+0x98]` (F2 redraw driver) | driver | `F9 §6`, `C04:247` |
| `SOBER_RENDER_SUPPRESS_SYNTHETIC` | E5: skip Route-B host swap with `[ev-suppress]` | suppressor | `F9 §6`, `T3_5:437` |
| `SOBER_FRAME_THREAD` | F6 render-loop thread driver | injector | `frame_thread.h:107`, `frame_thread.c:63` |
| `sober_frame_thread_should_withhold()` | F6 module verdict (`ft_opted_in() && s_installed`) | verdict | `frame_thread.c:261–265`, `frame_thread.h:196` |

Plus `SOBER_EVIDENCE_FRAME` (`C04:248`, "fire the gate callback" diagnostic,
F1 callback concern) — `F9 §6` marks it withhold-if-retained; include it
whenever that diagnostic exists. `SOBER_NOTIFIER_PROBE` (F1 byte-patching
diagnostic) is **already** in the chain (`guest_app_shell.c:1693`).

### 2.2 Shipped literal knobs — withhold if the file is installed (all currently absent from chain)

| F | Module / file | Exact env name | Evidence | Class |
| --- | --- | --- | --- | --- |
| F2 | `frame_redraw.{c,h}` | `SOBER_FRAME_REDRAW` | `frame_redraw.h:109`, `frame_redraw.c:260`; seeds nativeData callback RVAs, publishes window, drives `0x2a20a00` | **injector → withhold** |
| F3 | `frame_dispatch.{c,h}` | `SOBER_FRAME_DISPATCH` | `frame_dispatch.h:110`, `frame_dispatch.c:69`; synthetic device object, guest dispatch drive, 4th GOT observer | **injector → withhold** |
| F3 | same | `sober_frame_dispatch_should_withhold()` | `frame_dispatch.c:637–640`, `frame_dispatch.h:261` | verdict → OR in |
| F4 | `frame_draw.{c,h}` | `SOBER_FRAME_DRAW` | `frame_draw.h:128`, `frame_draw.c:258`; GOT wrappers + synthetic draw-descriptor drive | **injector → withhold** |
| F5 | `frame_surface.{c,h}` | `SOBER_FRAME_SURFACE` | `frame_surface.h:133`, `frame_surface.c:359`; two GOT writers, ctx/nativeData writes, lifecycle thunks | **injector → withhold** |
| F5 | same | `SOBER_WINDOW_WIDTH`, `SOBER_WINDOW_HEIGHT` | `frame_surface.c:404–405` | already in chain `:1713–1714` — parity OK |
| F6 | `frame_thread.{c,h}` | `SOBER_FRAME_THREAD` | `frame_thread.h:107`, `frame_thread.c:63` | **injector → withhold** |
| F1 | `frame_callbacks` — **no file** | `SOBER_EVIDENCE_FRAME` (if retained) | `F9 §3`, `C04:248` | diagnostic driver → withhold |
| F1 | `provenance_gate`/`notifier_probe` (owner of the concern) | `SOBER_NOTIFIER_PROBE` | `guest_app_shell.c:1693` | already in chain |

`SOBER_FRAME_REDRAW_DRIVE` appears in `G9 §3` as a canonical alias for the F2
drive; treat it as an **alias** of `SOBER_EVIDENCE_REDRAW` / `SOBER_FRAME_REDRAW`
(§4.1) and include it if the lead adopts that spelling.

### 2.3 Names that must NOT join the chain (observation-only / fail-closed / marker / real path)

| Exact name | Why out |
| --- | --- |
| `SOBER_FRAME_EVIDENCE` (F7; exact `"0"` kills) | Fail-closed: all decisive accessors 0, verdict `DISABLED` (`frame_evidence.c:143–154,329–350`; `G1 §4.4`). **Near-name collision:** this is NOT `SOBER_RENDER_FRAME_EVIDENCE`. |
| `SOBER_FRAME_SYNTH_TAG` (F8; `0/off/no/false`) | Fail-safe: only the additive tag goes inert; `render_guest` RA classifier still tags Route-B synthetic (`frame_synthetic_tag.h:42–50`, `G1 §4.4`). Dispute noted in §0.5. |
| `SOBER_PROVENANCE_GATE` | Fail-closed positive credential (`provenance_gate.h:50–58`, `G1 §4.4`). |
| `SOBER_SHELL_MARKER`, `SOBER_FRAME_MARKER`, `SOBER_INIT_MARKER` | Marker output paths, not guest-state seeds (`D02 §5.5`). |
| `SOBER_RENDER`, `SOBER_APP_SHELL` | Legitimate real-path enables required by decision 013 (`D02 §5.6`). |

---

## 3. Classification proof: observation-only vs injector

### 3.1 Observation-only — not withheld

- **F7 `frame_evidence`**: header `frame_evidence.h:7–23` — "Observation only …
  NEVER emits a marker, NEVER invokes a JNI handler/callback, NEVER patches
  guest bytes and NEVER writes a guest global". Verified in `frame_evidence.c`:
  all stores are to local atomics (`s_*`); there is no `mprotect`/GOT write and
  no `setenv`. `guest_render_ok()` fails closed on `=0`, `MIXED`, missing legs.
  Its kill switch is `SOBER_FRAME_EVIDENCE=0` → **not withheld**.
- **F8 `frame_synthetic_tag`**: `frame_synthetic_tag.h:5–50` — "pure
  bookkeeping … does NOT … patch any guest byte, and writes no guest global";
  kill is fail-safe because the pre-existing RA classifier still classifies
  the Route-B swap synthetic. `frame_synthetic_tag.c` contains no guest store.
  Its kill switch is `SOBER_FRAME_SYNTH_TAG=0` → **not withheld** (subject to
  the §0.5 wiring condition).

### 3.2 Injector/driver — withheld

- **F2 `frame_redraw`**: seeds the exact guest callback RVAs into
  `nativeData+0x88/0x90/0x98/0xa0`, publishes `0x72a3308`, installs three GOT
  observers, calls guest `0x2a20a00` (`frame_redraw.h:1–40,54–58`).
- **F3 `frame_dispatch`**: drives the guest graphics-device routine
  `0x6490488` with a host-shaped synthetic device so the guest executes
  `call *0x18(%rax)` at `0x649067b`; header explicitly: "WITHHOLD SEED: use
  `sober_frame_dispatch_should_withhold()`" (`frame_dispatch.h:58–67,95`).
- **F4 `frame_draw`**: writes 8-byte JUMP_SLOT pointers at `0x7229fc8/
  0x722a040/0x722a048` and drives guest dispatcher `0x6496d0c` with a synthetic
  descriptor; header: "Everything here is harness-supplied guest state …
  must stay in `seeded_state`" (`frame_draw.h:5–8,30–34`).
- **F5 `frame_surface`**: installs GOT `0x72294a8` **and** `0x722a178`,
  writes ctx `+0x18/+0x28` and nativeData `+0x140`, calls guest helpers
  `0x26fea5a`, `0x649cf72`, `0x649d1c2` and lifecycle thunks
  `0x2a20850/0x2a208c0/0x2a20a00` (`frame_surface.h:12–18,64–85`; `G9 §2.1`).
- **F6 `frame_thread`**: dedicated pthread drives guest `0x2a20a00` repeatedly;
  `should_withhold()` is true whenever default-ON and installed
  (`frame_thread.c:261–265`; `G9 §3`). Its kill switch is **not** fail-closed,
  so the literal and the verdict both join the chain (`F9 §3`).

`G9 §4` recommends retiring/merging F1–F5 and F8; **retirement/merge does not
erase the withhold obligation** — a knob merged into `render_guest` joins the
chain in the same change (`MODULE_INTEGRATION.md §1`, `G7:401`).

---

## 4. Alias / prefix parity rules (lead-facing)

### 4.1 Literal alias parity

`D02 §5.1`: when one behavior is enabled by either of two names, **both names
stay in the chain permanently** — dropping either is a concrete false-green
bypass (precedents `SOBER_STARTAPP_V2`/`SOBER_STARTAPP_PARAMS`,
`SOBER_HELPER_REPAIR`/`_V2`). Frame alias sets to keep paired:

- `SOBER_EVIDENCE_SURFACE` ↔ `SOBER_FRAME_SURFACE` (F5 window/GOT driver).
- `SOBER_EVIDENCE_REDRAW` ↔ `SOBER_FRAME_REDRAW` ↔ `SOBER_FRAME_REDRAW_DRIVE`
  (F2 redraw driver; F6 `SOBER_FRAME_THREAD` overlaps this drive role).
- `SOBER_EVIDENCE_DEVICE` ↔ `SOBER_FRAME_DRAW` (F4 residual plain-GLES draw
  wrappers / device path).
- `SOBER_RENDER_FRAME_EVIDENCE` (E1 RA-split driver) is **NOT** an alias of
  `SOBER_FRAME_EVIDENCE` (F7 kill switch) — near-name collision (`F9 §6`).
  Chain only the former; never "fix" this by prefix-matching `FRAME_EVIDENCE`.
- `SOBER_RENDER_SUPPRESS_SYNTHETIC` (skip the swap) is **NOT** an alias of
  `SOBER_FRAME_SYNTH_TAG` (disable the observation tag) — different semantics;
  withhold the suppressor, keep the tag kill switch out (§0.5 caveat).

### 4.2 Prefix parity

`D02 §5.4/§7`: the chain uses `env_prefix_active("SOBER_*_")` so **future value
modifiers cannot silently escape**. Apply the same to frame drivers:

- Add `env_prefix_active("SOBER_EVIDENCE_")` — covers all E-series driver
  spellings and any future `SOBER_EVIDENCE_*` modifier.
- Keep `SOBER_RENDER_FRAME_EVIDENCE` and `SOBER_RENDER_SUPPRESS_SYNTHETIC` as
  explicit terms (no shared driver prefix beyond `SOBER_RENDER_`, which must
  **not** be prefix-withheld: `SOBER_RENDER` is the legitimate real path).
- **Do not** add `env_prefix_active("SOBER_FRAME_")`: it would capture the two
  observation kill switches (`SOBER_FRAME_EVIDENCE`, `SOBER_FRAME_SYNTH_TAG`)
  and the marker path `SOBER_FRAME_MARKER`. Enumerate the injector literals
  (`SOBER_FRAME_DISPATCH`, `_DRAW`, `_REDRAW`, `_SURFACE`, `_THREAD`) instead.
- If a frame module later gains a modifier family (e.g. `SOBER_FRAME_THREAD_*`
  config overrides), add that exact prefix in the same change (D02 Tier‑2 rule).
- Preserve the five existing prefixes `SOBER_GAME_`, `SOBER_STARTAPP_`,
  `SOBER_SGV2_`, `SOBER_PLATFORM_PARAMS_`, `SOBER_PROP_`
  (`guest_app_shell.c:1685–1688,1717`).

### 4.3 Presence semantics / value modifiers

`seeded_state` uses `getenv(name)` and `env_prefix_active(prefix)`: both match
**presence, not value**. So `SOBER_FRAME_THREAD=0` also withholds — acceptable
("any harness touch disqualifies"), and the safe direction. Value modifiers
that change guest state are covered by the prefix rules, not by checking the
value (`D02 §4 Tier 2`).

### 4.4 D09 allowlist parity

If D09 is widened from `guest_app_shell.c` to the frame TUs, extend its
`benign` allowlist (`tests/D09-withhold-audit.sh:81–89`) with
`SOBER_FRAME_EVIDENCE`, `SOBER_FRAME_SYNTH_TAG` and `SOBER_PROVENANCE_GATE`;
otherwise these fail-closed kill switches show as false gaps. Marker paths and
`SOBER_RENDER`/`SOBER_APP_SHELL` stay unlisted because they are not guest-state
seeds (`D02 §5–6`).

---

## 5. Disputed F8 point — verification detail

`F10 §1.3/FP-5/FC-9` calls `SOBER_FRAME_SYNTH_TAG=0` a "suppression knob"
because `enter()` becomes inert while decisive counts stay live; `G9 §3`
demands withhold-or-fail-closed. Source check (`frame_synthetic_tag.c:45–90`):
`enabled()` re-reads the env every call; when disabled, `enter/leave` are
no-ops, `active()` is 0, `note_swap()` is a no-op, and **no render_guest or
frame_evidence counter is touched**. The existing `rg_in_guest(ra)` classifier
in `render_guest.c:181–185,255–264` is independent and still tags the Route-B
swap synthetic by its host RA (`R1 §1`). Therefore the shipped kill switch
cannot, by itself, manufacture a guest-tagged green — it only removes an
additive defence. Ruling adopted here: **observation kill switch, not withheld**
(`G1 §4.4`, `MODULE_INTEGRATION.md §4`); however, if the lead wires `active()`
as a *decisive* classifier (rather than additive defence), FC‑9 requires
either `guest_render_ok()==0` when the tag is off or adding
`SOBER_FRAME_SYNTH_TAG` to the chain. Also keep the F10 note: bracket **all**
host→guest swap entries (including `sober_render_guest_swap_vtable()`) and keep
`g_seen`/`tagged_swaps` monotonic (no post-run `reset()`).

---

## 6. Exact patch shape for lead (informational; G3 edits nothing)

```c
/* guest_app_shell.c, extend the task-#477 / module-verdict block, :1746ff */
                       env_prefix_active("SOBER_EVIDENCE_") ||
                       getenv("SOBER_RENDER_FRAME_EVIDENCE") ||
                       getenv("SOBER_RENDER_SUPPRESS_SYNTHETIC") ||
                       getenv("SOBER_EVIDENCE_FRAME")          ||
                       /* shipped F2/F3/F4/F5 injector literals (until merged) */
                       getenv("SOBER_FRAME_REDRAW")            ||
                       getenv("SOBER_FRAME_REDRAW_DRIVE")      ||
                       getenv("SOBER_FRAME_DISPATCH")          ||
                       getenv("SOBER_FRAME_DRAW")              ||
                       getenv("SOBER_FRAME_SURFACE")           ||
                       getenv("SOBER_FRAME_THREAD")            ||
                       ...
                       sober_frame_thread_should_withhold()    ||
                       sober_frame_dispatch_should_withhold();
```

`SOBER_WINDOW_WIDTH`/`_HEIGHT` are already at `:1713–1714`; do not duplicate.
Do **not** add `SOBER_FRAME_EVIDENCE`, `SOBER_FRAME_SYNTH_TAG`,
`SOBER_PROVENANCE_GATE`, marker paths or `SOBER_RENDER`/`SOBER_APP_SHELL`.

---

## 7. D09-style audit command for lead (read-only, exact)

```bash
cd /home/user/sober-linux

# (a) Original D09 — still file-local, default-OFF, always exits 0.
LC_ALL=C SOBER_D09_WITHHOLD_AUDIT=1 bash tests/D09-withhold-audit.sh --run

# (b) G3 frame parity check — chain literals/prefixes vs the exact policy list,
#     plus an unclassified-literal scan over every frame_* TU. Read-only.
python3 - <<'PY'
import re, pathlib
root = pathlib.Path('.')
gate = (root/'src/linux/guest_app_shell.c').read_text(errors='replace')
m = re.search(r'int\s+seeded_state\s*=(.*?);', gate, re.S)
blk = m.group(1) if m else ''
chain = set(re.findall(r'getenv\("(SOBER_[A-Z0-9_]+)"\)', blk))
prefixes = set(re.findall(r'env_prefix_active\("(SOBER_[A-Z0-9_]+)"\)', blk))
MUST = {
 'SOBER_RENDER_FRAME_EVIDENCE','SOBER_EVIDENCE_SURFACE','SOBER_EVIDENCE_DEVICE',
 'SOBER_EVIDENCE_REDRAW','SOBER_RENDER_SUPPRESS_SYNTHETIC','SOBER_FRAME_THREAD',
 'SOBER_FRAME_DISPATCH','SOBER_FRAME_DRAW','SOBER_FRAME_SURFACE','SOBER_FRAME_REDRAW',
 'SOBER_FRAME_REDRAW_DRIVE','SOBER_EVIDENCE_FRAME',
}
BENIGN = {'SOBER_FRAME_EVIDENCE','SOBER_FRAME_SYNTH_TAG','SOBER_PROVENANCE_GATE',
          'SOBER_FRAME_MARKER','SOBER_SHELL_MARKER','SOBER_INIT_MARKER',
          'SOBER_RENDER','SOBER_APP_SHELL'}
def covered(k): return k in chain or any(k.startswith(p) for p in prefixes)
print('chain terms:', len(chain), 'prefixes:', sorted(prefixes))
print('MISSING must-join :', sorted(k for k in MUST if not covered(k)))
print('BENIGN in chain   :', sorted(k for k in BENIGN if k in chain))
un = []
for p in sorted((root/'src/jni').glob('frame_*.[ch]')):
    for k in set(re.findall(r'"(SOBER_[A-Z0-9_]+)"', p.read_text(errors='replace'))):
        if not covered(k) and k not in MUST and k not in BENIGN:
            un.append((p.name, k))
print('UNCLASSIFIED frame env literals:', sorted(set(un)) or '(none)')
PY
```

Expected **current** output (verified): `chain terms: 122`;
`prefixes: ['SOBER_GAME_', 'SOBER_PLATFORM_PARAMS_', 'SOBER_PROP_',
'SOBER_SGV2_', 'SOBER_STARTAPP_']`; `MISSING must-join` lists all 12 names in
`MUST`; `BENIGN in chain: []`; `UNCLASSIFIED …: (none)`. The intended clean
state after the lead applies §6: `MISSING must-join : []` and
`UNCLASSIFIED …: (none)` (or only `SOBER_FRAME_EVIDENCE` / `SOBER_FRAME_SYNTH_TAG`
shown under the D09 benign allowlist of §4.4).

---

## 8. Final answer (compact)

- **Withhold (env):** `SOBER_RENDER_FRAME_EVIDENCE`, `SOBER_EVIDENCE_SURFACE`,
  `SOBER_EVIDENCE_DEVICE`, `SOBER_EVIDENCE_REDRAW`,
  `SOBER_RENDER_SUPPRESS_SYNTHETIC`, `SOBER_EVIDENCE_FRAME`,
  `SOBER_FRAME_THREAD`, `SOBER_FRAME_DISPATCH`, `SOBER_FRAME_DRAW`,
  `SOBER_FRAME_REDRAW`, `SOBER_FRAME_REDRAW_DRIVE` (alias),
  `SOBER_FRAME_SURFACE`. **Withhold (verdict):** `sober_frame_thread_should_withhold()`,
  `sober_frame_dispatch_should_withhold()`. Already in chain and to be kept:
  `SOBER_NOTIFIER_PROBE`, `SOBER_WINDOW_WIDTH`, `SOBER_WINDOW_HEIGHT`.
- **Do not withhold:** `SOBER_FRAME_EVIDENCE=0`, `SOBER_FRAME_SYNTH_TAG=0`
  (observation kill switches; §5 caveat), `SOBER_PROVENANCE_GATE=0`, marker
  paths, `SOBER_RENDER`, `SOBER_APP_SHELL`.
- **Parity:** pair canonical/shipped aliases permanently; prefix-match only
  driver families (`SOBER_EVIDENCE_`), never `SOBER_FRAME_` or `SOBER_RENDER_`
  wholesale; presence (not value) withholds.
- **Verified:** F7/F8 are observation-only and fail closed/safe; F2/F3/F4/F5/F6
  are injectors and must be withheld. Engine unchanged; marker **WITHHELD**.
