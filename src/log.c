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

/* Replaces every C0 control character (including \n, \r, ESC) and DEL with a
 * visible "\xHH" escape so a value that ends up in a log message can never
 * forge a fake "[timestamp] [LEVEL] ..." line or smuggle a raw terminal
 * escape sequence to whoever later tails/cats the log file (CWE-117 log
 * injection). Regular bytes, including multi-byte UTF-8 (always >= 0x80),
 * are copied through unchanged. */
static void escape_control_chars(const char *in, char *out, size_t out_size)
{
    size_t oi = 0;
    for (size_t i = 0; in[i] != '\0'; i++) {
        unsigned char c = (unsigned char) in[i];
        if (c < 0x20 || c == 0x7F) {
            if (oi + 5 >= out_size) {
                break;
            }
            oi += (size_t) snprintf(out + oi, out_size - oi, "\\x%02x", c);
        } else {
            if (oi + 2 >= out_size) {
                break;
            }
            out[oi++] = (char) c;
        }
    }
    out[oi] = '\0';
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

    char safe_message[sizeof(message) * 4];
    escape_control_chars(message, safe_message, sizeof(safe_message));

    char line[sizeof(safe_message) + 128];
    int n = snprintf(line, sizeof(line), "[%s] [%s] %s\n", ts, upper_level, safe_message);
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
