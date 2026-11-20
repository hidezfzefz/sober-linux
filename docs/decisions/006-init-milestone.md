# Decision 006 — New milestone: guest init_array completes natively

Status: accepted (lead). Supersedes the "full Roblox port" scope *for this
team only*; the long-term goal is unchanged.

## Done condition for this team

`tests/guest-dlopen.sh` maps the unmodified AppImage `libroblox.so`
(BuildID `204ad049...`) natively and the guest's `init_array` **completes** —
no `basalt`/`mocktail`/Android runtime, no translation layer, no Wine. Measured
by a deterministic "guest init complete" signal in the harness.

Explicitly out of scope for this team: rendering a window, login/app shell,
full JNI replacement, `AMediaCodec`/`AImageReader` (they stay documented stubs).

## Why this is the next blocker

`tests/guest-dlopen.sh` currently reports:

```
dlopen harness exit: 139
last init: calling init: .../libroblox.so
RESULT: all DT_NEEDED + versioned symbols resolved; blocker has moved
        into the guest's init_array (Android/Bionic bootstrap patching).
```

All 411 Bionic versioned symbols resolve via `build/ndk-stubs/libc.so`
(forwarding to glibc). What is missing is the **runtime bootstrap** the guest
expects Android to have performed before its constructors run:

- allocator object slot `0x7834570`, initializer `0x2251b2a`
- JNI singleton slot `0x7d9cf40`, initializer `0x1dba834`
- empty string slot `0x79413b8`, initializer `0x225dd2a`
- pre-JNI registry slot `0x7cdd118`, initializer `0x227be14`
- constructor ranges `[2,3)` and `[5,3618)` over `init_array` at `0x7220e58`
- six mimalloc bridges (`0x1dbab02`, `0x1dbad62`, `0x1dbdfba`, `0x227aafa`,
  `0x1dd60e5`, `0x1dbe559`)

These RVAs are from the **AppImage** build only. The Flatpak engine
(`791c3f9e...`, 2.745) is a different build — never mix RVAs.

## Team / ownership (fresh Convio team `TEAM-GJ4W-25NU-FJLZ`)

| Path | Owner |
| --- | --- |
| `src/compat/bionic_bootstrap/` | worker-1 |
| `analysis/bootstrap/` | worker-2 |
| `tests/`, `src/linux/`, `CMakeLists.txt`, `Makefile`, `build/` | worker-3 |
| `docs/`, `README.md`, `PROGRESS.md` | lead |

Frozen for this milestone (read-only): existing `src/compat/android_compat.*`,
`gl_egl_forward.c`, `bionic_libc.*`.
