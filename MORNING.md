# Weekend n10 — audit findings for MORNING (one page)

Owner: audit, worktree `/tmp/opencode/n10-audit`, branch `n10-audit`. Cores 6-7, READ-ONLY. No device/runs/network/guest writes/gate edits/hand-set bytes/fake returns/seeder/probe bridge. No deletions.

## Faulting instruction first
Guest `int3 0x6caef66` (reported RIP `0x6caef67`, bytes `89 c0 e8 62` = `mov eax,eax; call abort@plt`), SIGTRAP signal=5, si_addr=0, rc=139 — proven 6/6 from surviving `n10-harness/ab_{off,on}_1/2/3.err`. No new runs this wave (authoritative input LOST).

## Headline: input gap / no rung credit
`/tmp/opencode/w11-arg0.json` (1,533,221 B, SHA `8215519f…`) is LOST and unrecoverable. All runs use modified input `w15-flags-1.json` (SHA `4d50a0ec…`). Every run quotes `[avd-boundary] MODIFIED-INPUT (no rung): diagnostic opt-in SOBER_N10_MODIFIED_INPUT=1; sha256=4d50a0ec… want=8215519f…`; `verified` = 0/6, `PASS-BOUNDARY-REPLAY` = 0/6; default gate exits 9 with `[checkpoint] rung1 INPUT MISSING/MISMATCH (no rung)`. NO rung credit, never PASS. Harness #735 source audit proved PASS is unreachable with bad SHA (both printers gated on `g_w29_arg0_sha_ok`).

## Verified claims (quoted raw lines)
- **harness #725** fail-closed input gate: `baseline.err` rc=9; gate `guest_dlopen_host.c:1282-1285` before memfd copy; hardcoded SHA banner removed (`avd_boundary.c:218-219`); SHA published only after open+hash (`:95`). Owner=harness, DEBT=1, removal condition = genuine SHA-8215519f input restored.
- **harness #735** source invariants: PASS unreachable on bad SHA; `0x1dbe4fc` NEVER bridged (`bootstrap_profile.h:63` bridges `0x1dbdfba`); withhold/marker unchanged (`git diff 892dc50 HEAD` has only rung-printer hunks at `guest_app_shell.c:1371/:2466`); allocator patch default OFF (`SOBER_ALLOCATOR_PATCH=1` only opt-in).
- **static-B #733** n10 HardAssert chain: latch hit `[N10_LATCH_HIT 0x2eb7520] hit=1 tid=511649 guest_RA=guest+0x2359342`; fault bytes latch=1, `0x7cef650=0`; HardAssert dispatcher index 0 (`23a146c xor edi,edi`); reporter `6caef4f` passes latch, `6caef58` fails flag → `6caef66 cc int3`. n9 TaskScheduler path uses index 6 but SAME dispatcher `6caed5d`/reporter/int3. RETRACTED: two different crash sites.
- **static-B #736** consolidated rungs: rung1 mechanism PROVEN but NO credit (lost input); rung2 AVD PROVEN / harness UNPROVEN (#734 entered, never returns, `N14_REPLAY 3/3` 0/6); rung3 UNDEFINED (no authenticated AVD capture; Waydroid oracle BLOCKED: adb unauthorized + no frida-server). All rung credit RETRACTED.
- **static-A #732/#731/#705**: `0x7cef650` sole direct writer `2279a8f mov %al` (ctor3602, `2279a4b xor eax,eax` ⇒ 0), indirect FFlag write-through via `227d51f lea #7cef650` → `227c18e` → `2365467` (`DebugForceTerminateOnRbxCrash`, default 0); `0x7cdede0` sole writer `1db9acf` in skipped ctor3 (DFLog-only). No auth before AppStart (`CHECKPOINT-LADDER.md:16/18/23-25/28/34`); cookie natives 0x2333e4d/0x2ec6a1d/0x2330328. DEX settings fetch endpoint `clientsettingscdn.<domain>/v2/settings-compressed/...dcz`, GET + Pragma, no auth header on path.
- **static-B #730**: TLS key `0x72a1b08` six refs, only write `pthread_key_create 0x2ea664f`; host seeders write TLS VALUE only; `0x72a3058` last writer host seeder inclusive range. RETRACTED cachedMethod as guest singleton.

## Correction (lead-flagged)
RETRACTED static-B `report4.md:16` "mimalloc flag `0x7cef650=0` is a downstream consequence of ctor3 skip". The bytes are INDEPENDENT: `0x7cdede0` ← skipped ctor3 `1db9acf`; `0x7cef650` ← ctor3602 `2279a8f` (direct, al=0) + indirect FFlag registration out-pointer `227d51f`/`2365467`. static-A corrected "NO engine writer" → "no direct writer; indirect FFlag registration writer exists".

## Evidence / required checks
- `/home/user/sober-evidence/MANIFEST.sha256`: 9 cited artifacts COPIED (never moved), `sha256sum -c` → 9/9 OK, dir 209 M. Sources intact. No deletions.
- Disk `df -h`: `/dev/nvme0n1p3` 230 G total, 194 G used, 36 G avail, 85 %.
- Secrets scan command+output shown in report; self-matches only (recorded command in ledger/notes), no credential values.
- Git: no captures/flag JSON/logs/tokens tracked; worktrees each own branch.
- Cited files still missing: `w11-arg0.json`, `w18-audit/audit-notes.md`, `w19-harness/report.md`, `w25-phone/base.apk` (plus glob prefixes); affected claims marked UNPROVEN, not deleted.

**Marker stays WITHHELD.**
