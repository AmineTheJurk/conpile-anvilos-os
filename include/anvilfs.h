#ifndef ANVIL_FS_H
#define ANVIL_FS_H

#ifdef __ANVIL_KERNEL__
#include "types.h"
#else
#include <stdint.h>
#endif

#define ANVILFS_MAGIC          0x46564E41 /* "ANVF" */
#define ANVILFS_BLOCK_SIZE     512
#define ANVILFS_PART_LBA       512
#define ANVILFS_TOTAL_BLOCKS   15872      /* ~7.75 MiB partition (8 MiB image) */
#define ANVILFS_MAX_INODES     512
#define ANVILFS_BITMAP_START   1
#define ANVILFS_BITMAP_BLOCKS  4          /* 2048 bytes = 16384 bits */
#define ANVILFS_INODE_START    5
#define ANVILFS_INODE_BLOCKS   64         /* 512 inodes * 64B = 32768B */
#define ANVILFS_DATA_START     69
#define ANVILFS_DIRECT_BLOCKS  8
#define ANVILFS_NAME_MAX       28

#define ANVILFS_TYPE_FREE      0
#define ANVILFS_TYPE_FILE      1
#define ANVILFS_TYPE_DIR       2

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_count;
    uint32_t free_blocks;
    uint32_t free_inodes;
    uint32_t root_inode;
    uint8_t  reserved[480];
} anvilfs_superblock_t;

typedef struct __attribute__((packed)) {
    uint16_t type;        /* 0=free, 1=file, 2=dir */
    uint16_t perms;       /* Unix rwxrwxrwx */
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t ctime;
    uint32_t mtime;
    uint16_t links;
    uint16_t flags;
    uint32_t direct[ANVILFS_DIRECT_BLOCKS];
    uint32_t indirect;
} anvilfs_inode_t;        /* 64 bytes exact */

typedef struct __attribute__((packed)) {
    uint32_t inode;
    char     name[ANVILFS_NAME_MAX];
} anvilfs_dirent_t;       /* 32 bytes exact (16 per 512B block) */

#ifdef __ANVIL_KERNEL__
bool anvilfs_mount(uint32_t ramdisk_addr, uint32_t ramdisk_size);
bool anvilfs_is_disk_backed(void);
const char* anvilfs_backend_name(void);
void anvilfs_statfs(uint32_t* total_blocks, uint32_t* free_blocks, uint32_t* total_inodes, uint32_t* free_inodes);

int  anvilfs_lookup(const char* path);
bool anvilfs_stat(const char* path, anvilfs_inode_t* out);
int  anvilfs_read_file(const char* path, void* buf, uint32_t max_len);
bool anvilfs_write_file(const char* path, const void* buf, uint32_t len, uint16_t perms);
bool anvilfs_mkdir(const char* path, uint16_t perms);
bool anvilfs_unlink(const char* path);
int  anvilfs_readdir(const char* path, anvilfs_dirent_t* entries, int max_entries);
#endif

#endif
