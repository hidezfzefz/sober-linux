# Decision 002 — Shared interfaces and contracts

Status: accepted (lead). Applies to all roles.

- `docs/interfaces/abi-map.md` (lead owns) is the canonical symbol table:
  name, signature, calling convention, source subsystem, evidence IDs.
  worker-3 implements exactly this contract in `src/compat/`. Workers 1 & 2
  feed it with evidence and request additions from lead.
- REA deliverables: `analysis/rea/findings.json` (machine-readable) and
  `analysis/rea/report.md` (narrative, evidence IDs inline).
- Ghidra deliverables: decompiled sources under `analysis/ghidra/` plus
  `analysis/ghidra/interface-map.md` mapping each decompiled function to an
  `abi-map.md` entry.
- Any proposed change to a contract is a message to lead, never a silent edit.
