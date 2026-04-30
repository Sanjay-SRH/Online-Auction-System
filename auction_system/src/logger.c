#include "logger.h"
#include <stdarg.h>

static FILE *log_fp = NULL;

void logger_init(void) {
    log_fp = fopen(LOG_FILE, "a");
    if (!log_fp) {
        fprintf(stderr, "[LOGGER] Cannot open log file %s: %s\n",
                LOG_FILE, strerror(errno));
    }
}

void logger_log(const char *level, const char *fmt, ...) {
    time_t  now = time(NULL);
    char    ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", localtime(&now));

    va_list ap;
    va_start(ap, fmt);

    /* Always mirror to stderr */
    fprintf(stderr, "[%s] [%s] ", ts, level);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);

    /* Write to file too */
    if (log_fp) {
        va_start(ap, fmt);
        fprintf(log_fp, "[%s] [%s] ", ts, level);
        vfprintf(log_fp, fmt, ap);
        fprintf(log_fp, "\n");
        fflush(log_fp);
        va_end(ap);
    }
}
