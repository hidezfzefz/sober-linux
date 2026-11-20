# Sober client binaries — inventory & REA triage

Worker-1, task #368. Date 2026-11-18. Static, read-only; no target was launched.
Machine: REA MCP 6.7.0 with Ghidra 12.1.4 bridge and the pwntools ELF layout adapter.
Machine-readable companion: `analysis/rea/findings.json`.

Executive summary
-----------------
There is a scope correction worth flagging to the lead: **the Sober client on this
PC is not a Windows PE.** It is a Linux-native ELF launcher (`sober`) that
dynamically loads an Android-target ELF engine (`libroblox.so`) through a small
Android trampoline (`libtrampoline.so`). Every one of those artifacts is x86-64
ELF. The only Windows PE Roblox artifacts on the box are a bootstrapper installer
and a Vinegar-cached Studio build; neither is the game client.
[ev_dc1114f88116e661c40948870eb04f1a8c09c413525e39c16cdcc4302c36fb31]
[ev_96cdca987fbc84aa7d9cf7098498c7396373fb5a4fe3ffc0c5ba850db665e71c]
[ev_def57750a30371b1a785525808bbd3ab9977456b94852e3f01eadb74dfd39624]

The native dependency chain, from REA-linked-library evidence:
`sober` → `libloader.so` → `libbadcpu.so`, with `sober_services` as a GTK4/WebKit
IPC helper; `libtrampoline.so` provides the Android `dlopen`/`dlsym` bridge into
`libroblox.so`.
[ev_dc1114f8…][ev_d14bb0b…][ev_ab41657…][ev_96cdca98…][ev_04fe3594…]

1. Target inventory
-------------------

### 1.1 Sober native launcher set (Flatpak `org.vinegarhq.Sober`)

All four are **ELF64 x86-64 (EM_X86_64, 64-bit, little-endian) with no section
headers**, so classic symbol/relocation tables are absent.

| Binary | SHA-256 | Size | REA evidence |
| --- | --- | --- | --- |
| `sober` | `a0967a20…b5211` | 7,612,072 | `ev_dc1114f88116e661c40948870eb04f1a8c09c413525e39c16cdcc4302c36fb31` |
| `sober_services` | `4d4ad69e…8cdff1` | 1,132,200 | `ev_04fe359416120a2748113527563d62d49ba208363d93b601c123192d1917ed3a` |
| `libloader.so` | `fa479186…5f7f8c` | 3,982,961 | `ev_d14bb0b3262bcef52cfb1d6d93017d585a0873051b89f1007ced6903b41f5b04` |
| `libbadcpu.so` | `ad394c74…9c22d` | 390,104 | `ev_ab4165782c4d929fc2ba1c23f67a11ecdb4c57c956f24b5c8e4824d0938e642f` |

- `sober`: ET_DYN PIE, entry `0x1e6740`, 17 segments, 0 sections / 0 symbols /
  0 relocations, interpreter `/lib64/ld-linux-x86-64.so.2`; mitigations PIE +
  NX + Partial RELRO, no executable stack. REA reports "Did not find any GOT
  entries", consistent with the stripped section table.
  `ev_dc1114f88116e661c40948870eb04f1a8c09c413525e39c16cdcc4302c36fb31`
- `sober_services`: ET_DYN PIE, entry `0x3b940`, 13 segments, same absence of
  sections; PIE + NX, RELRO undetermined by the static heuristic.
  `ev_04fe359416120a2748113527563d62d49ba208363d93b601c123192d1917ed3a`
- `libloader.so`: ET_DYN, no entry point (library), 12 segments; PIE + NX.
  `ev_d14bb0b3262bcef52cfb1d6d93017d585a0873051b89f1007ced6903b41f5b04`
- `libbadcpu.so`: ET_DYN, no entry point, 10 segments; PIE + NX.
  `ev_ab4165782c4d929fc2ba1c23f67a11ecdb4c57c956f24b5c8e4824d0938e642f`

Paths: `/var/lib/flatpak/app/org.vinegarhq.Sober/x86_64/stable/active/files/bin/`.

### 1.2 Roblox Android client containers

`base.apk` — `fff6fcb4…498a`, 104,550,055 B, REA format `apk` / kind `archive`.
[artifact graph `ev_876386fe7b419f431195fffbea1a75c6b74e8e6c2a14eaaa9549a254fcfeb794`,
inventory `ev_19995ef406d27239e82faa97e33e53ec46692f5d2abc642d4199f7941c4cd552`,
summary `ev_6cf526bb2a778471f7aa1e3fa66dc902cb142b02bf9b01631fad83b93551b8aa`]

- 7,000 central-directory entries examined; artifact graph 2,259 nodes /
  2,371 occurrences / 2,370 edges; 0 integrity contradictions; `source_complete: true`.
  `ev_6cf526bb2a778471f7aa1e3fa66dc902cb142b02bf9b01631fad83b93551b8aa`
- Package `com.roblox.client`, versionName `2.745.1142`, versionCode `20136`,
  minSdk 26, targetSdk 35; 14,097 classes, 2,370 resources, 29 permissions;
  manifest decode complete; signature verification **not performed**.
  Engine: JADX headless 0.7.1.
  `ev_9c8f4dbe81e5b698071437c3ef189e0df79f106975438a2a6f22fc7049e40254`

`split_config.x86_64.apk` — `51b8f6da…da6fb`, 53,732,314 B, format `apk`.
[artifact graph `ev_cd932d3d0f292034ee1046d35c497a1dded5e98af261c97bc64872324ab72711`,
inventory `ev_788ffeb7b962e6a7e05a22a2b445135b7e20ea5d4559bb82d4e78ec35ec02f47`,
summary `ev_2269b286a31e206f5ed88f63355e60c5d4d68cc24f36e5b08f52221b6fc35725`]

- 41 entries examined; graph 14 nodes / 14 occurrences / 13 edges, all files,
  0 contradictions, `source_complete: true`.
  `ev_2269b286a31e206f5ed88f63355e60c5d4d68cc24f36e5b08f52221b6fc35725`
- Native libs carried: `libroblox.so`, `libtrampoline.so`,
  `libbacktrace-native.so`, `libdatastore_shared_counter.so`, `libeigen_blas.so`,
  `libeigen_lapack.so`, `libimage_processing_util_jni.so`,
  `librenderscript-toolkit.so`, `libsurface_util_jni.so`, `libyuv_shared.so`,
  `libzstd-jni-1.5.7-6.so`. (Entry names from extraction into
  `analysis/rea/work/`; the REA graph evidence above backs the 14-entry inventory.)

### 1.3 Windows PE artifacts (comparison only)

`RobloxPlayerInstaller.exe` — `0c46c704…0946b`, 13,550,544 B. REA: PE32+,
machine `0x8664` (x86-64), 7 sections, no CLI data directory (native, not .NET).
[`ev_def57750a30371b1a785525808bbd3ab9977456b94852e3f01eadb74dfd39624`]
Resource tree present, resource-directory RVA 20,893,696.
[`ev_4d412c37115d9594f9bcc79b1c132f91652602823405327e9c9d525790bb11f5`]
Strings (local-shell, not REA evidence) show `roblox-player` / `roblox-studio`
schemes, `-isInstallerLaunch`, `WinInstallerPersonalizedToken`, manifest fetching
and payload names including `RobloxApp.zip` and `WebView2.zip` — a bootstrapper,
not the game engine.

`RobloxStudioBeta.exe` (Vinegar version cache) — `de46fef8…f8bde0`,
225,542,608 B, PE32+ x86-64, 9 sections (local-shell inventory). No REA evidence
ID in this pass: it exceeds the 64 MiB `inspect_pe_resources` default cap.

2. Imports, linked libraries and API surface
--------------------------------------------

### 2.1 Native launcher (glibc API surface)
- `sober` needs 20 libraries, **with `libloader.so` first**, then crypto/curl/
  freetype/fontconfig/z/libsecret/glib-2.0/gobject-2.0/libxml2/GLESv2/EGL/
  gstreamer (1.0/app/video)/dbus-1, plus libm, libgcc_s, libc and ld-linux.
  Reading: network + keyring, AndroidGL surface, video playback, desktop
  integration and text rendering.
  `ev_dc1114f88116e661c40948870eb04f1a8c09c413525e39c16cdcc4302c36fb31`
- `libloader.so` needs only `libbadcpu.so`, libgcc_s, libc and ld-linux — a
  minimal trampoline loader with no other custom dependency.
  `ev_d14bb0b3262bcef52cfb1d6d93017d585a0873051b89f1007ced6903b41f5b04`
- `libbadcpu.so` needs only libgcc_s and libc — consistent with a SIGILL handler
  and CPU feature probing.
  `ev_ab4165782c4d929fc2ba1c23f67a11ecdb4c57c956f24b5c8e4824d0938e642f`
- `sober_services` needs WebKitGTK-6.0, GTK-4, Soup-3.0, JavaScriptCoreGTK-6.0,
  Adwaita-1 plus GLib/GObject/Gio — a WebView IPC helper, not a renderer.
  `ev_04fe359416120a2748113527563d62d49ba208363d93b601c123192d1917ed3a`

### 2.2 Android trampoline (the actual bridge API)
`libtrampoline.so` — sha256 `cb176fc08…8020`, 4,920 B, ELF64 ET_DYN with 24
sections, 11 segments, 8 symbols, 9 relocations, entry `0x4760`, interpreter
`/system/bin/linker64`, Full RELRO and stack canary; 25 procedures / 41 strings.
[layout `ev_96cdca987fbc84aa7d9cf7098498c7396373fb5a4fe3ffc0c5ba850db665e71c`,
overview `ev_943bb261c4e2da0d1f4020caefca774533052b34d11dd066348e51352bae087a`]

- Needs `liblog.so`, `libdl.so`, `libc.so` only. Imported API:
  `__libc_init`, `__cxa_atexit`, `dlopen`, `dlsym`, `dlerror`,
  `__android_log_print`, `__stack_chk_fail`.
  `ev_96cdca987fbc84aa7d9cf7098498c7396373fb5a4fe3ffc0c5ba850db665e71c`
- Embedded strings confirmed by REA string search: `dlopen`, `dlopen: %s`
  [`ev_743f3d6dc481b6d65780996f09b5c94fc55baf4c7c5b7d5d32e3c166ce14987c`];
  `crashpad_trampoline`, `CrashpadHandlerMain`
  [`ev_acdfded49dc4e7230fc55d778bdac4dbd4d9c1ceffd6e20ab7b3a0c87eb1ab51`];
  `usage: %s <path>`
  [`ev_cdb6ca7387b824fa5707ad0be0651cb7067108949286c9cd24c1a5bb6bac4290`].

### 2.3 The engine (`libroblox.so`)
Extracted from the split APK: sha256 `8afce3e695d08fe830d7857752c459de9a06c97390c7365a6660827e5d072e78`,
116,461,944 B. REA objdump evidence:
- file format `elf64-x86-64`, flags `HAS_SYMS, DYNAMIC, D_PAGED`, start address `0x0`
  [`ev_95f5941c0554ff3819090ae028df91eb3fd6242bf4b4e244d7f38ae310175fa0`];
- `objdump -t` returns "no symbols" — the static `.symtab` is stripped
  [`ev_f6dae8ea284092230cc867de36ed667bb86d6c089621e0a986f8780827268736`].

A full dynamic-import/export inventory could **not** be produced with a fresh REA
evidence ID in this pass: `inspect_binary_layout` rejects the 116 MB file at the
32 MiB input cap, and Ghidra auto-analysis of this file previously timed out.
The prior pass's `readelf` extracts (source=local-shell, **not** REA evidence)
are retained at `/home/user/sober-rea-fresh/elf-dynamic.txt`, `elf-dynsyms.txt`
and `native-strings-report.json`; they report 10 `NEEDED` libraries
(`libandroid`, `liblog`, `libOpenMAXAL`, `libmediandk`, `libGLESv2`, `libEGL`,
`libOpenSLES`, `libdl`, `libm`, `libc`), SONAME `libroblox.so`, `BIND_NOW`/`NOW`,
1,183 dynamic symbols (Android `ALooper_*`, `ANativeWindow_*`,
`AConfiguration_*`, `AAssetManager_*`, `AHardwareBuffer_*`, `__android_log_*`,
GLES/EGL, OpenSLES/OpenMAXAL, media NDK) and 573 `Java_…` JNI exports.
Treat those figures as derived, not REA-attested.

3. Tooling limitations encountered (hand-off)
----------------------------------------------
1. **Ghidra-backed REA fails on sectionless ELFs and raw-binary targets.**
   `ReaGhidraAnalysisOptions.java` throws
   `IllegalArgumentException: Unknown Ghidra analyzer for this program: GCC Exception Handlers`,
   and the bridge reports "Ghidra exited before bridge startup". Reproduced on
   `libbadcpu.so` this pass. Use `inspect_binary_layout`, `inspect_with_objdump`,
   `inspect_android_package` and `inspect_artifact` instead — none require Ghidra
   analysis. String-search evidence was obtainable only for the sectioned
   `libtrampoline.so`.
2. `inspect_binary_layout` input cap is 32 MiB (effective `file_size_bytes`
   67,108,864) — blocks `libroblox.so`.
3. `inspect_pe_resources` default cap is 64 MiB (raise `max_file_bytes` up to
   536,870,912 if Studio PE resources are needed).
4. APK signature verification is not performed by `inspect_android_package`
   [`ev_9c8f4dbe81e5b698071437c3ef189e0df79f106975438a2a6f22fc7049e40254`].

4. Bottom line for the team goal
--------------------------------
Any "run natively on Linux with no translation layer" plan has to replace two
distinct surfaces, not one: (a) the glibc/GTK/WebKit side of the Sober launcher,
which is already native Linux, and (b) the **Android Bionic + JNI + NDK API
surface** of `libroblox.so`, which is the real translation boundary. The smallest
concrete evidence-backed bridge to attack first is `libtrampoline.so`'s
`dlopen`/`dlsym`/`__android_log_print` imports
[`ev_96cdca987fbc84aa7d9cf7098498c7396373fb5a4fe3ffc0c5ba850db665e71c`].

5. AppImage build & mocktail bridge profile (task #372, decisions 003/004)
---------------------------------------------------------------------------
Lead re-scoped the primary target to `/home/user/Downloads/sober.AppImage`
(sha256 `72f1ef21…c839`, local-shell inventory). Its extracted engine is
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` — sha256
`3e498d64…41335`, 120,041,712 B, BuildID `204ad049599c96afb75b8a21d5051063e821d6c9`,
ELF64 x86-64 for Android 26 / NDK r28c, stripped. REA objdump confirms
`elf64-x86-64`, `HAS_SYMS, DYNAMIC, D_PAGED`, start address `0x0`.
[`ev_af5de5324a9e1df40ada21a17ab9fe86a747fc6c81a0421574c952f5d077eba4`]
This is **a different build** from the Flatpak engine in section 1
(BuildID `791c3f9e…`, version 2.745.1142); do not mix their RVAs.

### 5.1 The host runtime to eliminate
- `basalt` (19,879,392 B, sha `fb4214f6…ca2d2`) is ELF64 ET_DYN PIE, entry
  `0xffa0`, 28 sections / 15 segments / 331 symbols; it needs only
  `libpython3.12.so.1.0` and `libc.so.6`; PIE + NX + canary + Full RELRO.
  [`ev_08ca2afd51cd353c80fedfb364c601e7be15107d8b9f506b77e20d8c650ee405`]
  Ghidra/REA overview: 5,208 procedures, 46,930 strings, 31 segments
  [`ev_e282d64e8d460e632e208dc4a20014c39dafe0d52eb0cf1d4ceeb60e5ee057bf`];
  string evidence includes `Py_InitializeFromConfig`
  [`ev_45d1e78189dde2651a20186571fd6ab7cc3f6735fa26dd6047cf71800e06fca5`].
  Its 331 dynamic symbols break down as 328 **undefined** Python C-API imports
  (`Py_InitializeFromConfig`, `PyEval_*`, `PyObject_*`, `PyMem_*`, `PyExc_*`)
  and only 2 defined symbols (`Py_Main`, `Py_GetArgcArgv`), from REA symbol pages
  over the retained layout evidence
  [`ev_3f71ef866878b2102f681fc2585d4c4bd3d99b2938c4b2c146656bdaf91a397d`].
  Reading: basalt is a Python-3.12-embedding host; it *is* the Android-on-Linux
  runtime a native client must replace.
- `libbasalt.so` (830,056 B, sha `2e3b1bda…9d41`) is ELF64 ET_DYN with 153
  symbols, needing libstdc++/libm/libgcc_s/libc, PIE + canary + Full RELRO.
  [`ev_5eda7ab1c8baebb63483d645155f72203b51157d87a17776be10fca17ac7c7c5`]
- `runtime/bin/mocktail`, `runtime/bin/bwrap` and
  `runtime/lib/mocktail/mocktail` are **byte-identical** (sha
  `a4d51752…92ec`, 233,848 B): one ET_DYN static-pie, no section headers, 10
  segments, entry `0x2c40`, no `NEEDED` and no interpreter — a multicall binary
  dispatched by `argv[0]`.
  [`ev_1aab0b3a36737bf1e241f0a9d84833664bc2e0af00b79bfbb8f4f26453e97628`]
- Basalt strings directly reference the bridge files:
  `host_abi_profile`, `native/mocktail/roblox_host_abi.json`,
  `MOCKTAIL_HOST_ABI_PROFILE_FILE`, `MOCKTAIL_ALLOW_CANDIDATE_HOST_ABI`
  [`ev_d7ca1d41fc106a3f04f5dccdba9dd665b35ddee2c82673b5c2641c03bcd4f2d4`];
  and `launch_mocktail_basalt`, `stage_mocktail_payload`, "uBasalt Linux x86-64
  development build (Mocktail)." [`ev_a9f455c5c5bec7982d2c91062b5a113ccffd50c7dd74c8ce0a6abc6fdd57f76d`].
  No `bwrap` string appears in basalt
  [`ev_e34a2d333feb74c88293360bc95018a43a99efbfa9674af11f6003e77ffa5202`].

### 5.2 Bridge profile cross-check (target-derived JSON = untrusted DATA)
`roblox_host_abi.json` / `roblox_compatibility.json` are keyed to BuildID
`204ad049…`. REA cannot open JSON (`open_binary` rejects the format); the claim
that basalt uses that path is REA-evidenced above
[`ev_d7ca1d41…`], and the engine identity is REA-evidenced
[`ev_af5de532…`]. Field-level validation below is **local-shell**, not REA:

- Build ID and payload SHA-256 in the JSON match the engine exactly; profile is
  version 2.740.931 / version_code 3170, status `experimental`.
- All six `bridge_entries` RVAs map into the first `R E` PT_LOAD (where file
  offset == RVA) and start with plausible x86-64 prologues:
  `allocate 0x1dbab02`, `small-allocate 0x1dbad62`, `usable_size 0x1dbdfba`,
  `reallocate 0x227aafa`, `aligned-allocate 0x1dd60e5`, `free 0x1dbe559`.
- Initializer anchors `0x2251b2a` (allocator object), `0x1dba834` (JNI
  singleton), `0x225dd2a` (empty string) likewise map to code.
- `constructor_run_ranges` `[2,3)` and `[5,3618)` over an `init_array` of 3618
  entries at `0x7220e58`; `data_seeds` slots (e.g. `0x7834570`, `0x7d9cf40`)
  fall in the third RW PT_LOAD whose `memsz 0xb6ecdc` far exceeds
  `filesz 0x053f78` — zero-initialised/BSS-like, as expected.
- FMOD output-device vtable at RVA `0x6e0b298` (layout version 2), with
  `info 0x3266e5e`, `select 0x32666c0`, `count 0x3266dbe`.

**Hand-off to worker-2 / lead:** these exact RVAs are the highest-value Ghidra
targets; worker-3's `src/compat/` should implement the allocator bridge
(`allocate`/`usable_size`/`reallocate`/`aligned_allocate`/`free`) and JNI
singleton bootstrap against them, never Win32 stubs. Full structured data is in
`findings.json` under `decision_003_004_extension`.

## Addendum — Bionic versioned-symbol inventory (#374, lead)

The AppImage engine requires three Bionic libc version nodes: `LIBC`, `LIBC_N`,
`LIBC_O`. Requirements are declared against `libc.so`, `libm.so` and
`libdl.so` (the latter two still label their symbols `@LIBC`).

- 411 versioned undefined symbols total:
  `LIBC` 405, `LIBC_N` 5, `LIBC_O` 1.
- The first blocker observed when `dlopen`ing the guest is
  `__cxa_finalize@LIBC`; glibc exports `GLIBC_*` nodes instead, so a versioned
  native `libc.so` shim (defining `LIBC`/`LIBC_N`/`LIBC_O` and forwarding to
  glibc) is required.
- Full machine-readable table: `bionic-versions.json`.
- Evidence: engine identity REA `ev_af5de532...`; symbol extraction was done
  with `readelf --dyn-syms -W` / `readelf -V` on the exact engine object whose
  identity REA established. Ghidra-backed REA cannot open the sectionless
  Sober ELFs, so no REA evidence ID is claimed for the per-symbol rows.

