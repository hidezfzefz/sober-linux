# Decision 010 — Phase 3: native window and first frame

Status: accepted (lead). Fresh Convio team `TEAM-CEXX-GSEL-3UA5`.

## Starting point

Phase 2 is complete and verified: `build/guest-init-host` maps the unmodified
AppImage `libroblox.so` (BuildID `204ad049...`) through a private memfd copy,
applies all 565,390 Android APS2 packed relocations, installs the native
Bionic bootstrap, drives all 3618 constructors, and exits 0 with
`guest init complete`. No Android runtime or translation layer is involved.

## Done condition

The extended harness must:
1. create a **native Linux window** (X11 or Wayland) through our own code;
2. establish a native EGL context/window surface;
3. drive the guest's post-init render path far enough to render a **first frame**;
4. print a deterministic `first frame rendered` marker;
5. preserve every phase-2 check (init completion, ctest 2/2, `verify-native.sh`,
   ldd libc-only for `sober-native`, on-disk engine hash unchanged).

Out of scope: full JNI replacement, login/app shell, and the remaining
`AMediaCodec`/`AImageReader` stubs.

## Ownership

| Path | Owner |
| --- | --- |
| `analysis/render/` | worker-1 |
| `src/render/` | worker-2 |
| `tests/`, `src/linux/`, `CMakeLists.txt`, `Makefile`, `build/` | worker-3 |
| `docs/`, `README.md`, `PROGRESS.md` | lead |

Frozen (read-only): `src/compat/`, `analysis/bootstrap/`, `analysis/rea/`.
All RVAs must come from the AppImage build `204ad049...`; the Flatpak build
`791c3f9e...` is a different binary.
