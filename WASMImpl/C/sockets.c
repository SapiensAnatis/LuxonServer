#include "luxon_server.h"
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

struct w2c_env {
    w2c_luxon__server* instance;
};

u32 w2c_env_socket_socket(struct w2c_env* env, u32 domain, u32 type, u32 protocol) {
    int res = socket((int)domain, (int)type, (int)protocol);
    return (u32)res;
}

u32 w2c_env_socket_bind(struct w2c_env* env, u32 sockfd, u32 addr_ptr, u32 addrlen) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (addr_ptr + addrlen > mem_size) return (u32)-1;

    int res = bind((int)sockfd, (const struct sockaddr*)(mem + addr_ptr), (socklen_t)addrlen);
    return (u32)res;
}

u32 w2c_env_socket_listen(struct w2c_env* env, u32 sockfd, u32 backlog) {
    int res = listen((int)sockfd, (int)backlog);
    return (u32)res;
}

u32 w2c_env_socket_accept(struct w2c_env* env, u32 sockfd, u32 addr_ptr, u32 addrlen_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;

    struct sockaddr* addr = addr_ptr ? (struct sockaddr*)(mem + addr_ptr) : NULL;
    socklen_t* addrlen = addrlen_ptr ? (socklen_t*)(mem + addrlen_ptr) : NULL;

    if (addrlen_ptr && addrlen_ptr + 4 > mem_size) return (u32)-1;
    if (addr_ptr && addrlen && addr_ptr + *addrlen > mem_size) return (u32)-1;

    int res = accept((int)sockfd, addr, addrlen);
    return (u32)res;
}

u32 w2c_env_socket_connect(struct w2c_env* env, u32 sockfd, u32 addr_ptr, u32 addrlen) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (addr_ptr + addrlen > mem_size) return (u32)-1;

    int res = connect((int)sockfd, (const struct sockaddr*)(mem + addr_ptr), (socklen_t)addrlen);
    return (u32)res;
}

u32 w2c_env_socket_send(struct w2c_env* env, u32 sockfd, u32 buf_ptr, u32 len, u32 flags) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (buf_ptr + len > mem_size) return (u32)-1;

    ssize_t res = send((int)sockfd, mem + buf_ptr, (size_t)len, (int)flags);
    return (u32)res;
}

u32 w2c_env_socket_recv(struct w2c_env* env, u32 sockfd, u32 buf_ptr, u32 len, u32 flags) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (buf_ptr + len > mem_size) return (u32)-1;

    ssize_t res = recv((int)sockfd, mem + buf_ptr, (size_t)len, (int)flags);
    return (u32)res;
}

u32 w2c_env_socket_sendto(struct w2c_env* env, u32 sockfd, u32 buf_ptr, u32 len, u32 flags, u32 dest_addr_ptr, u32 addrlen) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (buf_ptr + len > mem_size) return (u32)-1;

    const struct sockaddr* dest = dest_addr_ptr ? (const struct sockaddr*)(mem + dest_addr_ptr) : NULL;
    ssize_t res = sendto((int)sockfd, mem + buf_ptr, (size_t)len, (int)flags, dest, (socklen_t)addrlen);
    return (u32)res;
}

u32 w2c_env_socket_recvfrom(struct w2c_env* env, u32 sockfd, u32 buf_ptr, u32 len, u32 flags, u32 src_addr_ptr, u32 addrlen_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (buf_ptr + len > mem_size) return (u32)-1;

    struct sockaddr* src = src_addr_ptr ? (struct sockaddr*)(mem + src_addr_ptr) : NULL;
    socklen_t* addrlen = addrlen_ptr ? (socklen_t*)(mem + addrlen_ptr) : NULL;
    ssize_t res = recvfrom((int)sockfd, mem + buf_ptr, (size_t)len, (int)flags, src, addrlen);
    return (u32)res;
}

u32 w2c_env_socket_setsockopt(struct w2c_env* env, u32 sockfd, u32 level, u32 optname, u32 optval_ptr, u32 optlen) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (optval_ptr + optlen > mem_size) return (u32)-1;

    int res = setsockopt((int)sockfd, (int)level, (int)optname, mem + optval_ptr, (socklen_t)optlen);
    return (u32)res;
}

u32 w2c_env_socket_shutdown(struct w2c_env* env, u32 sockfd, u32 how) {
    int res = shutdown((int)sockfd, (int)how);
    return (u32)res;
}

u32 w2c_env_socket_close(struct w2c_env* env, u32 sockfd) {
    int res = close((int)sockfd);
    return (u32)res;
}

u32 w2c_env_socket_fcntl(struct w2c_env* env, u32 fd, u32 cmd, u32 arg) {
    int res = fcntl((int)fd, (int)cmd, (int)arg);
    return (u32)res;
}

u32 w2c_env_socket_ioctl(struct w2c_env* env, u32 fd, u32 request, u32 argp) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;

    void* host_argp = NULL;
    if (argp) {
        if (argp + 4 > mem_size) return (u32)-1;
        host_argp = mem + argp;
    }
    int res = ioctl((int)fd, (unsigned long)request, host_argp);
    return (u32)res;
}

u32 w2c_env_socket_inet_pton(struct w2c_env* env, u32 af, u32 src_ptr, u32 dst_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (!src_ptr || !dst_ptr || src_ptr >= mem_size || dst_ptr >= mem_size) return 0;

    uint32_t len = 0;
    while (src_ptr + len < mem_size && mem[src_ptr + len] != '\0') {
        len++;
    }
    if (src_ptr + len >= mem_size) return 0;

    int res = inet_pton((int)af, (const char*)(mem + src_ptr), mem + dst_ptr);
    return (u32)res;
}

u32 w2c_env_socket_inet_ntop(struct w2c_env* env, u32 af, u32 src_ptr, u32 dst_ptr, u32 size) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;
    if (!src_ptr || !dst_ptr || dst_ptr + size > mem_size) return 0;

    const char* res = inet_ntop((int)af, mem + src_ptr, (char*)(mem + dst_ptr), (socklen_t)size);
    return res ? dst_ptr : 0;
}

u32 w2c_env_socket_select(struct w2c_env* env, u32 nfds, u32 readfds_ptr, u32 writefds_ptr, u32 exceptfds_ptr, u32 timeout_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    uint32_t mem_size = w2c_luxon__server_memory(env->instance)->size;

    if (readfds_ptr && readfds_ptr + 128 > mem_size) return (u32)-1;
    if (writefds_ptr && writefds_ptr + 128 > mem_size) return (u32)-1;
    if (exceptfds_ptr && exceptfds_ptr + 128 > mem_size) return (u32)-1;

    fd_set* rfds = readfds_ptr ? (fd_set*)(mem + readfds_ptr) : NULL;
    fd_set* wfds = writefds_ptr ? (fd_set*)(mem + writefds_ptr) : NULL;
    fd_set* efds = exceptfds_ptr ? (fd_set*)(mem + exceptfds_ptr) : NULL;

    struct timeval host_tv;
    struct timeval* tv = NULL;
    if (timeout_ptr) {
        if (timeout_ptr + 16 > mem_size) return (u32)-1;
        host_tv.tv_sec  = *(int64_t*)(mem + timeout_ptr);
        host_tv.tv_usec = *(int64_t*)(mem + timeout_ptr + 8);
        tv = &host_tv;
    }

    int res = select((int)nfds, rfds, wfds, efds, tv);

    if (timeout_ptr && res >= 0) {
        *(int64_t*)(mem + timeout_ptr)     = host_tv.tv_sec;
        *(int64_t*)(mem + timeout_ptr + 8) = host_tv.tv_usec;
    }

    return (u32)res;
}

u32 w2c_env_socket_getaddrinfo(struct w2c_env* env, u32 node_ptr, u32 service_ptr, u32 hints_ptr, u32 res_ptr) {
    uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
    const char* node = node_ptr ? (const char*)(mem + node_ptr) : NULL;
    const char* service = service_ptr ? (const char*)(mem + service_ptr) : NULL;

    struct addrinfo host_hints;
    struct addrinfo* host_hints_ptr = NULL;
    if (hints_ptr) {
        memset(&host_hints, 0, sizeof(host_hints));
        host_hints.ai_flags    = *(int*)(mem + hints_ptr + 0);
        host_hints.ai_family   = *(int*)(mem + hints_ptr + 4);
        host_hints.ai_socktype = *(int*)(mem + hints_ptr + 8);
        host_hints.ai_protocol = *(int*)(mem + hints_ptr + 12);
        host_hints_ptr = &host_hints;
    }

    struct addrinfo* host_res = NULL;
    int ret = getaddrinfo(node, service, host_hints_ptr, &host_res);
    if (ret != 0) {
        return (u32)ret;
    }

    uint32_t prev_next_offset = 0;

    for (struct addrinfo* curr = host_res; curr != NULL; curr = curr->ai_next) {
        uint32_t wasm_ai = w2c_luxon__server_malloc(env->instance, 32);

        uint32_t wasm_addr = 0;
        if (curr->ai_addr && curr->ai_addrlen > 0) {
            wasm_addr = w2c_luxon__server_malloc(env->instance, curr->ai_addrlen);
            mem = w2c_luxon__server_memory(env->instance)->data;
            memcpy(mem + wasm_addr, curr->ai_addr, curr->ai_addrlen);
        }

        uint32_t wasm_canon = 0;
        if (curr->ai_canonname) {
            size_t clen = strlen(curr->ai_canonname) + 1;
            wasm_canon = w2c_luxon__server_malloc(env->instance, clen);
            mem = w2c_luxon__server_memory(env->instance)->data;
            memcpy(mem + wasm_canon, curr->ai_canonname, clen);
        }

        mem = w2c_luxon__server_memory(env->instance)->data;
        *(int*)(mem + wasm_ai + 0)       = curr->ai_flags;
        *(int*)(mem + wasm_ai + 4)       = curr->ai_family;
        *(int*)(mem + wasm_ai + 8)       = curr->ai_socktype;
        *(int*)(mem + wasm_ai + 12)      = curr->ai_protocol;
        *(uint32_t*)(mem + wasm_ai + 16) = (uint32_t)curr->ai_addrlen;
        *(uint32_t*)(mem + wasm_ai + 20) = wasm_addr;
        *(uint32_t*)(mem + wasm_ai + 24) = wasm_canon;
        *(uint32_t*)(mem + wasm_ai + 28) = 0;

        if (prev_next_offset == 0) {
            *(uint32_t*)(mem + res_ptr) = wasm_ai;
        } else {
            *(uint32_t*)(mem + prev_next_offset) = wasm_ai;
        }
        prev_next_offset = wasm_ai + 28;
    }

    freeaddrinfo(host_res);
    return 0;
}

void w2c_env_socket_freeaddrinfo(struct w2c_env* env, u32 res_ptr) {
    uint32_t curr = res_ptr;
    while (curr != 0) {
        uint8_t* mem = w2c_luxon__server_memory(env->instance)->data;
        uint32_t addr_ptr  = *(uint32_t*)(mem + curr + 20);
        uint32_t canon_ptr = *(uint32_t*)(mem + curr + 24);
        uint32_t next_ptr  = *(uint32_t*)(mem + curr + 28);

        if (addr_ptr)  w2c_luxon__server_free(env->instance, addr_ptr);
        if (canon_ptr) w2c_luxon__server_free(env->instance, canon_ptr);
        w2c_luxon__server_free(env->instance, curr);

        curr = next_ptr;
    }
}
