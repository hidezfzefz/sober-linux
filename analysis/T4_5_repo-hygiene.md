# T4-5 — Repository hygiene & delivery cleanup plan

**Agent:** T4-5 (Team 4, callback + assurance). **Date:** 2026-11-20.
**Scope:** this file only — analysis + a concrete remediation plan. **No source,
`CMakeLists.txt`, `docs/`, `tests/`, README, git or engine file was modified, and
nothing was pushed.** Engine is frozen (guest `libroblox.so` BuildID
`204ad049…`, SHA-256 `3e498d6494…`); this plan never proposes touching it.

**Strict-marker statement.** The `app shell rendered` marker remains
**WITHHELD** under decisions 013/038. Every item below is *delivery hygiene* —
it does not emit a marker, does not weaken the gate, and does not claim goal
success. The headline (`goal NOT PROVEN`, callback `0/1`, marker `WITHHELD`,
unchecked DoD boxes) must stay the first thing a reader sees.

Inputs synthesised: A2 (native build / hard constraints, ≈90 %; "gaps are
automation/hygiene", as recorded in `docs/REVIEW-100.md:55`), A11
(`REVIEW_A11_delivery.md`, gaps G1–G8), A12 (`REVIEW_A12_file-inventory.md`,
§6), A14 (`REVIEW_A14_honesty-risk.md`, §6), plus my own read-only re-check of
the working tree and `.git` on 2026-11-20.

---

## 0. Bottom line

The *build system and shipped ELF are sound*; the *repository packaging is not*.
387 generated `build/` files are tracked despite `Makefile:9` saying the
opposite; there is an orphan `.o` from an abandoned Win32 design, two parallel
CMake object trees with divergent/stale duplicate libraries, force-added
gitignored paths, no LICENSE/CI/tag/release, squashed history reachable only via
reflog, and a diagnostic harness that carries `DT_TEXTREL`. All of it is safely
fixable in an ordered sequence of **small, individually-committable, reversible
steps** that keep the private remote and the frozen artifacts intact.

Recommended shape of the cleanup: **(1) ignore + document first, (2) untrack
with `git rm --cached` (never delete working files), (3) pin release binaries
and their SHAs via a generated manifest, (4) preserve history on a secondary
ref, (5) add a CI gate that regenerates and diffs the figures, (6) rebuild and
refresh figures after any binary change.** Do not rewrite `main`.

---

## 1. Snapshot drift — read this before running any command

A11/A12 were written against commit `afed56d` (1 root commit, 680 tracked
files). The tree has since moved. My re-check:

| Fact | A11/A12 snapshot | Now (2026-11-20) |
| --- | --- | --- |
| HEAD | `afed56d`, single root commit | `f8d5031` "Full 14-auditor project review…", `git rev-list --count HEAD` = **2** |
| Tracked files | 680 | **694** (new untracked `src/jni/*_default.c/.h`, `scheduler_guest.*` from other workers — **do not touch**, they are other agents' work) |
| Tracked `build/` files | 387 | **387** (unchanged — H1 still fully open) |
| Dirty tracked files | 2 CTest logs | **none** (CTest logs currently clean; still tracked → will re-dirty) |
| Remote | `main` == `afed56d`, private | `git ls-remote origin refs/heads/main` = `f8d5031…` — **still in sync, still private** |
| README `guest-init-host` SHA | stale `733c8f…` | **fixed**: README `b82bda6b9f8e…` == on-disk `b82bda6b9f8eed35…` (R2 resolved for now) |
| README "22 opt-in modules" | stale by ~4× | **fixed**: now "live OR-chain … 87 env checks" (`README.md:59-62`) |
| Verification badge link | stale report 471 | **fixed**: links `tests/D12-final-invariants.md` (`README.md:13`) |
| Gate wording | "notifier provenance required" | **fixed**: "required by policy, not positively coded yet" (`README.md:53`) |

**Consequence:** several honesty edits from A14 §6 are already applied — do not
re-apply them blindly. But the *structural* findings (H1–H9 below) are all still
open, and the README SHA is correct **only because the binary has not been
rebuilt since the edit** — it will go stale again at the next rebuild. That is
the core reproducibility defect to design out, not just patch once.

---

## 2. Findings and evidence (verified this session)

| ID | Finding | Source | My verification / status |
| --- | --- | --- | --- |
| **H1** | Whole CMake out-of-source tree tracked; `Makefile:9` says "Nothing in build/ is ever committed" | A11 G2, A12 §6.1 | **OPEN.** `git ls-files build/ \| wc -l` = **387**. Includes `CMakeCache.txt`, Makefiles, `CompilerId` `a.out`, `.o`, `.a`, `.so`, CTest logs. `make clean` deletes tracked content. |
| **H2** | Orphan object with no source: `build/CMakeFiles/sober_compat.dir/src/compat/win32_compat.c.o` (+ its `.d`) | A12 §5.4/§6.2 | **OPEN.** No `win32_compat.c` tracked; `docs/decisions/004-…` rules out any Win32 surface. 97,392 B (`02a5771ece9a…`). |
| **H3** | Two parallel CMake object trees; stale duplicate libs/DSOs | A12 §6.3–6.4 | **OPEN.** `build/CMakeFiles/` (root configure) and `build/src/{jni,render,trace}/CMakeFiles/` (active `add_subdirectory` configure). Root `sober_jni.dir` 13 files vs active 55; root `libsober_jni.a` 44,502 B vs active 333,876 B; `libsober_render.a` 35,130 vs 36,400 B; `native_trace.so` 30,656 vs correctly named `libnative_trace.so` 50,680 B. |
| **H4** | Stale/empty artifacts in `analysis/` | A12 §6.6/§6.8 | **OPEN.** `analysis/ghidra/decompiled/network_0x2a326ff.{err,json}` are 0 B; `analysis/bootstrap/__pycache__/decode_aps2.cpython-315.pyc` is a committed Python-3.15 bytecode cache. |
| **H5** | `.gitignore` contradicted by tracked files | A11 G3/G4, A12 §6.9 | **OPEN.** 20 tracked `appData/` files; `analysis/rea/work/lib/x86_64/libtrampoline.so` force-added under an ignored dir; comment "exceed GitHub 100 MB file limit" is inaccurate (the excluded Ghidra bundles are 80/54 MB, only the guest `.so` is >100 MB). Note: A11's "15 MB `libroblox-snapshot.json` still tracked" should be re-checked before acting on it. |
| **H6** | No LICENSE, no tags/releases, no CI workflow | A11 G7 | **OPEN.** No `LICENSE*`, no `.github/`, `git tag -l` empty. Private repo is confirmed in sync (`f8d5031…`), so any CI added must not leak secrets or make the repo public. |
| **H7** | Squashed history / unreachable provenance; risky commit subject | A11 G1, A14 R13, `REVIEW-100` H1 | **OPEN.** `afed56d` is a root commit; the phase chain (`b8bdab0`, `6230e40`, `129bd6f`, …) survives only in reflog and `.git/worktrees/verifier-head/HEAD` (= `b8bdab0`). I confirmed those objects still exist locally. Subject "native … port" can be over-read; bodies are empty. |
| **H8** | Stale README figures reappear on every binary rebuild | A11 G5, A14 R2 | **PARTIALLY FIXED.** SHA now matches, but it is a *hand-maintained literal*. `tools/gen_progress.py` generates badges/map only, has no `--check`, and does not generate the binary-inventory SHA table or the hand-maintained counts. Same class of defect as A14 R3/R14 (fixed counts/line numbers drift). |
| **H9** | `DT_TEXTREL` and build warnings | A2 "automation/hygiene" gap, D12/D01 "no compiler/linker warnings" | **OPEN / document + gate.** Verified with `readelf -d`: **`build/guest-init-host` has `TEXTREL` + `BIND_NOW` + `PIE`**, NEEDED = libc only. **`build/sober-native` has no TEXTREL** — only `BIND_NOW`, `PIE`, NEEDED libc. So text relocations are confined to the harness-only diagnostic, not the shipped ELF. CMake uses `-Wall -Wextra -fvisibility=hidden` (no `-Werror`); latest build logs I could inspect contain no `warning:`/`CMake Warning` lines, consistent with D12's "no warnings" claim — but nothing mechanically enforces that on future builds. |

**Not defects — do not "fix":** all 33 `src/` headers are referenced; every
tracked `src/**/*.c` is named in a CMakeLists; `sober-native` ldd is libc-only;
the 80/54/112 MB analysis bundles are genuinely ignored; the honesty posture
(headline, `0/1`, `WITHHELD`, unchecked DoD boxes) is intact.

---

## 3. Cleanup principles

1. **Private first.** Keep `origin` = `hidezfzefz/sober-linux` (private). Never
   change visibility. Releases on a private repo stay private; CI runs on the
   existing credential, no new secrets in tracked files.
2. **Untrack, don't delete.** Use `git rm -r --cached …` so working files stay
   on disk for anyone mid-build; a later `git clean` can remove them locally.
3. **One concern per commit.** Each phase below is a separate commit, so any
   step can be reverted with `git revert <sha>` without unwinding the rest.
4. **Pin, don't hand-type.** Generate SHAs/counts with a script and assert them
   in CI, so "fix after each binary rebuild" becomes automatic (H8).
5. **Preserve provenance before any destructive git operation.** A `git gc`,
   `reflog expire`, or forced push can destroy the only copies of the phase
   history (H7). Save it first.
6. **Engine frozen.** No change to `libroblox.so` or any decision/gate logic;
   the guest binary stays the pinned external artifact.

---

## 4. Ordered remediation plan (commands for the lead to run)

All commands are run from `/home/user/sober-linux`. They are **not** executed by
this agent. The lead owns `src/`, `CMakeLists.txt`, `docs/`, `tests/`, README and
git. Do not stage other agents' untracked `src/jni/*_default*` files.

### Phase 0 — Preflight and safety net (no history rewrite, ~2 min)

```bash
git status -sb
git ls-remote origin refs/heads/main          # expect f8d5031… or newer; must match local
git rev-parse HEAD
git rev-list --count HEAD

# Preserve the squashed-phase provenance before anything can trigger gc.
git branch backup/afed56d-pre-hygiene afed56d          # published squash point
git branch backup/phase-history b8bdab0                # tip of surviving phase chain
# Optional but recommended: portable archive of everything, including reflog-only objects
git bundle create /tmp/opencode/sober-hygiene-backup.bundle --all

# Record the current ground truth for the README/binary table.
sha256sum build/sober-native build/guest-init-host build/ndk-stubs/libc.so \
  | tee /tmp/opencode/bin-sha-before.txt
```

Keep `/tmp/opencode/*.bundle` until the cleanup is accepted. The two `backup/*`
branches are local-only until you decide whether to publish them (Phase 5).

### Phase 1 — Documentation and ignore rules first (non-destructive)

Edit `.gitignore` (lead-owned): add `build/` and reword the inaccurate comment
(H1, H5):

```text
# Generated CMake out-of-source tree — never committed (see Makefile:9)
build/
appData/

# Large generated analysis artifacts (bulk/bandwidth; only the guest .so is >100 MB)
analysis/rea/work/lib/
analysis/ghidra/work/*bundle.json
# Local Python bytecode caches
__pycache__/
*.pyc
```

Then, still in Phase 1, add a short provenance/licence note (H6): a top-level
`LICENSE` or `NOTICE.md` stating the repo's own terms and that the guest
`libroblox.so` is **external, frozen, not redistributed** (SHA `3e498d6494…`),
plus the README sentence that a clone cannot reproduce init/render/JNI runs
without the consumer supplying that exact pinned target (A11 G6).

```bash
# Confirm the new rules match before untracking anything.
git check-ignore -v build/CMakeCache.txt appData/logs/foo.log \
  analysis/bootstrap/__pycache__/x.pyc analysis/rea/work/lib/x86_64/libroblox.so
git add .gitignore LICENSE NOTICE.md README.md    # whichever of the latter two you add
git commit -m "hygiene: ignore generated build tree; document provenance and external target"
```

Do **not** commit a LICENSE that grants rights you don't own — if licence terms
are undecided, commit only `NOTICE.md` and defer the LICENSE choice.

### Phase 2 — Untrack generated and force-added artifacts (reversible)

```bash
# H1: everything under build/ (working files stay on disk)
git rm -r --cached build/

# H2: orphan object + dependency (explicit, in case build/ ever re-added)
git rm --cached build/CMakeFiles/sober_compat.dir/src/compat/win32_compat.c.o \
                 build/CMakeFiles/sober_compat.dir/src/compat/win32_compat.c.o.d

# H4: empty placeholders and committed bytecode cache
git rm --cached analysis/ghidra/decompiled/network_0x2a326ff.err \
                 analysis/ghidra/decompiled/network_0x2a326ff.json \
                 analysis/bootstrap/__pycache__/decode_aps2.cpython-315.pyc

# H5: force-added, gitignored paths (keep the guest .so OUT of the repo)
git rm -r --cached appData/
git rm --cached analysis/rea/work/lib/x86_64/libtrampoline.so
# Re-check the A11 "15 MB libroblox-snapshot.json" claim before removing:
git ls-files -s analysis/ghidra/work/libroblox-snapshot.json 2>/dev/null

git commit -m "hygiene: untrack generated build tree, orphan objects, caches and gitignored captures"
```

Result: tracked-file count should drop by ~410+ (H1 + H2 + H4 + H5). `git
status -sb` stays clean; `make`/`ctest` still work locally but no longer dirty
the tree. H3's duplicate trees disappear because *all* of `build/` is untracked
— do not spend time cherry-picking individual stale duplicates.

### Phase 3 — Pin the release binaries and their SHAs (kills H8)

Pick **one** strategy; (a) is recommended for a private repo with no release
infrastructure yet.

**(a) GitHub Releases (preferred).** Keep no binaries in git; publish the four
named artifacts as assets of a private release. Then committed
`docs/assets/binary-inventory.sha256` is the authoritative pin:

```bash
# From a verified build, produce the manifest (network-free, deterministic).
sha256sum build/sober-native build/guest-init-host build/ndk-stubs/libc.so \
  > docs/assets/binary-inventory.sha256
git add docs/assets/binary-inventory.sha256 && git commit -m "docs: pin release binary SHA-256 inventory"
gh release create v0.1.0-audit --title "v0.1.0 (partial; marker WITHHELD)" \
  --notes "Native build only; goal NOT achieved; see README" \
  build/sober-native build/guest-init-host build/ndk-stubs/libc.so
```

`gh release create` on a private repo keeps assets private; confirm with
`gh release view` and an unauthenticated fetch.

**(b) In-tree `release/` directory (if the lead must ship binaries in git).**
Force-add **only** the four named ELFs, in a dedicated directory, and document
that these are build outputs regenerated after each rebuild:

```bash
mkdir -p release
cp -p build/sober-native build/guest-init-host build/ndk-stubs/libc.so release/
sha256sum release/* > release/SHA256SUMS
git add -f release/ && git commit -m "build: ship pinned release binaries with checksums"
```

Either way, extend `tools/gen_progress.py` (lead-owned) to emit the binary
inventory and counts from `sha256sum` and a manifest, and add a `--check` mode
that exits non-zero on drift. README then *includes/generates* the table
instead of a hand-typed SHA, so "fix after each binary rebuild" is a single
`python3 tools/gen_progress.py --check` failure, not a silent stale figure.

### Phase 4 — Publish provenance without rewriting `main` (H7)

```bash
# Optional: make the phase chain reachable from a published ref.
git push origin backup/afed56d-pre-hygiene:refs/heads/history/clean-squash
git push origin backup/phase-history:refs/heads/history/phase-chain
# Or publish them as lightweight tags instead of branches:
git tag development-phase-chain b8bdab0
git tag pre-hygiene-snapshot afed56d
git push origin development-phase-chain pre-hygiene-snapshot
```

Do **not** rebase/amend/force-push `main` to fix the commit subject (A14 R13):
rehashing the published commit would break every clone and the "remote in sync"
property. Keep the honest README headline prominent instead, and hedge the
subject only in *new* commit messages. Tag the current accepted state after the
cleanup: `git tag -a hygiene-v1 -m "..." && git push origin hygiene-v1`.

### Phase 5 — CI gate (H6, H9; also mechanises H8)

Add `.github/workflows/verify.yml` (lead-owned) that, on push/PR to `main`:

```yaml
name: verify
on: [push, pull_request]
jobs:
  native:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure + build
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --parallel
      - name: Assert no text relocations in shipped ELF
        run: |
          readelf -d build/sober-native > /tmp/sober-dyn.txt
          ! grep -q TEXTREL /tmp/sober-dyn.txt
          grep -q BIND_NOW /tmp/sober-dyn.txt
      - name: Assert libc-only ldd
        run: ldd build/sober-native | grep -Ev 'libc\.so|ld-linux|linux-vdso' && exit 1 || true
      - name: ctest
        run: ctest --test-dir build --output-on-failure
      - name: Figures are reproducible
        run: python3 tools/gen_progress.py --check
```

Notes for the lead: the guest `libroblox.so` is external, so CI runs the
*native/ctest* surface only — never claim it re-runs the guest init/render/JNI
runs. `guest-init-host` is known to carry `DT_TEXTREL`; scope the assertion to
`sober-native` (the shipped artifact) and document the harness TEXTREL as an
accepted property of the diagnostic (in-place guest patching), not a defect in
the shipped ELF. If future builds want to forbid text relocations outright,
add `-Wl,-z,text` to the shipped target only — that is a `CMakeLists.txt`
change the lead must evaluate separately; it is **not** part of this hygiene
patch and must not touch the frozen engine.

### Phase 6 — Rebuild → refresh figures → commit (the "after each binary rebuild" rule)

Every time a binary is rebuilt, the SHA changes, so the manifest/README table
must be regenerated in the *same* commit as the binary:

```bash
cmake --build build --parallel
sha256sum build/sober-native build/guest-init-host build/ndk-stubs/libc.so \
  > docs/assets/binary-inventory.sha256     # or release/SHA256SUMS
python3 tools/gen_progress.py                # regenerate badges/map/counts
git add docs/assets/ README.md               # plus release/ if strategy (b)
git commit -m "build: rebuild artifacts and refresh generated figures"
python3 tools/gen_progress.py --check        # must be silent / rc=0
```

### Phase 7 — Final clean-snapshot and push

Decide explicitly whether the parallel `analysis/REVIEW_A*.md` reports (incl.
this one) ship. Commit them deliberately or leave them untracked — do not let
them drift into a later commit by accident.

```bash
git status -sb                              # clean except the deliberate review-report decision
git add analysis/REVIEW_A*.md analysis/T4_5_repo-hygiene.md   # if shipping
git commit -m "docs: include full 14-auditor review set"
git push origin main
git ls-remote origin refs/heads/main         # confirm remote == local HEAD
git status -sb                               # ## main...origin/main, no ahead/behind
```

Never push with `--force`; the private repo must stay a straight, verifiable
history.

---

## 5. Acceptance checklist (lead runs after each phase)

- `git ls-files build/ | wc -l` → `0`; `git ls-files appData/ | wc -l` → `0`.
- `make clean && make` leaves `git status -sb` clean (only `build/` churn, now ignored).
- `sha256sum build/{sober-native,guest-init-host,ndk-stubs/libc.so}` matches the
  committed manifest; `python3 tools/gen_progress.py --check` passes.
- `readelf -d build/sober-native | grep TEXTREL` → no match; `ldd` → libc only.
- `ctest --test-dir build --output-on-failure` → 2/2; `tests/app-shell.sh`
  remains intentionally RED (do not green it).
- `git tag -l` non-empty; `gh repo view --json visibility` → `PRIVATE`;
  unauthenticated `git ls-remote` → 401/404.
- README first screen still: goal NOT PROVEN, `0/1`, `WITHHELD`, four unchecked
  DoD boxes. No figure in it asserts marker emission.

## 6. Risk / rollback register

| Risk | Mitigation |
| --- | --- |
| Untracking `build/` hides binaries a consumer expects at those paths | Phase 3 publishes pinned assets (Releases or `release/`), README points there; manifest gives exact SHAs. |
| Phase history lost to gc during cleanup | Phase 0 backup branches + bundle, before any destructive command. |
| `git rm --cached appData/` removes someone's captured logs from git | They remain on every existing clone's disk; commit a `NOTICE` on where captures live. Re-add only deliberately with `git add -f`. |
| CI job mistakes harness TEXTREL for a shipped defect | Assert only on `sober-native`; document `guest-init-host` as diagnostic. No `-z text`/`-Werror` change without lead review. |
| Hand-typed licence grants rights we don't own | Default to `NOTICE.md` + external-target disclaimer; decide LICENSE separately. |
| Other agents' untracked files get staged by `git add -A` | Use explicit `git add <paths>` throughout; do not run `git add -A`. |

---

## 7. Handoff note

This document is analysis only. The lead should execute Phases 0–7 in order,
keeping the marker **WITHHELD** throughout; re-run §5 after each phase. The
engine stays frozen, the private remote stays private, and the honest headline
stays unchanged. No source file, build file, doc, test, README, git ref or
remote was touched by this agent.

**Final verdict: the defects are packaging/hygiene, not product. Cleanup is
low-risk if done in this order; the only irreversible hazard — loss of the
squashed-phase provenance — is handled in Phase 0 before any other step. Marker
WITHHELD; no success claimed.**
