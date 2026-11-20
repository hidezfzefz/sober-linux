# Decision 012 — Phase 4: native JNI bridge and app shell

Status: accepted (lead). Fresh Convio team `TEAM-CYLN-PBYP-H7RL`.

## Starting point

Phase 3 is complete: the harness maps the AppImage engine (`204ad049...`),
runs all 3618 constructors, creates a visible native X11 window, calls the
guest render entry point at RVA `0x649d422`, and prints `first frame rendered`
gated on that guest call. No Android runtime or translation layer.

## Done condition

- a native JNI bridge satisfies the guest's JNI callback surface without
  `basalt`/`mocktail`;
- the harness drives the guest's app-shell startup path;
- a natively rendered login/app-shell frame is presented;
- a deterministic `app shell rendered` marker is printed, asserted by
  `tests/app-shell.sh`;
- all phase-2/3 checks remain green, `sober-native` ldd stays clean, and the
  on-disk engine hash is unchanged.

A **native JVM is acceptable** (e.g. OpenJDK running as native code) — it is a
runtime, not an instruction-translation layer. No Wine/Proton/box64/Android
runtime.

## Ownership

| Path | Owner |
| --- | --- |
| `analysis/jni/` | worker-1 |
| `src/jni/` | worker-2 |
| `tests/`, `src/linux/`, `CMakeLists.txt`, `Makefile`, `build/` | worker-3 |
| `docs/`, `README.md`, `PROGRESS.md` | lead |

Frozen (read-only): `src/compat/`, `src/render/`, `analysis/render/`,
`analysis/bootstrap/`, `analysis/rea/`. AppImage build `204ad049...` only;
never mix RVAs with the Flatpak build `791c3f9e...`.
