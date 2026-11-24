# n10 WAVE3 #735 — harness (one page, SOURCE AUDIT, NO runs)

HOST-ONLY, READ-ONLY source audit. NO runs, no network, no device, no guest
writes/hand-set bytes/fake returns/seeder/probe bridge. DELETE NOTHING.
Worktree `/tmp/opencode/n10-harness`, branch n10-harness, HEAD `26878a6`.
All claims are source facts quoted with `file:line`; no rung credit.

## FAULTING INSTRUCTION FIRST (from surviving diagnostic log, not a new run)
`guest+0x06caef67` — reported RIP after guest int3 at `0x6caef66`, `signal=5`,
`si_addr=0`:
`ab_off_1.err:[W28_FAULT_INSN] rip=0x00007f21040baf67 guest_rva=0x06caef67`
The audit below explains why no rung credit can attach to that fault.

## (a) PASS-BOUNDARY-REPLAY is unreachable when SHA != 8215519f — PROVEN
Exactly two printf sites of the string exist:
- `src/linux/guest_app_shell.c:1378` `static const char pass[] = "PASS-BOUNDARY-REPLAY (DEBT=";`
  — selected at `:1382` only when `n10_bad_input` is false:
  ```
  src/linux/guest_app_shell.c:1380  const int n10_bad_input = !g_w29_arg0_sha_ok ||
  1381      getenv(SOBER_AVD_BOUNDARY_UNVERIFIED) != NULL;
  1382  const char *lab = n10_bad_input ? "MODIFIED-INPUT (no credit) "
  1383                                  : (boundary ? pass : fail);
  ```
  `pass` cannot be chosen when `g_w29_arg0_sha_ok==0` regardless of `boundary`,
  any env switch, or `SOBER_W21_WATCH` credential.
- `src/linux/guest_app_shell.c:2476` `} else if (boundary && g_w29_arg0_sha_ok)`
  immediately before `:2478` `fprintf(... "[checkpoint] rung1 PASS-BOUNDARY-REPLAY (DEBT=%d)\n", ...)`.
  The `boundary` branch is reached only after `:2473 if (modified_input)` with
  `modified_input = !g_w29_arg0_sha_ok || getenv(SOBER_AVD_BOUNDARY_UNVERIFIED) != NULL`.
- Third printer, input gate: `src/linux/guest_dlopen_host.c:1285`
  `fprintf(stderr, "[checkpoint] rung1 INPUT MISSING/MISMATCH (no rung)\n");`
  — this path only emits the failure label.
- The only `1` for `g_w29_arg0_sha_ok` is `avd_boundary.c:139`:
  `g_w29_arg0_sha_ok = (strcmp(hex, SOBER_AVD_BOUNDARY_SHA256) == 0);`
  The diagnostic opt-in returns 1 from the gate but does NOT set this global
  (`avd_boundary.c:154-160`: "g_w29_arg0_sha_ok stays 0, so every rung printer
  emits MODIFIED-INPUT (no credit) and PASS-BOUNDARY-REPLAY remains unreachable").
**Conclusion: no switch can produce PASS with a mismatched SHA.**

## (b) No probe bridge for 0x1dbe4fc — PROVEN
- `src/compat/bionic_bootstrap/bootstrap_profile.h:63`:
  `#define SOBER_BRIDGE_USABLE_SIZE_RVA   0x1dbdfbau` — the bridged usable-size
  entry is `0x1dbdfba`, NOT the probe function `0x1dbe4fc`.
- `src/compat/bionic_bootstrap/bionic_bootstrap.c:890-893` bridge array lists
  `{ SOBER_BRIDGE_USABLE_SIZE_RVA, fp_usable, ... "usable-size" }` and the 7 other
  RVAs (:62-67,71,74 of the header); `0x1dbe4fc` appears in NO array entry.
- `bionic_bootstrap.c:1607-1609`: "the 8 bridge patches are all allocator entry
  points, never the page source; they do not make 1dbe4fc see a registered
  pointer. ... 1dbe4fc is NEVER bridged."
- `bionic_bootstrap.c:1642-1643` runtime banner: `"[N9_ALLOC_PATCH] OFF (default); guest allocator unpatched; 1dbe4fc NOT bridged\n"`.

## (c) Withhold list + marker unchanged — PROVEN
`git diff 892dc50 HEAD -- src/jni/avd_boundary.c` starts with context
`@@ -92,6 +92,80 @@ int sober_avd_boundary_should_withhold(void)` and adds
code only AFTER that function; its body is unchanged:
`src/jni/avd_boundary.c:89-92 return sober_avd_boundary_enabled();`
Marker path unchanged: `guest_app_shell.c:102 static void emit_marker(...)`,
`:2382 avd_boundary_provenance = sober_avd_boundary_should_withhold();`,
`:2386 "[app-shell] marker withheld: %s\n"`, `:2390 diagnostic guest-state...`,
`:2416 if (!seeded_state && shell_cb && render_ok)` — all untouched by the
diff (`git diff 892dc50 HEAD -- src/linux/guest_app_shell.c` contains only rung
label changes at :1380-1388 and :2476, no marker/withhold hunk).

## (d) Allocator patch default OFF — PROVEN
`src/compat/bionic_bootstrap/bionic_bootstrap.c:1610-1615`:
```
static int bb_alloc_patch_enabled(void)
{
    const char *e = getenv("SOBER_ALLOCATOR_PATCH");
    return (e && e[0] == '1' && e[1] == '\0') ? 1 : 0;
}
```
`:1638 if (!getenv("SOBER_BOOTSTRAP_NO_BRIDGES") && bb_alloc_patch_enabled())`
— absent env ⇒ no patch (default OFF); only `SOBER_ALLOCATOR_PATCH=1` opts in.

## NOT DONE
No runs (per task), no ledger edit, no device/captures/object build. The
verified-SHA success path remains UNPROVEN (w11-arg0.json missing). No questions.
