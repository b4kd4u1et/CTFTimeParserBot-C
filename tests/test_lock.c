// deps: lock.c
#include <stdio.h>
#include <unistd.h>

#include "lock.h"
#include "test_helpers.h"

static void test_normal_acquire_release_cycle(void)
{
    const char *path = "/tmp/ctf_test_lock_normal.lock";
    unlink(path);

    lockfile_t l1;
    CHECK(lock_acquire(&l1, path) == 1, "first acquire should succeed");
    lock_release(&l1, path);
    CHECK(access(path, F_OK) == 0, "the lock file should persist after release (no unlink -- see K2)");

    lockfile_t l2;
    CHECK(lock_acquire(&l2, path) == 1, "re-acquiring the persisted lock file should succeed");
    lock_release(&l2, path);

    unlink(path);
}

static void test_concurrent_acquire_is_refused(void)
{
    const char *path = "/tmp/ctf_test_lock_concurrent.lock";
    unlink(path);

    lockfile_t holder;
    CHECK(lock_acquire(&holder, path) == 1, "first process should acquire the lock");

    lockfile_t contender;
    CHECK(lock_acquire(&contender, path) == 0,
          "a second acquire while the first is held should return 0 (not block, not succeed)");

    lock_release(&holder, path);
    unlink(path);
}

/* Regression test for the audit's K1 finding: open() must refuse to follow
 * a pre-existing symlink at the lock path, and must never touch its target. */
static void test_symlink_attack_refused(void)
{
    const char *target = "/tmp/ctf_test_lock_target.txt";
    const char *symlink_path = "/tmp/ctf_test_lock_symlink.lock";

    FILE *f = fopen(target, "w");
    CHECK(f != NULL, "setup: should be able to create the target file");
    if (f) {
        fputs("IMPORTANT DATA\n", f);
        fclose(f);
    }

    unlink(symlink_path);
    CHECK(symlink(target, symlink_path) == 0, "setup: should be able to create the symlink");

    lockfile_t l;
    int r = lock_acquire(&l, symlink_path);
    CHECK(r == -1, "acquiring a lock at a path that is a symlink must be refused, not follow it");

    FILE *check = fopen(target, "r");
    char buf[64] = {0};
    if (check) {
        size_t n = fread(buf, 1, sizeof(buf) - 1, check);
        (void) n;
        fclose(check);
    }
    CHECK_STR_EQ(buf, "IMPORTANT DATA\n", "the symlink target's content must be untouched");

    unlink(symlink_path);
    unlink(target);
}

int main(void)
{
    test_normal_acquire_release_cycle();
    test_concurrent_acquire_is_refused();
    test_symlink_attack_refused();
    TEST_SUMMARY();
}
