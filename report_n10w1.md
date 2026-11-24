# n10 WAVE1 #725 — harness (one page)

Worktree `/tmp/opencode/n10-harness` branch n10-harness, HOST-ONLY change,
ledger DEBT=1. Cores 0-1, flock held for baseline. DELETE NOTHING, NO network,
no device, no guest writes/hand-set bytes/fake returns/seeder/probe bridge.
SF_FULL=1, DEX object OFF, ctor3 OFF.

## FIRST RESULT — NO FAULT: fail-closed exit BEFORE guest work
Baseline run rc=**9**, 3 stderr lines, no memfd copy, no dlopen, no crash-handler
banner:
```
[harness] guest: /tmp/opencode/n9-harness/n9bin/libroblox.so
[avd-boundary] INPUT MISSING/MISMATCH (no rung): open failed: /tmp/opencode/w11-arg0.json
[checkpoint] rung1 INPUT MISSING/MISMATCH (no rung)
```
Evidence `baseline.err` / `baseline.rc`, `baseline_n10.sh`.
`w11-arg0.json` is missing, so the gate fires exactly as designed — this run is
DIAGNOSTIC ONLY, **no rung credit**, and never prints PASS-BOUNDARY-REPLAY.

## Source diff (quoted, host-only)
`src/linux/guest_dlopen_host.c` — gate before `copy_guest_to_memfd()`:
```
+#include "avd_boundary.h"
+    /* n10 HOST-ONLY FAIL CLOSED: verify input SHA BEFORE any guest work.
+     * Missing/mismatched input -> no rung, exit 9. No guest bytes touched. */
+    if (!sober_avd_boundary_fail_closed()) {
+        fprintf(stderr, "[checkpoint] rung1 INPUT MISSING/MISMATCH (no rung)\n");
+        return 9;
+    }
```
`src/jni/avd_boundary.c` — hash computed after `fopen`/`fread`, SHA published
only on success; **hardcoded SHA banner removed from provenance**:
```
+char g_n10_arg0_sha_hex[65];  /* published only after successful open+hash */
+int sober_avd_boundary_fail_closed(void) { ... fopen / fseek / ftell / fread
+    sha256(data, sz, digest); g_w29_arg0_sha_ok = (strcmp(hex, SOBER_AVD_BOUNDARY_SHA256) == 0);
+    if (ok) { memcpy(g_n10_arg0_sha_hex, hex, sizeof(hex));
+              fprintf(stderr, "[avd-boundary] SHA-256 verified after open+hash: %s\n", ...); }
+    else { g_n10_arg0_sha_hex[0] = '\0';
+           fprintf(stderr, "[avd-boundary] INPUT MISSING/MISMATCH (no rung): sha256=%s want=%s\n", ...); }
```
```
-             "class=\"%s\" session=\"%s\" file=%s sha256=%s argc=3 ..."
-             SOBER_AVD_BOUNDARY_PATH, SOBER_AVD_BOUNDARY_SHA256, ...
+    /* n10: NO hardcoded SHA banner.  Publish sha256 only after open+hash. */
+    if (g_n10_arg0_sha_hex[0]) ... sha256=%s ... g_n10_arg0_sha_hex ...
+    else ... sha256=UNVERIFIED ...
```
`src/linux/guest_app_shell.c` — both rung printers fail closed:
```
+const int n10_bad_input = !g_w29_arg0_sha_ok || getenv(SOBER_AVD_BOUNDARY_UNVERIFIED) != NULL;
+const char *lab = n10_bad_input ? "MODIFIED-INPUT (no credit) " : (boundary ? pass : fail);
+if (!n10_bad_input) { append DEBT number and ')' }
-            } else if (boundary)
+            } else if (boundary && g_w29_arg0_sha_ok)
```

## Ledger
Host-only input gate, owner=harness, DEBT=1, gate/withhold list unchanged;
removal condition: a genuine 1,533,221-byte SHA-8215519f boundary input is
available again (then this gate is unnecessary). Entry sent to lead.

## NOT DONE / UNPROVEN
No new guest runs beyond the fail-closed baseline (input missing); the
"SHA verified after open+hash" success path could not be exercised — the
authoritative input file does not exist locally. UNPROVEN: rung behavior with
a correct input. No device/captures/object build. No questions asked.

## Evidence
`/tmp/opencode/n10-harness/baseline.err`, `baseline.rc`, `report_n10w1.md`.
