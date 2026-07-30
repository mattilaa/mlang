#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

enum
{
    MLANG_LOG_OUT = 0,
    MLANG_LOG_WARN = 1,
    MLANG_LOG_ERR = 2,
    MLANG_LOG_LEVEL_COUNT = 3
};

static pthread_mutex_t g_log_mutex;
static pthread_once_t g_log_once = PTHREAD_ONCE_INIT;
static FILE* g_forward_files[MLANG_LOG_LEVEL_COUNT];
static int g_timestamps_enabled;
static int g_timestamp_colors_enabled = 1;
static char g_timestamp_colors[MLANG_LOG_LEVEL_COUNT][32] = {
    "\033[32m",
    "\033[33m",
    "\033[31m"
};

typedef struct MlangLog
{
    FILE* forward_files[MLANG_LOG_LEVEL_COUNT];
    int timestamps_enabled;
    int timestamp_colors_enabled;
    char timestamp_colors[MLANG_LOG_LEVEL_COUNT][32];
} MlangLog;

static int valid_level(int level);

static void init_log_mutex(void)
{
    (void)pthread_mutex_init(&g_log_mutex, NULL);
}

static FILE* console_for_level(int level)
{
    return level == MLANG_LOG_OUT ? stdout : stderr;
}

static const char* prefix_for_level(int level)
{
    if(level == MLANG_LOG_WARN)
        return "warn: ";
    if(level == MLANG_LOG_ERR)
        return "err: ";
    return "";
}

static void init_logger_defaults(MlangLog* logger)
{
    if(!logger)
        return;
    logger->timestamp_colors_enabled = 1;
    for(int level = 0; level < MLANG_LOG_LEVEL_COUNT; ++level)
    {
        (void)snprintf(logger->timestamp_colors[level],
                       sizeof(logger->timestamp_colors[level]),
                       "%s",
                       g_timestamp_colors[level]);
    }
}

static int set_color_code(char dest[32], const char* color)
{
    if(!dest || !color)
        return -1;
    (void)snprintf(dest, 32, "%s", color);
    return 0;
}

static int set_sgr_color_code(char dest[32], int sgr_code)
{
    if(!dest)
        return -1;
    if(sgr_code <= 0)
    {
        dest[0] = '\0';
        return 0;
    }
    (void)snprintf(dest, 32, "\033[%dm", sgr_code);
    return 0;
}

static int64_t write_log_line(FILE* fp,
                              int level,
                              int timestamps_enabled,
                              int timestamp_colors_enabled,
                              const char timestamp_colors[MLANG_LOG_LEVEL_COUNT][32],
                              const char* prefix,
                              const char* msg)
{
    if(!fp || !msg)
        return -1;

    const size_t prefix_len = strlen(prefix);
    const size_t msg_len = strlen(msg);
    size_t written = 0;

    if(timestamps_enabled)
    {
        time_t now = time(NULL);
        struct tm tm_now;
#if defined(_WIN32)
        localtime_s(&tm_now, &now);
#else
        localtime_r(&now, &tm_now);
#endif
        char timestamp[32];
        int timestamp_len = snprintf(timestamp,
                                     sizeof(timestamp),
                                     "[%d/%d/%04d/%02d:%02d:%02d] ",
                                     tm_now.tm_mon + 1,
                                     tm_now.tm_mday,
                                     tm_now.tm_year + 1900,
                                     tm_now.tm_hour,
                                     tm_now.tm_min,
                                     tm_now.tm_sec);
        if(timestamp_colors_enabled && valid_level(level))
        {
            const char* color = timestamp_colors[level];
            if(color && color[0] != '\0')
                written += fwrite(color, 1, strlen(color), fp);
        }
        if(timestamp_len > 0)
            written += fwrite(timestamp, 1, (size_t)timestamp_len, fp);
        if(timestamp_colors_enabled)
            written += fwrite("\033[37m", 1, 5, fp);
    }
    if(prefix_len > 0)
        written += fwrite(prefix, 1, prefix_len, fp);
    written += fwrite(msg, 1, msg_len, fp);
    if(timestamps_enabled && timestamp_colors_enabled)
        written += fwrite("\033[0m", 1, 4, fp);
    written += fwrite("\n", 1, 1, fp);
    if(fflush(fp) != 0)
        return -1;

    return (int64_t)written;
}

static int valid_level(int level)
{
    return level >= 0 && level < MLANG_LOG_LEVEL_COUNT;
}

int64_t __mlang_std_log_write(int level, const char* msg)
{
    if(!valid_level(level) || !msg)
        return -1;

    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);

    const char* prefix = prefix_for_level(level);
    int64_t console_written = write_log_line(console_for_level(level),
                                             level,
                                             g_timestamps_enabled,
                                             g_timestamp_colors_enabled,
                                             (const char (*)[32])g_timestamp_colors,
                                             prefix,
                                             msg);

    FILE* forward = g_forward_files[level];
    if(forward)
        (void)write_log_line(forward,
                             level,
                             g_timestamps_enabled,
                             g_timestamp_colors_enabled,
                             (const char (*)[32])g_timestamp_colors,
                             prefix,
                             msg);

    (void)pthread_mutex_unlock(&g_log_mutex);
    return console_written;
}

int64_t __mlang_std_log_out(const char* msg)
{
    return __mlang_std_log_write(MLANG_LOG_OUT, msg);
}

int64_t __mlang_std_log_warn(const char* msg)
{
    return __mlang_std_log_write(MLANG_LOG_WARN, msg);
}

int64_t __mlang_std_log_err(const char* msg)
{
    return __mlang_std_log_write(MLANG_LOG_ERR, msg);
}

static int set_forward_file_locked(int level, const char* path)
{
    if(!valid_level(level))
        return -1;

    FILE* next = NULL;
    if(path && path[0] != '\0')
    {
        next = fopen(path, "ab");
        if(!next)
            return -1;
    }

    if(g_forward_files[level])
        (void)fclose(g_forward_files[level]);
    g_forward_files[level] = next;
    return 0;
}

static int set_logger_forward_file_locked(MlangLog* logger, int level, const char* path)
{
    if(!logger || !valid_level(level))
        return -1;

    FILE* next = NULL;
    if(path && path[0] != '\0')
    {
        next = fopen(path, "ab");
        if(!next)
            return -1;
    }

    if(logger->forward_files[level])
        (void)fclose(logger->forward_files[level]);
    logger->forward_files[level] = next;
    return 0;
}

static int set_logger_all_forward_files_locked(MlangLog* logger, const char* path)
{
    if(!logger || !path || path[0] == '\0')
        return -1;

    int rc = 0;
    for(int level = 0; level < MLANG_LOG_LEVEL_COUNT; ++level)
    {
        if(set_logger_forward_file_locked(logger, level, path) != 0)
            rc = -1;
    }
    return rc;
}

static void close_logger_forward_files_locked(MlangLog* logger)
{
    if(!logger)
        return;

    for(int level = 0; level < MLANG_LOG_LEVEL_COUNT; ++level)
    {
        if(logger->forward_files[level])
        {
            (void)fclose(logger->forward_files[level]);
            logger->forward_files[level] = NULL;
        }
    }
}

int __mlang_std_log_forward_level_to_file(int level, const char* path)
{
    if(!valid_level(level))
        return -1;

    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);
    int rc = set_forward_file_locked(level, path);
    (void)pthread_mutex_unlock(&g_log_mutex);
    return rc;
}

int __mlang_std_log_forward_all_to_file(const char* path)
{
    if(!path || path[0] == '\0')
        return -1;

    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);

    int rc = 0;
    for(int level = 0; level < MLANG_LOG_LEVEL_COUNT; ++level)
    {
        if(set_forward_file_locked(level, path) != 0)
            rc = -1;
    }

    (void)pthread_mutex_unlock(&g_log_mutex);
    return rc;
}

int __mlang_std_log_clear_forwarding(void)
{
    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);

    for(int level = 0; level < MLANG_LOG_LEVEL_COUNT; ++level)
    {
        if(g_forward_files[level])
        {
            (void)fclose(g_forward_files[level]);
            g_forward_files[level] = NULL;
        }
    }

    (void)pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

int __mlang_std_log_set_timestamps_enabled(int enabled)
{
    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);
    g_timestamps_enabled = enabled ? 1 : 0;
    (void)pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

int __mlang_std_log_set_timestamp_colors_enabled(int enabled)
{
    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);
    g_timestamp_colors_enabled = enabled ? 1 : 0;
    (void)pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

int __mlang_std_log_set_timestamp_color(int level, const char* color)
{
    if(!valid_level(level))
        return -1;

    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);
    int rc = set_color_code(g_timestamp_colors[level], color ? color : "");
    (void)pthread_mutex_unlock(&g_log_mutex);
    return rc;
}

int __mlang_std_log_set_timestamp_color_code(int level, int sgr_code)
{
    if(!valid_level(level))
        return -1;

    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);
    int rc = set_sgr_color_code(g_timestamp_colors[level], sgr_code);
    (void)pthread_mutex_unlock(&g_log_mutex);
    return rc;
}

int64_t __mlang_std_log_new_with_options(const char* path, int timestamps_enabled)
{
    (void)pthread_once(&g_log_once, init_log_mutex);

    MlangLog* logger = (MlangLog*)calloc(1, sizeof(MlangLog));
    if(!logger)
        return 0;
    init_logger_defaults(logger);
    logger->timestamps_enabled = timestamps_enabled ? 1 : 0;

    if(path && path[0] != '\0')
    {
        (void)pthread_mutex_lock(&g_log_mutex);
        int rc = set_logger_all_forward_files_locked(logger, path);
        (void)pthread_mutex_unlock(&g_log_mutex);
        if(rc != 0)
        {
            free(logger);
            return 0;
        }
    }

    return (int64_t)(intptr_t)logger;
}

int64_t __mlang_std_log_new(const char* path)
{
    return __mlang_std_log_new_with_options(path, 0);
}

int __mlang_std_log_set_logger_output_path(int64_t handle, const char* path)
{
    MlangLog* logger = (MlangLog*)(intptr_t)handle;
    if(!logger || !path || path[0] == '\0')
        return -1;

    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);
    int rc = set_logger_all_forward_files_locked(logger, path);
    (void)pthread_mutex_unlock(&g_log_mutex);
    return rc;
}

int __mlang_std_log_reset_logger_output_path(int64_t handle)
{
    MlangLog* logger = (MlangLog*)(intptr_t)handle;
    if(!logger)
        return -1;

    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);
    close_logger_forward_files_locked(logger);
    (void)pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

int __mlang_std_log_set_logger_timestamps_enabled(int64_t handle, int enabled)
{
    MlangLog* logger = (MlangLog*)(intptr_t)handle;
    if(!logger)
        return -1;

    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);
    logger->timestamps_enabled = enabled ? 1 : 0;
    (void)pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

int __mlang_std_log_set_logger_timestamp_colors_enabled(int64_t handle, int enabled)
{
    MlangLog* logger = (MlangLog*)(intptr_t)handle;
    if(!logger)
        return -1;

    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);
    logger->timestamp_colors_enabled = enabled ? 1 : 0;
    (void)pthread_mutex_unlock(&g_log_mutex);
    return 0;
}

int __mlang_std_log_set_logger_timestamp_color(int64_t handle, int level, const char* color)
{
    MlangLog* logger = (MlangLog*)(intptr_t)handle;
    if(!logger || !valid_level(level))
        return -1;

    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);
    int rc = set_color_code(logger->timestamp_colors[level], color ? color : "");
    (void)pthread_mutex_unlock(&g_log_mutex);
    return rc;
}

int __mlang_std_log_set_logger_timestamp_color_code(int64_t handle, int level, int sgr_code)
{
    MlangLog* logger = (MlangLog*)(intptr_t)handle;
    if(!logger || !valid_level(level))
        return -1;

    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);
    int rc = set_sgr_color_code(logger->timestamp_colors[level], sgr_code);
    (void)pthread_mutex_unlock(&g_log_mutex);
    return rc;
}

int __mlang_std_log_close(int64_t handle)
{
    MlangLog* logger = (MlangLog*)(intptr_t)handle;
    if(!logger)
        return 0;

    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);
    close_logger_forward_files_locked(logger);
    (void)pthread_mutex_unlock(&g_log_mutex);
    free(logger);
    return 0;
}

int64_t __mlang_std_log_logger_write(int64_t handle, int level, const char* msg)
{
    MlangLog* logger = (MlangLog*)(intptr_t)handle;
    if(!logger || !valid_level(level) || !msg)
        return -1;

    (void)pthread_once(&g_log_once, init_log_mutex);
    (void)pthread_mutex_lock(&g_log_mutex);

    const char* prefix = prefix_for_level(level);
    int64_t console_written = write_log_line(console_for_level(level),
                                             level,
                                             logger->timestamps_enabled,
                                             logger->timestamp_colors_enabled,
                                             (const char (*)[32])logger->timestamp_colors,
                                             prefix,
                                             msg);

    FILE* forward = logger->forward_files[level];
    if(forward)
        (void)write_log_line(forward,
                             level,
                             logger->timestamps_enabled,
                             logger->timestamp_colors_enabled,
                             (const char (*)[32])logger->timestamp_colors,
                             prefix,
                             msg);

    (void)pthread_mutex_unlock(&g_log_mutex);
    return console_written;
}
