#include "log.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define ROTATE_SIZE (5 * 1024 * 1024)

static void rotate_if_needed(const char *log_file)
{
    struct stat st;
    if (stat(log_file, &st) == 0 && st.st_size > ROTATE_SIZE) {
        char old_path[1024];
        snprintf(old_path, sizeof(old_path), "%s.old", log_file);
        rename(log_file, old_path);
    }
}

void log_msg(const char *log_file, const char *level, const char *fmt, ...)
{
    rotate_if_needed(log_file);

    int fd = open(log_file, O_CREAT | O_WRONLY | O_APPEND, 0644);
    if (fd < 0) {
        return; /* best-effort logging: nothing sensible to do on failure */
    }

    if (flock(fd, LOCK_EX) != 0) {
        close(fd);
        return;
    }

    time_t now = time(NULL);
    struct tm tmv;
    gmtime_r(&now, &tmv);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tmv);

    char upper_level[16];
    size_t i = 0;
    for (; level[i] && i < sizeof(upper_level) - 1; i++) {
        upper_level[i] = (char) toupper((unsigned char) level[i]);
    }
    upper_level[i] = '\0';

    char message[4096];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(message, sizeof(message), fmt, ap);
    va_end(ap);

    char line[4224];
    int n = snprintf(line, sizeof(line), "[%s] [%s] %s\n", ts, upper_level, message);
    if (n > 0) {
        size_t total = (size_t) n < sizeof(line) ? (size_t) n : sizeof(line) - 1;
        size_t written = 0;
        while (written < total) {
            ssize_t w = write(fd, line + written, total - written);
            if (w <= 0) {
                break;
            }
            written += (size_t) w;
        }
    }

    flock(fd, LOCK_UN);
    close(fd);
}
