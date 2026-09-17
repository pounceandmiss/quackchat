#define _GNU_SOURCE
#include "ringbroker.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#define RING_NAME_MAX 63

static socklen_t make_addr(const char *name, struct sockaddr_un *a)
{
    const size_t n = strlen(name);
    if (n == 0 || n + 1 > sizeof(a->sun_path))
        return 0;
    memset(a, 0, sizeof(*a));
    a->sun_family = AF_UNIX;
    memcpy(a->sun_path + 1, name, n);
    return (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + n);
}

static int same_uid(int s)
{
    struct ucred cr;
    socklen_t len = sizeof(cr);
    return getsockopt(s, SOL_SOCKET, SO_PEERCRED, &cr, &len) == 0 && cr.uid == getuid();
}

static void set_timeouts(int s)
{
    const struct timeval tv = {1, 0};
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

// rtc-mv names rings tv-<hex>; nothing else in the process is handed out.
static int valid_ring_name(const char *s, size_t n)
{
    if (n < 4 || n > RING_NAME_MAX || strncmp(s, "tv-", 3) != 0)
        return 0;
    for (size_t i = 3; i < n; i++) {
        if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f')))
            return 0;
    }
    return 1;
}

static int links_to(const char *path, const char *want)
{
    char link[128];
    const ssize_t n = readlink(path, link, sizeof(link) - 1);
    if (n < 0)
        return 0;
    link[n] = 0;
    return strcmp(link, want) == 0;
}

// The dup is checked again: the fd matched may have been closed and reused
// meanwhile. Not reopened through /proc: Android's SELinux policy denies that.
static int open_ring(const char *ring)
{
    char want[96];
    snprintf(want, sizeof(want), "/memfd:%s (deleted)", ring);

    DIR *d = opendir("/proc/self/fd");
    if (!d)
        return -1;
    int fd = -1;
    struct dirent *e;
    while (fd < 0 && (e = readdir(d)) != NULL) {
        char path[64];
        snprintf(path, sizeof(path), "/proc/self/fd/%s", e->d_name);
        if (e->d_name[0] == '.' || !links_to(path, want))
            continue;
        fd = fcntl(atoi(e->d_name), F_DUPFD_CLOEXEC, 0);
        if (fd < 0)
            continue;
        snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
        if (!links_to(path, want)) {
            close(fd);
            fd = -1;
        }
    }
    closedir(d);
    return fd;
}

static void reply(int s, int fd)
{
    char ok = fd >= 0;
    struct iovec iov = {&ok, 1};
    char ctl[CMSG_SPACE(sizeof(int))];
    struct msghdr m;
    memset(&m, 0, sizeof(m));
    memset(ctl, 0, sizeof(ctl));
    m.msg_iov = &iov;
    m.msg_iovlen = 1;
    if (fd >= 0) {
        m.msg_control = ctl;
        m.msg_controllen = sizeof(ctl);
        struct cmsghdr *c = CMSG_FIRSTHDR(&m);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c), &fd, sizeof(int));
    }
    sendmsg(s, &m, MSG_NOSIGNAL);
}

static void serve_one(int s)
{
    set_timeouts(s);
    if (!same_uid(s))
        return;
    char name[RING_NAME_MAX + 2];
    const ssize_t n = recv(s, name, sizeof(name) - 1, 0);
    if (n <= 0 || !valid_ring_name(name, (size_t)n)) {
        reply(s, -1);
        return;
    }
    name[n] = 0;
    const int fd = open_ring(name);
    reply(s, fd);
    if (fd >= 0)
        close(fd);
}

static void *serve(void *arg)
{
    const int ls = (int)(intptr_t)arg;
    for (;;) {
        const int s = accept4(ls, NULL, NULL, SOCK_CLOEXEC);
        if (s < 0) {
            if (errno == EINTR || errno == ECONNABORTED)
                continue;
            break;
        }
        serve_one(s);
        close(s);
    }
    close(ls);
    return NULL;
}

int ringbroker_start(const char *socket_name)
{
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static int started, result;

    pthread_mutex_lock(&lock);
    if (started) {
        pthread_mutex_unlock(&lock);
        return result;
    }
    started = 1;
    result = -1;

    struct sockaddr_un a;
    const socklen_t len = make_addr(socket_name, &a);
    const int ls = len ? socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0) : -1;
    if (ls >= 0) {
        pthread_t t;
        if (bind(ls, (struct sockaddr *)&a, len) == 0 && listen(ls, 8) == 0 &&
            pthread_create(&t, NULL, serve, (void *)(intptr_t)ls) == 0) {
            pthread_detach(t);
            result = 0;
        } else {
            close(ls);
        }
    }
    pthread_mutex_unlock(&lock);
    return result;
}

static int recv_fd(int s)
{
    char ok = 0;
    struct iovec iov = {&ok, 1};
    char ctl[CMSG_SPACE(sizeof(int))];
    struct msghdr m;
    memset(&m, 0, sizeof(m));
    m.msg_iov = &iov;
    m.msg_iovlen = 1;
    m.msg_control = ctl;
    m.msg_controllen = sizeof(ctl);
    if (recvmsg(s, &m, MSG_CMSG_CLOEXEC) != 1)
        return -1;

    int fd = -1;
    struct cmsghdr *c = CMSG_FIRSTHDR(&m);
    if (c && c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS &&
        c->cmsg_len == CMSG_LEN(sizeof(int)))
        memcpy(&fd, CMSG_DATA(c), sizeof(int));
    if (!ok && fd >= 0) {
        close(fd);
        fd = -1;
    }
    return fd;
}

int ringbroker_fetch(const char *socket_name, const char *ring_name)
{
    struct sockaddr_un a;
    const socklen_t len = make_addr(socket_name, &a);
    const size_t n = strlen(ring_name);
    if (!len || n == 0 || n > RING_NAME_MAX)
        return -1;
    const int s = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (s < 0)
        return -1;
    set_timeouts(s);
    int fd = -1;
    if (connect(s, (struct sockaddr *)&a, len) == 0 && same_uid(s) &&
        send(s, ring_name, n, MSG_NOSIGNAL) == (ssize_t)n)
        fd = recv_fd(s);
    close(s);
    return fd;
}
