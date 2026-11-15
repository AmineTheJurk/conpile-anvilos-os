/*
 * mkanvilfs - Host tool (Linux & Windows) to build a populated AnvilFS filesystem
 * image and pack the complete bootable raw USB image (anvilos_usb.bin) for
 * PassMark ImageUSB / dd / QEMU.
 *
 * Usage:
 *   mkanvilfs <mbr.bin> <stage2.bin> <kernel.bin> <out_usb.bin> <file1:guest_path> ...
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "../include/anvilfs.h"

#define TOTAL_USB_SECTORS 16384 /* 8 MiB raw USB image */

static uint8_t disk_img[TOTAL_USB_SECTORS * ANVILFS_BLOCK_SIZE];
static uint8_t* part = &disk_img[ANVILFS_PART_LBA * ANVILFS_BLOCK_SIZE];

static void set_blk_used(uint32_t blk) {
    uint32_t byte = blk / 8;
    uint32_t bit  = blk % 8;
    part[ANVILFS_BITMAP_START * ANVILFS_BLOCK_SIZE + byte] |= (uint8_t)(1U << bit);
}

static uint32_t host_alloc_block(anvilfs_superblock_t* sb) {
    for (uint32_t blk = ANVILFS_DATA_START; blk < sb->total_blocks; blk++) {
        uint32_t byte = blk / 8;
        uint32_t bit  = blk % 8;
        if (!(part[ANVILFS_BITMAP_START * ANVILFS_BLOCK_SIZE + byte] & (1U << bit))) {
            set_blk_used(blk);
            sb->free_blocks--;
            memset(&part[blk * ANVILFS_BLOCK_SIZE], 0, ANVILFS_BLOCK_SIZE);
            return blk;
        }
    }
    return 0;
}

static anvilfs_inode_t* get_inode(uint32_t ino) {
    return (anvilfs_inode_t*)(part + ANVILFS_INODE_START * ANVILFS_BLOCK_SIZE + ino * sizeof(anvilfs_inode_t));
}

static uint32_t host_alloc_inode(anvilfs_superblock_t* sb) {
    for (uint32_t i = 1; i < sb->inode_count; i++) {
        anvilfs_inode_t* in = get_inode(i);
        if (in->type == ANVILFS_TYPE_FREE) {
            sb->free_inodes--;
            memset(in, 0, sizeof(*in));
            return i;
        }
    }
    return 0;
}

static void add_dirent(anvilfs_superblock_t* sb, uint32_t dir_ino, uint32_t child_ino, const char* name) {
    anvilfs_inode_t* dir = get_inode(dir_ino);
    uint32_t idx = dir->size / sizeof(anvilfs_dirent_t);
    uint32_t b_idx = idx / 16;
    uint32_t slot  = idx % 16;
    uint32_t blk   = 0;
    if (b_idx < ANVILFS_DIRECT_BLOCKS) {
        if (!dir->direct[b_idx]) dir->direct[b_idx] = host_alloc_block(sb);
        blk = dir->direct[b_idx];
    } else {
        uint32_t ind_idx = b_idx - ANVILFS_DIRECT_BLOCKS;
        if (ind_idx >= 128) return;
        if (!dir->indirect) dir->indirect = host_alloc_block(sb);
        uint32_t* ptrs = (uint32_t*)(part + dir->indirect * ANVILFS_BLOCK_SIZE);
        if (!ptrs[ind_idx]) ptrs[ind_idx] = host_alloc_block(sb);
        blk = ptrs[ind_idx];
    }
    anvilfs_dirent_t* ents = (anvilfs_dirent_t*)(part + blk * ANVILFS_BLOCK_SIZE);
    ents[slot].inode = child_ino;
    strncpy(ents[slot].name, name, ANVILFS_NAME_MAX - 1);
    ents[slot].name[ANVILFS_NAME_MAX - 1] = '\0';
    dir->size += sizeof(anvilfs_dirent_t);
}

static uint32_t host_mkdir(anvilfs_superblock_t* sb, uint32_t parent_ino, const char* name) {
    uint32_t ino = host_alloc_inode(sb);
    anvilfs_inode_t* in = get_inode(ino);
    in->type  = ANVILFS_TYPE_DIR;
    in->perms = 0755;
    in->links = 2;
    add_dirent(sb, ino, ino, ".");
    add_dirent(sb, ino, parent_ino, "..");
    add_dirent(sb, parent_ino, ino, name);
    return ino;
}

static uint32_t lookup_or_create_dir(anvilfs_superblock_t* sb, const char* path) {
    if (strcmp(path, "/") == 0) return sb->root_inode;
    char tmp[256];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';
    uint32_t cur = sb->root_inode;
    char* tok = strtok(tmp, "/");
    while (tok) {
        anvilfs_inode_t* dir = get_inode(cur);
        uint32_t n = dir->size / sizeof(anvilfs_dirent_t);
        uint32_t found = 0;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t b = dir->direct[i / 16];
            anvilfs_dirent_t* ents = (anvilfs_dirent_t*)(part + b * ANVILFS_BLOCK_SIZE);
            if (ents[i % 16].inode && strcmp(ents[i % 16].name, tok) == 0) {
                found = ents[i % 16].inode;
                break;
            }
        }
        if (!found) found = host_mkdir(sb, cur, tok);
        cur = found;
        tok = strtok(NULL, "/");
    }
    return cur;
}

static void add_file_bytes(anvilfs_superblock_t* sb, const char* guest_path, const uint8_t* data, uint32_t len, uint16_t perms) {
    char dir_path[256];
    strncpy(dir_path, guest_path, sizeof(dir_path) - 1);
    dir_path[sizeof(dir_path) - 1] = '\0';
    char* slash = strrchr(dir_path, '/');
    const char* fname = slash ? slash + 1 : guest_path;
    if (slash) {
        if (slash == dir_path) strcpy(dir_path, "/");
        else *slash = '\0';
    } else {
        strcpy(dir_path, "/");
    }

    uint32_t dir_ino = lookup_or_create_dir(sb, dir_path);
    uint32_t ino = host_alloc_inode(sb);
    anvilfs_inode_t* in = get_inode(ino);
    in->type  = ANVILFS_TYPE_FILE;
    in->perms = perms;
    in->links = 1;
    in->size  = len;

    uint32_t blocks_needed = (len + ANVILFS_BLOCK_SIZE - 1) / ANVILFS_BLOCK_SIZE;
    uint32_t ind_blk = 0;
    uint32_t* ind_ptrs = NULL;

    for (uint32_t b = 0; b < blocks_needed; b++) {
        uint32_t blk = host_alloc_block(sb);
        if (b < ANVILFS_DIRECT_BLOCKS) {
            in->direct[b] = blk;
        } else {
            if (!ind_blk) {
                ind_blk = host_alloc_block(sb);
                in->indirect = ind_blk;
                ind_ptrs = (uint32_t*)(part + ind_blk * ANVILFS_BLOCK_SIZE);
            }
            ind_ptrs[b - ANVILFS_DIRECT_BLOCKS] = blk;
        }
        uint32_t off = b * ANVILFS_BLOCK_SIZE;
        uint32_t chunk = len - off;
        if (chunk > ANVILFS_BLOCK_SIZE) chunk = ANVILFS_BLOCK_SIZE;
        memcpy(part + blk * ANVILFS_BLOCK_SIZE, data + off, chunk);
    }
    add_dirent(sb, dir_ino, ino, fname);
}

static void load_into_sector(const char* path, uint32_t lba, uint32_t max_bytes) {
    FILE* f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Failed to open %s\n", path); exit(1); }
    size_t got = fread(disk_img + lba * ANVILFS_BLOCK_SIZE, 1, max_bytes, f);
    fclose(f);
    printf("[mkanvilfs] Placed %s (%u bytes) @ LBA %u\n", path, (unsigned)got, lba);
}

int main(int argc, char** argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <mbr.bin> <stage2.bin> <kernel.bin> <out_usb.bin> [host:guest ...]\n", argv[0]);
        return 1;
    }

    memset(disk_img, 0, sizeof(disk_img));
    load_into_sector(argv[1], 0, 512);
    load_into_sector(argv[2], 1, 8192);
    load_into_sector(argv[3], 17, 131072);

    /* Format AnvilFS partition @ LBA 512 */
    anvilfs_superblock_t* sb = (anvilfs_superblock_t*)part;
    memset(sb, 0, sizeof(*sb));
    sb->magic        = ANVILFS_MAGIC;
    sb->version      = 1;
    sb->block_size   = ANVILFS_BLOCK_SIZE;
    sb->total_blocks = ANVILFS_TOTAL_BLOCKS;
    sb->inode_count  = ANVILFS_MAX_INODES;
    sb->free_blocks  = ANVILFS_TOTAL_BLOCKS - ANVILFS_DATA_START;
    sb->free_inodes  = ANVILFS_MAX_INODES - 1;
    sb->root_inode   = 1;

    for (uint32_t b = 0; b < ANVILFS_DATA_START; b++) set_blk_used(b);

    uint32_t root = host_alloc_inode(sb);
    anvilfs_inode_t* rin = get_inode(root);
    rin->type  = ANVILFS_TYPE_DIR;
    rin->perms = 0755;
    rin->links = 2;
    add_dirent(sb, root, root, ".");
    add_dirent(sb, root, root, "..");

    lookup_or_create_dir(sb, "/bin");
    lookup_or_create_dir(sb, "/dev");
    lookup_or_create_dir(sb, "/etc");
    lookup_or_create_dir(sb, "/lib/modules");
    lookup_or_create_dir(sb, "/home/user/Desktop");
    lookup_or_create_dir(sb, "/tmp");

    const char* motd = "Welcome to AnvilOS 1.0 (Unix-like x86 OS with AnvilFS + ANV1 + OpenBSD Modules)\n"
                       "Flash anvilos_usb.bin to a USB drive using PassMark ImageUSB!\n";
    add_file_bytes(sb, "/etc/motd", (const uint8_t*)motd, (uint32_t)strlen(motd), 0644);

    const char* osrel = "NAME=\"AnvilOS\"\nVERSION=\"1.0.0\"\nARCH=\"i686\"\nFS=\"AnvilFS\"\nEXEC=\"ANV1\"\nKMOD=\"AKM1/OpenBSD\"\n";
    add_file_bytes(sb, "/etc/os-release", (const uint8_t*)osrel, (uint32_t)strlen(osrel), 0644);

    const char* readme = "AnvilOS On-The-Go Quick Guide\n"
                         "=============================\n"
                         "- Desktop Icons: Double-click Terminal, Files, System Info, AnvilPad, or .anv apps.\n"
                         "- Loadable Drivers: /lib/modules/*.mod (OpenBSD GPU, FB, Wi-Fi, Ethernet; loaded on demand!).\n"
                         "- Shell (ash): ls, lsmod, lspci, ifconfig, df, ps, free, run, shutdown, reboot.\n"
                         "- Custom Compiler: Use agcc.exe on Windows (or agcc on Linux) to compile .nc -> .anv!\n";
    add_file_bytes(sb, "/home/user/readme.txt", (const uint8_t*)readme, (uint32_t)strlen(readme), 0644);

    int added_files = 0;
    for (int i = 5; i < argc; i++) {
        if (argv[i][0] == '@') {
            FILE* mf = fopen(argv[i] + 1, "r");
            if (!mf) { fprintf(stderr, "Cannot open manifest %s\n", argv[i] + 1); return 1; }
            char line[512];
            while (fgets(line, sizeof(line), mf)) {
                size_t l = strlen(line);
                while (l > 0 && (line[l - 1] == '\n' || line[l - 1] == '\r' || line[l - 1] == ' ')) {
                    line[--l] = '\0';
                }
                if (l == 0 || line[0] == '#') continue;
                char* colon = strchr(line, ':');
                if (!colon) continue;
                *colon = '\0';
                const char* host_p  = line;
                const char* guest_p = colon + 1;
                FILE* f = fopen(host_p, "rb");
                if (!f) { fprintf(stderr, "Cannot open %s\n", host_p); fclose(mf); return 1; }
                fseek(f, 0, SEEK_END);
                long sz = ftell(f);
                fseek(f, 0, SEEK_SET);
                uint8_t* buf = (uint8_t*)malloc((size_t)(sz > 0 ? sz : 1));
                if (!buf) { fclose(f); fclose(mf); return 1; }
                size_t got = fread(buf, 1, (size_t)sz, f);
                fclose(f);
                add_file_bytes(sb, guest_p, buf, (uint32_t)got, 0755);
                free(buf);
                added_files++;
            }
            fclose(mf);
            continue;
        }
        char spec[512];
        strncpy(spec, argv[i], sizeof(spec) - 1);
        spec[sizeof(spec) - 1] = '\0';
        char* colon = strchr(spec, ':');
        if (!colon) continue;
        *colon = '\0';
        const char* host_p  = spec;
        const char* guest_p = colon + 1;

        FILE* f = fopen(host_p, "rb");
        if (!f) { fprintf(stderr, "Cannot open %s\n", host_p); return 1; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t* buf = (uint8_t*)malloc((size_t)(sz > 0 ? sz : 1));
        if (!buf) return 1;
        size_t got = fread(buf, 1, (size_t)sz, f);
        fclose(f);
        add_file_bytes(sb, guest_p, buf, (uint32_t)got, 0755);
        free(buf);
        added_files++;
        printf("[mkanvilfs] Added %s -> %s (%u bytes)\n", host_p, guest_p, (unsigned)got);
    }
    printf("[mkanvilfs] Packed %d files into AnvilFS (free_blocks=%u/%u, free_inodes=%u/%u)\n",
           added_files, sb->free_blocks, sb->total_blocks, sb->free_inodes, sb->inode_count);

    FILE* out = fopen(argv[4], "wb");
    if (!out) { perror("open out_usb"); return 1; }
    fwrite(disk_img, 1, sizeof(disk_img), out);
    fclose(out);
    printf("[mkanvilfs] Generated bootable USB image %s (%u MB, ImageUSB ready!)\n",
           argv[4], (unsigned)(sizeof(disk_img) / (1024 * 1024)));
    return 0;
}
