#ifndef ANVIL_VFS_H
#define ANVIL_VFS_H

#include "types.h"
#include "anvilfs.h"

#define VFS_MAX_FD 32

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR   2
#define O_CREAT  0x40
#define O_TRUNC  0x200

typedef struct {
    uint32_t inode;
    uint16_t type;
    uint16_t perms;
    uint32_t size;
} vfs_stat_t;

void vfs_init(void);
int  vfs_open(const char* path, int flags);
int  vfs_read(int fd, void* buf, uint32_t count);
int  vfs_write(int fd, const void* buf, uint32_t count);
int  vfs_close(int fd);
bool vfs_stat(const char* path, vfs_stat_t* st);
bool vfs_mkdir(const char* path);
bool vfs_unlink(const char* path);
int  vfs_readdir(const char* path, anvilfs_dirent_t* out, int max_entries);
void vfs_normalize_path(const char* cwd, const char* input, char* out, size_t out_sz);

#endif
