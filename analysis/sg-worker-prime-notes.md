# Latch prerequisite — authoritative DEX + native chain (read-only)

Engine `3e498d64…335` / BuildID `204ad049…d6c9`; base.apk `114a6f8e…ead4`.
No guest byte patch, no host-triggered guest call on the clean path, no gate
weakening, no edit to `guest_app_shell.c`. Marker WITHHELD.

## Ordered RA list + gate
`uh/e$f.a()` (DEX `classes2.dex`, AsyncTask) → `NativeGLInterface.nativeInitClientSettings(String,String,String)` → native `0x2359277`: converts 3 jstrings via `GetStringUTFChars 0x2315532` into std::strings at `-0x80/-0x98/-0xb0(%rbp)` → `call 0x2eb7155` at `0x235933d` (rdi = string#1) → `0x2eb7198 test len(s1),len(s1)`: non-empty → `0x2eb71a1 movb $0, D08@0x7697d08` → `0x2eb71de call 0x235a58c` → gate `0x2eb71e8` (`D08==0 → je 0x2eb72bd → … → 0x2eb7520 movb $1,0x7cdd149`) ; empty s1 → `je 0x2eb7225` skips the whole block. `0x2eb7520` is INSIDE `0x2eb7155` (function start `0x2eb7155` after `call 0x2ea6a10` at `0x2eb7150`), i.e. **latch is written inside the same nativeInitClientSettings call**, after the gate.

## (1) Genuine AVD caller / strings
`uh/e$f.a()` smali line 159: `invoke-static {v0, v1, p1}, NativeGLInterface->nativeInitClientSettings(String,String,String)I`
- `v0 = com/roblox/client/startup/b$b.d()` (field `b`) = **settings path**; the same method checks `v0 == null || v0.isEmpty()` and returns 1 early, so on AVD this string is guaranteed **non-empty** (real path, e.g. `/data/local/tmp/ClientAppSettings.json`; its exact runtime value is a field, not a DEX constant).
- `v1` = override payload from `uj/a(RobloxApplication.f()).a()` or `com/roblox/client/startup/a.o()`.
- `p1` (third) = `Lrg/w0.M()`. (If `rh/h.z5()` is true, the 4-arg `nativeInitClientSettingsSigned` is used instead.)

## (2) Latch timing
Written at `0x2eb7520` in the callee `0x2eb7155`, reached from `0x235933d` within the same `nativeInitClientSettings` call — only when string#1 is non-empty AND the gate at `0x2eb71e8` sees `D08 == 0`. `CF0=0x21/D08=1` are observed after the `0x235a58c` call at `0x2eb71de`; the gate reads them immediately afterwards. Full RIP-relative scan finds only the startup zero-store to CF0 (`0x2096448`) and several D08 clears (`0x2eb71a1`, `0x2eb7945`, `0x2eb8515`, `0x2ee4ad3`, all `movw $0x100`/`movb $0`), so the `0x21` is written register-indirectly (std::string copy) inside `0x235a58c` — inferred, not a proven direct store. Latch `0x7cdd149` has 2352 RIP-relative references but a single writer, `0x2eb7520`.

## (3) What the harness must supply (not fabricate)
The guest's **own Java startup** must reach `uh/e$f.a()` and deliver a non-empty real settings path plus real override payload through faithful JNI (`GetStringUTFChars` at `0x2315532`), with the settings file actually readable so `0x235a58c` leaves `D08 == 0` at the gate. FFlags are not the switch (`0x72a31b8` stays 0). A host call to `0x2359277`, a synthetic jstring, a seeded `CF0/D08` or a patched gate would all be fabricated state and are forbidden. The clean harness has no Java caller, hence the gate branch that writes the latch is unreachable today.

## First divergence
`0x6caef67` — `RBXCRASH "Can't initialize the TaskScheduler before flags have been loaded"`, `0x7cdd149 == NULL`, downstream of the `0x2eb76bd` gate branch. Worker `" RBX Worker A"` exists; FLog `_IO_fflush` divergence stays fixed by the stdio-GOT rebind; `0x2a0daf7`/attach remain unreached.

Verification: engine SHA unchanged, marker WITHHELD.
