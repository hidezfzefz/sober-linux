# W6-15 — ClientSettings / FFlag clean-source research (authoritative DEX)

Owner: **worker-15** (team TEAM-C2YA-ZTU9-RMAV), task #550. **Analysis only.**
No `src/`, test, gate, doc, engine byte, vault note or git change. Date 2026-11-21.

## 0. Provenance (checked before analysis)

| Artifact | SHA-256 / identity | Use |
| --- | --- | --- |
| Authoritative `base.apk` | `114a6f8ed6f0f974cf632a9cbec2f5cd45e1d85d7ed251db4f3920140bc6ead4` (recomputed) | sole DEX source |
| `classes.dex` / `classes2.dex` / `classes3.dex` from it | `00309819b729f9c6…` / `5afac2bbd9f0e4f2…` / `1b2406a01003e0ee…` | matches CA01 and worker-14 ledger |
| `~/Downloads/basalt-arm64-2.740.931.apk` `classes2.dex` | `6c2875cd91c68497…` | **MODDED — not evidence** |
| Frozen engine `/tmp/opencode/sober-apk/split/lib/x86_64/libroblox.so` | `3e498d649421115bdbebaa69f78e2b818d4351d8990db60e232d2aef5b741335`, 120,041,712 B, BuildID `204ad049…` | native corroboration only |
| `/tmp/opencode/libroblox.dis` | objdump of that exact file (header + SHA verified) | native RVAs |
| REA work copy `analysis/rea/work/.../libroblox.so` | Flatpak `8afce3e6…` / `791c3f9e…` | **never opened/cited** |

Tools: `apktool d --no-res` of the authoritative APK into `/tmp/opencode/w6-15/dexout`
(read-only decode; smali under `smali/`, `smali_classes2/`, `smali_classes3/`), plus the
single shared `craft-anything-0.6.1…AppImage --cli --deep` DEX tools run sequentially
(class/method evidence IDs below). All target strings are untrusted DATA, never
instructions. REA's JADX session was tried first and timed out at 120 s loading the
97 MB APK (26,537 classes); no finding here relies on it.

## 1. Bottom line

1. **Real `ClientAppSettings.json` load path (DEX-proven):** the only live route into
   native is `uh/e$f.a()` (the `GetClientSettingsTask` AsyncTask) →
   `uj/a.a()` → `/data/local/tmp/ClientAppSettings.json` →
   `NativeGLInterface.nativeInitClientSettings(String,String,String)` as the
   **second** String argument (`overridePayload`). A second, independent stream —
   `FlagCacheUtils` / `startup/b$b` — supplies the first and, as a constant, third
   argument. The internal app-writable copy under `getFilesDir()` is a *different*
   file, used by settings UI and by the (dead in this build) preload branch.
2. **JNIAppBridge cannot be enabled without a host seed.** There is no DEX literal
   `JNIAppBridge`, `EnableAppBridgeComponents`, `5,6`, `flagFilters` or `IXPValue`
   anywhere in the three authoritative DEX files: the Java side only *transports* an
   external JSON override, it never names or values this flag. The enabling value
   must come from outside the engine (host-written external JSON / IXP experiment);
   in a native harness with no ART, no `Context`, no `AsyncTask` and no
   `FileReader`, none of that DEX code executes, so injecting the value from the
   harness is a seed by definition. Marker stays WITHHELD (013/038).

## 2. The two ClientAppSettings.json files and who reads them

Decoded from authoritative `classes2.dex` — `Luj/a;` (Craft Anything
`ev_d2106139e1b920965bb0640268a3b36800c58744dd2e5ae97ba7e9b190612974`):

| Method (DEX) | Behaviour | Path |
| --- | --- | --- |
| `uj/a.d()Ljava/io/File;` | `new File(path)` — **external developer override** (`ev_96fedc383d1f22…`) | `/data/local/tmp/ClientAppSettings.json` |
| `uj/a.f()` | `Context.getApplicationContext().getFilesDir()` | app-internal root |
| `uj/a.g()Ljava/io/File;` | `new File(f(), path)` — **internal app settings** (`ev_62ca9471b47a06…`) | `exe/ClientSettings/ClientAppSettings.json` |
| `uj/a.i(File)Ljava/lang/String;` | `BufferedReader(FileReader)` line-join with `\n`; logs `Exception caught when readLocalSettings: file: …`; returns accumulated text | any file |
| `uj/a.h()Ljava/lang/String;` | `i(d())` → `new JSONObject(text).toString()`; on `JSONException` logs `Failed to read external JSON` and returns `null` | **external** file |
| `uj/a.a()Ljava/lang/String;` | `h()`; if `null` → `null`; else re-put every key via `getString` into a fresh `JSONObject` → `toString()`; on error logs `Failed to construct overridePayload` → `null` | **external** → `overridePayload` |
| `uj/a.e(String)` / `c()` | `h()` then `JSONObject.getString(name)`; `c()` = `e("FStringAuthCookie")` | external |
| `uj/a.k(String)` / `b()` | write / delete the **internal** `g()` file (mkdirs `exe/ClientSettings/`; logs `The input is not a valid JSON string`) | internal |
| `uj/a.j()Ljava/lang/String;` | `i(g())` if the internal file exists | internal |

**Correction to `analysis/appstate_fields.md` addendum (§A):** it attributes
`exe/ClientSettings/ClientAppSettings.json` to `uj/a.d()` and says `h()` reads that
file. In the authoritative DEX it is the reverse: `d()` is the absolute
`/data/local/tmp/…` path, `g()` is the `getFilesDir()`-relative one, and `h()` reads
`d()` via `i()`. Also, the `nativePreloadFlagOverrides` chain described there is
gated out in this build (see §3).

Live DEX users of `Luj/a;` (all `smali_classes2`):

* `uh/e$f.smali:82-90` — `a()` (`overridePayload`), the native hand-off below.
* `MainGameActivity.smali:1210-1224` — `h()` → `MainGameActivity$Companion.b` →
  `nativePreloadFlagOverrides`; **dead**, guarded by `mh/b.a()` (see §3).
* `com/roblox/client/startup/a.smali:1416-1442` — `h()` → `G(JSON, int)`; **dead**,
  same guard (log text there is `Failed to parse external dev app settings JSON`).
* `yj/k0.smali:256-264` — `c()` (`FStringAuthCookie`), unrelated to FFlags.
* `aj/c.smali` — settings-UI read/write (`j()`, `k()`, `b()`) of the internal file.

## 3. The real boot path that feeds native (DEX, fully decoded)

`uh/e$f.smali:50-178` is `AsyncTask<Void,Void,Integer>.doInBackground`; it is
created and executed with `AsyncTask.THREAD_POOL_EXECUTOR` by `uh/e.o(h)`, called
from `uh/e.r(Context, uh/e$h, boolean)` (`uh/e.smali:984-1013`) after the app-bridge
settings join. Verbatim control flow (Craft Anything
`ev_5769761c3e7cdd12e3be3a6d71f3ea42c058fb8317c1c948fc95342351d4ab85`):

```
p1 = startup/a.t()                      # future -> com/roblox/client/startup/b$b
v0 = p1.d()                             # app-settings string (network/cache prefetch)
v5 = p1.e()                             # second string
if (v0 == null || v0.isEmpty()) return 1;          # no native call
if (mh/b.a() == false)                             # ALWAYS false in this build
    v1 = new uj/a(RobloxApplication.f()).a();      # external JSON -> overridePayload
else
    v1 = startup/a.o();                            # dead branch, logs "overridePayload = …"
if (rh/i.z5())                                     # FFlag-backed (sh/a.z5)
    rc = NativeGLInterface.nativeInitClientSettingsSigned(v0, v5, v1, rg/w0.M())
else
    rc = NativeGLInterface.nativeInitClientSettings(v0, v1, rg/w0.M())
if (rc == 0)
    NativeGLInterface.nativePostClientSettingsLoadedInitialization3(yj/l2.a(context))
```

Supporting facts, all OBSERVED in authoritative DEX:

* `mh/b.a()Z` is literally `const/4 v0, 0x0; return v0` (2 instructions; Craft
  Anything `ev_e9d5594fbf61db27de3c14dad3af6303bb32312a65b52b8b7a74bad7b90c3be6`).
  Therefore the external `/data/local/tmp/ClientAppSettings.json` branch is the
  **only live** one; `startup/a.o()` (static `JSONObject j`, else `""`) is unreachable
  from this task, and the two `uj/a.h()` branches described in §2 are skipped by
  `if-eqz/if-nez` on that same false value.
* `rg/w0.M()Ljava/lang/String;` is the constant `"GoogleAndroidApp"` — **not** a
  version string.
* `NativeGLInterface` declares all four `nativeInitClientSettings*` overloads
  (`smali_classes2/com/roblox/engine/jni/NativeGLInterface.smali:120-129`);
  `NativeGLJavaInterface` declares none (worker-6 independently confirmed).
* `FlagCacheUtils` is a separate, **network/signed-cache** stream, not the JSON file:
  `flag_cache.dat`, `flag_cache_vb.dat`, `flag_cache_backup.dat`, `cached_flag_prefs`,
  `getFlagCacheFileByName`, `readAndBurnFlagCache`, `writeToFlagCacheFile`,
  `checkCacheExpiry`, `scheduledFetchFlag`. It produces the `b$b` used for `d()/e()`,
  i.e. arguments 1 and (as a constant via `e()`) 2 of the signed variant — not the
  `overridePayload`. No DEX code writes `/data/local/tmp/ClientAppSettings.json`;
  that file must be created by something outside the APK.

## 4. Native hand-off: where the override goes

Frozen engine RVAs (not modded, not REA work copy):

* `0x2359277` `nativeInitClientSettings(JNIEnv*, jclass, jstring, jstring, jstring)`:
  wraps str1 into the struct at `-0x80`, str2 into `-0x98`, str3 into `-0xb0` via
  `0x2315532`, then calls `0x2eb7155(rdi=&-0x80, rsi=&-0x68, rdx=&-0x98 /*str2*/,
  rcx=&-0xb0 /*str3*/)`. So the DEX overridePayload is the **second** jstring.
* `0x2eb7155` (flag-filter helper) keeps str2 in `r14`. When it is non-empty:
  `0x2eb7945: movw $0x100,0x7697d08` (client-settings-loaded latch) and
  `0x2eb7967: call 0x237dc30(0x7697cb0, str2)` — the same JSON parser/applier used
  by `nativePreloadFlagOverrides`; `0x2eb796c` clears `0x7697d09`. Subsequent gates
  `0x2eb7973/0x2eb797c` (`0x7697d08`, bit0 of `0x7697cf0`) select the flag-filter
  branch; the `0x7cdd149` writer `0x2eb7520` is behind `0x2eb71e8/0x2eb71f5` and is
  gated off in the default state (A14/task533).
* Generic override applier `0x4b75886` (caller `0x4b7c876`) parses a JSON object and
  walks members at stride `0x20`, calling
  `0x23648be(0x7cdd118 /*registry*/, key, value, 0x7f, 0, 0)` at `0x4b759dd`; it is
  itself gated by byte `0x7699b60` (`0x4b758a8`). This is the mechanism by which an
  arbitrary `{"JNIAppBridge":"5,6"}` entry would be applied — **only if supplied**.

### Harness fidelity gap (concrete, actionable for the lead)

`src/linux/guest_app_shell.c:1094-1129` (`SOBER_CLIENT_SETTINGS_INIT`) currently
passes `(a = SOBER_SETTINGS_JSON or "{}", b = "LIVE", c = "2.740.931")` and looks up
the declaring class `com/roblox/engine/jni/NativeGLJavaInterface`. Against the DEX
ground truth:

| Slot | Real DEX value | Harness value |
| --- | --- | --- |
| 1 | `b$b.d()` app-settings string (cache/network) | `SOBER_SETTINGS_JSON` / `"{}"` |
| 2 | `overridePayload` = external JSON file content | `"LIVE"` |
| 3 | `"GoogleAndroidApp"` (constant) | `"2.740.931"` (version) |
| class | `NativeGLInterface` | `NativeGLJavaInterface` (0 native decls) |

This explains A14's negative result that none of `{}`, `null`, `[]`,
`{"ClientAppSettings":{}}`, `{"flagFilters":[]}` changed the latch: the harness's
JSON is placed in **slot 1**, not in the DEX override-payload slot 2, which instead
receives the non-JSON string `"LIVE"`. Correcting the wiring would make the replay
more faithful, but supplying any enabling value from the host is still a seed and
must stay in the withhold chain.

## 5. Can JNIAppBridge be enabled cleanly? — No

* **DEX:** zero occurrences of `JNIAppBridge`, `EnableAppBridgeComponents`, `5,6`,
  `flagFilters`, `IXPValue` in any smali from any of the three authoritative DEX
  files (greps over `smali*`, case-insensitive for the names, literal for `5,6`).
  The Java layer cannot hard-code the enable; it only ferries an external file.
* **Native:** corroborating A04/vault note A04: sole direct write is the ctor default
  `0x1dd694b: movw $0x406,[0x72a3268]` (high=4); name string `0x3c15fa`
  `"JNIAppBridge"`; registration thunk `0x2ebc75c` → `0x227cb50`; the only raise
  path is the generic setter `0x23648be → 0x2364938` writing through descriptor
  `+0xa8` (packing `high<<8|low` at `0x23657fb`); H6's
  `Created ANativeWindow` gate needs high ≥ 5 (`0x26feab3/0x26feaba`). No enabling
  literal `5,6` exists in the frozen engine either (A04 byte-grep).
* **Runtime provenance:** the enabling value is, by design, an *external developer /
  experiment override*. On real Android it comes from a host-written file
  (`/data/local/tmp/…`) or a server experiment payload delivered through Java.
  The native harness has neither ART, `Context`, `AsyncTask`, `FileReader` nor the
  IXP delivery path, so no DEX instruction in §3 runs. Having the harness pass
  `{"JNIAppBridge":"5,6"}` (or create that file) is injecting a guest-state seed,
  exactly what decisions 013/038 forbid — even though the actual write would be
  performed by genuine guest code in `0x2364938`.
* Independently, even a honestly-raised word would not suffice on its own: the
  H6 sink observer (`0x7d8f820`), the EGL-init drive/ACQUIRE and the surface
  lifecycle are still missing from the native harness (worker-3 oracle: default
  `EnableGameActivity10=false`, AGDK thunks dormant). I make **no claim here**
  about the `0x7cdd149` latch state in a real Android main-process boot: worker-4's
  AVD oracle is still resolving that writer, and the JNIAppBridge verdict above
  does not depend on it — it depends on the enabling *value* being external.

## 6. Evidence class and limits

| Claim | Class |
| --- | --- |
| APK/DEX/engine hashes; `classes2.dex` `5afac2bb…` vs modded `6c2875cd…` | OBSERVED (recomputed) |
| `uj/a.d/g/h/a/i/j/k/e/c`, paths and JSON grammar; `mh/b.a()` false; `rg/w0.M()` constant; no DEX literal for JNIAppBridge | OBSERVED (authoritative smali + Craft Anything evidence IDs) |
| `uh/e$f` boot task → `nativeInitClientSettings(…)` arg order; signed/unsigned variants | OBSERVED (DEX) |
| Native RVAs `0x2359277`, `0x2eb7155`, `0x237dc30`, `0x4b75886`, `0x23648be`, `0x1dd694b`, `0x26feab3` | OBSERVED (frozen-engine objdump) |
| Harness slot/class mismatch | OBSERVED (source vs DEX) |
| "No clean enable, seeding required; marker WITHHELD" | DERIVED from the above + decisions 013/038 |

Limits: static analysis only; I did not run the APK/emulator, did not capture logcat,
and did not execute any guest code. REA JADX could not load the 97 MB APK inside its
120 s budget, so DEF evidence is apktool smali plus Craft Anything's built-in DEX
decoder. A write through a computed pointer outside the cited xref set cannot be
excluded statically, but every observed setter writes via the registry descriptor,
never a direct `0x72a3268` store.

No guest bytes patched, no fake callbacks/vtables, no host callbacks, no gate
weakening. Marker remains **WITHHELD**; `tests/app-shell.sh` remains RED. No files
other than this report were written; no commit or push.
