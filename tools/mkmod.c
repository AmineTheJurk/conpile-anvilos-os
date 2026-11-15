/*
 * mkmod - Host tool to convert linked x86-32 module ELFs into AnvilOS Loadable
 * Kernel Modules (.mod / AKM1) and generate /lib/modules/modules.idx (AMIX).
 *
 * Usage:
 *   mkmod build <in.elf> <out.mod>
 *   mkmod index <out.idx> <mod1.mod> <mod2.mod> ...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../include/kmod.h"

#define EI_NIDENT 16
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_NOBITS   8
#define SHT_REL      9

#define R_386_32     1
#define R_386_PC32   2

typedef struct {
    unsigned char e_ident[EI_NIDENT];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t sh_name;
    uint32_t sh_type;
    uint32_t sh_flags;
    uint32_t sh_addr;
    uint32_t sh_offset;
    uint32_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint32_t sh_addralign;
    uint32_t sh_entsize;
} Elf32_Shdr;

typedef struct {
    uint32_t st_name;
    uint32_t st_value;
    uint32_t st_size;
    uint8_t  st_info;
    uint8_t  st_other;
    uint16_t st_shndx;
} Elf32_Sym;

typedef struct {
    uint32_t r_offset;
    uint32_t r_info;
} Elf32_Rel;

static uint8_t* read_entire_file(const char* path, size_t* out_sz) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "mkmod: cannot open %s\n", path); exit(1); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t* buf = (uint8_t*)malloc((size_t)(sz > 0 ? sz : 1));
    if (!buf) { fprintf(stderr, "mkmod: OOM\n"); exit(1); }
    if (sz > 0 && fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        fprintf(stderr, "mkmod: read error\n"); exit(1);
    }
    fclose(f);
    *out_sz = (size_t)sz;
    return buf;
}

static int cmd_build(const char* in_elf, const char* out_mod) {
    size_t sz = 0;
    uint8_t* elf = read_entire_file(in_elf, &sz);
    if (sz < sizeof(Elf32_Ehdr)) { fprintf(stderr, "mkmod: invalid ELF\n"); return 1; }
    const Elf32_Ehdr* eh = (const Elf32_Ehdr*)elf;
    if (eh->e_ident[0] != 0x7F || eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L'  || eh->e_ident[3] != 'F') {
        fprintf(stderr, "mkmod: bad ELF magic in %s\n", in_elf);
        return 1;
    }

    const Elf32_Shdr* shdrs = (const Elf32_Shdr*)(elf + eh->e_shoff);
    const char* shstr = (const char*)(elf + shdrs[eh->e_shstrndx].sh_offset);

    const Elf32_Shdr* s_meta = NULL;
    const Elf32_Shdr* s_ids  = NULL;
    const Elf32_Shdr* s_bss  = NULL;
    const Elf32_Shdr* s_sym  = NULL;
    uint32_t payload_end = 0;

    for (uint16_t i = 0; i < eh->e_shnum; i++) {
        const char* nm = shstr + shdrs[i].sh_name;
        if (strcmp(nm, ".modmeta") == 0) s_meta = &shdrs[i];
        else if (strcmp(nm, ".modids") == 0) s_ids = &shdrs[i];
        else if (strcmp(nm, ".bss") == 0) s_bss = &shdrs[i];
        else if (shdrs[i].sh_type == SHT_SYMTAB) s_sym = &shdrs[i];
        else if (strcmp(nm, ".text") == 0 || strcmp(nm, ".rodata") == 0 || strcmp(nm, ".data") == 0) {
            uint32_t end = shdrs[i].sh_addr + shdrs[i].sh_size;
            if (end > payload_end) payload_end = end;
        }
    }

    if (!s_meta || s_meta->sh_size < sizeof(mod_meta_raw_t)) {
        fprintf(stderr, "mkmod: %s missing .modmeta\n", in_elf);
        return 1;
    }

    /* Check undefined external symbols */
    if (s_sym && s_sym->sh_link < eh->e_shnum) {
        const char* strtab = (const char*)(elf + shdrs[s_sym->sh_link].sh_offset);
        uint32_t nsym = s_sym->sh_size / sizeof(Elf32_Sym);
        const Elf32_Sym* syms = (const Elf32_Sym*)(elf + s_sym->sh_offset);
        for (uint32_t i = 1; i < nsym; i++) {
            if (syms[i].st_shndx == 0 && syms[i].st_name != 0) {
                const char* sn = strtab + syms[i].st_name;
                if (sn[0]) {
                    fprintf(stderr, "mkmod: ERROR undefined symbol '%s' in %s\n", sn, in_elf);
                    return 1;
                }
            }
        }
    }

    const mod_meta_raw_t* meta = (const mod_meta_raw_t*)(elf + s_meta->sh_offset);
    uint32_t id_count = s_ids ? (s_ids->sh_size / (uint32_t)sizeof(mod_pci_id_t)) : 0;
    uint32_t bss_size = s_bss ? s_bss->sh_size : 0;

    if (payload_end == 0) payload_end = 4;
    uint8_t* payload = (uint8_t*)calloc(1, payload_end);
    for (uint16_t i = 0; i < eh->e_shnum; i++) {
        const char* nm = shstr + shdrs[i].sh_name;
        if ((strcmp(nm, ".text") == 0 || strcmp(nm, ".rodata") == 0 || strcmp(nm, ".data") == 0) &&
            shdrs[i].sh_type != SHT_NOBITS && shdrs[i].sh_size > 0) {
            memcpy(payload + shdrs[i].sh_addr, elf + shdrs[i].sh_offset, shdrs[i].sh_size);
        }
    }

    /* Collect R_386_32 relocations targeting loadable sections */
    uint32_t rel_cap = 256, rel_cnt = 0;
    uint32_t* rels = (uint32_t*)malloc(rel_cap * sizeof(uint32_t));
    for (uint16_t i = 0; i < eh->e_shnum; i++) {
        if (shdrs[i].sh_type == SHT_REL) {
            const char* rname = shstr + shdrs[i].sh_name;
            if (strcmp(rname, ".rel.text") != 0 &&
                strcmp(rname, ".rel.rodata") != 0 &&
                strcmp(rname, ".rel.data") != 0) {
                continue;
            }
            uint32_t nrel = shdrs[i].sh_size / sizeof(Elf32_Rel);
            const Elf32_Rel* rtab = (const Elf32_Rel*)(elf + shdrs[i].sh_offset);
            for (uint32_t j = 0; j < nrel; j++) {
                uint8_t rtype = (uint8_t)(rtab[j].r_info & 0xFF);
                if (rtype == R_386_32) {
                    if (rel_cnt == rel_cap) {
                        rel_cap *= 2;
                        rels = (uint32_t*)realloc(rels, rel_cap * sizeof(uint32_t));
                    }
                    rels[rel_cnt++] = rtab[j].r_offset;
                }
            }
        }
    }

    akm_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic        = AKM_MAGIC;
    hdr.version      = 1;
    hdr.drv_class    = meta->drv_class;
    hdr.entry_off    = eh->e_entry;
    hdr.id_count     = id_count;
    hdr.reloc_count  = rel_cnt;
    hdr.payload_size = payload_end;
    hdr.bss_size     = bss_size;
    hdr.match_class  = meta->match_class;
    hdr.match_sub    = meta->match_sub;
    memcpy(hdr.name, meta->name, sizeof(hdr.name));
    hdr.name[sizeof(hdr.name) - 1] = '\0';
    memcpy(hdr.origin, meta->origin, sizeof(hdr.origin));
    hdr.origin[sizeof(hdr.origin) - 1] = '\0';

    FILE* out = fopen(out_mod, "wb");
    if (!out) { fprintf(stderr, "mkmod: cannot write %s\n", out_mod); return 1; }
    fwrite(&hdr, sizeof(hdr), 1, out);
    if (id_count > 0) fwrite(elf + s_ids->sh_offset, sizeof(mod_pci_id_t), id_count, out);
    if (rel_cnt > 0)  fwrite(rels, sizeof(uint32_t), rel_cnt, out);
    fwrite(payload, 1, payload_end, out);
    fclose(out);

    free(rels);
    free(payload);
    free(elf);
    return 0;
}

static int cmd_index(const char* out_idx, int nmods, char** mod_paths) {
    amix_mod_entry_t* mods = (amix_mod_entry_t*)calloc((size_t)nmods, sizeof(amix_mod_entry_t));
    uint32_t id_cap = 4096, id_cnt = 0;
    amix_id_entry_t* ids = (amix_id_entry_t*)malloc(id_cap * sizeof(amix_id_entry_t));

    for (int i = 0; i < nmods; i++) {
        size_t sz = 0;
        uint8_t* buf = read_entire_file(mod_paths[i], &sz);
        if (sz < sizeof(akm_header_t)) { fprintf(stderr, "mkmod: bad mod %s\n", mod_paths[i]); return 1; }
        const akm_header_t* hdr = (const akm_header_t*)buf;
        if (hdr->magic != AKM_MAGIC) { fprintf(stderr, "mkmod: bad AKM1 magic %s\n", mod_paths[i]); return 1; }

        memcpy(mods[i].name, hdr->name, sizeof(mods[i].name));
        mods[i].drv_class   = hdr->drv_class;
        mods[i].match_class = hdr->match_class;
        mods[i].match_sub   = hdr->match_sub;

        const mod_pci_id_t* mids = (const mod_pci_id_t*)(buf + sizeof(akm_header_t));
        for (uint32_t j = 0; j < hdr->id_count; j++) {
            if (id_cnt == id_cap) {
                id_cap *= 2;
                ids = (amix_id_entry_t*)realloc(ids, id_cap * sizeof(amix_id_entry_t));
            }
            ids[id_cnt].vendor_id = mids[j].vendor_id;
            ids[id_cnt].device_id = mids[j].device_id;
            ids[id_cnt].mod_index = (uint16_t)i;
            ids[id_cnt].reserved  = 0;
            id_cnt++;
        }
        free(buf);
    }

    amix_header_t ih;
    memset(&ih, 0, sizeof(ih));
    ih.magic     = AMIX_MAGIC;
    ih.version   = 1;
    ih.mod_count = (uint16_t)nmods;
    ih.id_count  = id_cnt;

    FILE* out = fopen(out_idx, "wb");
    if (!out) { fprintf(stderr, "mkmod: cannot write %s\n", out_idx); return 1; }
    fwrite(&ih, sizeof(ih), 1, out);
    fwrite(mods, sizeof(amix_mod_entry_t), (size_t)nmods, out);
    fwrite(ids, sizeof(amix_id_entry_t), id_cnt, out);
    fclose(out);

    printf("[mkmod] Built %s (%u modules, %u OpenBSD PCI IDs indexed)\n",
           out_idx, (unsigned)nmods, (unsigned)id_cnt);
    free(ids);
    free(mods);
    return 0;
}

int main(int argc, char** argv) {
    if (argc >= 4 && strcmp(argv[1], "build") == 0) {
        return cmd_build(argv[2], argv[3]);
    }
    if (argc >= 4 && strcmp(argv[1], "index") == 0) {
        return cmd_index(argv[2], argc - 3, &argv[3]);
    }
    fprintf(stderr, "Usage:\n  %s build <in.elf> <out.mod>\n  %s index <out.idx> <mods...>\n", argv[0], argv[0]);
    return 1;
}
