#ifndef CTF_LOCK_H
#define CTF_LOCK_H

typedef struct {
    int fd; /* -1 when not held */
} lockfile_t;

/* Atomically acquires an exclusive, non-blocking lock on `path`
 * (open(O_CREAT) + flock(LOCK_EX|LOCK_NB) -- no TOCTOU race, and the OS
 * releases the lock automatically if the process dies, so no stale-lock
 * cleanup is ever needed). On success writes the caller's PID into the
 * file and returns 1. Returns 0 if another process already holds the
 * lock, or -1 on any other error (permissions, disk full, ...). */
int lock_acquire(lockfile_t *lock, const char *path);

/* Releases the lock (if held) and unlinks the lock file. Safe to call on
 * a lockfile_t that failed to acquire. */
void lock_release(lockfile_t *lock, const char *path);

#endif
