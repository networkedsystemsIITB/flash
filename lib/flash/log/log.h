/**
 * Copyright (c) 2020 rxi
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See `log.c` for details.
 *
 * Taken from https://github.com/rxi/log.c and modified for flash
 */

#ifndef LOG_H
#define LOG_H

typedef struct log_Event log_Event;
typedef void (*log_LogFn)(log_Event *ev);
typedef void (*log_LockFn)(int lock, void *udata);

enum { LOG_TRACE, LOG_DEBUG, LOG_INFO, LOG_WARN, LOG_ERROR, LOG_FATAL };

// clang-format off
#ifdef LOG_ENABLE_DEBUG
#define log_trace(...) log_log(LOG_TRACE, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_debug(...) log_log(LOG_DEBUG, __FILE__, __LINE__, __func__, __VA_ARGS__)
#else
#define log_trace(...) do { } while (0)
#define log_debug(...) do { } while (0)
#endif
#define log_info(...) log_log(LOG_INFO, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_warn(...) log_log(LOG_WARN, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_error(...) log_log(LOG_ERROR, __FILE__, __LINE__, __func__, __VA_ARGS__)
#define log_fatal(...) log_log(LOG_FATAL, __FILE__, __LINE__, __func__, __VA_ARGS__)
// clang-format on

const char *log_level_string(int level);
void log_set_lock(log_LockFn fn, void *udata);
void log_set_level(int level);
void log_set_quiet(int enable);
int log_add_callback(log_LogFn fn, void *udata, int level);
int log_add_fp(void *fp, int level);
void log_set_level_from_env(void);

void log_log(int level, const char *file, int line, const char *caller, const char *fmt, ...);

#define FAST_LOG_TO_FILE

#ifdef FAST_LOG_TO_FILE
#define FAST_LOG_BATCH_SIZE 1024
#define FAST_LOG_SIZE 256
#define FAST_LOG_DIR "flash_nf_logs/"

/* Log a string to a file named after the nf_id
   The log is buffered and written in batches for efficiency
   Call fast_log_flush(nf_id) to flush the buffer to the file
   NOTE: Manually clear the file contents before starting logging for a new run
*/
void fast_log(int nf_id, const char *fmt, ...);
void fast_log_flush(int nf_id);
#else

#define fast_log(...) \
	do {          \
	} while (0)
#define fast_log_flush(...) \
	do {                \
	} while (0)

#endif

#endif /* LOG_H */
