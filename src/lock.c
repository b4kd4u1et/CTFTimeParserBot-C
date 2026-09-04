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

    /* O_NOFOLLOW: refuse to follow an existing symlink at `path`. Without
     * it, a local attacker could pre-create `path` as a symlink to any
     * file the service account can write (CWE-61); this open() would then
     * silently open and later ftruncate() *that* file instead of a plain
     * lock file. O_NOFOLLOW only affects an *existing* symlink -- O_CREAT
     * still creates an ordinary new file when nothing is there yet. */
    int fd = open(path, O_CREAT | O_RDWR | O_NOFOLLOW, 0644);
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
    /* Deliberately does NOT unlink `path`. Removing the lock file here would
     * reopen the classic flock+unlink TOCTOU race: a process that opened
     * the (still-existing) file just before this unlink() keeps believing
     * it holds "the" lock on that inode, while unlink() detaches the name
     * from it -- a process arriving after the unlink() then open()s a
     * brand-new inode at the same path and acquires an uncontended lock,
     * so two processes end up each holding an exclusive lock on two
     * different inodes that no longer share a path. Leaving the file in
     * place means every acquire always flock()s the *same* inode, which
     * sidesteps that whole class of confusion; the file is just an empty,
     * harmless marker between runs. */
    (void) path;
    if (lock->fd >= 0) {
        flock(lock->fd, LOCK_UN);
        close(lock->fd);
        lock->fd = -1;
    }
}
