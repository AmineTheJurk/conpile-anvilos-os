#include "vfs.h"
#include "mem.h"
#include "string.h"
#include "serial.h"

typedef struct {
    bool     used;
    char     path[128];
    int      flags;
    uint32_t offset;
    uint32_t size;
    uint8_t* data;
    bool     dirty;
    bool     is_dev_serial;
    bool     is_dev_null;
} vfs_fd_t;

static vfs_fd_t fds[VFS_MAX_FD];

void vfs_init(void) {
    memset(fds, 0, sizeof(fds));
}

void vfs_normalize_path(const char* cwd, const char* input, char* out, size_t out_sz) {
    char combined[256];
    if (!input || !*input) {
        strncpy(out, cwd && *cwd ? cwd : "/", out_sz - 1);
        out[out_sz - 1] = '\0';
        return;
    }

    if (input[0] == '/') {
        strncpy(combined, input, sizeof(combined) - 1);
    } else {
        snprintf(combined, sizeof(combined), "%s/%s", (cwd && strcmp(cwd, "/") != 0) ? cwd : "", input);
    }
    combined[sizeof(combined) - 1] = '\0';

    const char* parts[32];
    int depth = 0;
    char* p = combined;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char* start = p;
        while (*p && *p != '/') p++;
        if (*p) *p++ = '\0';

        if (strcmp(start, ".") == 0) {
            continue;
        } else if (strcmp(start, "..") == 0) {
            if (depth > 0) depth--;
        } else if (depth < 32) {
            parts[depth++] = start;
        }
    }

    if (depth == 0) {
        strncpy(out, "/", out_sz);
        return;
    }

    out[0] = '\0';
    for (int i = 0; i < depth; i++) {
        if (strlen(out) + 1 + strlen(parts[i]) + 1 >= out_sz) break;
        strcat(out, "/");
        strcat(out, parts[i]);
    }
}

int vfs_open(const char* path, int flags) {
    if (!path) return -1;
    int slot = -1;
    for (int i = 3; i < VFS_MAX_FD; i++) {
        if (!fds[i].used) { slot = i; break; }
    }
    if (slot < 0) return -1;

    memset(&fds[slot], 0, sizeof(vfs_fd_t));
    strncpy(fds[slot].path, path, 127);
    fds[slot].flags = flags;

    if (strcmp(path, "/dev/ttyS0") == 0) {
        fds[slot].used = true;
        fds[slot].is_dev_serial = true;
        return slot;
    }
    if (strcmp(path, "/dev/null") == 0) {
        fds[slot].used = true;
        fds[slot].is_dev_null = true;
        return slot;
    }

    anvilfs_inode_t in;
    bool exists = anvilfs_stat(path, &in);
    if (!exists) {
        if (!(flags & O_CREAT)) return -1;
        fds[slot].used  = true;
        fds[slot].size  = 0;
        fds[slot].data  = NULL;
        fds[slot].dirty = true;
        return slot;
    }

    if (in.type != ANVILFS_TYPE_FILE) return -1;
    fds[slot].used = true;

    if ((flags & O_TRUNC) || in.size == 0) {
        fds[slot].size  = 0;
        fds[slot].data  = NULL;
        fds[slot].dirty = (flags & O_TRUNC) != 0;
    } else {
        fds[slot].data = (uint8_t*)kmalloc(in.size + 1);
        if (!fds[slot].data) { fds[slot].used = false; return -1; }
        int got = anvilfs_read_file(path, fds[slot].data, in.size);
        fds[slot].size = (got > 0) ? (uint32_t)got : 0;
    }
    return slot;
}

int vfs_read(int fd, void* buf, uint32_t count) {
    if (fd < 3 || fd >= VFS_MAX_FD || !fds[fd].used) return -1;
    if (fds[fd].is_dev_null) return 0;
    if (fds[fd].is_dev_serial) {
        uint8_t* out = (uint8_t*)buf;
        for (uint32_t i = 0; i < count; i++) out[i] = (uint8_t)serial_getc();
        return (int)count;
    }
    if (fds[fd].offset >= fds[fd].size || !fds[fd].data) return 0;
    uint32_t avail = fds[fd].size - fds[fd].offset;
    uint32_t n = count < avail ? count : avail;
    memcpy(buf, fds[fd].data + fds[fd].offset, n);
    fds[fd].offset += n;
    return (int)n;
}

int vfs_write(int fd, const void* buf, uint32_t count) {
    if (fd < 3 || fd >= VFS_MAX_FD || !fds[fd].used) return -1;
    if (fds[fd].is_dev_null) return (int)count;
    if (fds[fd].is_dev_serial) {
        const char* s = (const char*)buf;
        for (uint32_t i = 0; i < count; i++) serial_putc(s[i]);
        return (int)count;
    }

    uint32_t needed = fds[fd].offset + count;
    if (needed > 65536) return -1;
    if (needed > fds[fd].size) {
        uint8_t* nb = (uint8_t*)kmalloc(needed);
        if (!nb) return -1;
        if (fds[fd].data && fds[fd].size > 0) {
            memcpy(nb, fds[fd].data, fds[fd].size);
            kfree(fds[fd].data);
        }
        fds[fd].data = nb;
        fds[fd].size = needed;
    }
    memcpy(fds[fd].data + fds[fd].offset, buf, count);
    fds[fd].offset += count;
    fds[fd].dirty = true;
    return (int)count;
}

int vfs_close(int fd) {
    if (fd < 3 || fd >= VFS_MAX_FD || !fds[fd].used) return -1;
    if (fds[fd].dirty && !fds[fd].is_dev_null && !fds[fd].is_dev_serial) {
        anvilfs_write_file(fds[fd].path, fds[fd].data ? fds[fd].data : (const void*)"", fds[fd].size, 0644);
    }
    if (fds[fd].data) kfree(fds[fd].data);
    memset(&fds[fd], 0, sizeof(vfs_fd_t));
    return 0;
}

bool vfs_stat(const char* path, vfs_stat_t* st) {
    anvilfs_inode_t in;
    int ino = anvilfs_lookup(path);
    if (ino < 0 || !anvilfs_stat(path, &in)) return false;
    st->inode = (uint32_t)ino;
    st->type  = in.type;
    st->perms = in.perms;
    st->size  = in.size;
    return true;
}

bool vfs_mkdir(const char* path) { return anvilfs_mkdir(path, 0755); }
bool vfs_unlink(const char* path) { return anvilfs_unlink(path); }
int  vfs_readdir(const char* path, anvilfs_dirent_t* out, int max_entries) {
    return anvilfs_readdir(path, out, max_entries);
}
