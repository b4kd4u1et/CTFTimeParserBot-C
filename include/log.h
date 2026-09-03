#ifndef CTF_LOG_H
#define CTF_LOG_H

/* Appends a "[YYYY-MM-DD HH:MM:SS] [LEVEL] message" line (UTC, level
 * upper-cased) to `log_file`, creating it if necessary. Rotates the file
 * to "<log_file>.old" first if it has grown past 5 MB. The write itself
 * uses an exclusive advisory lock so concurrent writers (parser + publisher
 * sharing one log, or overlapping runs) never interleave partial lines. */
void log_msg(const char *log_file, const char *level, const char *fmt, ...)
#ifdef __GNUC__
    __attribute__((format(printf, 3, 4)))
#endif
    ;

#endif
