#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    int64_t size;
    void* data;
} mlang_list_t;

static char* mlang_strdup(const char* s)
{
    if(!s)
        return NULL;
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1);
    if(!out)
        return NULL;
    (void)memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

static char* read_line_alloc(FILE* fp)
{
    if(!fp)
        return NULL;

    size_t cap = 128;
    size_t len = 0;
    char* buf = (char*)malloc(cap);
    if(!buf)
        return NULL;

    for(;;)
    {
        int ch = fgetc(fp);
        if(ch == EOF)
        {
            if(len == 0)
            {
                free(buf);
                return NULL;
            }
            break;
        }

        if(ch == '\n')
            break;
        if(ch == '\r')
            continue;

        if(len + 1 >= cap)
        {
            size_t next = cap * 2;
            char* p = (char*)realloc(buf, next);
            if(!p)
            {
                free(buf);
                return NULL;
            }
            buf = p;
            cap = next;
        }
        buf[len++] = (char)ch;
    }

    buf[len] = '\0';
    return buf;
}

int64_t __mlang_std_fs_open_read(const char* path)
{
    if(!path)
        return 0;
    FILE* fp = fopen(path, "rb");
    return (int64_t)(intptr_t)fp;
}

int64_t __mlang_std_fs_open_write(const char* path)
{
    if(!path)
        return 0;
    FILE* fp = fopen(path, "wb");
    return (int64_t)(intptr_t)fp;
}

int __mlang_std_fs_close(int64_t handle)
{
    FILE* fp = (FILE*)(intptr_t)handle;
    if(!fp)
        return 0;
    return fclose(fp);
}

int64_t __mlang_std_fs_read_line(int64_t handle, char* buf, int64_t capacity)
{
    FILE* fp = (FILE*)(intptr_t)handle;
    if(!fp || !buf || capacity <= 1)
        return -1;

    char* line = read_line_alloc(fp);
    if(!line)
        return -1;

    int64_t srcLen = (int64_t)strlen(line);
    int64_t n = srcLen < (capacity - 1) ? srcLen : (capacity - 1);
    if(n > 0)
        (void)memcpy(buf, line, (size_t)n);
    buf[n] = '\0';
    free(line);
    return n;
}

int64_t __mlang_std_fs_write(int64_t handle, const char* s)
{
    FILE* fp = (FILE*)(intptr_t)handle;
    if(!fp || !s)
        return -1;
    size_t n = strlen(s);
    size_t w = fwrite(s, 1, n, fp);
    fflush(fp);
    return (int64_t)w;
}

mlang_list_t __mlang_std_fs_read_all_lines(int64_t handle)
{
    mlang_list_t out;
    out.size = 0;
    out.data = NULL;

    FILE* fp = (FILE*)(intptr_t)handle;
    if(!fp)
        return out;

    if(fseek(fp, 0, SEEK_SET) != 0)
        return out;

    size_t cap = 16;
    char** lines = (char**)malloc(sizeof(char*) * cap);
    if(!lines)
        return out;

    size_t count = 0;
    for(;;)
    {
        char* line = read_line_alloc(fp);
        if(!line)
            break;

        if(count == cap)
        {
            size_t next = cap * 2;
            char** p = (char**)realloc(lines, sizeof(char*) * next);
            if(!p)
            {
                free(line);
                break;
            }
            lines = p;
            cap = next;
        }

        lines[count++] = line;
    }

    if(count == 0)
    {
        free(lines);
        return out;
    }

    out.size = (int64_t)count;
    out.data = (void*)lines;
    return out;
}

void __mlang_std_fs_free_lines(mlang_list_t lines)
{
    if(lines.size <= 0 || !lines.data)
        return;

    char** data = (char**)lines.data;
    for(int64_t i = 0; i < lines.size; ++i)
    {
        free(data[i]);
    }
    free(data);
}
