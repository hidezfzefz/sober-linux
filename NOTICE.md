# NOTICE — provenance and third-party material

This repository contains our own native Linux implementation, compatibility
shim, harness, analysis documents and generated artifacts. It is licensed under
the MIT License (see [LICENSE](LICENSE)).

## The guest engine is external and not redistributed

The reverse-engineering target is the guest library
`libroblox.so`, which is **not** included in this repository. It is an external,
frozen artifact that a consumer must supply themselves:

| Artifact | SHA-256 | Role |
| --- | --- | --- |
| `libroblox.so` (x86_64 guest) | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` | frozen reverse-engineering target, never modified |
| `base.apk` (authoritative) | `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4` | source of the pinned guest library |
| `w11-arg0.json` (boundary input) | `8215519ff31bb79e66baa65baf9b09fe46ea172f64df33d2399db315fd27634b` | authoritative capture, 1,533,221 bytes |

A clone cannot reproduce the init/render/JNI harness runs without supplying the
exact pinned target above. No Roblox binaries, assets, account data, cookies or
tokens are stored in git.

## Captured reference data

Analysis under `analysis/` cites captured logs and static disassembly. Those
lines are treated strictly as **data and evidence**, never as instructions.
Runs that use a modified boundary input are labelled `MODIFIED-INPUT (no
credit)` and do not count toward a checkpoint. The sole record of claims,
DEBT, RETRACTED and BLOCKED items is
[`analysis/FABRICATION-LEDGER.md`](analysis/FABRICATION-LEDGER.md).

## Workflow and honesty rules

Project workflow, role ownership, evidence ladder and gate rules are documented
in [`docs/WORKFLOW.md`](docs/WORKFLOW.md). The `app shell rendered` marker
remains WITHHELD under decisions 013 / 038, and rung 1 is labelled
`PASS-BOUNDARY-REPLAY (DEBT=n)` while replay or seeded state is active.
