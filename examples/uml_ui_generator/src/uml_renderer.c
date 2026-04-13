#include <ctype.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"
#include "stb_easy_font.h"

static char g_last_error[512];

typedef enum
{
    NODE_START = 0,
    NODE_ACTION = 1,
    NODE_DECISION = 2,
    NODE_END = 3
} node_type_t;

typedef struct
{
    int r;
    int g;
    int b;
    int a;
} color_t;

typedef struct
{
    char id[64];
    node_type_t type;
    char label[160];
    color_t fill;
    color_t stroke;
    color_t text;
    int level;
    int slot;
    int x;
    int y;
    int width;
    int height;
} node_t;

typedef struct
{
    int from;
    int to;
    char label[64];
    color_t color;
} edge_t;

typedef struct
{
    char title[160];
    node_t nodes[128];
    int node_count;
    edge_t edges[256];
    int edge_count;
} diagram_t;

typedef struct
{
    int width;
    int height;
    unsigned char *pixels;
} image_t;

static void set_errorf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_last_error, sizeof(g_last_error), fmt, ap);
    va_end(ap);
}

const char *uml_last_error(void)
{
    return g_last_error;
}

static char *read_text_file(const char *path)
{
    FILE *fp = fopen(path, "rb");
    long size;
    char *buf;
    size_t got;

    if(!fp)
    {
        set_errorf("failed to open input file: %s", path);
        return NULL;
    }
    if(fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        set_errorf("failed to seek input file: %s", path);
        return NULL;
    }
    size = ftell(fp);
    if(size < 0)
    {
        fclose(fp);
        set_errorf("failed to measure input file: %s", path);
        return NULL;
    }
    if(fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        set_errorf("failed to rewind input file: %s", path);
        return NULL;
    }

    buf = (char *)malloc((size_t)size + 1);
    if(!buf)
    {
        fclose(fp);
        set_errorf("out of memory");
        return NULL;
    }

    got = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if(got != (size_t)size)
    {
        free(buf);
        set_errorf("failed to read input file: %s", path);
        return NULL;
    }
    buf[size] = '\0';
    return buf;
}

static void trim_in_place(char *s)
{
    size_t len;
    size_t start = 0;

    if(!s)
        return;
    len = strlen(s);
    while(start < len && isspace((unsigned char)s[start]))
        start++;
    while(len > start && isspace((unsigned char)s[len - 1]))
        len--;
    if(start > 0)
        memmove(s, s + start, len - start);
    s[len - start] = '\0';
}

static int split_fields(char *line, char **fields, int max_fields)
{
    int count = 0;
    char *cur = line;

    while(count < max_fields)
    {
        fields[count++] = cur;
        while(*cur != '\0' && *cur != '|')
            cur++;
        if(*cur == '\0')
            break;
        *cur = '\0';
        cur++;
    }
    return count;
}

static int eq_ci(const char *a, const char *b)
{
    while(*a != '\0' && *b != '\0')
    {
        if(tolower((unsigned char)*a) != tolower((unsigned char)*b))
            return 0;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static color_t color_rgba(int r, int g, int b, int a)
{
    color_t c;
    c.r = r;
    c.g = g;
    c.b = b;
    c.a = a;
    return c;
}

static int parse_hex_byte(const char *s)
{
    int value = 0;
    int i;

    for(i = 0; i < 2; ++i)
    {
        char ch = s[i];
        value <<= 4;
        if(ch >= '0' && ch <= '9')
            value |= ch - '0';
        else if(ch >= 'a' && ch <= 'f')
            value |= 10 + ch - 'a';
        else if(ch >= 'A' && ch <= 'F')
            value |= 10 + ch - 'A';
        else
            return -1;
    }
    return value;
}

static int parse_color(const char *text, color_t fallback, color_t *out)
{
    int r;
    int g;
    int b;

    if(!text || text[0] == '\0')
    {
        *out = fallback;
        return 1;
    }
    if(text[0] != '#' || strlen(text) != 7)
        return 0;
    r = parse_hex_byte(text + 1);
    g = parse_hex_byte(text + 3);
    b = parse_hex_byte(text + 5);
    if(r < 0 || g < 0 || b < 0)
        return 0;
    *out = color_rgba(r, g, b, 255);
    return 1;
}

static int parse_node_type(const char *text, node_type_t *out)
{
    if(eq_ci(text, "start"))
    {
        *out = NODE_START;
        return 1;
    }
    if(eq_ci(text, "action"))
    {
        *out = NODE_ACTION;
        return 1;
    }
    if(eq_ci(text, "decision"))
    {
        *out = NODE_DECISION;
        return 1;
    }
    if(eq_ci(text, "end"))
    {
        *out = NODE_END;
        return 1;
    }
    return 0;
}

static int find_node_index(const diagram_t *diagram, const char *id)
{
    int i;
    for(i = 0; i < diagram->node_count; ++i)
    {
        if(strcmp(diagram->nodes[i].id, id) == 0)
            return i;
    }
    return -1;
}

static int parse_diagram_text(diagram_t *diagram, const char *text)
{
    char *owned = NULL;
    char *cursor;
    int line_no = 0;

    memset(diagram, 0, sizeof(*diagram));
    owned = (char *)malloc(strlen(text) + 1);
    if(!owned)
    {
        set_errorf("out of memory");
        return 0;
    }
    strcpy(owned, text);

    cursor = owned;
    while(*cursor != '\0')
    {
        char *line = cursor;
        char *newline = strchr(cursor, '\n');
        char *fields[8];
        int count;

        line_no++;
        if(newline)
        {
            *newline = '\0';
            cursor = newline + 1;
        }
        else
        {
            cursor += strlen(cursor);
        }

        trim_in_place(line);
        if(line[0] == '\0' || line[0] == '#')
            continue;

        count = split_fields(line, fields, 8);
        if(count <= 0)
            continue;

        if(eq_ci(fields[0], "title"))
        {
            if(count < 2)
            {
                free(owned);
                set_errorf("line %d: title requires one field", line_no);
                return 0;
            }
            trim_in_place(fields[1]);
            snprintf(diagram->title, sizeof(diagram->title), "%s", fields[1]);
            continue;
        }

        if(eq_ci(fields[0], "node"))
        {
            node_t *node;
            node_type_t type;

            if(count < 7)
            {
                free(owned);
                set_errorf("line %d: node format is node|id|type|label|fill|stroke|text", line_no);
                return 0;
            }
            if(diagram->node_count >= (int)(sizeof(diagram->nodes) / sizeof(diagram->nodes[0])))
            {
                free(owned);
                set_errorf("too many nodes");
                return 0;
            }
            if(!parse_node_type(fields[2], &type))
            {
                free(owned);
                set_errorf("line %d: unknown node type '%s'", line_no, fields[2]);
                return 0;
            }
            node = &diagram->nodes[diagram->node_count];
            memset(node, 0, sizeof(*node));
            trim_in_place(fields[1]);
            trim_in_place(fields[3]);
            trim_in_place(fields[4]);
            trim_in_place(fields[5]);
            trim_in_place(fields[6]);
            if(find_node_index(diagram, fields[1]) >= 0)
            {
                free(owned);
                set_errorf("line %d: duplicate node id '%s'", line_no, fields[1]);
                return 0;
            }
            snprintf(node->id, sizeof(node->id), "%s", fields[1]);
            snprintf(node->label, sizeof(node->label), "%s", fields[3]);
            node->type = type;
            if(!parse_color(fields[4], color_rgba(219, 234, 254, 255), &node->fill) ||
               !parse_color(fields[5], color_rgba(37, 99, 235, 255), &node->stroke) ||
               !parse_color(fields[6], color_rgba(15, 23, 42, 255), &node->text))
            {
                free(owned);
                set_errorf("line %d: invalid color field", line_no);
                return 0;
            }
            diagram->node_count++;
            continue;
        }

        if(eq_ci(fields[0], "edge"))
        {
            edge_t *edge;

            if(count < 5)
            {
                free(owned);
                set_errorf("line %d: edge format is edge|from|to|label|color", line_no);
                return 0;
            }
            if(diagram->edge_count >= (int)(sizeof(diagram->edges) / sizeof(diagram->edges[0])))
            {
                free(owned);
                set_errorf("too many edges");
                return 0;
            }
            trim_in_place(fields[1]);
            trim_in_place(fields[2]);
            trim_in_place(fields[3]);
            trim_in_place(fields[4]);
            edge = &diagram->edges[diagram->edge_count];
            memset(edge, 0, sizeof(*edge));
            edge->from = find_node_index(diagram, fields[1]);
            edge->to = find_node_index(diagram, fields[2]);
            if(edge->from < 0 || edge->to < 0)
            {
                free(owned);
                set_errorf("line %d: edge references unknown node", line_no);
                return 0;
            }
            snprintf(edge->label, sizeof(edge->label), "%s", fields[3]);
            if(!parse_color(fields[4], color_rgba(71, 85, 105, 255), &edge->color))
            {
                free(owned);
                set_errorf("line %d: invalid edge color", line_no);
                return 0;
            }
            diagram->edge_count++;
            continue;
        }

        free(owned);
        set_errorf("line %d: unknown record type '%s'", line_no, fields[0]);
        return 0;
    }

    free(owned);
    if(diagram->node_count == 0)
    {
        set_errorf("diagram contains no nodes");
        return 0;
    }
    return 1;
}

static int max_i32(int a, int b)
{
    return a > b ? a : b;
}

static int min_i32(int a, int b)
{
    return a < b ? a : b;
}

static void compute_node_sizes(diagram_t *diagram)
{
    int i;
    for(i = 0; i < diagram->node_count; ++i)
    {
        node_t *node = &diagram->nodes[i];
        int label_len = (int)strlen(node->label);
        if(node->type == NODE_START || node->type == NODE_END)
        {
            node->width = 62;
            node->height = 62;
        }
        else if(node->type == NODE_DECISION)
        {
            node->width = max_i32(140, 72 + label_len * 8);
            if((node->width & 1) != 0)
                node->width++;
            node->height = 92;
        }
        else
        {
            node->width = max_i32(170, 76 + label_len * 8);
            node->height = 74;
        }
    }
}

static void compute_layout(diagram_t *diagram, int *out_w, int *out_h)
{
    int i;
    int level_counts[64];
    int max_level = 0;
    int max_cols = 1;
    int margin_x = 70;
    int margin_y = 80;
    int col_spacing = 90;
    int row_spacing = 110;
    int level_max_width[64];
    int level_max_height[64];

    memset(level_counts, 0, sizeof(level_counts));
    memset(level_max_width, 0, sizeof(level_max_width));
    memset(level_max_height, 0, sizeof(level_max_height));

    for(i = 0; i < diagram->node_count; ++i)
    {
        diagram->nodes[i].level = 0;
    }

    for(i = 0; i < diagram->node_count; ++i)
    {
        int e;
        for(e = 0; e < diagram->edge_count; ++e)
        {
            const edge_t *edge = &diagram->edges[e];
            if(edge->to > edge->from)
            {
                int next_level = diagram->nodes[edge->from].level + 1;
                if(next_level > diagram->nodes[edge->to].level)
                    diagram->nodes[edge->to].level = next_level;
            }
        }
    }

    for(i = 0; i < diagram->node_count; ++i)
    {
        node_t *node = &diagram->nodes[i];
        if(node->level > max_level)
            max_level = node->level;
        node->slot = level_counts[node->level]++;
        if(node->width > level_max_width[node->level])
            level_max_width[node->level] = node->width;
        if(node->height > level_max_height[node->level])
            level_max_height[node->level] = node->height;
    }

    for(i = 0; i <= max_level; ++i)
    {
        if(level_counts[i] > max_cols)
            max_cols = level_counts[i];
    }

    *out_w = margin_x * 2 + max_cols * 220 + (max_cols - 1) * col_spacing;
    *out_h = margin_y * 2 + (max_level + 1) * 120 + max_level * row_spacing;
    if(diagram->title[0] != '\0')
        *out_h += 56;

    for(i = 0; i < diagram->node_count; ++i)
    {
        node_t *node = &diagram->nodes[i];
        int cols = level_counts[node->level];
        int row_y = margin_y + node->level * (120 + row_spacing);
        int usable_w = *out_w - margin_x * 2;
        int step = cols > 0 ? usable_w / (cols + 1) : usable_w;
        int top_extra = diagram->title[0] != '\0' ? 56 : 0;

        node->x = margin_x + step * (node->slot + 1);
        node->y = row_y + top_extra;
    }
}

static int clamp_int(int v, int lo, int hi)
{
    if(v < lo)
        return lo;
    if(v > hi)
        return hi;
    return v;
}

static int blend_channel(int dst, int src, int alpha)
{
    return (dst * (255 - alpha) + src * alpha) / 255;
}

static void set_pixel(image_t *img, int x, int y, color_t color)
{
    unsigned char *px;
    if(x < 0 || y < 0 || x >= img->width || y >= img->height)
        return;
    px = img->pixels + ((size_t)y * (size_t)img->width + (size_t)x) * 4u;
    px[0] = (unsigned char)blend_channel(px[0], color.r, color.a);
    px[1] = (unsigned char)blend_channel(px[1], color.g, color.a);
    px[2] = (unsigned char)blend_channel(px[2], color.b, color.a);
    px[3] = 255;
}

static void clear_image(image_t *img, color_t color)
{
    int x;
    int y;
    for(y = 0; y < img->height; ++y)
    {
        for(x = 0; x < img->width; ++x)
            set_pixel(img, x, y, color);
    }
}

static void fill_rect(image_t *img, int x0, int y0, int x1, int y1, color_t color)
{
    int x;
    int y;
    x0 = clamp_int(x0, 0, img->width);
    y0 = clamp_int(y0, 0, img->height);
    x1 = clamp_int(x1, 0, img->width);
    y1 = clamp_int(y1, 0, img->height);
    for(y = y0; y < y1; ++y)
    {
        for(x = x0; x < x1; ++x)
            set_pixel(img, x, y, color);
    }
}

static void fill_circle(image_t *img, int cx, int cy, int radius, color_t color)
{
    int x;
    int y;
    int r2 = radius * radius;
    for(y = cy - radius; y <= cy + radius; ++y)
    {
        for(x = cx - radius; x <= cx + radius; ++x)
        {
            int dx = x - cx;
            int dy = y - cy;
            if(dx * dx + dy * dy <= r2)
                set_pixel(img, x, y, color);
        }
    }
}

static void stroke_circle(image_t *img, int cx, int cy, int radius, int thickness, color_t color)
{
    int x;
    int y;
    int outer = radius * radius;
    int inner = (radius - thickness) * (radius - thickness);
    for(y = cy - radius; y <= cy + radius; ++y)
    {
        for(x = cx - radius; x <= cx + radius; ++x)
        {
            int dx = x - cx;
            int dy = y - cy;
            int d2 = dx * dx + dy * dy;
            if(d2 <= outer && d2 >= inner)
                set_pixel(img, x, y, color);
        }
    }
}

static void fill_rounded_rect(image_t *img, int x, int y, int w, int h, int radius, color_t color)
{
    fill_rect(img, x + radius, y, x + w - radius, y + h, color);
    fill_rect(img, x, y + radius, x + radius, y + h - radius, color);
    fill_rect(img, x + w - radius, y + radius, x + w, y + h - radius, color);
    fill_circle(img, x + radius, y + radius, radius, color);
    fill_circle(img, x + w - radius, y + radius, radius, color);
    fill_circle(img, x + radius, y + h - radius, radius, color);
    fill_circle(img, x + w - radius, y + h - radius, radius, color);
}

static void stroke_rounded_rect(image_t *img, int x, int y, int w, int h, int radius, int thickness, color_t color)
{
    fill_rounded_rect(img, x, y, w, thickness + radius, radius, color);
    fill_rounded_rect(img, x, y + h - thickness - radius, w, thickness + radius, radius, color);
    fill_rect(img, x, y + radius, x + thickness, y + h - radius, color);
    fill_rect(img, x + w - thickness, y + radius, x + w, y + h - radius, color);
}

static void fill_diamond(image_t *img, int cx, int cy, int w, int h, color_t color)
{
    int half_w = w / 2;
    int half_h = h / 2;
    int y;
    for(y = -half_h; y <= half_h; ++y)
    {
        float t = 1.0f - (float)abs(y) / (float)half_h;
        int span = (int)(t * (float)half_w);
        fill_rect(img, cx - span, cy + y, cx + span + 1, cy + y + 1, color);
    }
}

static void stroke_diamond(image_t *img, int cx, int cy, int w, int h, int thickness, color_t color)
{
    int i;
    for(i = 0; i < thickness; ++i)
    {
        int hw = w / 2 - i;
        int hh = h / 2 - i;
        int x0 = cx;
        int y0 = cy - hh;
        int x1 = cx + hw;
        int y1 = cy;
        int x2 = cx;
        int y2 = cy + hh;
        int x3 = cx - hw;
        int y3 = cy;
        int steps = max_i32(hw, hh) * 2 + 1;
        int s;
        for(s = 0; s <= steps; ++s)
        {
            float t = (float)s / (float)steps;
            int ax = (int)(x0 + (x1 - x0) * t);
            int ay = (int)(y0 + (y1 - y0) * t);
            int bx = (int)(x1 + (x2 - x1) * t);
            int by = (int)(y1 + (y2 - y1) * t);
            int cx2 = (int)(x2 + (x3 - x2) * t);
            int cy2 = (int)(y2 + (y3 - y2) * t);
            int dx = (int)(x3 + (x0 - x3) * t);
            int dy = (int)(y3 + (y0 - y3) * t);
            set_pixel(img, ax, ay, color);
            set_pixel(img, bx, by, color);
            set_pixel(img, cx2, cy2, color);
            set_pixel(img, dx, dy, color);
        }
    }
}

static void draw_line(image_t *img, int x0, int y0, int x1, int y1, int thickness, color_t color)
{
    int dx = abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    int half = thickness / 2;

    for(;;)
    {
        int ox;
        int oy;
        for(oy = -half; oy <= half; ++oy)
        {
            for(ox = -half; ox <= half; ++ox)
                set_pixel(img, x0 + ox, y0 + oy, color);
        }
        if(x0 == x1 && y0 == y1)
            break;
        if(2 * err >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if(2 * err <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

static void draw_arrow_head(image_t *img, int tip_x, int tip_y, int dx, int dy, color_t color)
{
    int len = 14;
    int wing = 7;
    float mag = sqrtf((float)(dx * dx + dy * dy));
    float ux;
    float uy;
    float px;
    float py;
    int lx;
    int ly;
    int rx;
    int ry;

    if(mag < 0.001f)
        return;
    ux = (float)dx / mag;
    uy = (float)dy / mag;
    px = -uy;
    py = ux;
    lx = (int)(tip_x - ux * len + px * wing);
    ly = (int)(tip_y - uy * len + py * wing);
    rx = (int)(tip_x - ux * len - px * wing);
    ry = (int)(tip_y - uy * len - py * wing);
    draw_line(img, tip_x, tip_y, lx, ly, 2, color);
    draw_line(img, tip_x, tip_y, rx, ry, 2, color);
}

static void draw_text(image_t *img, int x, int y, const char *text, color_t color)
{
    typedef struct
    {
        float x;
        float y;
        float z;
        unsigned char c[4];
    } easy_font_vertex_t;

    char buffer[99999];
    unsigned char rgba[4];
    int quads;
    int i;

    rgba[0] = (unsigned char)color.r;
    rgba[1] = (unsigned char)color.g;
    rgba[2] = (unsigned char)color.b;
    rgba[3] = (unsigned char)color.a;
    quads = stb_easy_font_print((float)x, (float)y, (char *)text, rgba,
                                buffer, (int)sizeof(buffer));
    for(i = 0; i < quads; ++i)
    {
        easy_font_vertex_t *v = (easy_font_vertex_t *)buffer + i * 4;
        float min_xf = v[0].x;
        float max_xf = v[0].x;
        float min_yf = v[0].y;
        float max_yf = v[0].y;
        int j;
        int min_x;
        int max_x;
        int min_y;
        int max_y;
        for(j = 1; j < 4; ++j)
        {
            if(v[j].x < min_xf)
                min_xf = v[j].x;
            if(v[j].x > max_xf)
                max_xf = v[j].x;
            if(v[j].y < min_yf)
                min_yf = v[j].y;
            if(v[j].y > max_yf)
                max_yf = v[j].y;
        }
        min_x = (int)floorf(min_xf);
        max_x = (int)ceilf(max_xf);
        min_y = (int)floorf(min_yf);
        max_y = (int)ceilf(max_yf);
        fill_rect(img, min_x, min_y, max_x, max_y, color);
    }
}

static int text_width(const char *text)
{
    return stb_easy_font_width((char *)text);
}

static void draw_title(image_t *img, const char *title)
{
    color_t title_color = color_rgba(15, 23, 42, 255);
    int x = img->width / 2 - text_width(title) / 2;
    draw_text(img, x, 26, title, title_color);
}

static void draw_node(image_t *img, const node_t *node)
{
    int x = node->x - node->width / 2;
    int y = node->y - node->height / 2;
    int label_x = node->x - text_width(node->label) / 2;
    int label_y = node->y - 6;
    int action_radius = 10;

    if(node->type == NODE_START)
    {
        fill_circle(img, node->x, node->y, node->width / 2, node->fill);
        stroke_circle(img, node->x, node->y, node->width / 2, 3, node->stroke);
    }
    else if(node->type == NODE_END)
    {
        fill_circle(img, node->x, node->y, node->width / 2, color_rgba(255, 255, 255, 255));
        stroke_circle(img, node->x, node->y, node->width / 2, 3, node->stroke);
        fill_circle(img, node->x, node->y, node->width / 2 - 10, node->fill);
    }
    else if(node->type == NODE_DECISION)
    {
        fill_diamond(img, node->x, node->y, node->width, node->height, node->fill);
        stroke_diamond(img, node->x, node->y, node->width, node->height, 3, node->stroke);
    }
    else
    {
        fill_rounded_rect(img, x, y, node->width, node->height, action_radius,
                          node->fill);
        stroke_rounded_rect(img, x, y, node->width, node->height, action_radius,
                            3, node->stroke);
    }

    if(node->label[0] != '\0')
        draw_text(img, label_x, label_y, node->label, node->text);
}

static void draw_polyline(image_t *img, int *xs, int *ys, int count, color_t color)
{
    int i;
    for(i = 0; i + 1 < count; ++i)
        draw_line(img, xs[i], ys[i], xs[i + 1], ys[i + 1], 3, color);
    if(count >= 2)
        draw_arrow_head(img, xs[count - 1], ys[count - 1],
                        xs[count - 1] - xs[count - 2],
                        ys[count - 1] - ys[count - 2], color);
}

static void draw_edge_label(image_t *img, int x, int y, const char *text, color_t color)
{
    color_t bg = color_rgba(255, 255, 255, 230);
    int w;
    if(text[0] == '\0')
        return;
    w = text_width(text);
    fill_rect(img, x - 6, y - 4, x + w + 6, y + 16, bg);
    draw_text(img, x, y, text, color);
}

static void draw_edge(image_t *img, const diagram_t *diagram, const edge_t *edge)
{
    const node_t *from = &diagram->nodes[edge->from];
    const node_t *to = &diagram->nodes[edge->to];
    int xs[6];
    int ys[6];
    int count = 0;

    if(to->level > from->level)
    {
        int sx = from->x;
        int sy = from->y + from->height / 2;
        int tx = to->x;
        int ty = to->y - to->height / 2;
        int mid_y = sy + (ty - sy) / 2;
        xs[count] = sx;
        ys[count++] = sy;
        xs[count] = sx;
        ys[count++] = mid_y;
        xs[count] = tx;
        ys[count++] = mid_y;
        xs[count] = tx;
        ys[count++] = ty;
        draw_polyline(img, xs, ys, count, edge->color);
        draw_edge_label(img, (sx + tx) / 2 - text_width(edge->label) / 2, mid_y - 20, edge->label, edge->color);
        return;
    }

    {
        int dir = to->x >= from->x ? 1 : -1;
        int sx = from->x + dir * (from->width / 2);
        int sy = from->y;
        int tx = to->x;
        int ty = to->y - to->height / 2;
        int bend_x = dir > 0 ? max_i32(from->x + from->width / 2 + 40, to->x + to->width / 2 + 40)
                             : min_i32(from->x - from->width / 2 - 40, to->x - to->width / 2 - 40);
        int top_y = min_i32(from->y - from->height / 2 - 28, to->y - to->height / 2 - 28);
        xs[count] = sx;
        ys[count++] = sy;
        xs[count] = bend_x;
        ys[count++] = sy;
        xs[count] = bend_x;
        ys[count++] = top_y;
        xs[count] = tx;
        ys[count++] = top_y;
        xs[count] = tx;
        ys[count++] = ty;
        draw_polyline(img, xs, ys, count, edge->color);
        draw_edge_label(img, bend_x - text_width(edge->label) / 2, top_y - 20, edge->label, edge->color);
    }
}

static int ensure_parent_dirs(const char *path)
{
    char buf[1024];
    size_t len = strlen(path);
    size_t i;

    if(len >= sizeof(buf))
    {
        set_errorf("output path is too long");
        return 0;
    }
    snprintf(buf, sizeof(buf), "%s", path);
    for(i = 1; i < len; ++i)
    {
        if(buf[i] == '/')
        {
            int rc;
            buf[i] = '\0';
            rc = mkdir(buf, 0755);
            (void)rc;
            buf[i] = '/';
        }
    }
    return 1;
}

int uml_render_file(const char *input_path, const char *output_path)
{
    char *text;
    diagram_t diagram;
    image_t image;
    int i;

    g_last_error[0] = '\0';
    text = read_text_file(input_path);
    if(!text)
        return 1;
    if(!parse_diagram_text(&diagram, text))
    {
        free(text);
        return 1;
    }
    free(text);

    compute_node_sizes(&diagram);
    compute_layout(&diagram, &image.width, &image.height);
    image.pixels = (unsigned char *)calloc((size_t)image.width * (size_t)image.height * 4u, 1u);
    if(!image.pixels)
    {
        set_errorf("out of memory");
        return 1;
    }

    clear_image(&image, color_rgba(248, 250, 252, 255));
    if(diagram.title[0] != '\0')
        draw_title(&image, diagram.title);

    for(i = 0; i < diagram.edge_count; ++i)
        draw_edge(&image, &diagram, &diagram.edges[i]);
    for(i = 0; i < diagram.node_count; ++i)
        draw_node(&image, &diagram.nodes[i]);

    if(!ensure_parent_dirs(output_path))
    {
        free(image.pixels);
        return 1;
    }
    if(stbi_write_png(output_path, image.width, image.height, 4, image.pixels, image.width * 4) == 0)
    {
        free(image.pixels);
        set_errorf("failed to write png: %s", output_path);
        return 1;
    }

    free(image.pixels);
    return 0;
}
