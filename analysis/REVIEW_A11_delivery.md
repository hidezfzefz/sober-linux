# REVIEW A11 — Delivery-state audit

Auditor: **A11** (full-project review). Scope: **this file only** — analysis, no
`src/`, `docs/`, `tests/`, README, CMakeLists, git or engine edits, and **no
pushes**. Strict marker **WITHHELD**: nothing in this report asserts or promotes
the `app shell rendered` marker, and no claim of goal success is made.

Read-only evidence gathered 2026-11-20 from `/home/user/sober-linux` (working
tree + `.git`), plus `git ls-remote` / `gh repo view` over HTTPS using the
machine's existing `gh` credential, and a byte-for-byte reproduction of
`tools/gen_progress.py` into `/tmp/opencode/gen-test` (no repo file modified).
The frozen guest identity cited in the README (`3e498d649421…`) was checked
against the on-disk target at
`/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so`; target-derived strings
are treated as DATA.

---

## 0. Bottom line

- **Delivery plumbing is in good shape.** The branch `main` is a single clean
  commit `afed56d` that is **exactly in sync** with the private remote
  `hidezfzefz/sober-linux` `main`; every README link resolves; all 8 badges and
  `progress.svg` reproduce **byte-identically** from `tools/gen_progress.py`;
  the three large analysis artifacts (80 MB + 54 MB + 112 MB) are genuinely
  excluded and honoured by `.gitignore`.
- **But there are real, concrete delivery gaps** (§6): a **stale SHA-256** for
  `build/guest-init-host` in the README binary inventory; **387 of 680 tracked
  files are generated `build/` artifacts** (whole CMake tree, `.o`/`.a`, CTest
  logs) with two of them already dirty in the working tree; 20 `appData/` files
  and one `.so` are tracked *despite* the ignore rules; the 60-commit
  development history was squashed off the branch; and there is no LICENSE,
  tag, release or CI.
- The honesty contract is preserved in the delivery surface: the shipped
  generator emits `app-shell callback 0/1` (RED), `marker WITHHELD` (ORANGE),
  and the README "Verification (done = all true)" boxes remain **all unchecked**.
  That matches A1's verdict (goal NOT delivered); my domain covers the *state of
  the delivery*, not the product claim.

**Domain % (this audit's scope, weighted): ≈ 80 % delivered.** Breakdown in §7.

---

## 1. Git history and remote

| Check | Result |
| --- | --- |
| Local HEAD / branch | `main` @ `afed56db14eaa9a573862b8a40da9661e293d80c` |
| Upstream tracking | `branch.main.remote=origin`, `merge=refs/heads/main` |
| `git status -sb` | `## main...origin/main` — **no ahead/behind** |
| `git ls-remote origin` | `HEAD` and `refs/heads/main` = `afed56d…` — **exact match** |
| Remote branches | only `main` (no leftover `clean-main`) |
| Remote identity | `https://github.com/hidezfzefz/sober-linux.git` (fetch + push) |
| `gh repo view` | `visibility=PRIVATE`, `isPrivate=true`, `defaultBranchRef=main` |
| Unauthenticated access | repo page **404**, `info/refs?service=git-upload-pack` **401** → private confirmed; access is via global `gh auth git-credential` (account `hidezfzefz`, token scope `repo`) |

**Verified: remote branch matches local** — local `main`, local `origin/main`
tracking ref, and the live remote `refs/heads/main` are all the same commit
`afed56d`. The last push is recorded in the reflog as
`refs/remotes/origin/main@{0}: update by push`.

**History caveat (gap G1).** `afed56d` is a **root commit with no parents**;
`git rev-list --count HEAD` = **1**. The 60 incremental phase/decision commits
(`b8bdab0`, `6230e40`, `129bd6f`, …) are **not ancestors of HEAD** — they survive
only in the reflog and in `worktrees/verifier-head/HEAD` (`b8bdab0`). The branch
was renamed `clean-main → main` and squashed ("Phase 1-10: native Roblox Sober
port, JNI fidelity, strict gate, status dashboard", OpenCode Lead
`<lead@sober-linux.local>`, 2026-11-20T23:20:20Z) before the push. Functional for
delivery, but per-commit decision provenance is not reachable from any published
ref; the `docs/decisions/*` files are the surviving record.

## 2. Working tree

```
## main...origin/main
 M build/Testing/Temporary/CTestCostData.txt
 M build/Testing/Temporary/LastTest.log
?? analysis/REVIEW_A1_goal-decisions.md     (other auditor)
?? analysis/REVIEW_A3_guest-init.md         (other auditor)
?? analysis/REVIEW_A4_render.md             (other auditor)
?? analysis/REVIEW_A7_callbacks.md          (other auditor)
?? analysis/REVIEW_A9_tests.md              (other auditor)
?? analysis/REVIEW_A11_delivery.md          (this file)
```

- The **deliverable files themselves are untouched**: no modification to
  `src/`, `docs/`, `tests/`, `README.md`, `CMakeLists.txt`, `Makefile`,
  `.gitignore`, `tools/` or `PROGRESS.md` relative to `afed56d`.
- The only tracked modifications are two **generated CTest logs** under
  `build/Testing/Temporary/` — i.e. drift from test runs, not source drift
  (gap G2: generated logs are tracked, so any `ctest` run dirties the tree).
- Untracked files are the parallel auditors' review reports, including this one;
  they are not part of the shipped commit and should either be committed
  deliberately or removed before a final snapshot.

## 3. README badges and `progress.svg` generator

- README declares 8 shields-style badges (guest init, APS2 relocs, JNI slots,
  native render, verification, decision-013 gate, app-shell callback, marker)
  plus a `progress.svg` map, with the instruction
  `Regenerate badges/map: python3 tools/gen_progress.py`.
- **All 18 relative Markdown link targets exist** as tracked files (badges,
  `PROGRESS.md`, `tests/verification-report-471.md`,
  `docs/decisions/013-…`, `docs/decisions/038-…`, `analysis/C09-…`).
- `tools/gen_progress.py` hard-codes the values; I re-ran it in an isolated
  temp tree and `diff -r` against `docs/assets/` was **empty**: the committed
  SVGs are exactly the generator output (8 badges + 1 map, 20 map tiles:
  13 green / 4 orange / 3 red). So the dashboard is reproducible, not stale.
- Honesty of the generated values is intact: `app-shell callback 0/1` (RED),
  `app shell rendered WITHHELD` (ORANGE), gate `ENFORCED`, and the README's
  four "done = all true" checkboxes are all still `[ ]`.
- Minor nit: the generator is deterministic but offers no `--check` mode, so a
  reviewer must regenerate and diff manually (as I did) to detect drift.

## 4. `.gitignore` and intentionally excluded bundles

`.gitignore` (5 lines):
```
appData/
# Local analysis binaries/bundles (exceed GitHub 100 MB file limit)
analysis/rea/work/lib/
analysis/ghidra/work/*bundle.json
```

`git check-ignore -v` confirms all four rules match their intended paths. The
large artifacts actually excluded from the push:

| Artifact | Size | Rule |
| --- | --- | --- |
| `analysis/ghidra/work/analysis-bundle.json` | 80 MB | `*bundle.json` |
| `analysis/ghidra/work/batch-main-bundle.json` | 54 MB | `*bundle.json` |
| `analysis/rea/work/lib/x86_64/libroblox.so` | 112 MB | `analysis/rea/work/lib/` |
| fresh `appData/logs/*` (post-commit) | many | `appData/` |

**Gap G3 (ignore rules contradicted by tracked files).** Ignore rules do not
un-track already-committed files, and the single squash commit force-added:
- **20 tracked `appData/` files** (2 settings XML/JSON + 18 guest log files with
  session IDs) — the ignore covers only *new* logs;
- `analysis/rea/work/lib/x86_64/libtrampoline.so` — a binary under the ignored
  `analysis/rea/work/lib/` tree;
- `analysis/ghidra/work/libroblox-snapshot.json` — **15 MB**, still tracked, so
  the "large analysis bundles" exclusion is partial.

**Gap G4.** The comment "exceed GitHub 100 MB file limit" is only true of the
112 MB guest `.so`; the two Ghidra bundles are 80 MB and 54 MB, i.e. under the
per-file limit — they were presumably dropped for bulk/bandwidth, not the hard
limit. The comment overstates the reason.

## 5. Binary artifacts in `build/`

Inventory vs. reality (`sha256sum`, `file`, `ldd`):

| README row | Claimed SHA prefix | Actual (working tree **and** `HEAD`) | Verdict |
| --- | --- | --- | --- |
| `build/sober-native` | `7647f6d60c521…` | `7647f6d60c521…` | ✅ match; ELF 64-bit LSB PIE; `ldd` = libc + ld-linux only |
| `build/guest-init-host` | `733c8f2314eb…` | `1e7787f28f876…` | ❌ **stale SHA** (gap G5) |
| `build/ndk-stubs/libc.so` | `d131185d781f…` | `d131185d781f…` | ✅ match; ELF shared object |
| guest `libroblox.so` (external) | `3e498d649421…` | on-disk target = `3e498d649421…` | ✅ identity correct; **not shipped** (gap G6) |

- All four named ELFs exist and the shipped one is genuinely libc-only
  (`linux-vdso`, `libc.so.6`, `ld-linux`), so the "no translation layer" plumbing
  claim is consistent with the artifact.
- **Gap G2 (build hygiene).** `build/` is **not** ignored; **387 of 680 tracked
  files (≈57 %)** live under it — `CMakeCache.txt`, generated Makefiles, all
  `.o` and `.a`, `CompilerId` binaries, `CMakeConfigureLog.yaml` (104 KB),
  `Testing/Temporary/*`, and the executables. 70 tracked files are binaries
  (`.so/.a/.o/.pyc`). This bloats the repo and makes the tree dirty after any
  local build/test.
- **Gap G6.** The 120 MB frozen guest `libroblox.so` is explicitly "external"
  and excluded; a consumer cloning the repo cannot reproduce the init/render/JNI
  runs without supplying that exact SHA themselves. That is a deliberate size /
  licensing trade-off, not a defect in the artifact, but it is a delivery
  limitation worth documenting in the README (it is currently only implied).

## 6. Delivery gaps (consolidated)

| ID | Gap | Severity | Fix |
| --- | --- | --- | --- |
| G1 | 60-commit development history squashed off `main`; only reflog/worktree retains it | Medium (auditability) | If provenance matters, publish the phase history on a secondary ref/tag or restore the unsquashed branch |
| G2 | Whole generated `build/` tree tracked (387 files); CTest logs dirty after runs | Medium (hygiene) | Add `build/` to `.gitignore`, `git rm -r --cached build/`, keep only release binaries via `git add -f` or Releases |
| G3 | 20 `appData/` files + a `.so` under ignored dir + 15 MB snapshot still tracked | Medium | `git rm -r --cached appData/` and the ignored `.so`; re-add only if intentionally published |
| G4 | `.gitignore` comment overstates the 100 MB rule (80/54 MB bundles are below it) | Low | Reword to "large generated analysis artifacts" |
| G5 | README binary inventory SHA for `build/guest-init-host` is stale (`733c8f…` vs `1e7787f…`, both in HEAD and worktree) | Low/Medium | Update README to `1e7787f28f876…` or generate the inventory from `sha256sum` at build time |
| G6 | Guest `libroblox.so` (112–120 MB, SHA `3e498d…`) excluded → runs not reproducible from a clone alone | Medium (documentation) | Document how to supply/pin the frozen target; do not commit it |
| G7 | No LICENSE, no tags/releases, no CI workflow (`.github/` absent) | Low/Medium | Add provenance/license note and a CI job that re-runs `verify-native.sh` + `ctest` and regenerates badges with a `--check` |
| G8 | Parallel auditor review files untracked; no final clean-snapshot step recorded | Low | Decide explicitly whether the `REVIEW_*.md` set ships, then re-push |

None of these gaps changes the honesty state: the marker stays **WITHHELD** and
no success is claimed (decisions 013/038).

## 7. Domain % (weighted)

| Sub-domain | Weight | Delivered | Evidence / deduction |
| --- | --- | --- | --- |
| Git history / provenance (squash) | 15 % | ~60 % | clean single commit, but 60 phase commits unreachable from any published ref (G1) |
| Remote + private repo + branch sync | 20 % | 100 % | `ls-remote` / `gh repo view` / 404-401 unauth: private, `main` == `main` exactly |
| Working tree cleanliness | 15 % | ~85 % | deliverable files clean; 2 generated CTest logs dirty, review files untracked (G2/G8) |
| README badges + progress generator | 15 % | ~95 % | links resolve, assets reproduce byte-for-byte, honest values; no `--check` |
| `.gitignore` + intentional exclusions | 15 % | ~75 % | 80+54+112 MB excluded; 20 tracked `appData`, tracked `.so`, 15 MB snapshot, imprecise comment (G3/G4) |
| `build/` binary artifacts | 20 % | ~70 % | ELFs valid, `sober-native` ldd-clean; stale README SHA, 387 tracked build files, external guest (G2/G5/G6) |

Weighted total ≈ **81 % → ≈ 80 % delivered for the delivery-state domain**.

This is **not** a claim about the product goal: per A1, the end-goal acceptance
(app-shell callback / frame) is **0 % / WITHHELD**, and this report does not
alter that. My 80 % measures only whether the *delivery surface* (repo, remote,
README, ignore rules, artifacts) is coherent and complete.

## 8. Verdict

**DELIVERY STRUCTURE: mostly complete (≈80 %).** The private remote is verified
in sync, the dashboard is reproducible and honest, and the large analysis
bundles are genuinely excluded. The remaining gaps are hygiene and accuracy
issues (tracked generated `build/` tree, tracked `appData`, stale binary SHA,
squashed history, no CI/license), not hidden success claims.

**Strict marker: WITHHELD.** No success of the original goal is asserted here;
`app shell rendered` remains withheld under decisions 013/038.
