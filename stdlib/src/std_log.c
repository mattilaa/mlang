#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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

typedef struct MlangLog
{
    FILE* forward_files[MLANG_LOG_LEVEL_COUNT];
} MlangLog;

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

static int64_t write_log_line(FILE* fp, const char* prefix, const char* msg)
{
    if(!fp || !msg)
        return -1;

    const size_t prefix_len = strlen(prefix);
    const size_t msg_len = strlen(msg);
    size_t written = 0;

    if(prefix_len > 0)
        written += fwrite(prefix, 1, prefix_len, fp);
    written += fwrite(msg, 1, msg_len, fp);
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
    int64_t console_written = write_log_line(console_for_level(level), prefix, msg);

    FILE* forward = g_forward_files[level];
    if(forward)
        (void)write_log_line(forward, prefix, msg);

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

int64_t __mlang_std_log_new(const char* path)
{
    (void)pthread_once(&g_log_once, init_log_mutex);

    MlangLog* logger = (MlangLog*)calloc(1, sizeof(MlangLog));
    if(!logger)
        return 0;

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
    int64_t console_written = write_log_line(console_for_level(level), prefix, msg);

    FILE* forward = logger->forward_files[level];
    if(forward)
        (void)write_log_line(forward, prefix, msg);

    (void)pthread_mutex_unlock(&g_log_mutex);
    return console_written;
}
