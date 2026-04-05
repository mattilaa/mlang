#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

static __thread char g_last_error[512];

typedef struct
{
    int64_t width;
    int64_t height;
    uint8_t* rgba;
} mlang_image_rgba_t;

static void set_error(const char* msg)
{
    if(!msg)
        msg = "std::image: unknown error";
    (void)snprintf(g_last_error, sizeof(g_last_error), "%s", msg);
}

static char* dup_cstr(const char* s)
{
    if(!s)
        return NULL;
    size_t n = strlen(s);
    char* out = (char*)malloc(n + 1);
    if(!out)
        return NULL;
    if(n > 0)
        (void)memcpy(out, s, n);
    out[n] = '\0';
    return out;
}

char* __mlang_std_image_last_error(void)
{
    return dup_cstr(g_last_error);
}

#if defined(__APPLE__)
static int load_image_rgba(const char* path, mlang_image_rgba_t* out)
{
    if(!path || !out)
    {
        set_error("std::image: invalid arguments");
        return 0;
    }

    memset(out, 0, sizeof(*out));

    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, (const UInt8*)path, (CFIndex)strlen(path), false);
    if(!url)
    {
        set_error("std::image: failed to create file URL");
        return 0;
    }

    CGImageSourceRef src = CGImageSourceCreateWithURL(url, NULL);
    CFRelease(url);
    if(!src)
    {
        set_error("std::image: failed to open image");
        return 0;
    }

    CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, NULL);
    CFRelease(src);
    if(!img)
    {
        set_error("std::image: failed to decode image");
        return 0;
    }

    const size_t width = CGImageGetWidth(img);
    const size_t height = CGImageGetHeight(img);
    if(width == 0 || height == 0)
    {
        CGImageRelease(img);
        set_error("std::image: decoded image has zero size");
        return 0;
    }

    const size_t row_bytes = width * 4;
    uint8_t* rgba = (uint8_t*)malloc(row_bytes * height);
    if(!rgba)
    {
        CGImageRelease(img);
        set_error("std::image: out of memory");
        return 0;
    }

    CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
    if(!color_space)
    {
        free(rgba);
        CGImageRelease(img);
        set_error("std::image: failed to create RGB color space");
        return 0;
    }

    CGContextRef ctx = CGBitmapContextCreate(rgba, width, height, 8, row_bytes,
                                             color_space,
                                             kCGImageAlphaPremultipliedLast |
                                                 kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(color_space);
    if(!ctx)
    {
        free(rgba);
        CGImageRelease(img);
        set_error("std::image: failed to create bitmap context");
        return 0;
    }

    CGContextSetBlendMode(ctx, kCGBlendModeCopy);
    CGContextDrawImage(ctx, CGRectMake(0, 0, (CGFloat)width, (CGFloat)height),
                       img);
    CGContextRelease(ctx);
    CGImageRelease(img);

    out->width = (int64_t)width;
    out->height = (int64_t)height;
    out->rgba = rgba;
    return 1;
}
#else
static int load_image_rgba(const char* path, mlang_image_rgba_t* out)
{
    (void)path;
    (void)out;
    set_error("std::image: image decoding backend is currently available on macOS only");
    return 0;
}
#endif

static void free_image_rgba(mlang_image_rgba_t* img)
{
    if(!img)
        return;
    free(img->rgba);
    img->rgba = NULL;
    img->width = 0;
    img->height = 0;
}

static int64_t clamp_i64(int64_t v, int64_t lo, int64_t hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static uint8_t luminance_u8(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint8_t)((299u * (unsigned)r + 587u * (unsigned)g +
                      114u * (unsigned)b) /
                     1000u);
}

static uint8_t darken_u8(uint8_t c, uint8_t numer, uint8_t denom)
{
    if(denom == 0)
        return c;
    return (uint8_t)(((unsigned)c * (unsigned)numer) / (unsigned)denom);
}

static void fit_rect(int64_t src_w, int64_t src_h, int64_t dst_w, int64_t dst_h,
                     int64_t* fit_w, int64_t* fit_h, int64_t* off_x,
                     int64_t* off_y)
{
    if(src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
    {
        *fit_w = 0;
        *fit_h = 0;
        *off_x = 0;
        *off_y = 0;
        return;
    }

    if(src_w * dst_h > src_h * dst_w)
    {
        *fit_w = dst_w;
        *fit_h = (src_h * dst_w) / src_w;
    }
    else
    {
        *fit_h = dst_h;
        *fit_w = (src_w * dst_h) / src_h;
    }
    if(*fit_w < 1)
        *fit_w = 1;
    if(*fit_h < 1)
        *fit_h = 1;
    *off_x = (dst_w - *fit_w) / 2;
    *off_y = (dst_h - *fit_h) / 2;
}

static void sample_average_rgba(const mlang_image_rgba_t* img, int64_t dst_x,
                                int64_t dst_y, int64_t sample_w,
                                int64_t sample_h, int64_t fit_w,
                                int64_t fit_h, int64_t off_x, int64_t off_y,
                                uint8_t* out_r, uint8_t* out_g, uint8_t* out_b)
{
    if(!img || !img->rgba || fit_w <= 0 || fit_h <= 0 || sample_w <= 0 ||
       sample_h <= 0)
    {
        *out_r = 0;
        *out_g = 0;
        *out_b = 0;
        return;
    }

    if(dst_x < off_x || dst_y < off_y || dst_x >= off_x + fit_w ||
       dst_y >= off_y + fit_h)
    {
        *out_r = 8;
        *out_g = 8;
        *out_b = 12;
        return;
    }

    const int64_t local_x = dst_x - off_x;
    const int64_t local_y = dst_y - off_y;
    const int64_t x0 = (local_x * img->width) / fit_w;
    int64_t x1 = ((local_x + 1) * img->width + fit_w - 1) / fit_w;
    const int64_t y0 = (local_y * img->height) / fit_h;
    int64_t y1 = ((local_y + 1) * img->height + fit_h - 1) / fit_h;
    if(x1 <= x0)
        x1 = x0 + 1;
    if(y1 <= y0)
        y1 = y0 + 1;

    const int64_t cx0 = clamp_i64(x0, 0, img->width);
    const int64_t cx1 = clamp_i64(x1, 0, img->width);
    const int64_t cy0 = clamp_i64(y0, 0, img->height);
    const int64_t cy1 = clamp_i64(y1, 0, img->height);

    uint64_t sum_r = 0;
    uint64_t sum_g = 0;
    uint64_t sum_b = 0;
    uint64_t sum_a = 0;
    uint64_t count = 0;
    for(int64_t sy = cy0; sy < cy1; ++sy)
    {
        const uint8_t* row = img->rgba + (size_t)(sy * img->width * 4);
        for(int64_t sx = cx0; sx < cx1; ++sx)
        {
            const uint8_t* px = row + (size_t)(sx * 4);
            sum_r += px[0];
            sum_g += px[1];
            sum_b += px[2];
            sum_a += px[3];
            ++count;
        }
    }

    if(count == 0)
    {
        *out_r = 0;
        *out_g = 0;
        *out_b = 0;
        return;
    }

    uint64_t r = sum_r / count;
    uint64_t g = sum_g / count;
    uint64_t b = sum_b / count;
    const uint64_t a = sum_a / count;
    if(a < 255)
    {
        r = (r * a + 8 * (255 - a)) / 255;
        g = (g * a + 8 * (255 - a)) / 255;
        b = (b * a + 12 * (255 - a)) / 255;
    }

    *out_r = (uint8_t)r;
    *out_g = (uint8_t)g;
    *out_b = (uint8_t)b;
}

static size_t append_text(char* out, size_t cap, size_t pos, const char* text)
{
    if(!out || !text || pos >= cap)
        return pos;
    while(*text && pos + 1 < cap)
        out[pos++] = *text++;
    out[pos] = '\0';
    return pos;
}

static size_t append_rgb_sgr(char* out, size_t cap, size_t pos, int is_bg,
                             uint8_t r, uint8_t g, uint8_t b)
{
    if(!out || pos >= cap)
        return pos;
    const int wrote = snprintf(out + pos, cap - pos, "\x1b[%d;2;%u;%u;%um",
                               is_bg ? 48 : 38, (unsigned)r, (unsigned)g,
                               (unsigned)b);
    if(wrote <= 0)
        return pos;
    pos += (size_t)wrote;
    if(pos >= cap)
        pos = cap - 1;
    out[pos] = '\0';
    return pos;
}

static size_t append_braille_utf8(char* out, size_t cap, size_t pos,
                                  uint8_t bits)
{
    if(!out || pos + 4 >= cap)
        return pos;
    uint32_t code = 0x2800u + (uint32_t)bits;
    out[pos++] = (char)(0xE0u | ((code >> 12) & 0x0Fu));
    out[pos++] = (char)(0x80u | ((code >> 6) & 0x3Fu));
    out[pos++] = (char)(0x80u | (code & 0x3Fu));
    out[pos] = '\0';
    return pos;
}

static const char* density_glyph_for_luma(uint8_t luma)
{
    if(luma < 18)
        return " ";
    if(luma < 56)
        return "·";
    if(luma < 96)
        return "░";
    if(luma < 144)
        return "▒";
    if(luma < 208)
        return "▓";
    return "█";
}

int64_t __mlang_std_image_probe_width(const char* path)
{
    mlang_image_rgba_t img;
    if(!load_image_rgba(path, &img))
        return -1;
    const int64_t width = img.width;
    free_image_rgba(&img);
    return width;
}

int64_t __mlang_std_image_probe_height(const char* path)
{
    mlang_image_rgba_t img;
    if(!load_image_rgba(path, &img))
        return -1;
    const int64_t height = img.height;
    free_image_rgba(&img);
    return height;
}

char* __mlang_std_image_render_truecolor(const char* path, int64_t columns,
                                         int64_t rows, int32_t glyph_mode)
{
    if(columns <= 0 || rows <= 0)
    {
        set_error("std::image: columns and rows must be > 0");
        return dup_cstr("");
    }

    mlang_image_rgba_t img;
    if(!load_image_rgba(path, &img))
        return dup_cstr("");

    const int use_half_blocks = glyph_mode == 0 ? 1 : 0;
    const int use_full_blocks = glyph_mode == 1 ? 1 : 0;
    const int use_density = glyph_mode == 2 ? 1 : 0;
    const int use_braille = glyph_mode == 3 ? 1 : 0;
    const int64_t sample_w = use_braille ? columns * 2 : columns;
    const int64_t sample_h =
        use_braille ? rows * 4 : (use_half_blocks ? rows * 2 : rows);

    int64_t fit_w = 0;
    int64_t fit_h = 0;
    int64_t off_x = 0;
    int64_t off_y = 0;
    fit_rect(img.width, img.height, sample_w, sample_h, &fit_w, &fit_h, &off_x,
             &off_y);

    const size_t estimate = (size_t)(rows * columns * 48 + rows * 8 + 64);
    char* out = (char*)malloc(estimate);
    if(!out)
    {
        free_image_rgba(&img);
        set_error("std::image: out of memory");
        return dup_cstr("");
    }
    out[0] = '\0';
    size_t pos = 0;

    for(int64_t y = 0; y < rows; ++y)
    {
        for(int64_t x = 0; x < columns; ++x)
        {
            uint8_t r0 = 0, g0 = 0, b0 = 0;
            uint8_t r1 = 0, g1 = 0, b1 = 0;
            sample_average_rgba(&img, use_braille ? x * 2 : x,
                                use_half_blocks ? y * 2 : (use_braille ? y * 4 : y), sample_w,
                                sample_h, fit_w, fit_h, off_x, off_y, &r0, &g0,
                                &b0);
            if(use_half_blocks)
            {
                sample_average_rgba(&img, x, y * 2 + 1, sample_w, sample_h,
                                    fit_w, fit_h, off_x, off_y, &r1, &g1, &b1);
                pos = append_rgb_sgr(out, estimate, pos, 0, r0, g0, b0);
                pos = append_rgb_sgr(out, estimate, pos, 1, r1, g1, b1);
                pos = append_text(out, estimate, pos, "▀");
            }
            else if(use_full_blocks)
            {
                pos = append_rgb_sgr(out, estimate, pos, 0, r0, g0, b0);
                pos = append_text(out, estimate, pos, "█");
            }
            else if(use_density)
            {
                const uint8_t luma = luminance_u8(r0, g0, b0);
                pos = append_rgb_sgr(out, estimate, pos, 0, r0, g0, b0);
                pos = append_rgb_sgr(out, estimate, pos, 1, darken_u8(r0, 1, 5),
                                     darken_u8(g0, 1, 5), darken_u8(b0, 1, 5));
                pos = append_text(out, estimate, pos, density_glyph_for_luma(luma));
            }
            else if(use_braille)
            {
                static const uint8_t bit_table[4][2] = {
                    {0x01u, 0x08u}, {0x02u, 0x10u}, {0x04u, 0x20u}, {0x40u, 0x80u}};
                uint8_t sub_r[4][2];
                uint8_t sub_g[4][2];
                uint8_t sub_b[4][2];
                uint8_t sub_l[4][2];
                uint32_t sum_r = 0;
                uint32_t sum_g = 0;
                uint32_t sum_b = 0;
                uint32_t sum_l = 0;
                for(int dy = 0; dy < 4; ++dy)
                {
                    for(int dx = 0; dx < 2; ++dx)
                    {
                        sample_average_rgba(&img, x * 2 + dx, y * 4 + dy,
                                            sample_w, sample_h, fit_w, fit_h,
                                            off_x, off_y, &sub_r[dy][dx],
                                            &sub_g[dy][dx], &sub_b[dy][dx]);
                        sub_l[dy][dx] = luminance_u8(sub_r[dy][dx], sub_g[dy][dx],
                                                     sub_b[dy][dx]);
                        sum_r += sub_r[dy][dx];
                        sum_g += sub_g[dy][dx];
                        sum_b += sub_b[dy][dx];
                        sum_l += sub_l[dy][dx];
                    }
                }
                const uint8_t avg_r = (uint8_t)(sum_r / 8u);
                const uint8_t avg_g = (uint8_t)(sum_g / 8u);
                const uint8_t avg_b = (uint8_t)(sum_b / 8u);
                const uint8_t avg_l = (uint8_t)(sum_l / 8u);
                uint8_t bits = 0;
                for(int dy = 0; dy < 4; ++dy)
                {
                    for(int dx = 0; dx < 2; ++dx)
                    {
                        if(sub_l[dy][dx] + 10 >= avg_l)
                            bits |= bit_table[dy][dx];
                    }
                }
                if(bits == 0 && avg_l > 36)
                    bits = 0x01u;
                pos = append_rgb_sgr(out, estimate, pos, 0, avg_r, avg_g, avg_b);
                pos = append_rgb_sgr(out, estimate, pos, 1, darken_u8(avg_r, 1, 6),
                                     darken_u8(avg_g, 1, 6), darken_u8(avg_b, 1, 6));
                pos = append_braille_utf8(out, estimate, pos, bits);
            }
        }
        pos = append_text(out, estimate, pos, "\x1b[0m\n");
    }
    pos = append_text(out, estimate, pos, "\x1b[0m");
    free_image_rgba(&img);
    return out;
}
