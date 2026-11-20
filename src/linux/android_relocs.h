/*
 * android_relocs.h - Native processing of Android packed dynamic relocations.
 *
 * The AppImage libroblox.so (BuildID 204ad049..., version 2.740.931) was linked
 * with `ld.lld --pack-dyn-relocs=android`: its dynamic relocations live in
 * DT_ANDROID_RELA (0x60000011) / DT_ANDROID_RELASZ (0x60000012) as an "APS2"
 * SLEB128 delta stream. glibc does not understand these tags, so a plain
 * dlopen() leaves the 3618-entry DT_INIT_ARRAY at RVA 0x7220e58 zero-filled and
 * the first constructor call is a NULL call (SIGSEGV, rip=0x0).
 *
 * This module decodes the stream natively and writes the same values the
 * Android linker would, using the glibc-resolved base address. It is a loader
 * helper, not a translation layer: no guest code is executed, no emulation.
 *
 * Owned by worker-3 (task #377).
 */
#ifndef SOBER_ANDROID_RELOCS_H
#define SOBER_ANDROID_RELOCS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Parsed, read-only description of the mapped guest ELF. */
struct sober_guest_elf {
    uintptr_t   base;                  /* runtime load bias (dlpi_addr)      */
    uintptr_t   android_rela_rva;      /* DT_ANDROID_RELA  (0x60000011)      */
    size_t      android_rela_size;     /* DT_ANDROID_RELASZ (0x60000012)     */
    uintptr_t   symtab_rva;            /* DT_SYMTAB                          */
    uintptr_t   strtab_rva;            /* DT_STRTAB                          */
    size_t      syment;                /* DT_SYMENT (24 for ELF64)           */
    uintptr_t   relro_rva;             /* PT_GNU_RELRO p_vaddr               */
    size_t      relro_memsz;           /* PT_GNU_RELRO p_memsz               */
    uintptr_t   init_array_rva;        /* DT_INIT_ARRAY                      */
    size_t      init_array_size;       /* DT_INIT_ARRAYSZ (bytes)            */
    size_t      init_array_count;      /* size / sizeof(uintptr_t)           */
    size_t      init_arraysz_file_off; /* file offset of DT_INIT_ARRAYSZ val */
};

/*
 * Parse an ELF64 image already read into `bytes` (the on-disk bytes of the
 * guest). No writes are performed. Returns 0 on success, -1 if the image is
 * not the expected Android/ELF64 layout.
 */
int sober_guest_elf_parse(const void *bytes, size_t size,
                          struct sober_guest_elf *out);

/* Human-readable summary for the harness log. */
void sober_guest_elf_describe(const struct sober_guest_elf *elf);

/*
 * Apply every DT_ANDROID_RELA relocation to the mapped image at elf->base.
 * Returns 0 on success, negative on a malformed stream. Fills the counters
 * (may be NULL) with exact totals for the verification report.
 *
 * A small number of non-relative relocations reference imported symbols; these
 * are resolved with dlsym(RTLD_DEFAULT, name) so the host's NDK stubs and glibc
 * provide the definition, exactly as the guest's DT_NEEDED scope would.
 */
int sober_android_relocs_apply(const struct sober_guest_elf *elf,
                               size_t *out_applied,
                               size_t *out_relative,
                               size_t *out_symbolic,
                               size_t *out_unknown,
                               size_t *out_unresolved);

#ifdef __cplusplus
}
#endif
#endif /* SOBER_ANDROID_RELOCS_H */
