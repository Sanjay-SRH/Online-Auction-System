#ifndef LOGGER_H
#define LOGGER_H

#include "common.h"

void logger_init(void);
void logger_log(const char *level, const char *fmt, ...);
#define LOG_INFO(...)  logger_log("INFO",  __VA_ARGS__)
#define LOG_WARN(...)  logger_log("WARN",  __VA_ARGS__)
#define LOG_ERROR(...) logger_log("ERROR", __VA_ARGS__)

#endif /* LOGGER_H */
