#include "anvilfs.h"
#include "ata.h"
#include "mem.h"
#include "string.h"
#include "timer.h"
#include "serial.h"

static bool     disk_backed = false;
static uint8_t* ram_disk    = NULL;
static uint32_t ram_blocks  = 0;
static anvilfs_superblock_t sb;

static bool blk_read(uint32_t block_idx, void* buf) {
    if (disk_backed) {
        return ata_read_sectors(ANVILFS_PART_LBA + block_idx, 1, buf);
    }
    if (!ram_disk || block_idx >= ram_blocks) return false;
    memcpy(buf, ram_disk + block_idx * ANVILFS_BLOCK_SIZE, ANVILFS_BLOCK_SIZE);
    return true;
}

static bool blk_write(uint32_t block_idx, const void* buf) {
    if (disk_backed) {
        return ata_write_sectors(ANVILFS_PART_LBA + block_idx, 1, buf);
    }
    if (!ram_disk || block_idx >= ram_blocks) return false;
    memcpy(ram_disk + block_idx * ANVILFS_BLOCK_SIZE, buf, ANVILFS_BLOCK_SIZE);
    return true;
}

static bool read_inode(uint32_t ino, anvilfs_inode_t* out) {
    if (ino >= ANVILFS_MAX_INODES) return false;
    uint32_t blk = ANVILFS_INODE_START + (ino / 8);
    uint32_t off = (ino % 8) * sizeof(anvilfs_inode_t);
    uint8_t sec[ANVILFS_BLOCK_SIZE];
    if (!blk_read(blk, sec)) return false;
    memcpy(out, sec + off, sizeof(anvilfs_inode_t));
    return true;
}

static bool write_inode(uint32_t ino, const anvilfs_inode_t* in) {
    if (ino >= ANVILFS_MAX_INODES) return false;
    uint32_t blk = ANVILFS_INODE_START + (ino / 8);
    uint32_t off = (ino % 8) * sizeof(anvilfs_inode_t);
    uint8_t sec[ANVILFS_BLOCK_SIZE];
    if (!blk_read(blk, sec)) return false;
    memcpy(sec + off, in, sizeof(anvilfs_inode_t));
    return blk_write(blk, sec);
}

static uint32_t alloc_block(void) {
    uint8_t bm[ANVILFS_BLOCK_SIZE];
    uint32_t max_b = disk_backed ? sb.total_blocks : ram_blocks;
    for (uint32_t b_sec = 0; b_sec < ANVILFS_BITMAP_BLOCKS; b_sec++) {
        if (!blk_read(ANVILFS_BITMAP_START + b_sec, bm)) return 0;
        for (uint32_t byte = 0; byte < ANVILFS_BLOCK_SIZE; byte++) {
            if (bm[byte] != 0xFF) {
                for (uint32_t bit = 0; bit < 8; bit++) {
                    if (!(bm[byte] & (1U << bit))) {
                        uint32_t blk = b_sec * ANVILFS_BLOCK_SIZE * 8 + byte * 8 + bit;
                        if (blk < ANVILFS_DATA_START || blk >= max_b) continue;
                        bm[byte] |= (uint8_t)(1U << bit);
                        blk_write(ANVILFS_BITMAP_START + b_sec, bm);
                        if (sb.free_blocks > 0) sb.free_blocks--;
                        blk_write(0, &sb);
                        uint8_t z[ANVILFS_BLOCK_SIZE];
                        memset(z, 0, sizeof(z));
                        blk_write(blk, z);
                        return blk;
                    }
                }
            }
        }
    }
    return 0;
}

static void free_block(uint32_t blk) {
    if (blk < ANVILFS_DATA_START || blk >= sb.total_blocks) return;
    uint32_t b_sec = blk / (ANVILFS_BLOCK_SIZE * 8);
    uint32_t rem   = blk % (ANVILFS_BLOCK_SIZE * 8);
    uint32_t byte  = rem / 8;
    uint32_t bit   = rem % 8;
    uint8_t bm[ANVILFS_BLOCK_SIZE];
    if (!blk_read(ANVILFS_BITMAP_START + b_sec, bm)) return;
    if (bm[byte] & (1U << bit)) {
        bm[byte] &= (uint8_t)~(1U << bit);
        blk_write(ANVILFS_BITMAP_START + b_sec, bm);
        sb.free_blocks++;
        blk_write(0, &sb);
    }
}

static int alloc_inode(void) {
    anvilfs_inode_t in;
    for (uint32_t i = 1; i < ANVILFS_MAX_INODES; i++) {
        if (read_inode(i, &in) && in.type == ANVILFS_TYPE_FREE) {
            if (sb.free_inodes > 0) sb.free_inodes--;
            blk_write(0, &sb);
            return (int)i;
        }
    }
    return -1;
}

static void free_inode_blocks(anvilfs_inode_t* in) {
    for (int i = 0; i < ANVILFS_DIRECT_BLOCKS; i++) {
        if (in->direct[i]) {
            free_block(in->direct[i]);
            in->direct[i] = 0;
        }
    }
    if (in->indirect) {
        uint32_t ptrs[128];
        if (blk_read(in->indirect, ptrs)) {
            for (int i = 0; i < 128; i++) {
                if (ptrs[i]) free_block(ptrs[i]);
            }
        }
        free_block(in->indirect);
        in->indirect = 0;
    }
}

static uint32_t get_inode_block(const anvilfs_inode_t* in, uint32_t idx) {
    if (idx < ANVILFS_DIRECT_BLOCKS) return in->direct[idx];
    uint32_t ind_idx = idx - ANVILFS_DIRECT_BLOCKS;
    if (ind_idx >= 128 || !in->indirect) return 0;
    uint32_t ptrs[128];
    if (!blk_read(in->indirect, ptrs)) return 0;
    return ptrs[ind_idx];
}

static bool set_inode_block(anvilfs_inode_t* in, uint32_t idx, uint32_t blk) {
    if (idx < ANVILFS_DIRECT_BLOCKS) {
        in->direct[idx] = blk;
        return true;
    }
    uint32_t ind_idx = idx - ANVILFS_DIRECT_BLOCKS;
    if (ind_idx >= 128) return false;
    if (!in->indirect) {
        in->indirect = alloc_block();
        if (!in->indirect) return false;
    }
    uint32_t ptrs[128];
    if (!blk_read(in->indirect, ptrs)) return false;
    ptrs[ind_idx] = blk;
    return blk_write(in->indirect, ptrs);
}

bool anvilfs_mount(uint32_t ramdisk_addr, uint32_t ramdisk_size) {
    disk_backed = false;
    ram_disk = NULL;

    if (ata_is_present() && ata_sector_count() >= ANVILFS_PART_LBA + 32) {
        uint8_t sec[ANVILFS_BLOCK_SIZE];
        if (ata_read_sectors(ANVILFS_PART_LBA, 1, sec)) {
            anvilfs_superblock_t* probe = (anvilfs_superblock_t*)sec;
            if (probe->magic == ANVILFS_MAGIC) {
                memcpy(&sb, probe, sizeof(sb));
                disk_backed = true;
                serial_printf("[FS] Mounted AnvilFS on ATA PIO (%s) @ LBA %u (%u free blocks)\n",
                              ata_model_name(), ANVILFS_PART_LBA, sb.free_blocks);
                return true;
            }
        }
    }

    if (ramdisk_addr && ramdisk_size >= ANVILFS_BLOCK_SIZE * 32) {
        const anvilfs_superblock_t* probe = (const anvilfs_superblock_t*)ramdisk_addr;
        if (probe->magic == ANVILFS_MAGIC) {
            uint32_t alloc_sz = 512 * 1024;
            ram_disk = (uint8_t*)kmalloc(alloc_sz);
            if (!ram_disk) return false;
            memset(ram_disk, 0, alloc_sz);
            uint32_t copy_sz = ramdisk_size < alloc_sz ? ramdisk_size : alloc_sz;
            memcpy(ram_disk, (const void*)ramdisk_addr, copy_sz);
            ram_blocks = alloc_sz / ANVILFS_BLOCK_SIZE;
            memcpy(&sb, ram_disk, sizeof(sb));
            serial_printf("[FS] Mounted AnvilFS in RAM-Disk mode (%u KB)\n", alloc_sz / 1024);
            return true;
        }
    }

    serial_puts("[FS] ERROR: Valid AnvilFS superblock not found!\n");
    return false;
}

bool anvilfs_is_disk_backed(void) { return disk_backed; }
const char* anvilfs_backend_name(void) { return disk_backed ? "ATA PIO (Persistent)" : "Live USB Ramdisk"; }

void anvilfs_statfs(uint32_t* total_blocks, uint32_t* free_blocks, uint32_t* total_inodes, uint32_t* free_inodes) {
    if (total_blocks) *total_blocks = disk_backed ? sb.total_blocks : ram_blocks;
    if (free_blocks)  *free_blocks  = sb.free_blocks;
    if (total_inodes) *total_inodes = sb.inode_count;
    if (free_inodes)  *free_inodes  = sb.free_inodes;
}

static int find_in_dir(uint32_t dir_ino, const char* name) {
    anvilfs_inode_t dir;
    if (!read_inode(dir_ino, &dir) || dir.type != ANVILFS_TYPE_DIR) return -1;

    uint32_t num_entries = dir.size / sizeof(anvilfs_dirent_t);
    uint32_t idx = 0;
    for (uint32_t b = 0; idx < num_entries; b++) {
        uint32_t blk = get_inode_block(&dir, b);
        if (!blk) break;
        anvilfs_dirent_t ents[16];
        if (!blk_read(blk, ents)) break;
        for (int i = 0; i < 16 && idx < num_entries; i++, idx++) {
            if (ents[i].inode != 0 && strcmp(ents[i].name, name) == 0) {
                return (int)ents[i].inode;
            }
        }
    }
    return -1;
}

static bool add_to_dir(uint32_t dir_ino, uint32_t child_ino, const char* name) {
    anvilfs_inode_t dir;
    if (!read_inode(dir_ino, &dir) || dir.type != ANVILFS_TYPE_DIR) return false;

    uint32_t num_entries = dir.size / sizeof(anvilfs_dirent_t);
    uint32_t idx = 0;
    for (uint32_t b = 0; idx < num_entries; b++) {
        uint32_t blk = get_inode_block(&dir, b);
        if (!blk) break;
        anvilfs_dirent_t ents[16];
        if (!blk_read(blk, ents)) return false;
        for (int i = 0; i < 16 && idx < num_entries; i++, idx++) {
            if (ents[i].inode == 0) {
                ents[i].inode = child_ino;
                strncpy(ents[i].name, name, ANVILFS_NAME_MAX - 1);
                ents[i].name[ANVILFS_NAME_MAX - 1] = '\0';
                return blk_write(blk, ents);
            }
        }
    }

    uint32_t b_idx = num_entries / 16;
    uint32_t slot  = num_entries % 16;
    uint32_t blk   = get_inode_block(&dir, b_idx);
    if (!blk) {
        blk = alloc_block();
        if (!blk || !set_inode_block(&dir, b_idx, blk)) return false;
    }
    anvilfs_dirent_t ents[16];
    if (!blk_read(blk, ents)) return false;
    ents[slot].inode = child_ino;
    strncpy(ents[slot].name, name, ANVILFS_NAME_MAX - 1);
    ents[slot].name[ANVILFS_NAME_MAX - 1] = '\0';
    if (!blk_write(blk, ents)) return false;

    dir.size += sizeof(anvilfs_dirent_t);
    dir.mtime = timer_uptime_ms();
    return write_inode(dir_ino, &dir);
}

static bool split_parent(const char* path, uint32_t* parent_ino_out, char* name_out) {
    if (!path || path[0] != '/') return false;
    char tmp[256];
    strncpy(tmp, path, sizeof(tmp) - 1);
    tmp[sizeof(tmp) - 1] = '\0';

    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = '\0';
    if (strcmp(tmp, "/") == 0) return false;

    char* slash = strrchr(tmp, '/');
    if (!slash) return false;
    strncpy(name_out, slash + 1, ANVILFS_NAME_MAX - 1);
    name_out[ANVILFS_NAME_MAX - 1] = '\0';
    if (name_out[0] == '\0') return false;

    if (slash == tmp) {
        *parent_ino_out = sb.root_inode;
        return true;
    }
    *slash = '\0';
    int pino = anvilfs_lookup(tmp);
    if (pino < 0) return false;
    *parent_ino_out = (uint32_t)pino;
    return true;
}

int anvilfs_lookup(const char* path) {
    if (!path || path[0] != '/') return -1;
    if (strcmp(path, "/") == 0) return (int)sb.root_inode;

    uint32_t cur = sb.root_inode;
    const char* p = path + 1;
    char part[ANVILFS_NAME_MAX];

    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        size_t i = 0;
        while (*p && *p != '/' && i < ANVILFS_NAME_MAX - 1) {
            part[i++] = *p++;
        }
        part[i] = '\0';
        while (*p && *p != '/') p++;

        int next = find_in_dir(cur, part);
        if (next < 0) return -1;
        cur = (uint32_t)next;
    }
    return (int)cur;
}

bool anvilfs_stat(const char* path, anvilfs_inode_t* out) {
    int ino = anvilfs_lookup(path);
    if (ino < 0) return false;
    return read_inode((uint32_t)ino, out);
}

int anvilfs_read_file(const char* path, void* buf, uint32_t max_len) {
    int ino = anvilfs_lookup(path);
    if (ino < 0) return -1;
    anvilfs_inode_t in;
    if (!read_inode((uint32_t)ino, &in) || in.type != ANVILFS_TYPE_FILE) return -1;

    uint32_t to_read = in.size < max_len ? in.size : max_len;
    uint32_t read_so_far = 0;
    uint8_t  sec[ANVILFS_BLOCK_SIZE];
    uint8_t* dst = (uint8_t*)buf;

    for (uint32_t b = 0; read_so_far < to_read; b++) {
        uint32_t blk = get_inode_block(&in, b);
        if (!blk || !blk_read(blk, sec)) break;
        uint32_t chunk = to_read - read_so_far;
        if (chunk > ANVILFS_BLOCK_SIZE) chunk = ANVILFS_BLOCK_SIZE;
        memcpy(dst + read_so_far, sec, chunk);
        read_so_far += chunk;
    }
    return (int)read_so_far;
}

bool anvilfs_write_file(const char* path, const void* buf, uint32_t len, uint16_t perms) {
    if (len > (ANVILFS_DIRECT_BLOCKS + 128) * ANVILFS_BLOCK_SIZE) return false;

    int ino = anvilfs_lookup(path);
    anvilfs_inode_t in;
    if (ino >= 0) {
        if (!read_inode((uint32_t)ino, &in) || in.type != ANVILFS_TYPE_FILE) return false;
        free_inode_blocks(&in);
    } else {
        uint32_t parent_ino;
        char name[ANVILFS_NAME_MAX];
        if (!split_parent(path, &parent_ino, name)) return false;
        ino = alloc_inode();
        if (ino < 0) return false;
        memset(&in, 0, sizeof(in));
        in.type  = ANVILFS_TYPE_FILE;
        in.perms = perms ? perms : 0644;
        in.links = 1;
        in.ctime = timer_uptime_ms();
        if (!add_to_dir(parent_ino, (uint32_t)ino, name)) return false;
    }

    uint32_t blocks_needed = (len + ANVILFS_BLOCK_SIZE - 1) / ANVILFS_BLOCK_SIZE;
    const uint8_t* src = (const uint8_t*)buf;
    uint8_t sec[ANVILFS_BLOCK_SIZE];

    for (uint32_t b = 0; b < blocks_needed; b++) {
        uint32_t blk = alloc_block();
        if (!blk || !set_inode_block(&in, b, blk)) {
            free_inode_blocks(&in);
            return false;
        }
        memset(sec, 0, sizeof(sec));
        uint32_t off = b * ANVILFS_BLOCK_SIZE;
        uint32_t chunk = len - off;
        if (chunk > ANVILFS_BLOCK_SIZE) chunk = ANVILFS_BLOCK_SIZE;
        memcpy(sec, src + off, chunk);
        blk_write(blk, sec);
    }

    in.size = len;
    in.mtime = timer_uptime_ms();
    return write_inode((uint32_t)ino, &in);
}

bool anvilfs_mkdir(const char* path, uint16_t perms) {
    if (anvilfs_lookup(path) >= 0) return false;
    uint32_t parent_ino;
    char name[ANVILFS_NAME_MAX];
    if (!split_parent(path, &parent_ino, name)) return false;

    int ino = alloc_inode();
    if (ino < 0) return false;

    anvilfs_inode_t in;
    memset(&in, 0, sizeof(in));
    in.type  = ANVILFS_TYPE_DIR;
    in.perms = perms ? perms : 0755;
    in.links = 2;
    in.ctime = timer_uptime_ms();
    in.mtime = in.ctime;
    if (!write_inode((uint32_t)ino, &in)) return false;

    if (!add_to_dir((uint32_t)ino, (uint32_t)ino, ".")) return false;
    if (!add_to_dir((uint32_t)ino, parent_ino, "..")) return false;
    return add_to_dir(parent_ino, (uint32_t)ino, name);
}

bool anvilfs_unlink(const char* path) {
    if (strcmp(path, "/") == 0) return false;
    uint32_t parent_ino;
    char name[ANVILFS_NAME_MAX];
    if (!split_parent(path, &parent_ino, name)) return false;
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return false;

    int ino = find_in_dir(parent_ino, name);
    if (ino < 0) return false;

    anvilfs_inode_t in;
    if (!read_inode((uint32_t)ino, &in)) return false;
    free_inode_blocks(&in);
    memset(&in, 0, sizeof(in));
    write_inode((uint32_t)ino, &in);
    sb.free_inodes++;
    blk_write(0, &sb);

    anvilfs_inode_t dir;
    if (!read_inode(parent_ino, &dir)) return false;
    uint32_t num_entries = dir.size / sizeof(anvilfs_dirent_t);
    uint32_t idx = 0;
    for (uint32_t b = 0; idx < num_entries; b++) {
        uint32_t blk = get_inode_block(&dir, b);
        if (!blk) break;
        anvilfs_dirent_t ents[16];
        if (!blk_read(blk, ents)) break;
        for (int i = 0; i < 16 && idx < num_entries; i++, idx++) {
            if (ents[i].inode == (uint32_t)ino && strcmp(ents[i].name, name) == 0) {
                ents[i].inode = 0;
                ents[i].name[0] = '\0';
                blk_write(blk, ents);
                return true;
            }
        }
    }
    return true;
}

int anvilfs_readdir(const char* path, anvilfs_dirent_t* entries, int max_entries) {
    int ino = anvilfs_lookup(path);
    if (ino < 0) return -1;
    anvilfs_inode_t dir;
    if (!read_inode((uint32_t)ino, &dir) || dir.type != ANVILFS_TYPE_DIR) return -1;

    int count = 0;
    uint32_t num_entries = dir.size / sizeof(anvilfs_dirent_t);
    uint32_t idx = 0;
    for (uint32_t b = 0; idx < num_entries && count < max_entries; b++) {
        uint32_t blk = get_inode_block(&dir, b);
        if (!blk) break;
        anvilfs_dirent_t ents[16];
        if (!blk_read(blk, ents)) break;
        for (int i = 0; i < 16 && idx < num_entries && count < max_entries; i++, idx++) {
            if (ents[i].inode != 0) {
                entries[count++] = ents[i];
            }
        }
    }
    return count;
}
