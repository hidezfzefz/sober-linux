# Decision 035 — Entry logger result: bogus object pointer at helper entry

Status: accepted (lead). Strict gate unchanged.

## Diagnostic implemented

A non-perturbing naked-asm entry logger (no int3, no signals, full
register/flag preservation, near-page rel32 stub) was added at:
- F_inner `0x23330e7` (opt-in `SOBER_FINNER_LOG`)
- consumer helper `0x233319a` field load `0x23331ae`
  (opt-in `SOBER_HELPER_LOG`, with `SOBER_HELPER_REPAIR` experimentation)

The guest engine had to be re-extracted from the AppImage after server
restarts cleared `/tmp`; the repo copy in `analysis/rea/work/` is the wrong
(Flatpak `791c3f9e`) build. The correct engine is BuildID `204ad049...`.

## Decisive output

```
[helper-log] installed at 0x23331ae
[finner-log] hit=1 obj=0x7f836840263f +0x68=0xffffff3085894cff slot=...
```

The object dereferenced by the consumer is `guest+0x233263f` — a **code
address** (bail-out block of the curl-getinfo wrapper `0x2332577`), not a heap
handle. That is the origin of the stale code bytes at `+0x68`.

All earlier hypotheses are therefore ruled out: allocator zeroing, object
graphs, caller patches at `0x6a26e78`, `0x245771e` and `0x234ddc0`, singleton
seeding. An attempted in-stub repair (calling guest getter `0x231f294`)
crashed; reading a cached handle did not fire because the repair predicate did
not match. All experimental paths remain opt-in and off by default.

## Next

Eight parallel research agents are tracing: all callers of `0x233319a`, the
safe-vs-failing path difference, the repair predicate bug, the origin of the
`0x233263f` residue, curl-holder field semantics, a safe guest-handle handoff,
an alternate callback path, and the getter handle type.

Phase-2/3 remain green; `app shell rendered` is still withheld.
