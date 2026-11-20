# Decision 001 — Folder layout and ownership

Status: accepted (lead). Applies to all roles.

All paths under `/home/user/sober-linux/`. Ownership is exclusive — do not
edit another role's files; post a `team_send` hand-off instead.

| Path | Owner |
| --- | --- |
| `analysis/rea/` | worker-1 |
| `analysis/ghidra/` | worker-2 |
| `src/compat/`, `src/linux/`, `CMakeLists.txt`, `Makefile` | worker-3 |
| `docs/decisions/`, `docs/interfaces/`, `README.md`, `tests/` | lead |
| `build/` | generated, never committed |

Task definition of done: deliverables exist in owned paths, claims carry
evidence, native build still succeeds, and a `team_send(kind: handoff)` is
posted describing what changed and the next role's action.
