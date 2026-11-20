# Decision 008 — Android packed RELA must be applied before bootstrap

Status: accepted (lead). New hard requirement discovered by worker-3.

## Finding

`libroblox.so` (AppImage build) carries Android packed relocations:

- `DT_ANDROID_RELA` = dynamic tag `0x60000011`, pointer `0x15e00`
- `DT_ANDROID_RELASZ` = dynamic tag `0x60000012`, size `0x1eb1f2` bytes

glibc does **not** process these tags. The file's `DT_INIT_ARRAY` at RVA
`0x7220e58` (3618 × u64) is all zeros in the file and is populated only by
packed relocations. Without applying them, `ctor[0]` is a NULL call and the
guest crashes with `signal=11 si_addr=0x0 rip=0x0` — this is the observed
root cause, replacing the earlier vague "init_array crash".

## Ruling / ownership

- **worker-3** implements native packed-RELA application in `src/linux/`
  (loader/harness side) before any guest code runs. This is native ELF
  relocation processing, not a translation layer.
- **worker-1**'s bootstrap patcher runs **after** packed relocations are
  applied and **before** constructors are driven. No need to duplicate
  relocation work in `src/compat/bionic_bootstrap/`.
- **worker-2** confirms the packed-RELA decode (format, relocation types,
  target ranges) in `analysis/bootstrap/bootstrap-map.md` with evidence.

## Required order (single canonical sequence)

1. Map guest with constructor suppression (decision 007).
2. Apply Android packed RELA from `0x15e00`, `0x1eb1f2` bytes (worker-3).
3. Apply native Bionic bootstrap seeds/bridges (worker-1).
4. Drive `init_array` ranges `[2,3)` and `[5,3618)` at `0x7220e58` (worker-3).
5. Emit `guest init complete`.

## Confirmed decode (worker-2, REA evidence)

Format is **AOSP APS2**: magic `APS2`, then an SLEB128 stream, per bionic
`linker_reloc_iterators.h::for_all_packed_relocs`.

- `num_relocs` = **565390** (SLEB128), decoded exactly, consuming all
  `0x1eb1f2` bytes.
- `r_offset` and `r_addend` are **running accumulators** with size_t wrap.
- Per group: `group_size`, `group_flags`, optional `group_r_offset_delta`
  (flag 2), `group_r_info` (flag 1), `group_r_addend` (flags 4|8).
- Flags: 1 = GROUPED_BY_INFO, 2 = GROUPED_BY_OFFSET_DELTA,
  4 = GROUPED_BY_ADDEND, 8 = HAS_ADDEND.
- Per relocation: add SLEB delta to `r_offset` (unless flag 2), to `r_info`
  (unless flag 1), to `r_addend` (if flag 8).
- Type histogram: 565308 `R_X86_64_RELATIVE` (8), 60 `R_X86_64_GLOB_DAT` (6),
  22 `R_X86_64_64` (1).
- Exactly **3618** `R_X86_64_RELATIVE` relocations target
  `0x7220e58..0x7227f60`; their addends are the real init_array pointers.
- Verified: `ctor[2]=0x1dbe2b1`, `[3]=0x1db9a40`, `[4]=0x1dba6c4`,
  `[5]=0x1dba834` — matching the profile anchors.
  `ctor[0]=0x2e62390`, `ctor[1]=0x1dba8f3`.
- REA evidence: identity `ev_af5de532...`, sections `ev_34eea014...`, full
  objdump disassembly `ev_d838d804...`.

**All 565390 relocations must be applied**, including the 82 non-relative GOT
ones — not only the 3618 init_array ones.

Do not mix RVAs with the Flatpak build (`791c3f9e...`).
