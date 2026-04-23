#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mlang_platform.h"

#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

static MLANG_THREAD_LOCAL char g_last_error[512];

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

static int64_t div_round_i64(int64_t numer, int64_t denom)
{
    if(denom == 0)
        return 0;
    if(numer >= 0)
        return (numer + denom / 2) / denom;
    return (numer - denom / 2) / denom;
}

static void compute_alpha_content_bounds(const mlang_image_rgba_t* img,
                                         int64_t* out_x0, int64_t* out_y0,
                                         int64_t* out_w, int64_t* out_h)
{
    if(!img || !img->rgba || img->width <= 0 || img->height <= 0)
    {
        *out_x0 = 0;
        *out_y0 = 0;
        *out_w = 0;
        *out_h = 0;
        return;
    }

    int64_t min_x = img->width;
    int64_t min_y = img->height;
    int64_t max_x = -1;
    int64_t max_y = -1;
    for(int64_t y = 0; y < img->height; ++y)
    {
        const uint8_t* row = img->rgba + (size_t)(y * img->width * 4);
        for(int64_t x = 0; x < img->width; ++x)
        {
            const uint8_t* px = row + (size_t)(x * 4);
            if(px[3] >= 8)
            {
                if(x < min_x)
                    min_x = x;
                if(y < min_y)
                    min_y = y;
                if(x > max_x)
                    max_x = x;
                if(y > max_y)
                    max_y = y;
            }
        }
    }

    if(max_x < min_x || max_y < min_y)
    {
        *out_x0 = 0;
        *out_y0 = 0;
        *out_w = img->width;
        *out_h = img->height;
        return;
    }

    *out_x0 = min_x;
    *out_y0 = min_y;
    *out_w = max_x - min_x + 1;
    *out_h = max_y - min_y + 1;
}

static int color_close_rgb(uint8_t r, uint8_t g, uint8_t b, uint8_t br,
                           uint8_t bg, uint8_t bb, uint8_t tol)
{
    const int dr = (int)r - (int)br;
    const int dg = (int)g - (int)bg;
    const int db = (int)b - (int)bb;
    return dr <= (int)tol && dr >= -(int)tol && dg <= (int)tol &&
           dg >= -(int)tol && db <= (int)tol && db >= -(int)tol;
}

static void compute_background_crop_bounds(const mlang_image_rgba_t* img,
                                           int64_t* io_x0, int64_t* io_y0,
                                           int64_t* io_w, int64_t* io_h)
{
    if(!img || !img->rgba || *io_w <= 0 || *io_h <= 0)
        return;

    int64_t x0 = *io_x0;
    int64_t y0 = *io_y0;
    int64_t w = *io_w;
    int64_t h = *io_h;
    uint64_t sum_r = 0;
    uint64_t sum_g = 0;
    uint64_t sum_b = 0;
    uint64_t count = 0;
    for(int64_t x = x0; x < x0 + w; ++x)
    {
        const uint8_t* top = img->rgba + (size_t)((y0 * img->width + x) * 4);
        const uint8_t* bot =
            img->rgba + (size_t)(((y0 + h - 1) * img->width + x) * 4);
        sum_r += top[0] + bot[0];
        sum_g += top[1] + bot[1];
        sum_b += top[2] + bot[2];
        count += 2;
    }
    for(int64_t y = y0 + 1; y < y0 + h - 1; ++y)
    {
        const uint8_t* left = img->rgba + (size_t)((y * img->width + x0) * 4);
        const uint8_t* right =
            img->rgba + (size_t)((y * img->width + (x0 + w - 1)) * 4);
        sum_r += left[0] + right[0];
        sum_g += left[1] + right[1];
        sum_b += left[2] + right[2];
        count += 2;
    }
    if(count == 0)
        return;
    const uint8_t bg_r = (uint8_t)(sum_r / count);
    const uint8_t bg_g = (uint8_t)(sum_g / count);
    const uint8_t bg_b = (uint8_t)(sum_b / count);
    const uint8_t tol = 34u;
    const int required_pct = 90;

    while(h > 8)
    {
        int64_t matches = 0;
        for(int64_t x = x0; x < x0 + w; ++x)
        {
            const uint8_t* px = img->rgba + (size_t)((y0 * img->width + x) * 4);
            if(color_close_rgb(px[0], px[1], px[2], bg_r, bg_g, bg_b, tol))
                ++matches;
        }
        if(matches * 100 < w * required_pct)
            break;
        ++y0;
        --h;
    }

    while(h > 8)
    {
        int64_t matches = 0;
        const int64_t y = y0 + h - 1;
        for(int64_t x = x0; x < x0 + w; ++x)
        {
            const uint8_t* px = img->rgba + (size_t)((y * img->width + x) * 4);
            if(color_close_rgb(px[0], px[1], px[2], bg_r, bg_g, bg_b, tol))
                ++matches;
        }
        if(matches * 100 < w * required_pct)
            break;
        --h;
    }

    while(w > 8)
    {
        int64_t matches = 0;
        for(int64_t y = y0; y < y0 + h; ++y)
        {
            const uint8_t* px = img->rgba + (size_t)((y * img->width + x0) * 4);
            if(color_close_rgb(px[0], px[1], px[2], bg_r, bg_g, bg_b, tol))
                ++matches;
        }
        if(matches * 100 < h * required_pct)
            break;
        ++x0;
        --w;
    }

    while(w > 8)
    {
        int64_t matches = 0;
        const int64_t x = x0 + w - 1;
        for(int64_t y = y0; y < y0 + h; ++y)
        {
            const uint8_t* px = img->rgba + (size_t)((y * img->width + x) * 4);
            if(color_close_rgb(px[0], px[1], px[2], bg_r, bg_g, bg_b, tol))
                ++matches;
        }
        if(matches * 100 < h * required_pct)
            break;
        --w;
    }

    *io_x0 = x0;
    *io_y0 = y0;
    *io_w = w;
    *io_h = h;
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

static void sample_average_rgba(const mlang_image_rgba_t* img, int64_t dst_x,
                                int64_t dst_y, int64_t sample_w,
                                int64_t sample_h, int64_t fit_w,
                                int64_t fit_h, int64_t off_x, int64_t off_y,
                                int64_t src_x0, int64_t src_y0,
                                int64_t src_w, int64_t src_h,
                                uint8_t* out_r, uint8_t* out_g, uint8_t* out_b)
{
    if(!img || !img->rgba || fit_w <= 0 || fit_h <= 0 || sample_w <= 0 ||
       sample_h <= 0 || src_w <= 0 || src_h <= 0)
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
    const int64_t x0 = src_x0 + (local_x * src_w) / fit_w;
    int64_t x1 = src_x0 + ((local_x + 1) * src_w + fit_w - 1) / fit_w;
    const int64_t y0 = src_y0 + (local_y * src_h) / fit_h;
    int64_t y1 = src_y0 + ((local_y + 1) * src_h + fit_h - 1) / fit_h;
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

static size_t append_codepoint_utf8(char* out, size_t cap, size_t pos,
                                    uint32_t code)
{
    if(!out || pos >= cap)
        return pos;
    if(code <= 0x7Fu)
    {
        if(pos + 2 >= cap)
            return pos;
        out[pos++] = (char)code;
    }
    else if(code <= 0x7FFu)
    {
        if(pos + 3 >= cap)
            return pos;
        out[pos++] = (char)(0xC0u | ((code >> 6) & 0x1Fu));
        out[pos++] = (char)(0x80u | (code & 0x3Fu));
    }
    else if(code <= 0xFFFFu)
    {
        if(pos + 4 >= cap)
            return pos;
        out[pos++] = (char)(0xE0u | ((code >> 12) & 0x0Fu));
        out[pos++] = (char)(0x80u | ((code >> 6) & 0x3Fu));
        out[pos++] = (char)(0x80u | (code & 0x3Fu));
    }
    else
    {
        if(pos + 5 >= cap)
            return pos;
        out[pos++] = (char)(0xF0u | ((code >> 18) & 0x07u));
        out[pos++] = (char)(0x80u | ((code >> 12) & 0x3Fu));
        out[pos++] = (char)(0x80u | ((code >> 6) & 0x3Fu));
        out[pos++] = (char)(0x80u | (code & 0x3Fu));
    }
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

static const char* ascii_glyph_for_luma(uint8_t luma)
{
    if(luma < 16)
        return " ";
    if(luma < 36)
        return ".";
    if(luma < 56)
        return ":";
    if(luma < 80)
        return "-";
    if(luma < 108)
        return "=";
    if(luma < 136)
        return "+";
    if(luma < 168)
        return "*";
    if(luma < 204)
        return "#";
    if(luma < 232)
        return "%";
    return "@";
}

static const char* edge_ascii_glyph_for_cell(uint8_t tl, uint8_t tr, uint8_t bl,
                                             uint8_t br, uint8_t avg)
{
    const int gx = (int)tr + (int)br - (int)tl - (int)bl;
    const int gy = (int)bl + (int)br - (int)tl - (int)tr;
    const int d1 = (int)tl + (int)br - (int)tr - (int)bl;
    const int d2 = (int)tr + (int)bl - (int)tl - (int)br;
    int ax = gx < 0 ? -gx : gx;
    int ay = gy < 0 ? -gy : gy;
    int ad1 = d1 < 0 ? -d1 : d1;
    int ad2 = d2 < 0 ? -d2 : d2;
    int best = ax;
    const char* glyph = "|";
    if(ay > best)
    {
        best = ay;
        glyph = "-";
    }
    if(ad1 > best)
    {
        best = ad1;
        glyph = "\\";
    }
    if(ad2 > best)
    {
        best = ad2;
        glyph = "/";
    }
    if(best < 18)
        return ascii_glyph_for_luma(avg);
    if(best < 36)
        return ".";
    return glyph;
}

static const char* unicode_line_glyph_for_cell(uint8_t tl, uint8_t tr,
                                               uint8_t bl, uint8_t br,
                                               uint8_t avg)
{
    const int gx = (int)tr + (int)br - (int)tl - (int)bl;
    const int gy = (int)bl + (int)br - (int)tl - (int)tr;
    /* d1 = tl+br - tr-bl; d2 = -d1 always, so both diagonals share the same
       magnitude. Distinguish NW-SE (╲, d1>0) from NE-SW (╱, d1<0) by sign. */
    const int d1 = (int)tl + (int)br - (int)tr - (int)bl;
    int ax = gx < 0 ? -gx : gx;
    int ay = gy < 0 ? -gy : gy;
    int ad = d1 < 0 ? -d1 : d1;
    int best = ax;
    const char* glyph = "│";
    if(ay > best)
    {
        best = ay;
        glyph = "─";
    }
    if(ad > best)
    {
        best = ad;
        glyph = d1 >= 0 ? "╲" : "╱";
    }
    if(best < 14)
        return density_glyph_for_luma(avg);
    if(best < 28)
        return "·";
    if(ax > 42 && ay > 42)
        return "┼";
    return glyph;
}

static uint32_t quadrant_codepoint_for_bits(uint8_t bits)
{
    static const uint32_t table[16] = {
        0x20u,   0x2598u, 0x259Du, 0x2580u, 0x2596u, 0x258Cu, 0x259Eu, 0x259Bu,
        0x2597u, 0x259Au, 0x2590u, 0x259Cu, 0x2584u, 0x2599u, 0x259Fu, 0x2588u};
    return table[bits & 0x0Fu];
}

/* Sextant block codepoints (Unicode 13 "Symbols for Legacy Computing")
   Layout — bit 0=tl, 1=tr, 2=ml, 3=mr, 4=bl, 5=br:
     tl tr      bit0 bit1
     ml mr  →   bit2 bit3
     bl br      bit4 bit5
   Patterns 1-60 map linearly to U+1FB00 + pattern - 1.
   Patterns 61-62 (5/6 filled) have no dedicated codepoint; the caller
   should apply the complement trick (invert bits, swap FG/BG) before
   calling this function so the passed pattern is always 1-31.
   Pattern 0 → space.  Pattern 63 → full block. */
static uint32_t sextant_codepoint_for_bits(uint8_t bits)
{
    bits &= 0x3Fu;
    if(bits == 0u) return 0x0020u;
    if(bits == 0x3Fu) return 0x2588u;
    return 0x1FB00u + (uint32_t)bits - 1u;
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

/* render_truecolor_impl: shared body for all public render entry points.
 * crop_w > 0  → use explicit (crop_x0, crop_y0, crop_w, crop_h) instead of
 *               auto-detecting content bounds.
 * exact_rows  → if non-zero, use it as the output row count instead of
 *               computing from the aspect ratio. */
static char* render_truecolor_impl(const char* path, int32_t columns,
                                    int32_t glyph_mode,
                                    int64_t crop_x0, int64_t crop_y0,
                                    int64_t crop_w,  int64_t crop_h,
                                    int32_t exact_rows)
{
    mlang_image_rgba_t img;
    if(!load_image_rgba(path, &img))
        return dup_cstr("");

    const int use_half_blocks = glyph_mode == 0 ? 1 : 0;
    const int use_full_blocks = glyph_mode == 1 ? 1 : 0;
    const int use_density = glyph_mode == 2 ? 1 : 0;
    const int use_braille = glyph_mode == 3 ? 1 : 0;
    const int use_quadrants = glyph_mode == 4 ? 1 : 0;
    const int use_ascii = glyph_mode == 5 ? 1 : 0;
    const int use_edge_ascii = glyph_mode == 6 ? 1 : 0;
    const int use_line_art = glyph_mode == 7 ? 1 : 0;
    const int use_color_split = glyph_mode == 8 ? 1 : 0;
    const int use_sextant = glyph_mode == 9 ? 1 : 0;
    const int use_braille_color = glyph_mode == 10 ? 1 : 0;
    int64_t src_x0, src_y0, src_w, src_h;
    if(crop_w > 0)
    {
        /* Explicit viewport crop — skip auto border detection */
        src_x0 = clamp_i64(crop_x0, 0, img.width  - 1);
        src_y0 = clamp_i64(crop_y0, 0, img.height - 1);
        src_w  = clamp_i64(crop_w,  1, img.width  - src_x0);
        src_h  = clamp_i64(crop_h,  1, img.height - src_y0);
    }
    else
    {
        src_x0 = 0;
        src_y0 = 0;
        src_w  = img.width;
        src_h  = img.height;
        compute_alpha_content_bounds(&img, &src_x0, &src_y0, &src_w, &src_h);
        compute_background_crop_bounds(&img, &src_x0, &src_y0, &src_w, &src_h);
    }
    const int64_t x_sub =
        (use_braille || use_braille_color || use_quadrants || use_edge_ascii ||
         use_line_art || use_color_split || use_sextant)
            ? 2
            : 1;
    const int64_t y_sub =
        (use_braille || use_braille_color) ? 4
            : (use_sextant ? 3
                           : (use_half_blocks ? 2
                                              : ((use_quadrants ||
                                                  use_edge_ascii ||
                                                  use_line_art ||
                                                  use_color_split)
                                                     ? 2
                                                     : 1)));
    const int64_t out_cols = columns;
    int64_t out_rows = exact_rows > 0
        ? (int64_t)exact_rows
        : div_round_i64(out_cols * src_h * x_sub, src_w * y_sub);
    if(out_rows < 1)
        out_rows = 1;
    const int64_t sample_w = out_cols * x_sub;
    const int64_t sample_h = out_rows * y_sub;
    const int64_t fit_w = sample_w;
    const int64_t fit_h = sample_h;
    const int64_t off_x = 0;
    const int64_t off_y = 0;

    const size_t estimate =
        (size_t)(out_rows * out_cols * 48 + out_rows * 8 + 64);
    char* out = (char*)malloc(estimate);
    if(!out)
    {
        free_image_rgba(&img);
        set_error("std::image: out of memory");
        return dup_cstr("");
    }
    out[0] = '\0';
    size_t pos = 0;

    for(int64_t y = 0; y < out_rows; ++y)
    {
        for(int64_t x = 0; x < out_cols; ++x)
        {
            uint8_t r0 = 0, g0 = 0, b0 = 0;
            uint8_t r1 = 0, g1 = 0, b1 = 0;
            sample_average_rgba(&img,
                                (use_braille || use_quadrants ||
                                 use_edge_ascii || use_line_art ||
                                 use_color_split || use_sextant)
                                    ? x * 2
                                    : x,
                                use_half_blocks ? y * 2
                                                : (use_braille
                                                       ? y * 4
                                                       : (use_sextant
                                                              ? y * 3
                                                              : ((use_quadrants ||
                                                                  use_edge_ascii ||
                                                                  use_line_art ||
                                                                  use_color_split)
                                                                     ? y * 2
                                                                     : y))),
                                sample_w,
                                sample_h, fit_w, fit_h, off_x, off_y, src_x0,
                                src_y0, src_w, src_h, &r0, &g0,
                                &b0);
            if(use_half_blocks)
            {
                sample_average_rgba(&img, x, y * 2 + 1, sample_w, sample_h,
                                    fit_w, fit_h, off_x, off_y, src_x0,
                                    src_y0, src_w, src_h, &r1, &g1, &b1);
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
            else if(use_ascii)
            {
                const uint8_t luma = luminance_u8(r0, g0, b0);
                pos = append_rgb_sgr(out, estimate, pos, 0, r0, g0, b0);
                pos = append_rgb_sgr(out, estimate, pos, 1, darken_u8(r0, 1, 6),
                                     darken_u8(g0, 1, 6), darken_u8(b0, 1, 6));
                pos = append_text(out, estimate, pos, ascii_glyph_for_luma(luma));
            }
            else if(use_edge_ascii)
            {
                uint8_t sub_r[2][2];
                uint8_t sub_g[2][2];
                uint8_t sub_b[2][2];
                uint32_t sum_r = 0;
                uint32_t sum_g = 0;
                uint32_t sum_b = 0;
                uint32_t sum_l = 0;
                for(int dy = 0; dy < 2; ++dy)
                {
                    for(int dx = 0; dx < 2; ++dx)
                    {
                        sample_average_rgba(&img, x * 2 + dx, y * 2 + dy,
                                            sample_w, sample_h, fit_w, fit_h,
                                            off_x, off_y, src_x0, src_y0,
                                            src_w, src_h, &sub_r[dy][dx],
                                            &sub_g[dy][dx], &sub_b[dy][dx]);
                        sum_r += sub_r[dy][dx];
                        sum_g += sub_g[dy][dx];
                        sum_b += sub_b[dy][dx];
                        sum_l += luminance_u8(sub_r[dy][dx], sub_g[dy][dx],
                                              sub_b[dy][dx]);
                    }
                }
                const uint8_t avg_r = (uint8_t)(sum_r / 4u);
                const uint8_t avg_g = (uint8_t)(sum_g / 4u);
                const uint8_t avg_b = (uint8_t)(sum_b / 4u);
                const uint8_t tl = luminance_u8(sub_r[0][0], sub_g[0][0], sub_b[0][0]);
                const uint8_t tr = luminance_u8(sub_r[0][1], sub_g[0][1], sub_b[0][1]);
                const uint8_t bl = luminance_u8(sub_r[1][0], sub_g[1][0], sub_b[1][0]);
                const uint8_t br = luminance_u8(sub_r[1][1], sub_g[1][1], sub_b[1][1]);
                const uint8_t avg_l = (uint8_t)(sum_l / 4u);
                pos = append_rgb_sgr(out, estimate, pos, 0, avg_r, avg_g, avg_b);
                pos = append_rgb_sgr(out, estimate, pos, 1,
                                     darken_u8(avg_r, 1, 6),
                                     darken_u8(avg_g, 1, 6),
                                     darken_u8(avg_b, 1, 6));
                pos = append_text(out, estimate, pos,
                                  edge_ascii_glyph_for_cell(tl, tr, bl, br, avg_l));
            }
            else if(use_line_art)
            {
                uint8_t sub_r[2][2];
                uint8_t sub_g[2][2];
                uint8_t sub_b[2][2];
                uint32_t sum_r = 0;
                uint32_t sum_g = 0;
                uint32_t sum_b = 0;
                uint32_t sum_l = 0;
                for(int dy = 0; dy < 2; ++dy)
                {
                    for(int dx = 0; dx < 2; ++dx)
                    {
                        sample_average_rgba(&img, x * 2 + dx, y * 2 + dy,
                                            sample_w, sample_h, fit_w, fit_h,
                                            off_x, off_y, src_x0, src_y0,
                                            src_w, src_h, &sub_r[dy][dx],
                                            &sub_g[dy][dx], &sub_b[dy][dx]);
                        sum_r += sub_r[dy][dx];
                        sum_g += sub_g[dy][dx];
                        sum_b += sub_b[dy][dx];
                        sum_l += luminance_u8(sub_r[dy][dx], sub_g[dy][dx],
                                              sub_b[dy][dx]);
                    }
                }
                const uint8_t avg_r = (uint8_t)(sum_r / 4u);
                const uint8_t avg_g = (uint8_t)(sum_g / 4u);
                const uint8_t avg_b = (uint8_t)(sum_b / 4u);
                const uint8_t tl = luminance_u8(sub_r[0][0], sub_g[0][0], sub_b[0][0]);
                const uint8_t tr = luminance_u8(sub_r[0][1], sub_g[0][1], sub_b[0][1]);
                const uint8_t bl = luminance_u8(sub_r[1][0], sub_g[1][0], sub_b[1][0]);
                const uint8_t br = luminance_u8(sub_r[1][1], sub_g[1][1], sub_b[1][1]);
                const uint8_t avg_l = (uint8_t)(sum_l / 4u);
                pos = append_rgb_sgr(out, estimate, pos, 0, avg_r, avg_g, avg_b);
                pos = append_rgb_sgr(out, estimate, pos, 1,
                                     darken_u8(avg_r, 1, 5),
                                     darken_u8(avg_g, 1, 5),
                                     darken_u8(avg_b, 1, 5));
                pos = append_text(out, estimate, pos,
                                  unicode_line_glyph_for_cell(tl, tr, bl, br,
                                                              avg_l));
            }
            else if(use_color_split)
            {
                /* Sample 2×2 sub-pixels and split the cell into two color groups
                   along the dominant edge direction, assigning each group its own
                   FG/BG color.  This gives proper color separation at boundaries
                   (e.g. green fractal branch on black) using half/quadrant blocks:
                     │  →  ▌ / ▐   (left vs right)
                     ─  →  ▀ / ▄   (top vs bottom)
                     ╲  →  ▚       (tl+br vs tr+bl)
                     ╱  →  ▞       (tr+bl vs tl+br)
                   Low-gradient cells fall back to density glyphs. */
                uint8_t sub_r[2][2];
                uint8_t sub_g[2][2];
                uint8_t sub_b[2][2];
                for(int dy = 0; dy < 2; ++dy)
                    for(int dx = 0; dx < 2; ++dx)
                        sample_average_rgba(&img, x * 2 + dx, y * 2 + dy,
                                            sample_w, sample_h, fit_w, fit_h,
                                            off_x, off_y, src_x0, src_y0,
                                            src_w, src_h, &sub_r[dy][dx],
                                            &sub_g[dy][dx], &sub_b[dy][dx]);

                const uint8_t tl_l = luminance_u8(sub_r[0][0],sub_g[0][0],sub_b[0][0]);
                const uint8_t tr_l = luminance_u8(sub_r[0][1],sub_g[0][1],sub_b[0][1]);
                const uint8_t bl_l = luminance_u8(sub_r[1][0],sub_g[1][0],sub_b[1][0]);
                const uint8_t br_l = luminance_u8(sub_r[1][1],sub_g[1][1],sub_b[1][1]);

                const int gx = (int)tr_l + br_l - tl_l - bl_l;
                const int gy = (int)bl_l + br_l - tl_l - tr_l;
                const int d1 = (int)tl_l + br_l - tr_l - bl_l;
                const int ax = gx < 0 ? -gx : gx;
                const int ay = gy < 0 ? -gy : gy;
                const int ad = d1 < 0 ? -d1 : d1;
                const int best = ax > ay ? (ax > ad ? ax : ad)
                                         : (ay > ad ? ay : ad);

                uint8_t fg_r, fg_g, fg_b, bg_r, bg_g, bg_b;
                const char* glyph;

                if(best < 12)
                {
                    /* Uniform region — density glyph with avg color */
                    const uint32_t sr = (uint32_t)sub_r[0][0] + sub_r[0][1]
                                      + sub_r[1][0] + sub_r[1][1];
                    const uint32_t sg = (uint32_t)sub_g[0][0] + sub_g[0][1]
                                      + sub_g[1][0] + sub_g[1][1];
                    const uint32_t sb = (uint32_t)sub_b[0][0] + sub_b[0][1]
                                      + sub_b[1][0] + sub_b[1][1];
                    fg_r = (uint8_t)(sr / 4u);
                    fg_g = (uint8_t)(sg / 4u);
                    fg_b = (uint8_t)(sb / 4u);
                    bg_r = darken_u8(fg_r, 1, 5);
                    bg_g = darken_u8(fg_g, 1, 5);
                    bg_b = darken_u8(fg_b, 1, 5);
                    glyph = density_glyph_for_luma(
                        (uint8_t)((tl_l + tr_l + bl_l + br_l) / 4u));
                }
                else if(ax >= ay && ax >= ad)
                {
                    /* Horizontal gradient → vertical split (left | right) */
                    const uint8_t l_r = (sub_r[0][0] + sub_r[1][0]) / 2u;
                    const uint8_t l_g = (sub_g[0][0] + sub_g[1][0]) / 2u;
                    const uint8_t l_b = (sub_b[0][0] + sub_b[1][0]) / 2u;
                    const uint8_t r_r = (sub_r[0][1] + sub_r[1][1]) / 2u;
                    const uint8_t r_g = (sub_g[0][1] + sub_g[1][1]) / 2u;
                    const uint8_t r_b = (sub_b[0][1] + sub_b[1][1]) / 2u;
                    if(gx >= 0)
                    {
                        /* right brighter → FG=right, ▐ fills right half */
                        fg_r=r_r; fg_g=r_g; fg_b=r_b;
                        bg_r=l_r; bg_g=l_g; bg_b=l_b;
                        glyph = "▐";
                    }
                    else
                    {
                        /* left brighter → FG=left, ▌ fills left half */
                        fg_r=l_r; fg_g=l_g; fg_b=l_b;
                        bg_r=r_r; bg_g=r_g; bg_b=r_b;
                        glyph = "▌";
                    }
                }
                else if(ay >= ax && ay >= ad)
                {
                    /* Vertical gradient → horizontal split (top / bottom) */
                    const uint8_t t_r = (sub_r[0][0] + sub_r[0][1]) / 2u;
                    const uint8_t t_g = (sub_g[0][0] + sub_g[0][1]) / 2u;
                    const uint8_t t_b = (sub_b[0][0] + sub_b[0][1]) / 2u;
                    const uint8_t bo_r = (sub_r[1][0] + sub_r[1][1]) / 2u;
                    const uint8_t bo_g = (sub_g[1][0] + sub_g[1][1]) / 2u;
                    const uint8_t bo_b = (sub_b[1][0] + sub_b[1][1]) / 2u;
                    if(gy <= 0)
                    {
                        /* top brighter → FG=top, ▀ fills upper half */
                        fg_r=t_r; fg_g=t_g; fg_b=t_b;
                        bg_r=bo_r; bg_g=bo_g; bg_b=bo_b;
                        glyph = "▀";
                    }
                    else
                    {
                        /* bottom brighter → FG=bottom, ▄ fills lower half */
                        fg_r=bo_r; fg_g=bo_g; fg_b=bo_b;
                        bg_r=t_r; bg_g=t_g; bg_b=t_b;
                        glyph = "▄";
                    }
                }
                else
                {
                    /* Diagonal split using quadrant diagonal blocks:
                       ▚ (U+259A) = upper-left + lower-right filled  (╲)
                       ▞ (U+259E) = upper-right + lower-left filled  (╱) */
                    const uint8_t a_r = (sub_r[0][0] + sub_r[1][1]) / 2u;
                    const uint8_t a_g = (sub_g[0][0] + sub_g[1][1]) / 2u;
                    const uint8_t a_b = (sub_b[0][0] + sub_b[1][1]) / 2u;
                    const uint8_t c_r = (sub_r[0][1] + sub_r[1][0]) / 2u;
                    const uint8_t c_g = (sub_g[0][1] + sub_g[1][0]) / 2u;
                    const uint8_t c_b = (sub_b[0][1] + sub_b[1][0]) / 2u;
                    if(d1 >= 0)
                    {
                        /* tl+br brighter → FG=a, ▚ fills tl+br corners */
                        fg_r=a_r; fg_g=a_g; fg_b=a_b;
                        bg_r=c_r; bg_g=c_g; bg_b=c_b;
                        glyph = "▚";
                    }
                    else
                    {
                        /* tr+bl brighter → FG=c, ▞ fills tr+bl corners */
                        fg_r=c_r; fg_g=c_g; fg_b=c_b;
                        bg_r=a_r; bg_g=a_g; bg_b=a_b;
                        glyph = "▞";
                    }
                }
                pos = append_rgb_sgr(out, estimate, pos, 0, fg_r, fg_g, fg_b);
                pos = append_rgb_sgr(out, estimate, pos, 1, bg_r, bg_g, bg_b);
                pos = append_text(out, estimate, pos, glyph);
            }
            else if(use_sextant)
            {
                /* 2×3 sub-pixel grid using Unicode sextant blocks (U+1FB00+).
                   Each of the 6 sub-pixels is classified as FG (bright) or BG
                   (dark) relative to the cell average.  The 6-bit pattern
                   selects the sextant codepoint; patterns > 31 use the
                   complement with swapped FG/BG for full coverage without gaps.
                   bit layout: bit0=tl, bit1=tr, bit2=ml, bit3=mr,
                                bit4=bl, bit5=br */
                uint8_t sub_r[3][2], sub_g[3][2], sub_b[3][2];
                uint8_t sub_l[3][2];
                uint32_t sum_r = 0, sum_g = 0, sum_b = 0, sum_l = 0;
                for(int dy = 0; dy < 3; ++dy)
                {
                    for(int dx = 0; dx < 2; ++dx)
                    {
                        sample_average_rgba(&img, x * 2 + dx, y * 3 + dy,
                                            sample_w, sample_h, fit_w, fit_h,
                                            off_x, off_y, src_x0, src_y0,
                                            src_w, src_h, &sub_r[dy][dx],
                                            &sub_g[dy][dx], &sub_b[dy][dx]);
                        sub_l[dy][dx] = luminance_u8(sub_r[dy][dx],
                                                     sub_g[dy][dx],
                                                     sub_b[dy][dx]);
                        sum_r += sub_r[dy][dx];
                        sum_g += sub_g[dy][dx];
                        sum_b += sub_b[dy][dx];
                        sum_l += sub_l[dy][dx];
                    }
                }
                const uint8_t avg_r = (uint8_t)(sum_r / 6u);
                const uint8_t avg_g = (uint8_t)(sum_g / 6u);
                const uint8_t avg_b = (uint8_t)(sum_b / 6u);
                const uint8_t avg_l = (uint8_t)(sum_l / 6u);

                /* Classify each sub-pixel: bright (+8 hysteresis) → FG */
                uint8_t bits = 0;
                uint32_t fg_r = 0, fg_g = 0, fg_b = 0, fg_n = 0;
                uint32_t bg_r = 0, bg_g = 0, bg_b = 0, bg_n = 0;
                for(int dy = 0; dy < 3; ++dy)
                {
                    for(int dx = 0; dx < 2; ++dx)
                    {
                        if(sub_l[dy][dx] + 8u >= avg_l)
                        {
                            bits |= (uint8_t)(1u << (dy * 2 + dx));
                            fg_r += sub_r[dy][dx];
                            fg_g += sub_g[dy][dx];
                            fg_b += sub_b[dy][dx];
                            ++fg_n;
                        }
                        else
                        {
                            bg_r += sub_r[dy][dx];
                            bg_g += sub_g[dy][dx];
                            bg_b += sub_b[dy][dx];
                            ++bg_n;
                        }
                    }
                }
                /* Ensure at least one pixel in each group when uniform */
                if(bits == 0u && avg_l > 24u)
                {
                    uint8_t best_l = 0;
                    int best_dy = 0, best_dx = 0;
                    for(int dy = 0; dy < 3; ++dy)
                        for(int dx = 0; dx < 2; ++dx)
                            if(sub_l[dy][dx] >= best_l)
                            {
                                best_l = sub_l[dy][dx];
                                best_dy = dy; best_dx = dx;
                            }
                    bits = (uint8_t)(1u << (best_dy * 2 + best_dx));
                    fg_r = sub_r[best_dy][best_dx];
                    fg_g = sub_g[best_dy][best_dx];
                    fg_b = sub_b[best_dy][best_dx];
                    fg_n = 1;
                    bg_r = sum_r - fg_r;
                    bg_g = sum_g - fg_g;
                    bg_b = sum_b - fg_b;
                    bg_n = 5;
                }
                if(fg_n == 0u) { fg_n = 1u; fg_r = sum_r/6u; fg_g = sum_g/6u; fg_b = sum_b/6u; }
                if(bg_n == 0u) { bg_n = 1u; bg_r = sum_r/6u; bg_g = sum_g/6u; bg_b = sum_b/6u; }

                uint8_t out_fg_r = (uint8_t)(fg_r/fg_n);
                uint8_t out_fg_g = (uint8_t)(fg_g/fg_n);
                uint8_t out_fg_b = (uint8_t)(fg_b/fg_n);
                uint8_t out_bg_r = (uint8_t)(bg_r/bg_n);
                uint8_t out_bg_g = (uint8_t)(bg_g/bg_n);
                uint8_t out_bg_b = (uint8_t)(bg_b/bg_n);

                /* Complement trick: patterns 32-62 have no sextant codepoint;
                   invert bits and swap FG/BG for identical visual result */
                if(bits > 31u && bits < 63u)
                {
                    bits = (~bits) & 0x3Fu;
                    uint8_t tmp;
                    tmp=out_fg_r; out_fg_r=out_bg_r; out_bg_r=tmp;
                    tmp=out_fg_g; out_fg_g=out_bg_g; out_bg_g=tmp;
                    tmp=out_fg_b; out_fg_b=out_bg_b; out_bg_b=tmp;
                }

                if(bits == 0u)
                {
                    /* Dark uniform cell — density fallback */
                    pos = append_rgb_sgr(out, estimate, pos, 0,
                                         avg_r, avg_g, avg_b);
                    pos = append_rgb_sgr(out, estimate, pos, 1,
                                         darken_u8(avg_r, 1, 5),
                                         darken_u8(avg_g, 1, 5),
                                         darken_u8(avg_b, 1, 5));
                    pos = append_text(out, estimate, pos,
                                      density_glyph_for_luma(avg_l));
                }
                else
                {
                    pos = append_rgb_sgr(out, estimate, pos, 0,
                                         out_fg_r, out_fg_g, out_fg_b);
                    pos = append_rgb_sgr(out, estimate, pos, 1,
                                         out_bg_r, out_bg_g, out_bg_b);
                    pos = append_codepoint_utf8(out, estimate, pos,
                                                sextant_codepoint_for_bits(bits));
                }
            }
            else if(use_braille_color)
            {
                /* BrailleColor: 2×4 braille sub-pixels with proper per-group
                   color.  FG = avg of bright dots, BG = avg of dark dots.
                   Unlike the original BrailleDots mode this fully separates
                   the two colors so e.g. a green/black boundary shows bright
                   green dots on a true black background. */
                static const uint8_t bit_table[4][2] = {
                    {0x01u, 0x08u}, {0x02u, 0x10u},
                    {0x04u, 0x20u}, {0x40u, 0x80u}};
                uint8_t sub_r[4][2], sub_g[4][2], sub_b[4][2];
                uint8_t sub_l[4][2];
                uint32_t sum_r = 0, sum_g = 0, sum_b = 0, sum_l = 0;
                for(int dy = 0; dy < 4; ++dy)
                    for(int dx = 0; dx < 2; ++dx)
                    {
                        sample_average_rgba(&img, x * 2 + dx, y * 4 + dy,
                                            sample_w, sample_h, fit_w, fit_h,
                                            off_x, off_y, src_x0, src_y0,
                                            src_w, src_h, &sub_r[dy][dx],
                                            &sub_g[dy][dx], &sub_b[dy][dx]);
                        sub_l[dy][dx] = luminance_u8(sub_r[dy][dx],
                                                     sub_g[dy][dx],
                                                     sub_b[dy][dx]);
                        sum_r += sub_r[dy][dx]; sum_g += sub_g[dy][dx];
                        sum_b += sub_b[dy][dx]; sum_l += sub_l[dy][dx];
                    }
                const uint8_t avg_r = (uint8_t)(sum_r / 8u);
                const uint8_t avg_g = (uint8_t)(sum_g / 8u);
                const uint8_t avg_b = (uint8_t)(sum_b / 8u);
                const uint8_t avg_l = (uint8_t)(sum_l / 8u);

                uint8_t bits = 0;
                uint32_t fg_r=0, fg_g=0, fg_b=0, fg_n=0;
                uint32_t bg_r=0, bg_g=0, bg_b=0, bg_n=0;
                for(int dy = 0; dy < 4; ++dy)
                    for(int dx = 0; dx < 2; ++dx)
                    {
                        if(sub_l[dy][dx] + 10u >= avg_l)
                        {
                            bits |= bit_table[dy][dx];
                            fg_r += sub_r[dy][dx]; fg_g += sub_g[dy][dx];
                            fg_b += sub_b[dy][dx]; ++fg_n;
                        }
                        else
                        {
                            bg_r += sub_r[dy][dx]; bg_g += sub_g[dy][dx];
                            bg_b += sub_b[dy][dx]; ++bg_n;
                        }
                    }

                /* For uniform bright cells, force the brightest dot into FG */
                if(bits == 0u && avg_l > 28u)
                {
                    uint8_t best_l = 0;
                    int bdy = 0, bdx = 0;
                    for(int dy = 0; dy < 4; ++dy)
                        for(int dx = 0; dx < 2; ++dx)
                            if(sub_l[dy][dx] >= best_l)
                            {
                                best_l = sub_l[dy][dx];
                                bdy = dy; bdx = dx;
                            }
                    bits = bit_table[bdy][bdx];
                    fg_r = sub_r[bdy][bdx]; fg_g = sub_g[bdy][bdx];
                    fg_b = sub_b[bdy][bdx]; fg_n = 1;
                    bg_r = sum_r - fg_r; bg_g = sum_g - fg_g;
                    bg_b = sum_b - fg_b; bg_n = 7;
                }
                if(fg_n == 0u)
                {
                    /* All dark — density fallback */
                    pos = append_rgb_sgr(out, estimate, pos, 0,
                                         avg_r, avg_g, avg_b);
                    pos = append_rgb_sgr(out, estimate, pos, 1,
                                         darken_u8(avg_r, 1, 5),
                                         darken_u8(avg_g, 1, 5),
                                         darken_u8(avg_b, 1, 5));
                    pos = append_text(out, estimate, pos,
                                      density_glyph_for_luma(avg_l));
                }
                else
                {
                    if(bg_n == 0u) { bg_n=1u; bg_r=sum_r/8u; bg_g=sum_g/8u; bg_b=sum_b/8u; }
                    pos = append_rgb_sgr(out, estimate, pos, 0,
                                         (uint8_t)(fg_r/fg_n),
                                         (uint8_t)(fg_g/fg_n),
                                         (uint8_t)(fg_b/fg_n));
                    pos = append_rgb_sgr(out, estimate, pos, 1,
                                         (uint8_t)(bg_r/bg_n),
                                         (uint8_t)(bg_g/bg_n),
                                         (uint8_t)(bg_b/bg_n));
                    pos = append_braille_utf8(out, estimate, pos, bits);
                }
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
                                            off_x, off_y, src_x0, src_y0,
                                            src_w, src_h, &sub_r[dy][dx],
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
            else if(use_quadrants)
            {
                static const uint8_t bit_table[2][2] = {
                    {0x01u, 0x02u},
                    {0x04u, 0x08u},
                };
                uint8_t sub_r[2][2];
                uint8_t sub_g[2][2];
                uint8_t sub_b[2][2];
                uint8_t sub_l[2][2];
                uint32_t sum_r = 0;
                uint32_t sum_g = 0;
                uint32_t sum_b = 0;
                uint32_t sum_l = 0;
                for(int dy = 0; dy < 2; ++dy)
                {
                    for(int dx = 0; dx < 2; ++dx)
                    {
                        sample_average_rgba(&img, x * 2 + dx, y * 2 + dy,
                                            sample_w, sample_h, fit_w, fit_h,
                                            off_x, off_y, src_x0, src_y0,
                                            src_w, src_h, &sub_r[dy][dx],
                                            &sub_g[dy][dx], &sub_b[dy][dx]);
                        sub_l[dy][dx] = luminance_u8(sub_r[dy][dx], sub_g[dy][dx],
                                                     sub_b[dy][dx]);
                        sum_r += sub_r[dy][dx];
                        sum_g += sub_g[dy][dx];
                        sum_b += sub_b[dy][dx];
                        sum_l += sub_l[dy][dx];
                    }
                }
                const uint8_t avg_r = (uint8_t)(sum_r / 4u);
                const uint8_t avg_g = (uint8_t)(sum_g / 4u);
                const uint8_t avg_b = (uint8_t)(sum_b / 4u);
                const uint8_t avg_l = (uint8_t)(sum_l / 4u);
                uint8_t bits = 0;
                uint32_t fg_r = 0, fg_g = 0, fg_b = 0, fg_n = 0;
                for(int dy = 0; dy < 2; ++dy)
                {
                    for(int dx = 0; dx < 2; ++dx)
                    {
                        if(sub_l[dy][dx] + 8 >= avg_l)
                        {
                            bits |= bit_table[dy][dx];
                            fg_r += sub_r[dy][dx];
                            fg_g += sub_g[dy][dx];
                            fg_b += sub_b[dy][dx];
                            ++fg_n;
                        }
                    }
                }
                if(bits == 0 && avg_l > 28)
                {
                    int best_dx = 0;
                    int best_dy = 0;
                    uint8_t best_l = 0;
                    for(int dy = 0; dy < 2; ++dy)
                    {
                        for(int dx = 0; dx < 2; ++dx)
                        {
                            if(sub_l[dy][dx] >= best_l)
                            {
                                best_l = sub_l[dy][dx];
                                best_dx = dx;
                                best_dy = dy;
                            }
                        }
                    }
                    bits = bit_table[best_dy][best_dx];
                    fg_r = sub_r[best_dy][best_dx];
                    fg_g = sub_g[best_dy][best_dx];
                    fg_b = sub_b[best_dy][best_dx];
                    fg_n = 1;
                }
                if(fg_n == 0)
                    fg_n = 1;
                pos = append_rgb_sgr(out, estimate, pos, 0,
                                     (uint8_t)(fg_r / fg_n),
                                     (uint8_t)(fg_g / fg_n),
                                     (uint8_t)(fg_b / fg_n));
                pos = append_rgb_sgr(out, estimate, pos, 1,
                                     avg_r,
                                     avg_g,
                                     avg_b);
                pos = append_codepoint_utf8(out, estimate, pos,
                                            quadrant_codepoint_for_bits(bits));
            }
        }
        pos = append_text(out, estimate, pos, "\x1b[0m\n");
    }
    pos = append_text(out, estimate, pos, "\x1b[0m");
    free_image_rgba(&img);
    return out;
}

/* Public entry point — aspect-ratio-preserving, auto content-crop */
char* __mlang_std_image_render_truecolor(const char* path, int32_t columns,
                                         int32_t rows, int32_t glyph_mode)
{
    if(columns <= 0 || rows <= 0)
    {
        set_error("std::image: columns and rows must be > 0");
        return dup_cstr("");
    }
    return render_truecolor_impl(path, columns, glyph_mode, 0, 0, 0, 0, 0);
}

/* Public entry point — exact output dimensions, explicit pixel crop region.
 * crop_x, crop_y, crop_w, crop_h are in source image pixels.
 * The crop region is scaled to fill exactly (columns × rows) terminal cells.
 * Use this for scrollable viewport backgrounds. */
char* __mlang_std_image_render_truecolor_crop(const char* path,
                                               int32_t columns, int32_t rows,
                                               int32_t glyph_mode,
                                               int32_t crop_x, int32_t crop_y,
                                               int32_t crop_w, int32_t crop_h)
{
    if(columns <= 0 || rows <= 0 || crop_w <= 0 || crop_h <= 0)
    {
        set_error("std::image: columns, rows and crop dimensions must be > 0");
        return dup_cstr("");
    }
    return render_truecolor_impl(path, columns, glyph_mode,
                                  (int64_t)crop_x, (int64_t)crop_y,
                                  (int64_t)crop_w, (int64_t)crop_h,
                                  rows);
}
