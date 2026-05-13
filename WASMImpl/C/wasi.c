#include "luxon_server.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifndef S_ISSOCK
#define S_ISSOCK(m) (((m) & S_IFMT) == S_IFSOCK)
#endif

struct w2c_wasi__snapshot__preview1 {
    w2c_luxon__server* instance;
};

static u32 errno_to_wasi(int err) {
    switch (err) {
        case 0: return 0;
        case EPERM: return 63;
        case ENOENT: return 44;
        case EBADF: return 8;
        case EAGAIN: return 6;
        case ENOMEM: return 48;
        case EACCES: return 2;
        case EBUSY: return 10;
        case EEXIST: return 20;
        case EINVAL: return 28;
        case ENOSPC: return 51;
        case EPIPE: return 64;
        case ENOSYS: return 52;
        case ECONNRESET: return 15;
        default: return 29; // EIO fallback
    }
}

u32 w2c_wasi__snapshot__preview1_clock_time_get(struct w2c_wasi__snapshot__preview1* wasi, u32 clock_id, u64 precision, u32 time_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (time_ptr + 8 > mem_size) return 28;

    struct timespec ts;
    clockid_t cid = CLOCK_REALTIME;
    if (clock_id == 1) cid = CLOCK_MONOTONIC;
    else if (clock_id == 2) cid = CLOCK_PROCESS_CPUTIME_ID;
    else if (clock_id == 3) cid = CLOCK_THREAD_CPUTIME_ID;

    if (clock_gettime(cid, &ts) < 0) return errno_to_wasi(errno);
    uint64_t nanos = (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    *(uint64_t*)(mem + time_ptr) = nanos;
    return 0;
}

u32 w2c_wasi__snapshot__preview1_environ_get(struct w2c_wasi__snapshot__preview1* wasi, u32 environ_ptr, u32 environ_buf_ptr) {
    return 0;
}

u32 w2c_wasi__snapshot__preview1_environ_sizes_get(struct w2c_wasi__snapshot__preview1* wasi, u32 environ_count_ptr, u32 environ_buf_size_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (environ_count_ptr + 4 > mem_size || environ_buf_size_ptr + 4 > mem_size) return 28;

    *(uint32_t*)(mem + environ_count_ptr) = 0;
    *(uint32_t*)(mem + environ_buf_size_ptr) = 0;
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_close(struct w2c_wasi__snapshot__preview1* wasi, u32 fd) {
    // Avoid closing standard file descriptors inadvertently
    if (fd <= 2) return 0;
    if (close((int)fd) < 0) return errno_to_wasi(errno);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_fdstat_get(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 stat_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (stat_ptr + 24 > mem_size) return 28;

    // Handle preopened directory descriptors gracefully
    if (fd == 3) {
        memset(mem + stat_ptr, 0, 24);
        mem[stat_ptr] = 3; // Directory
        memset(mem + stat_ptr + 8, 0xff, 16); // Maximum rights
        return 0;
    }

    struct stat st;
    if (fstat((int)fd, &st) < 0) return errno_to_wasi(errno);

    uint8_t filetype = 0;
    if (S_ISCHR(st.st_mode)) filetype = 2;
    else if (S_ISDIR(st.st_mode)) filetype = 3;
    else if (S_ISREG(st.st_mode)) filetype = 4;
    else if (S_ISSOCK(st.st_mode)) filetype = 6;

    memset(mem + stat_ptr, 0, 24);
    mem[stat_ptr] = filetype;
    memset(mem + stat_ptr + 8, 0xff, 16);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_fdstat_set_flags(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 flags) {
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_filestat_get(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 buf_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (buf_ptr + 64 > mem_size) return 28;

    struct stat st;
    if (fstat((int)fd, &st) < 0) return errno_to_wasi(errno);

    uint8_t filetype = 0;
    if (S_ISCHR(st.st_mode)) filetype = 2;
    else if (S_ISDIR(st.st_mode)) filetype = 3;
    else if (S_ISREG(st.st_mode)) filetype = 4;
    else if (S_ISSOCK(st.st_mode)) filetype = 6;

    memset(mem + buf_ptr, 0, 64);
    *(uint64_t*)(mem + buf_ptr)      = st.st_dev;
    *(uint64_t*)(mem + buf_ptr + 8)  = st.st_ino;
    mem[buf_ptr + 16]                = filetype;
    *(uint64_t*)(mem + buf_ptr + 24) = st.st_nlink;
    *(uint64_t*)(mem + buf_ptr + 32) = st.st_size;
    *(uint64_t*)(mem + buf_ptr + 40) = (uint64_t)st.st_atime * 1000000000ULL;
    *(uint64_t*)(mem + buf_ptr + 48) = (uint64_t)st.st_mtime * 1000000000ULL;
    *(uint64_t*)(mem + buf_ptr + 56) = (uint64_t)st.st_ctime * 1000000000ULL;
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_filestat_set_size(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u64 size) {
    if (ftruncate((int)fd, (off_t)size) < 0) return errno_to_wasi(errno);
    return 0;
}

// Emulate a pre-opened root/current working directory at FD 3
u32 w2c_wasi__snapshot__preview1_fd_prestat_get(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 buf_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (buf_ptr + 8 > mem_size) return 28;

    if (fd == 3) {
        mem[buf_ptr] = 0; // WASI_PREOPENTYPE_DIR
        *(uint32_t*)(mem + buf_ptr + 4) = 2; // Length of path "."
        return 0;
    }
    return 8; // EBADF signals end of pre-opened list
}

u32 w2c_wasi__snapshot__preview1_fd_prestat_dir_name(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 path_ptr, u32 path_len) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (path_ptr + path_len > mem_size) return 28;

    if (fd == 3) {
        if (path_len >= 2) {
            memcpy(mem + path_ptr, ".", 2);
            return 0;
        }
        return 28; // EINVAL
    }
    return 8; // EBADF
}

u32 w2c_wasi__snapshot__preview1_path_open(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 dirflags, u32 path_ptr, u32 path_len, u32 oflags, u64 fs_rights_base, u64 fs_rights_inheriting, u32 fdflags, u32 opened_fd_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (path_ptr + path_len > mem_size || opened_fd_ptr + 4 > mem_size) return 28;

    char* host_path = (char*)malloc(path_len + 1);
    if (!host_path) return 48; // ENOMEM
    memcpy(host_path, mem + path_ptr, path_len);
    host_path[path_len] = '\0';

    // Intercept relative WASI paths for system random devices and point them to absolute host paths
    if (strcmp(host_path, "dev/urandom") == 0 || strcmp(host_path, "/dev/urandom") == 0) {
        free(host_path);
        host_path = strdup("/dev/urandom");
    } else if (strcmp(host_path, "dev/random") == 0 || strcmp(host_path, "/dev/random") == 0) {
        free(host_path);
        host_path = strdup("/dev/random");
    }

    int host_flags = O_RDWR;
    if (oflags & 1) host_flags |= O_CREAT;
    if (oflags & 2) host_flags |= O_DIRECTORY;
    if (oflags & 4) host_flags |= O_EXCL;
    if (oflags & 8) host_flags |= O_TRUNC;

    if (fdflags & 1) host_flags |= O_APPEND;
    if (fdflags & 4) host_flags |= O_NONBLOCK;

    int new_fd = open(host_path, host_flags, 0666);

    if (new_fd < 0) {
        if ((errno == EACCES || errno == EPERM || errno == EISDIR) && !(oflags & (1 | 8)) && !(fdflags & 1)) {
            new_fd = open(host_path, O_RDONLY | (host_flags & ~O_RDWR));
        }
    }

    free(host_path);
    if (new_fd < 0) return 44; // ENOENT / mapped error

    mem = w2c_luxon__server_memory(wasi->instance)->data;
    *(uint32_t*)(mem + opened_fd_ptr) = new_fd;
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_read(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 iovs_ptr, u32 iovs_len, u32 nread_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (nread_ptr + 4 > mem_size) return 28;

    uint32_t total_read = 0;
    for (u32 i = 0; i < iovs_len; i++) {
        uint32_t iov_ptr = iovs_ptr + i * 8;
        if (iov_ptr + 8 > mem_size) return 28;
        uint32_t buf_ptr = *(uint32_t*)(mem + iov_ptr);
        uint32_t buf_len = *(uint32_t*)(mem + iov_ptr + 4);
        if (buf_ptr + buf_len > mem_size) return 28;

        ssize_t res = read((int)fd, mem + buf_ptr, buf_len);
        if (res < 0) {
            if (total_read > 0) break;
            return errno_to_wasi(errno);
        }
        total_read += res;
        if ((size_t)res < buf_len || res == 0) break;
    }
    *(uint32_t*)(mem + nread_ptr) = total_read;
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_seek(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u64 offset, u32 whence, u32 newoffset_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (newoffset_ptr + 8 > mem_size) return 28;

    int w = SEEK_SET;
    if (whence == 1) w = SEEK_CUR;
    else if (whence == 2) w = SEEK_END;

    off_t res = lseek((int)fd, (off_t)offset, w);
    if (res == (off_t)-1) return errno_to_wasi(errno);
    *(uint64_t*)(mem + newoffset_ptr) = (uint64_t)res;
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_sync(struct w2c_wasi__snapshot__preview1* wasi, u32 fd) {
    if (fsync((int)fd) < 0) return errno_to_wasi(errno);
    return 0;
}

u32 w2c_wasi__snapshot__preview1_fd_write(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 iovs_ptr, u32 iovs_len, u32 nwritten_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (nwritten_ptr + 4 > mem_size) return 28;

    uint32_t total_written = 0;
    for (u32 i = 0; i < iovs_len; i++) {
        uint32_t iov_ptr = iovs_ptr + i * 8;
        if (iov_ptr + 8 > mem_size) return 28;
        uint32_t buf_ptr = *(uint32_t*)(mem + iov_ptr);
        uint32_t buf_len = *(uint32_t*)(mem + iov_ptr + 4);
        if (buf_ptr + buf_len > mem_size) return 28;

        ssize_t res = write((int)fd, mem + buf_ptr, buf_len);
        if (res < 0) {
            if (total_written > 0) break;
            return errno_to_wasi(errno);
        }
        total_written += res;
        if ((size_t)res < buf_len) break;
    }
    *(uint32_t*)(mem + nwritten_ptr) = total_written;
    return 0;
}

u32 w2c_wasi__snapshot__preview1_path_create_directory(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 path_ptr, u32 path_len) { return 52; }
u32 w2c_wasi__snapshot__preview1_path_filestat_get(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 flags, u32 path_ptr, u32 path_len, u32 buf_ptr) { return 52; }
u32 w2c_wasi__snapshot__preview1_path_filestat_set_times(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 flags, u32 path_ptr, u32 path_len, u64 atim, u64 mtim, u32 fst_flags) { return 52; }
u32 w2c_wasi__snapshot__preview1_path_readlink(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 path_ptr, u32 path_len, u32 buf_ptr, u32 buf_len, u32 bufused_ptr) { return 52; }
u32 w2c_wasi__snapshot__preview1_path_remove_directory(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 path_ptr, u32 path_len) { return 52; }
u32 w2c_wasi__snapshot__preview1_path_unlink_file(struct w2c_wasi__snapshot__preview1* wasi, u32 fd, u32 path_ptr, u32 path_len) { return 52; }
u32 w2c_wasi__snapshot__preview1_poll_oneoff(struct w2c_wasi__snapshot__preview1* wasi, u32 in_ptr, u32 out_ptr, u32 nsubscriptions, u32 nevents_ptr) { return 52; }

void w2c_wasi__snapshot__preview1_proc_exit(struct w2c_wasi__snapshot__preview1* wasi, u32 rval) {
    exit((int)rval);
}

u32 w2c_wasi__snapshot__preview1_random_get(struct w2c_wasi__snapshot__preview1* wasi, u32 buf_ptr, u32 buf_len) {
    uint8_t* mem = w2c_luxon__server_memory(wasi->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(wasi->instance)->size;
    if (buf_ptr + buf_len > mem_size) return 28;

    FILE* f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t ignored = fread(mem + buf_ptr, 1, buf_len, f);
        (void)ignored;
        fclose(f);
    } else {
        for (u32 i = 0; i < buf_len; i++) {
            mem[buf_ptr + i] = rand() & 0xff;
        }
    }
    return 0;
}
