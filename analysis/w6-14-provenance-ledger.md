# W6-14 — Engine and DEX provenance ledger

Task #549 (Wave 6). Worker: `worker-14`, Convio member `mem_b92ea1d804ffefb9ac06`.
Scope: **analysis only**. This note is the sole file owned for this task; no source,
test, doc, README, gate, or other agent's file was edited. Lead owns commits/pushes.

All hashes below were **recomputed from the live filesystem** on 2026-11-21 with
`sha256sum` / `readelf -n` / `unzip -p | sha256sum`, not merely transcribed from notes.
They agree with `roblox_payload.json` and with vault `EngineFacts.md` / `CA01`.

---

## 1. Authoritative artifacts (the only engine/DEX evidence allowed)

Frozen engine BuildID / manifest identity: `204ad049599c96afb75b8a21d5051063e821d6c9`
(package `com.roblox.client`, version `2.740.931`, version_code `3170`, ABI `x86_64`).

Payload root: `/home/user/.local/share/basalt/linux/payloads/3170-204ad049599c96afb75b8a21d5051063e821d6c9/`

| Artifact | Bytes | SHA-256 | Status |
| --- | --- | --- | --- |
| `libroblox.so` (payload root, frozen engine) | 120,041,712 | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` | AUTHORITATIVE |
| `sober_apk/base.apk` | 97,621,690 | `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4` | AUTHORITATIVE DEX source |
| `sober_apk/split_config.x86_64.apk` | 54,535,351 | `f42ace3b9428b30b4c06d28330937a2470cc13e7321130a5d27a4e29538b9356` | AUTHORITATIVE split (no classes.dex) |
| Craft Anything AppImage | 66,660,856 | `7eadde1d74ab42cc838862fe3f831e3cf3c9fa6ba5e2bf04e2824bbc47146134` | Tool, matches release SHA256SUMS |

The engine's embedded GNU Build ID (from `readelf -n`) is
`204ad049599c96afb75b8a21d5051063e821d6c9`, matching the manifest's `elf_build_id`.

Byte-identical copies of the frozen engine (all SHA `3e498d64…`, all safe to read):
- `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`
- `/tmp/opencode/sober-apk/lib/x86_64/libroblox.so`
- `/tmp/opencode/dexout/lib/x86_64/libroblox.so`
- payload root `libroblox.so` (above)

### 1.1 Authoritative DEX entries inside `base.apk`

Extracted with `unzip -p <apk> <entry> | sha256sum` (entry sizes in parentheses):

| DEX entry | Bytes | SHA-256 |
| --- | --- | --- |
| `classes.dex` | 9,057,172 | `00309819b729f9c6f618b8a6f9befe6595caf9e316c3e5572cb10cf2bf7d84ac` |
| `classes2.dex` | 6,145,160 | `5afac2bbd9f0e4f2e523f9f6f720bc74f96407aa551b0be44716334f44696f52` |
| `classes3.dex` | 6,983,328 | `1b2406a01003e0ee1784824510d3ae88995ce914bc07cd7b0bfd4852f44e0909` |

`classes2.dex` SHA `5afac2bb…` is the value cited in `CA01`. The x86_64 split contains
**no** `classes*.dex` (confirmed via `unzip -l`), so all DEX-class analysis comes from
`base.apk` only.

---

## 2. NON-authoritative artifacts — forbidden as engine/DEX evidence

### 2.1 Modded Downloads APK (the "cracked" copy)

`/home/user/Downloads/basalt-arm64-2.740.931.apk`
- Bytes: 140,041,064
- SHA-256: `9dff6b9a1ed01746d37b142fcfd32c0dc56e94b2221b73d22bb1bc432e639bff`
- This APK is **arm64** and **modded**. It is NOT the SHA-matched companion of the
  frozen x86_64 engine and must **never** be used as DEX evidence for BuildID 204ad049.

| DEX entry | Bytes | SHA-256 | vs authoritative |
| --- | --- | --- | --- |
| `classes.dex` | 9,074,880 | `5eb0c7c7a37ce8661dc9cb97182a0838dbf33e8b9d84c4b83aa705d1a7aa9e0e` | DIFFERS |
| `classes2.dex` | 6,171,720 | `6c2875cd91c6849710d28250673a18fd4408a3f0975faadc01181d5336dac928` | DIFFERS |
| `classes3.dex` | 7,036,916 | `5579bd2647f0774515e9601575ee6ac2e0895d42a6b5ae0084a5a7861e90aedc` | DIFFERS |

The modded `classes2.dex` `6c2875cd…` is the value `CA01` flags as the decoy. All three
DEX digests differ from the authoritative set — bytes were changed — so any DEX fact
sourced from this APK is inadmissible.

### 2.2 Flatpak 2.745 engine copy (wrong BuildID / wrong RVAs) — provenance trap

A 116,461,944-byte `libroblox.so` (distinct from the frozen 120,041,712-byte engine)
appears in several REA work directories. It is SHA `8afce3e695d08fe830d7857752c459de9a06c97390c7365a6660827e5d072e78`
with embedded Build ID `791c3f9ef0857460f52862b6c4b3a6c96009be11` — exactly the
Flatpak 2.745 build that `EngineFacts.md` warns **never to mix** (it carries 2.745 RVAs).
Confirmed locations of this wrong build:
- `/home/user/sober-linux/analysis/rea/work/lib/x86_64/libroblox.so`
- `/tmp/opencode/verifier-head/analysis/rea/work/lib/x86_64/libroblox.so`
- `/home/user/sober-rea-fresh/work/split/lib/x86_64/libroblox.so`
- `/tmp/opencode/scratch-sober/analysis/rea/work/lib/x86_64/libroblox.so`
- `/tmp/opencode/w1-scratch/analysis/rea/work/lib/x86_64/libroblox.so`
- `/home/user/.config/freebuff-desktop/local-projects/aac09484-af9e-401f-855e-a2fbdb270785/rea-out/roblox-2.745/libroblox.so`

**Action for all agents:** any REA/disassembly session must be opened against a
SHA `3e498d64…` / BuildID `204ad049…` copy. If a task's REA work dir contains the
`8afce3e6…` / `791c3f9e…` file, discard that target and re-open the authoritative one
before citing any RVA. Mixing these two builds is the single largest provenance risk
in this repository.

Other non-authoritative engines present but not used (recorded to prevent accidental
use): Cordial cache builds `2.737.0.1584` (118,134,416 B), `2.741.0.1061`
(120,444,968 B), mocktail payloads `3222-…` (120,604,544 B) and `2998-…`
(118,166,864 B), and the waydroid arm64 install at
`~/.local/share/waydroid/data/app/…/lib/arm64/libroblox.so` (109,193,800 B).

---

## 3. Tool versions (as installed / measured)

| Tool | Version / identity | Use policy |
| --- | --- | --- |
| Craft Anything CLI | `craft-anything 0.6.1` (AppImage SHA `7eadde1d…`), run with `--deep` | RESEARCH ORACLE only — never the final runtime |
| Android SDK `adb` (system, `/usr/bin/adb`) | 1.0.41 / `36.0.1-android-tools` | Research oracle only |
| Android SDK `adb` (`/home/user/Android/Sdk/platform-tools/adb`) | 1.0.41 / `37.0.1-15733141` | Research oracle only |
| Android emulator | `37.2.12.0` (build_id `16428233`), `/home/user/Android/Sdk/emulator` | Research oracle only |
| SDK platforms | `android-16`, `android-37.0`; system-image `android-37.0` | Research oracle only |
| `sdkmanager` | not on PATH under `/home/user/Android/Sdk/cmdline-tools` | n/a |
| Java (host) | Oracle GraalVM 21.0.12+7.1 (build `21.0.12+7-LTS-jvmci-23.1-b96`), runtime `21.0.12` | Research oracle only |
| binutils `objdump` / `readelf` | Gentoo 2.47.0 | Static analysis (primary native map) |

`ANDROID_HOME` / `ANDROID_SDK_ROOT` were unset in the shell; SDK lives at
`/home/user/Android/Sdk`. Emulator/KVM and the Android SDK are **research-only
oracles** and must never be treated as the final runtime or as provenance for the
native harness's success.

---

## 4. Allowed vs forbidden evidence sources

**ALLOWED (citable for BuildID 204ad049 provenance):**
1. The frozen engine at SHA `3e498d64…` / BuildID `204ad049…` and its byte-identical copies (§1).
2. Authoritative `base.apk` `114a6f8e…`, split `f42ace3b…`, and the three DEX entries in §1.1.
3. `roblox_payload.json` manifest (schema v1) at the payload root.
4. Static analysis of the above with `objdump`/`readelf`, and Craft Anything `--deep`
   DEX tools run against the authoritative APK (per `CA01`).
5. Android SDK/emulator/KVM and the JVM as **research oracles** to understand real
   Java boot / surface-lifecycle behaviour — observations may motivate analysis but
   are not the native harness runtime and do not by themselves satisfy the gate.

**FORBIDDEN (not citable):**
1. `~/Downloads/basalt-arm64-2.740.931.apk` (modded/arm64, §2.1) and its DEX entries.
2. Any Flatpak 2.745 engine copy SHA `8afce3e6…` / BuildID `791c3f9e…` (§2.2), or any
   artifact keyed to 2.745 RVAs.
3. Outputs from any REA/session whose opened target is not SHA `3e498d64…`.
4. Fabrication of any kind: guest byte patches, fake vtables, host callbacks, synthetic
   observations, counter inflation, or relabeling host-triggered seeds as clean boot.
5. Treating a research-oracle observation as proof of the marker or of a clean guest frame.

---

## 5. Compliance note

This ledger changes no behavioural state. Marker `app shell rendered` remains
**WITHHELD** under decisions 013/038; `tests/app-shell.sh` stays RED. No gate was
weakened, no seed was relabeled clean, and no other agent's file was touched.
`seeded_state` semantics and the strict 013/038 gate are unaffected by this note.
