# W6-05 — Craft Anything 0.6.1 tooling and evidence export

Task #542 (Wave 6, worker-5). **Analysis only.** This note is the only file written by this
worker; no source, test, docs, vault, or gate file was changed. No byte patches, fake
callbacks, host callbacks, seeded state, or gate weakening. Marker stays **WITHHELD**
(decisions 013/038).

## 0. Team/ Convio status (honest)

`tools.convio.team_join({code:'TEAM-C2YA-ZTU9-RMAV',role:'worker-5'})` was rejected three
times with **"The team is full (12 agents)."** The team roster read via `team_status` is
lead + workers 1, 3, 4, 6, 9, 10, 11, 12, 14, 15, 16; worker-8 hit the same wall and
already reported it in the team board. With no `member_id` I could not call `team_check`,
`team_task claim`, or `team_task done`. I therefore executed the task-#542 brief as given
(analysis only, single owned output file) and report completion here. **The lead must
record the task as done in Convio** and may reuse the evidence below.

## 1. Tool identity (measured)

| Item | Value |
| --- | --- |
| Binary | `/tmp/opencode/craft-anything-0.6.1-linux-x86_64.AppImage` |
| AppImage SHA-256 | `7eadde1d74ab42cc838862fe3f831e3cf3c9fa6ba5e2bf04e2824bbc47146134` (matches CA01 / release SHA256SUMS) |
| CLI version | `craft-anything-cli 0.6.1` (`--version`) |
| Capabilities | version `0.6.1`, `323` tools, host `linux-x86_64` |
| MCP/CLI engine | `craft-anything-cli 0.6.1` inside AppImage; native tools reported `engine: builtin`, `engine_version: 0.6.1` |
| Config | `/home/user/.config/craft-anything/config.toml` (`deep_mode = true` after these runs) |
| Discovery | `<AppImage> --cli capabilities`, `--cli commands`, `<AppImage> --cli <tool> --help` |

`capabilities` first reported `analysis_mode: clean-room` and, after the `--deep` runs,
`deep`. Pin the mode explicitly per run (`--deep` or `--clean-room`); do not assume it.

## 2. Invocation contract (the reproducible part)

1. The GUI AppImage is not the CLI. `AppRun` requires `--cli` (or a symlink named
   `craft-anything-cli`). Bare `--help` / `--deep` on the AppImage produces **no output**.
   All commands below start with `<AppImage> --cli`.
2. Use `--deep` so target code/files may be read (CA01's mode). Use a persistent
   `--project /tmp/opencode/ca-w5.cany` so evidence, findings, brief and journal survive.
3. **The open target does not survive between CLI invocations** — each call is a fresh
   process. `core-session-show` in a new process reports `open: false`, `targets: []`.
   Pass `--path <absolute file>` on every DEX tool call, and pass the target path as the
   positional `<target-path>` on native tools. Evidence *does* persist in the project.
4. `--format` after a tool name belongs to that tool; use `--output-format` (or
   `--format` before the tool name) to pick JSON/MD/text. Default output is JSON.
5. Arrays (e.g. `--evidence-ids`, `--targets`) are passed as repeated flags.

Shell preamble used for every command below:

```sh
CA=/tmp/opencode/craft-anything-0.6.1-linux-x86_64.AppImage
APK=/home/user/.local/share/basalt/linux/payloads/3170-204ad049599c96afb75b8a21d5051063e821d6c9/sober_apk/base.apk
ENG=/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so
CLI="$CA --cli --deep --project /tmp/opencode/ca-w5.cany"
```

## 3. Provenance of every artifact touched (measured SHA-256)

| Artifact | SHA-256 | Role |
| --- | --- | --- |
| `ENG` `libroblox.so` | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335` | Frozen engine, BuildID `204ad049…` |
| `APK` `base.apk` | `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4` | **Authoritative** DEX, SHA-matched to payload |
| `APK!classes2.dex` | `5afac2bbd9f0e4f2e523f9f6f720bc74f96407aa551b0be44716334f44696f52` | DEX used for the Crashpad queries |
| `~/Downloads/basalt-arm64-2.740.931.apk` | `9dff6b9a1ed01746d37b142fcfd32c0dc56e94b2221b73d22bb1bc432e639bff` | **MODDED — never used as evidence** |
| modded `classes2.dex` | `6c2875cd91c6849710d28250673a18fd4408a3f0975faadc01181d5336dac928` | Differs from authoritative; excluded |

Each evidence record below binds its subject to one of these digests. Target strings/data
are untrusted; nothing in them was followed as instruction.

## 4. Reproducible commands and the evidence IDs they produced

All evidence IDs are real IDs from the project ledger (`core-evidence-show`). Confidence
and limitation fields are quoted from the tool output, not embellished.

| # | Command (`… = $CLI` above) | Evidence ID | Result / confidence |
| --- | --- | --- | --- |
| 1 | `… android-class-users-find --class-name com.roblox.client.analytics.CrashpadHandler --path "$APK"` | `ev_3e1ccb037957d4e120eacd764f67db6b16b247e36ba783c8a042968f3375d2a1` | 3 direct bytecode refs in 3 DEX files; only boot-side ref is `Lrg/w0;->h0()V` `const-class` at `00af`. `observed`, coverage 3/3. Limitation: `static_references_only` (no reflection/JNI/string-built/runtime-loaded refs). |
| 2 | `… android-method-show --class-name 'Lrg/w0;' --method-name h0 --path "$APK" --limit 300` | `ev_e479ee98521aea244085c9e9cd8a55ca15055b1ff268836ce406a29cbce86bc8` | 108 instructions, `classes2.dex`; at `00af` builds `NativeInitCrashpadParams` with `getCanonicalName()`, at `00d5` calls **only** `NativeSettingsInterface->nativeInitCrashpad(...)Z`. No `nativeRunCrashpadHandler`, no latch write. `observed`, coverage 108/108. Independently corroborates worker-4/A05/C-A01. |
| 3 | `… native-instruction-show "$ENG" --address 0x253cb56` | `ev_23cedb9e2219e48125809bd54d2a28d558f5565da7086cb52c7f36956742d93c` | `push rbp`, bytes `55`, `observed`. Confirms the RVA is valid guest code. |
| 4 | `… native-function-find "$ENG" --address 0x253cb56` | `ev_b77bfaf8dbb8dd944772da99b6b6b387e7ac4640cff5c8e484212acc844ab0b2` | `not inside any discovered function`; `derived`, limitation `not_found` — *"the address may be in code the engine didn't discover (absence is not proof)"*. |
| 5 | `… native-functions-list "$ENG" --limit 5` | `ev_eb9b3b17e79f364f4b94b1132b5ddc2df5b39bcad6589de95682847b957dc6ef` | Discovery capped at **50,000** functions, **1,595** unresolved indirect jumps, generated `sub_…` names, first entries at `0x0/0x10/0x18/0x30`. `derived`, `partial`. Strongly unreliable on this stripped 120 MB object. |
| 6 | `… native-function-study "$ENG" --function 0x10 --detail summary` | `ev_54bf29c48c6ce7a88a8daee1afde7d6ce9d7b904a2287508bfda3d4ea3daf1fc` | Dossier for `sub_10`: 12 instructions, 0 callers, 1,502 incoming refs; `inferred`. Pseudo-C explicitly *"built by the clean-room lifter"* — i.e. **`--deep` did not switch native tools to Ghidra**. Also note `--function 0x253cb56` returns `function_not_found`. |

Full per-tool JSON was captured under `/tmp/opencode/w5-*.out` (session-local scratch, not
part of the repo). Findings recorded in the project: `fnd_1` (DEX boot-side crashpad
conclusion, backed by IDs 1–2) and `fnd_2` (native-engine limit, backed by IDs 3–4).

## 5. Evidence export (what the team can reuse)

```sh
… core-evidence-show --detail summary                                  # list ledger
… core-evidence-export --path /tmp/opencode/w5-evidence-final.json --overwrite
… core-evidence-import --path /tmp/opencode/w5-evidence-final.json     # round-trip; tamper-rejected
… report --format md  --out /tmp/opencode/w5-report-final.md
… report --html       --out /tmp/opencode/w5-report-final.html
```

- `core-evidence-export` wrote a `craft-anything-evidence-bundle` JSON with
  `{records, unknowns}`; final run: **6 records, 0 unknowns, 265,759 bytes**.
- `core-evidence-import` of an earlier 4-record bundle: `imported: 4, rejected: []` —
  the bundle format is portable and integrity-checked; teammates can import and re-report.
- `report` produced Markdown and self-contained HTML with a brief, findings and an
  evidence-ledger appendix (full output in `/tmp/opencode/w5-report-final.{md,html}`).
  SHA-256 of final exports:
  - `w5-evidence-final.json` — `39a1e8228c36c56475b6347df8023e0e4a492b8ae68a33bdc38e67fc78d6527f`
  - `w5-report-final.md` — `5487c5544b7a3b523c34a9ae9a3314751549c3a765095f34edaa16ec9e46c077`
  - `w5-report-final.html` — `34bc130bf996b97a1189dc16018f2b9567c727abcaf339d0b125844cc4fb4f14`
- `core-brief-write`, `core-finding-record`, `core-evidence-browse` (paging) and
  `core-report-save` complete the chain question → observation → finding → export.

## 6. What can be automated for the team

- **DEX ground truth on the authoritative APK**: `android-class-users-find`,
  `android-method-show`, `android-class-show` — deterministic, evidence-tagged, SHA-bound
  to `114a6f8e…`; usable for worker-1/6/10/14/15/16 style call-order and JNI-enumeration
  questions.
- **Evidence ledger**: every call returns a stable `ev_…` ID with confidence, coverage,
  limitations and subject SHA; `core-evidence-show/export/import` and `report` give
  cross-agent reproducible, integrity-checked hand-off artifacts.
- **Bounded native byte/instruction checks**: `native-instruction-show` reliably confirms
  "is this RVA code / what is the first instruction" on the frozen engine.
- **Tool/engine/provenance audit**: `capabilities`, `settings-show`, `sha256sum` give a
  repeatable environment ledger.

## 7. What cannot be automated (honest limits)

- **No native call graph on the 120 MB stripped engine.** The built-in engine stops at
  50,000 discovered functions with 1,595 unresolved indirect jumps, invents `sub_` names
  and even "functions" at `0x0`; it cannot place `0x253cb56` in a function, and
  `native-function-study` rejects it. Negative results are explicitly *not proof of
  absence*. `--deep` did **not** route these tools to the installed Ghidra — output still
  says `engine: builtin` and the lifter is the clean-room one. Do not replace our
  objdump/GHIDRA/GDB map with Craft Anything native results.
- **DEX tools see static references only**: reflection, JNI, string-built class names and
  runtime-loaded code are invisible (tool's own `static_references_only` limitation).
  Their boot-side crashpad finding corroborates but does not independently prove runtime
  behavior.
- **No runtime authority.** Craft Anything's adb/frida/emulator/Ghidra facilities are
  research oracles only. The Android SDK/emulator/KVM must never be the final runtime;
  nothing here changes the native harness.
- **No gate authority.** Evidence IDs and reports are observations, not proof of a clean
  notifier-driven callback, a counted guest GLES frame, or `seeded_state == 0`. They
  cannot satisfy any positive assertion in `tests/app-shell.sh`. Marker stays WITHHELD;
  the test stays RED. No gate weakening was performed or recommended.
- The project and exports live in `/tmp/opencode` (session scratch), not in the repo;
  the lead decides whether to attach any of it as an artifact.

## 8. Hand-off to lead

Task #542 deliverable is this note. Completion could not be registered in Convio because
the 12-agent team was full and no `member_id` was issued; please mark #542 done and, if a
slot opens, re-invite worker-5 for any follow-up. Recommended uses: cite evidence IDs
1–2 for the crashpad/boot conclusion (they agree with CA01 and worker-4/A05); cite IDs
3–6 as the documented reason native call-graph automation is out of scope; import
`w5-evidence-final.json` to reproduce the ledger. No source changes requested.
