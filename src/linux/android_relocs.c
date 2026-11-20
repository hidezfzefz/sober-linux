/*
 * android_relocs.c - Native decoder for Android's packed (APS2/SLEB128) RELA
 * relocation stream. See android_relocs.h for the root-cause explanation.
 *
 * Format (lld / bionic linker_reloc_iterators.h):
 *   'APS2' <sleb num_relocations> <sleb initial_r_offset>
 *   repeated groups:
 *     <sleb group_size> <sleb group_flags>
 *     [sleb group_offset_delta]        if GROUPED_BY_OFFSET_DELTA (2)
 *     [sleb group_r_info]              if GROUPED_BY_INFO        (1)
 *     [sleb group_addend_delta]        if GROUPED_BY_ADDEND(4) & HAS_ADDEND(8)
 *     then group_size entries:
 *       [sleb r_offset_delta]          unless GROUPED_BY_OFFSET_DELTA
 *       [sleb r_info]                  unless GROUPED_BY_INFO
 *       [sleb r_addend_delta]          if HAS_ADDEND and not GROUPED_BY_ADDEND
 *
 * Owned by worker-3 (task #377).
 */
#define _GNU_SOURCE
#include "android_relocs.h"

#include <dlfcn.h>
#include <elf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* Android uses non-standard dynamic tags for the packed stream. */
#ifndef DT_ANDROID_RELA
#define DT_ANDROID_RELA   0x60000011
#endif
#ifndef DT_ANDROID_RELASZ
#define DT_ANDROID_RELASZ 0x60000012
#endif

#define GROUPED_BY_INFO        1u
#define GROUPED_BY_OFFSET_DELTA 2u
#define GROUPED_BY_ADDEND      4u
#define GROUP_HAS_ADDEND       8u

/* ------------------------------------------------------------------ */
/* ELF parsing                                                         */
/* ------------------------------------------------------------------ */

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

int sober_guest_elf_parse(const void *bytes, size_t size,
                          struct sober_guest_elf *out)
{
    if (!bytes || !out || size < sizeof(Elf64_Ehdr)) return -1;
    memset(out, 0, sizeof(*out));

    const uint8_t *b = bytes;
    if (b[EI_MAG0] != ELFMAG0 || b[EI_MAG1] != ELFMAG1 ||
        b[EI_MAG2] != ELFMAG2 || b[EI_MAG3] != ELFMAG3 ||
        b[EI_CLASS] != ELFCLASS64 || b[EI_DATA] != ELFDATA2LSB) return -1;

    uint64_t phoff = rd64(b + 0x20);
    uint16_t phentsize = rd16(b + 0x36);
    uint16_t phnum = rd16(b + 0x38);
    if (phentsize < sizeof(Elf64_Phdr) || phnum == 0 ||
        phoff + (uint64_t)phnum * phentsize > size) return -1;

    uint64_t dyn_off = 0, dyn_size = 0;
    uint64_t relro_vaddr = 0, relro_memsz = 0;
    int have_dynamic = 0, have_relro = 0;

    for (uint16_t i = 0; i < phnum; ++i) {
        const uint8_t *ph = b + phoff + (uint64_t)i * phentsize;
        uint32_t type = rd32(ph);
        if (type == PT_DYNAMIC) {
            dyn_off = rd64(ph + 8);
            dyn_size = rd64(ph + 32);
            have_dynamic = 1;
        } else if (type == 0x6474e552u /* PT_GNU_RELRO */) {
            relro_vaddr = rd64(ph + 16);
            relro_memsz = rd64(ph + 40);
            have_relro = 1;
        }
    }
    if (!have_dynamic || dyn_off + dyn_size > size) return -1;

    /* Dynamic entries: d_tag (8) + d_val (8). Locate the tags we need. */
    int have_rela = 0, have_arraysz = 0;
    for (uint64_t o = 0; o + 16 <= dyn_size; o += 16) {
        const uint8_t *d = b + dyn_off + o;
        uint64_t tag = rd64(d);
        uint64_t val = rd64(d + 8);
        switch (tag) {
        case 0: goto done_dynamic;              /* DT_NULL */
        case DT_SYMTAB:         out->symtab_rva = (uintptr_t)val; break;
        case DT_STRTAB:         out->strtab_rva = (uintptr_t)val; break;
        case DT_SYMENT:         out->syment = (size_t)val; break;
        case DT_INIT_ARRAY:     out->init_array_rva = (uintptr_t)val; break;
        case DT_INIT_ARRAYSZ:
            out->init_array_size = (size_t)val;
            out->init_arraysz_file_off = dyn_off + o + 8;
            have_arraysz = 1;
            break;
        case DT_ANDROID_RELA:
            out->android_rela_rva = (uintptr_t)val;
            have_rela = 1;
            break;
        case DT_ANDROID_RELASZ:
            out->android_rela_size = (size_t)val;
            break;
        default: break;
        }
    }
done_dynamic:
    if (!have_rela || out->android_rela_size == 0) return -1;
    if (out->syment == 0) out->syment = sizeof(Elf64_Sym);
    if (have_relro) {
        out->relro_rva = (uintptr_t)relro_vaddr;
        out->relro_memsz = (size_t)relro_memsz;
    }
    if (have_arraysz && out->init_array_size)
        out->init_array_count = out->init_array_size / sizeof(uintptr_t);
    return 0;
}

void sober_guest_elf_describe(const struct sober_guest_elf *elf)
{
    fprintf(stderr,
        "[relocs] DT_ANDROID_RELA rva=%#lx size=%#zx | DT_INIT_ARRAY rva=%#lx "
        "entries=%zu | symtab=%#lx strtab=%#lx relro=%#lx/%#zx\n",
        (unsigned long)elf->android_rela_rva, elf->android_rela_size,
        (unsigned long)elf->init_array_rva, elf->init_array_count,
        (unsigned long)elf->symtab_rva, (unsigned long)elf->strtab_rva,
        (unsigned long)elf->relro_rva, elf->relro_memsz);
}

/* ------------------------------------------------------------------ */
/* SLEB128                                                             */
/* ------------------------------------------------------------------ */

static int sleb_get(const uint8_t **cursor, const uint8_t *end, int64_t *out)
{
    uint64_t result = 0;
    unsigned shift = 0;
    uint8_t byte;
    do {
        if (*cursor >= end) return -1;
        byte = *(*cursor)++;
        if (shift < 64) result |= (uint64_t)(byte & 0x7fu) << shift;
        shift += 7;
    } while (byte & 0x80u);
    if (shift < 64 && (byte & 0x40u)) result |= ~(uint64_t)0 << shift;
    *out = (int64_t)result;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Relocation application                                              */
/* ------------------------------------------------------------------ */

static const char *symbol_name(const struct sober_guest_elf *elf,
                               uint64_t sym_index)
{
    if (!elf->symtab_rva || !elf->strtab_rva || !elf->syment) return NULL;
    const uint8_t *sym = (const uint8_t *)(elf->base + elf->symtab_rva +
                                           sym_index * elf->syment);
    uint32_t st_name = (uint32_t)rd32(sym);
    return (const char *)(elf->base + elf->strtab_rva + st_name);
}

int sober_android_relocs_apply(const struct sober_guest_elf *elf,
                               size_t *out_applied, size_t *out_relative,
                               size_t *out_symbolic, size_t *out_unknown,
                               size_t *out_unresolved)
{
    size_t applied = 0, relative = 0, symbolic = 0, unknown = 0, unresolved = 0;
    if (!elf || !elf->base || !elf->android_rela_rva ||
        !elf->android_rela_size) return -1;

    const uint8_t *start = (const uint8_t *)(elf->base + elf->android_rela_rva);
    const uint8_t *end = start + elf->android_rela_size;
    const uint8_t *p = start;

    /* Skip the 4-byte 'APS2' magic before the SLEB128 header. */
    if (p + 4 > end || memcmp(p, "APS2", 4) != 0) {
        fprintf(stderr, "[relocs] missing APS2 magic\n");
        return -2;
    }
    p += 4;

    int64_t num_relocs = 0, r_offset = 0;
    if (sleb_get(&p, end, &num_relocs) || sleb_get(&p, end, &r_offset) ||
        num_relocs <= 0) {
        fprintf(stderr, "[relocs] header decode failed: num=%lld offset=%lld\n",
                (long long)num_relocs, (long long)r_offset);
        return -2;
    }

    /* The packed stream is DELTA encoded but glibc has already protected the
     * RELRO window. Open it for writing for the duration of the pass. */
    long page = sysconf(_SC_PAGESIZE);
    if (page <= 0) page = 4096;
    uintptr_t prot_start = 0, prot_end = 0;
    if (elf->relro_rva && elf->relro_memsz) {
        prot_start = (elf->base + elf->relro_rva) & ~(uintptr_t)(page - 1);
        prot_end = (elf->base + elf->relro_rva + elf->relro_memsz +
                    (uintptr_t)(page - 1)) & ~(uintptr_t)(page - 1);
        if (mprotect((void *)prot_start, prot_end - prot_start,
                     PROT_READ | PROT_WRITE) != 0) {
            fprintf(stderr, "[relocs] WARNING: mprotect RELRO RW failed\n");
        }
    }

    int64_t r_info = 0, r_addend = 0;
    int64_t idx = 0;
    int sleb_rc = 0;
#define GETS(v) do { sleb_rc = sleb_get(&p, end, &(v)); if (sleb_rc) goto malformed; } while (0)
    while (idx < num_relocs) {
        int64_t group_size = 0, flags = 0;
        GETS(group_size); GETS(flags);
        if (group_size <= 0) { sleb_rc = 9; goto malformed; }

        int64_t group_delta = 0;
        if ((uint32_t)flags & GROUPED_BY_OFFSET_DELTA) GETS(group_delta);
        if ((uint32_t)flags & GROUPED_BY_INFO) GETS(r_info);
        unsigned addend_mode = (uint32_t)flags &
                               (GROUP_HAS_ADDEND | GROUPED_BY_ADDEND);
        if (addend_mode == (GROUP_HAS_ADDEND | GROUPED_BY_ADDEND)) {
            int64_t delta; GETS(delta); r_addend += delta;
        } else if (addend_mode == 0) {
            r_addend = 0;
        }

        for (int64_t i = 0; i < group_size; ++i) {
            if ((uint32_t)flags & GROUPED_BY_OFFSET_DELTA)
                r_offset += group_delta;
            else {
                int64_t delta; GETS(delta); r_offset += delta;
            }
            if (!((uint32_t)flags & GROUPED_BY_INFO)) GETS(r_info);
            if (addend_mode == GROUP_HAS_ADDEND) {
                int64_t delta; GETS(delta); r_addend += delta;
            }

            uint64_t *target = (uint64_t *)(elf->base + (uintptr_t)r_offset);
            uint32_t type = (uint32_t)(uint64_t)r_info;      /* low 32 bits */
            uint64_t symidx = (uint64_t)r_info >> 32;

            if (type == 8 /* R_X86_64_RELATIVE */) {
                *target = (uint64_t)(elf->base + (uintptr_t)r_addend);
                ++relative;
            } else if (type == 6 /* GLOB_DAT */ || type == 1 /* 64 */) {
                const char *name = symidx ? symbol_name(elf, symidx) : NULL;
                void *value = name ? dlsym(RTLD_DEFAULT, name) : NULL;
                if (!value) {
                    if (name && (strstr(name, "gcov") || strstr(name, "ZSTD_trace"))) {
                        /* optional profiling hooks: benign if absent */
                    } else {
                        fprintf(stderr, "[relocs] unresolved symbol '%s' "
                                        "(type=%u rva=%#lx)\n",
                                name ? name : "?", type,
                                (unsigned long)r_offset);
                    }
                    *target = 0;
                    ++unresolved;
                } else {
                    *target = (uint64_t)(uintptr_t)value +
                              (type == 1 ? (uint64_t)r_addend : 0);
                    ++symbolic;
                }
            } else {
                /* Unknown relocation type: leave the slot as-is. */
                if (unknown < 12)
                    fprintf(stderr, "[relocs] unknown type=%u rva=%#lx\n",
                            type, (unsigned long)r_offset);
                ++unknown;
            }
            ++applied;
            ++idx;              /* idx counts RELOCATIONS, not groups */
        }
    }

    /* Task #475: GLOB_DAT resolution via dlsym(RTLD_DEFAULT) binds the guest's
     * pthread_getspecific/setspecific PLTs to glibc, bypassing our Bionic TSD
     * shim (decision 013). Opt-in rebind just those two GOT slots to the
     * versioned LIBC stubs; glibc stack canaries are untouched. */
    if (getenv("SOBER_BIONIC_TSD_GOT")) {
        void *stub = dlopen("libc.so", RTLD_NOLOAD | RTLD_LAZY);
        if (stub) {
            void *get = dlvsym(stub, "pthread_getspecific", "LIBC");
            void *set = dlvsym(stub, "pthread_setspecific", "LIBC");
            uint64_t *g_get = (uint64_t *)(elf->base + 0x72298b0u);
            uint64_t *g_set = (uint64_t *)(elf->base + 0x72295c8u);
            if (get) *g_get = (uint64_t)(uintptr_t)get;
            if (set) *g_set = (uint64_t)(uintptr_t)set;
            fprintf(stderr,
                    "[relocs] Bionic TSD GOT rebind: get=%p set=%p "
                    "(slots 0x72298b0/0x72295c8)\n", get, set);
        }
    }

    if (prot_start && prot_end) {
        mprotect((void *)prot_start, prot_end - prot_start, PROT_READ);
    }

    if (out_applied) *out_applied = applied;
    if (out_relative) *out_relative = relative;
    if (out_symbolic) *out_symbolic = symbolic;
    if (out_unknown) *out_unknown = unknown;
    if (out_unresolved) *out_unresolved = unresolved;
    fprintf(stderr,
        "[relocs] applied=%zu (relative=%zu symbolic=%zu unknown=%zu "
        "unresolved=%zu) consumed=%td/%zu\n",
        applied, relative, symbolic, unknown, unresolved,
        p - start, elf->android_rela_size);
    return 0;

malformed:
    fprintf(stderr, "[relocs] MALFORMED packed stream at offset %td "
                    "(reloc %lld/%lld, decode=%d)\n",
            p - start, (long long)idx, (long long)num_relocs, sleb_rc);
    return -3;
}
