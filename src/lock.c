#include "lock.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

int lock_acquire(lockfile_t *lock, const char *path)
{
    lock->fd = -1;

    int fd = open(path, O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        return -1;
    }

    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        int saved_errno = errno;
        close(fd);
        return (saved_errno == EWOULDBLOCK) ? 0 : -1;
    }

    if (ftruncate(fd, 0) != 0) {
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }

    char pid_str[32];
    int n = snprintf(pid_str, sizeof(pid_str), "%ld", (long) getpid());
    if (n > 0) {
        ssize_t written = write(fd, pid_str, (size_t) n);
        (void) written; /* best-effort; the lock itself is what matters */
    }

    lock->fd = fd;
    return 1;
}

void lock_release(lockfile_t *lock, const char *path)
{
    if (lock->fd >= 0) {
        flock(lock->fd, LOCK_UN);
        close(lock->fd);
        lock->fd = -1;
        unlink(path);
    }
}
