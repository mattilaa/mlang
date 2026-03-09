#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>

static char g_protocol_last_error[256] = "";
static int32_t g_protocol_last_opcode = 0;

static void protocol_set_error(const char* msg)
{
    if(!msg)
        msg = "unknown protocol error";
    snprintf(g_protocol_last_error, sizeof(g_protocol_last_error), "%s", msg);
}

static int write_exact(int fd, const unsigned char* data, size_t n)
{
    size_t off = 0u;
    while(off < n)
    {
        ssize_t w = write(fd, data + off, n - off);
        if(w < 0)
        {
            if(errno == EINTR)
                continue;
            return 0;
        }
        if(w == 0)
            return 0;
        off += (size_t)w;
    }
    return 1;
}

static int read_exact(int fd, unsigned char* data, size_t n)
{
    size_t off = 0u;
    while(off < n)
    {
        ssize_t r = read(fd, data + off, n - off);
        if(r < 0)
        {
            if(errno == EINTR)
                continue;
            return 0;
        }
        if(r == 0)
            return 0;
        off += (size_t)r;
    }
    return 1;
}

static uint32_t load_be_u32(const unsigned char* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static void store_be_u32(unsigned char* p, uint32_t v)
{
    p[0] = (unsigned char)((v >> 24) & 0xFFu);
    p[1] = (unsigned char)((v >> 16) & 0xFFu);
    p[2] = (unsigned char)((v >> 8) & 0xFFu);
    p[3] = (unsigned char)(v & 0xFFu);
}

const char* __mlang_std_protocol_last_error(void)
{
    return g_protocol_last_error;
}

int32_t __mlang_std_protocol_last_opcode(void)
{
    return g_protocol_last_opcode;
}

int64_t __mlang_std_protocol_stream_write_text(int64_t socket_handle,
                                               int32_t opcode,
                                               const char* payload)
{
    int fd = (int)socket_handle;
    if(fd <= 0)
    {
        protocol_set_error("invalid socket handle");
        return -1;
    }

    if(!payload)
        payload = "";

    size_t payload_len = strlen(payload);
    if(payload_len > 0xFFFFFFFFu)
    {
        protocol_set_error("payload too large");
        return -1;
    }

    unsigned char header[12];
    header[0] = 'M';
    header[1] = 'L';
    header[2] = 'P';
    header[3] = '1';
    store_be_u32(header + 4, (uint32_t)opcode);
    store_be_u32(header + 8, (uint32_t)payload_len);

    if(!write_exact(fd, header, sizeof(header)))
    {
        protocol_set_error("failed to write frame header");
        return -1;
    }
    if(payload_len > 0u &&
       !write_exact(fd, (const unsigned char*)payload, payload_len))
    {
        protocol_set_error("failed to write frame payload");
        return -1;
    }

    g_protocol_last_error[0] = '\0';
    return (int64_t)payload_len;
}

int64_t __mlang_std_protocol_stream_read_text(int64_t socket_handle,
                                              char* out,
                                              int64_t capacity,
                                              int64_t max_payload_bytes)
{
    int fd = (int)socket_handle;
    if(fd <= 0 || !out || capacity <= 0)
    {
        protocol_set_error("invalid read arguments");
        return -1;
    }

    unsigned char header[12];
    if(!read_exact(fd, header, sizeof(header)))
    {
        protocol_set_error("failed to read frame header");
        return -1;
    }

    if(header[0] != 'M' || header[1] != 'L' || header[2] != 'P' ||
       header[3] != '1')
    {
        protocol_set_error("invalid frame magic");
        return -1;
    }

    uint32_t opcode_u32 = load_be_u32(header + 4);
    uint32_t payload_len_u32 = load_be_u32(header + 8);
    size_t payload_len = (size_t)payload_len_u32;

    if(max_payload_bytes >= 0 && payload_len > (size_t)max_payload_bytes)
    {
        protocol_set_error("payload exceeds configured maximum");
        return -1;
    }

    char* payload = (char*)malloc(payload_len + 1u);
    if(!payload)
    {
        protocol_set_error("allocation failed");
        return -1;
    }

    if(payload_len > 0u &&
       !read_exact(fd, (unsigned char*)payload, payload_len))
    {
        free(payload);
        protocol_set_error("failed to read frame payload");
        return -1;
    }
    payload[payload_len] = '\0';

    if(payload_len + 1u > (size_t)capacity)
    {
        free(payload);
        protocol_set_error("output buffer too small for payload");
        return -1;
    }

    if(payload_len > 0u)
        memcpy(out, payload, payload_len);
    out[payload_len] = '\0';

    free(payload);
    g_protocol_last_opcode = (int32_t)opcode_u32;
    g_protocol_last_error[0] = '\0';
    return (int64_t)payload_len;
}
