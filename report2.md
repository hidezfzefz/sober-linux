# n10w2 #730 static-B — TLS key + FFlag global mapping (READ-ONLY)

FAULTING INSTRUCTION FIRST: guest `int3 0x6caef66` (RIP `0x6caef67`), reporter `0x6caed7c`; n10 A/B latch=1, `[0x7cef650]=0` → mimalloc HardAssert. All runs MODIFIED INPUT (w11-arg0.json LOST), NO rung credit. DELETE NOTHING; no network/runs/writes.

## PROVEN — all 6 references to TLS key global `0x72a1b08`

Quoted from surviving `analysis/A04-tsd-key-audit.md:92-107`:
```
Created once by the `pthread_once` callback `0x2ea6640`:
`0x2ea6641 lea 0x72a1b08,%rdi`, `0x2ea6648 lea 0x2ea6670,%rsi` (destructor),
`0x2ea664f call pthread_key_create@plt`; on success flag `0x72a1b04 = 1` ...

2ea6455  getspecific for descriptor slot lookup        (0x2ea6440)
2ea650c  getspecific re-check after once/mutex path    (0x2ea6440)
2ea6565  setspecific of freshly allocated array        (0x2ea6440)
2ea661a  key passed to pthread_key_delete              (0x2ea6610)
2ea6641  &key passed to pthread_key_create             (0x2ea6640)
2ea6685  destructor re-arms setspecific(key-1, ...)    (0x2ea6670)
```
- The **only write to the key global itself** is by `pthread_key_create` at `0x2ea664f` via `&key` (`0x2ea6641`), inside the one-shot `pthread_once` callback; success sets flag `0x72a1b04=1`, teardown `0x2ea6610` calls `pthread_key_delete` with the same key.
- `0x2ea6565` (`setspecific`) writes the **TLS value** for that key (the per-thread descriptor array), not the key global. Value observed as **3** in logs (`analysis/tls-2a0de60-r70-correction.md:47`: `2ea6455: mov 0x72a1b08,%edi ; global pthread_key_t (value 3 in logs)`).
- Host-side seeders also write the TLS **value** for key 3, never the key global (`analysis/tsd-static-buffer-abort.md:64-68`, raw):
```
[sober-jni] seeded STUB TSD key 3 (rva 0x72a1b08) -> 0x... (4096 bytes)   ; calloc, VALID
[app-shell] TLS seed (key rva 0x72a1b08, 4096 bytes) -> rc=0
[app-shell] stub-TSD seed: key=3 state=0x... rc=0                         ; static, INVALID
```
- Without `SOBER_STUB_TSD_SEED`, guest `pthread_getspecific(3)` resolves through the NDK stub into the glibc key namespace and returns NULL, so `0x2ea6520` allocates a fresh TLS array per call → nesting counter mismatch (`tls-2a0de60-r70-correction.md:90-99`).

## PROVEN — `[0x72a3058]` after nativeInitClientSettings (from surviving ledger/quotes)

`analysis/FABRICATION-LEDGER.md:146-157` (verbatim):
```
breakpoints (`0x2359277` SETTINGS_ENTER, `0x23115c8` RSI_ORIGIN, watch on `0x72a3058`) ...
- **PROVEN (corroborated):** `GLOBAL[0x72a3058] = 0x5555574528f0` and
  `ctx = [G+0x18] = 0x5555558f8ac0` — both match `stop.err` boundary-pre log
  `fflag_ctx=0x5555574528f0 fflag_obj=0x5555558f8ac0` (independent log source).
- **PROVEN (source-verified):** last writer ... harness's own
  `native_jni.c:1416-1429` cached-jmethodID seeding over the **inclusive** range
  `0x72a2f48..0x72a3058`; final iteration writes `sr_find_method_object` named
  `cachedMethod@%lx` into `0x72a3058`, clobbering the guest FFlag context global. `[ctx+0] =
  0x654d646568636163` ("cachedMe") ...
```
- Ledger #13: that seeder clobber was a **host defect**, removed by default (skip `0x72a3058`); Run A1 logged `[SEEDER_WRITE]` slots up to `0x72a3050` with **no** `0x72a3058` write — exclusion works (ledger:186).
- Genuine guest writer: sole write at `0x230cce4` (helper `0x230cc82`), value `&0x72a31c8` singleton, read at `0x227b204`, virtual call `*0x8(%rax)` `0x227b213`; **NOT reached when JNI_OnLoad is skipped** (ledger:208-209, static-jni3 #609).
- Run C dumps: `[0x72a3058]` NULL at entry → guest singleton after (lazy-init PROVEN); `ctx+0` genuine guest code pointer, not host "cachedMe" (ledger:215).
- Surviving raw boundary line (`/tmp/opencode/n9-harness/raw_rund_fflag.txt:2`): `3705:[app-shell] AVD boundary pre: arg0_len=1533221 ... D08=0 latch=0 fflag_ctx=0x7fffefedf1c8 fflag_obj=0x555555c50880` — ctx/obj sampled *before* the settings native in that run.

## RETRACTED / UNPROVEN

- RETRACTED: treating the harness `cachedMethod@72a3058` value as a genuine guest singleton — ledger #13 defect removal; default path now excludes it.
- UNPROVEN (source gone, cannot re-verify): `/tmp/opencode/w22-static-jni3/engine_full.dis`, `probe.out`/`probe_fast.out` (zero probe records — ledger:146-149), `w19-harness/report.md`, `stop.err`. Claims above are **inherited from surviving ledger/analysis quotes**, not re-measured here.
- NOT DONE: no runs/device/captures/guest writes/gate/seeder/probe bridge/fake returns/path remaps; no captures/tokens in git.
