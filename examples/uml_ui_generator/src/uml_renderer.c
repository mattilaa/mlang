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
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static char g_last_error[512];
static const int UML_STROKE = 1;
static const int UML_AA_SCALE = 4;
static const float UML_TEXT_SIZE = 14.0f;
static int text_width(const char *text);

typedef enum
{
    NODE_START = 0,
    NODE_ACTION = 1,
    NODE_DECISION = 2,
    NODE_END = 3
} node_type_t;

typedef enum
{
    DIAGRAM_ACTIVITY = 0,
    DIAGRAM_SEQUENCE = 1
} diagram_kind_t;

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
    diagram_kind_t kind;
    float scale;
    float box_radius;
    float edge_radius;
    node_t nodes[128];
    int node_count;
    edge_t edges[256];
    int edge_count;
} diagram_t;

typedef struct
{
    char id[64];
    char label[160];
    color_t fill;
    color_t stroke;
    color_t text;
    int x;
    int width;
} participant_t;

typedef struct
{
    int from;
    int to;
    char label[160];
    color_t color;
    int y;
} message_t;

typedef struct
{
    char title[160];
    float scale;
    float box_radius;
    float edge_radius;
    participant_t participants[24];
    int participant_count;
    message_t messages[256];
    int message_count;
} sequence_diagram_t;

typedef struct
{
    int width;
    int height;
    unsigned char *pixels;
} image_t;

typedef struct
{
    int ready;
    int available;
    unsigned char *ttf_data;
    stbtt_fontinfo info;
    int ascent;
    int descent;
    int line_gap;
} font_cache_t;

static font_cache_t g_font;

static int ensure_parent_dirs(const char *path);

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

static color_t mix_color(color_t a, color_t b, int weight_b)
{
    color_t out;
    int weight_a = 255 - weight_b;
    out.r = (a.r * weight_a + b.r * weight_b) / 255;
    out.g = (a.g * weight_a + b.g * weight_b) / 255;
    out.b = (a.b * weight_a + b.b * weight_b) / 255;
    out.a = (a.a * weight_a + b.a * weight_b) / 255;
    return out;
}

static color_t activity_border_color(void)
{
    return color_rgba(20, 20, 20, 255);
}

static color_t activity_text_color(void)
{
    return color_rgba(20, 20, 20, 255);
}

static color_t activity_fill_color(color_t base)
{
    return mix_color(base, color_rgba(255, 255, 255, 255), 160);
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

static int parse_diagram_kind(const char *text, diagram_kind_t *out)
{
    if(eq_ci(text, "activity") || eq_ci(text, "flow"))
    {
        *out = DIAGRAM_ACTIVITY;
        return 1;
    }
    if(eq_ci(text, "sequence"))
    {
        *out = DIAGRAM_SEQUENCE;
        return 1;
    }
    return 0;
}

static float parse_scale_value(const char *text, float fallback)
{
    char *end = NULL;
    double value;

    if(!text || text[0] == '\0')
        return fallback;
    value = strtod(text, &end);
    if(end == text || (end && *end != '\0') || value <= 0.05)
        return fallback;
    return (float)value;
}

static float parse_option_value(const char *text, float fallback)
{
    char *end = NULL;
    double value;

    if(!text || text[0] == '\0')
        return fallback;
    value = strtod(text, &end);
    if(end == text || (end && *end != '\0') || value < 0.0)
        return fallback;
    return (float)value;
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

static int find_participant_index(const sequence_diagram_t *diagram,
                                  const char *id)
{
    int i;
    for(i = 0; i < diagram->participant_count; ++i)
    {
        if(strcmp(diagram->participants[i].id, id) == 0)
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
    diagram->scale = 1.0f;
    diagram->kind = DIAGRAM_ACTIVITY;
    diagram->box_radius = 8.0f;
    diagram->edge_radius = 14.0f;
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

        if(eq_ci(fields[0], "scale"))
        {
            if(count < 2)
            {
                free(owned);
                set_errorf("line %d: scale requires one field", line_no);
                return 0;
            }
            trim_in_place(fields[1]);
            diagram->scale = parse_scale_value(fields[1], 1.0f);
            continue;
        }

        if(eq_ci(fields[0], "box_radius"))
        {
            if(count < 2)
            {
                free(owned);
                set_errorf("line %d: box_radius requires one field", line_no);
                return 0;
            }
            trim_in_place(fields[1]);
            diagram->box_radius = parse_option_value(fields[1], 8.0f);
            continue;
        }

        if(eq_ci(fields[0], "edge_radius"))
        {
            if(count < 2)
            {
                free(owned);
                set_errorf("line %d: edge_radius requires one field", line_no);
                return 0;
            }
            trim_in_place(fields[1]);
            diagram->edge_radius = parse_option_value(fields[1], 14.0f);
            continue;
        }

        if(eq_ci(fields[0], "diagram"))
        {
            if(count < 2)
            {
                free(owned);
                set_errorf("line %d: diagram requires one field", line_no);
                return 0;
            }
            trim_in_place(fields[1]);
            if(!parse_diagram_kind(fields[1], &diagram->kind))
            {
                free(owned);
                set_errorf("line %d: unknown diagram type '%s'", line_no,
                           fields[1]);
                return 0;
            }
            continue;
        }

        if(eq_ci(fields[0], "node"))
        {
            if(diagram->kind == DIAGRAM_SEQUENCE)
                continue;
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
            if(diagram->kind == DIAGRAM_SEQUENCE)
                continue;
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

        if(diagram->kind == DIAGRAM_SEQUENCE &&
           (eq_ci(fields[0], "participant") || eq_ci(fields[0], "message")))
        {
            continue;
        }

        free(owned);
        set_errorf("line %d: unknown record type '%s'", line_no, fields[0]);
        return 0;
    }

    free(owned);
    if(diagram->kind == DIAGRAM_ACTIVITY && diagram->node_count == 0)
    {
        set_errorf("diagram contains no nodes");
        return 0;
    }
    return 1;
}

static int parse_sequence_text(sequence_diagram_t *diagram, const char *text)
{
    char *owned = NULL;
    char *cursor;
    int line_no = 0;

    memset(diagram, 0, sizeof(*diagram));
    diagram->scale = 1.0f;
    diagram->box_radius = 0.0f;
    diagram->edge_radius = 0.0f;
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

        if(eq_ci(fields[0], "diagram"))
        {
            if(count < 2)
            {
                free(owned);
                set_errorf("line %d: diagram requires one field", line_no);
                return 0;
            }
            trim_in_place(fields[1]);
            if(!eq_ci(fields[1], "sequence"))
            {
                free(owned);
                set_errorf("line %d: sequence input requires diagram|sequence",
                           line_no);
                return 0;
            }
            continue;
        }

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

        if(eq_ci(fields[0], "scale"))
        {
            if(count < 2)
            {
                free(owned);
                set_errorf("line %d: scale requires one field", line_no);
                return 0;
            }
            trim_in_place(fields[1]);
            diagram->scale = parse_scale_value(fields[1], 1.0f);
            continue;
        }

        if(eq_ci(fields[0], "box_radius"))
        {
            if(count < 2)
            {
                free(owned);
                set_errorf("line %d: box_radius requires one field", line_no);
                return 0;
            }
            trim_in_place(fields[1]);
            diagram->box_radius = parse_option_value(fields[1], 0.0f);
            continue;
        }

        if(eq_ci(fields[0], "edge_radius"))
        {
            if(count < 2)
            {
                free(owned);
                set_errorf("line %d: edge_radius requires one field", line_no);
                return 0;
            }
            trim_in_place(fields[1]);
            diagram->edge_radius = parse_option_value(fields[1], 0.0f);
            continue;
        }

        if(eq_ci(fields[0], "participant"))
        {
            participant_t *p;
            if(count < 6)
            {
                free(owned);
                set_errorf("line %d: participant format is participant|id|label|fill|stroke|text",
                           line_no);
                return 0;
            }
            if(diagram->participant_count >=
               (int)(sizeof(diagram->participants) / sizeof(diagram->participants[0])))
            {
                free(owned);
                set_errorf("too many participants");
                return 0;
            }
            trim_in_place(fields[1]);
            trim_in_place(fields[2]);
            trim_in_place(fields[3]);
            trim_in_place(fields[4]);
            trim_in_place(fields[5]);
            if(find_participant_index(diagram, fields[1]) >= 0)
            {
                free(owned);
                set_errorf("line %d: duplicate participant id '%s'", line_no,
                           fields[1]);
                return 0;
            }
            p = &diagram->participants[diagram->participant_count];
            memset(p, 0, sizeof(*p));
            snprintf(p->id, sizeof(p->id), "%s", fields[1]);
            snprintf(p->label, sizeof(p->label), "%s", fields[2]);
            if(!parse_color(fields[3], color_rgba(219, 234, 254, 255), &p->fill) ||
               !parse_color(fields[4], color_rgba(37, 99, 235, 255), &p->stroke) ||
               !parse_color(fields[5], color_rgba(15, 23, 42, 255), &p->text))
            {
                free(owned);
                set_errorf("line %d: invalid participant color field", line_no);
                return 0;
            }
            diagram->participant_count++;
            continue;
        }

        if(eq_ci(fields[0], "message"))
        {
            message_t *m;
            if(count < 5)
            {
                free(owned);
                set_errorf("line %d: message format is message|from|to|label|color",
                           line_no);
                return 0;
            }
            if(diagram->message_count >=
               (int)(sizeof(diagram->messages) / sizeof(diagram->messages[0])))
            {
                free(owned);
                set_errorf("too many messages");
                return 0;
            }
            trim_in_place(fields[1]);
            trim_in_place(fields[2]);
            trim_in_place(fields[3]);
            trim_in_place(fields[4]);
            m = &diagram->messages[diagram->message_count];
            memset(m, 0, sizeof(*m));
            m->from = find_participant_index(diagram, fields[1]);
            m->to = find_participant_index(diagram, fields[2]);
            if(m->from < 0 || m->to < 0)
            {
                free(owned);
                set_errorf("line %d: message references unknown participant",
                           line_no);
                return 0;
            }
            snprintf(m->label, sizeof(m->label), "%s", fields[3]);
            if(!parse_color(fields[4], color_rgba(71, 85, 105, 255), &m->color))
            {
                free(owned);
                set_errorf("line %d: invalid message color", line_no);
                return 0;
            }
            diagram->message_count++;
            continue;
        }

        free(owned);
        set_errorf("line %d: unknown sequence record type '%s'", line_no,
                   fields[0]);
        return 0;
    }

    free(owned);
    if(diagram->participant_count == 0)
    {
        set_errorf("sequence diagram contains no participants");
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

static int text_height(void)
{
    return (int)ceilf(UML_TEXT_SIZE);
}

static char *read_binary_file(const char *path, size_t *out_size)
{
    FILE *fp = fopen(path, "rb");
    long size;
    char *buf;
    size_t got;

    if(!fp)
        return NULL;
    if(fseek(fp, 0, SEEK_END) != 0)
    {
        fclose(fp);
        return NULL;
    }
    size = ftell(fp);
    if(size <= 0)
    {
        fclose(fp);
        return NULL;
    }
    if(fseek(fp, 0, SEEK_SET) != 0)
    {
        fclose(fp);
        return NULL;
    }
    buf = (char *)malloc((size_t)size);
    if(!buf)
    {
        fclose(fp);
        return NULL;
    }
    got = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if(got != (size_t)size)
    {
        free(buf);
        return NULL;
    }
    *out_size = (size_t)size;
    return buf;
}

static int ensure_font_loaded(void)
{
    static const char *candidates[] = {
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Geneva.ttf",
        "/Library/Fonts/Arial Unicode.ttf"
    };
    size_t i;

    if(g_font.ready)
        return g_font.available;

    memset(&g_font, 0, sizeof(g_font));
    g_font.ready = 1;

    for(i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i)
    {
        size_t font_size = 0;
        unsigned char *data = (unsigned char *)read_binary_file(candidates[i],
                                                                &font_size);
        int offset;
        if(!data)
            continue;
        offset = stbtt_GetFontOffsetForIndex(data, 0);
        if(offset < 0 || !stbtt_InitFont(&g_font.info, data, offset))
        {
            free(data);
            continue;
        }
        g_font.ttf_data = data;
        stbtt_GetFontVMetrics(&g_font.info, &g_font.ascent, &g_font.descent,
                              &g_font.line_gap);
        g_font.available = 1;
        return 1;
    }

    return 0;
}

static void compute_node_sizes(diagram_t *diagram)
{
    int i;
    float scale = diagram->scale;
    int text_h = text_height();
    int action_pad_x = (int)(16.0f * scale);
    int action_pad_y = (int)(10.0f * scale);
    int decision_pad_x = (int)(22.0f * scale);
    int decision_pad_y = (int)(16.0f * scale);
    for(i = 0; i < diagram->node_count; ++i)
    {
        node_t *node = &diagram->nodes[i];
        int label_w = text_width(node->label);
        if(node->type == NODE_START || node->type == NODE_END)
        {
            node->width = (int)(62.0f * scale);
            node->height = (int)(62.0f * scale);
        }
        else if(node->type == NODE_DECISION)
        {
            node->width = max_i32((int)(128.0f * scale),
                                  label_w + decision_pad_x * 2);
            if((node->width & 1) != 0)
                node->width++;
            node->height = max_i32((int)(54.0f * scale),
                                   text_h + decision_pad_y * 2);
        }
        else
        {
            node->width = max_i32((int)(104.0f * scale),
                                  label_w + action_pad_x * 2);
            node->height = max_i32((int)(38.0f * scale),
                                   text_h + action_pad_y * 2);
        }
    }
}

static void compute_layout(diagram_t *diagram, int *out_w, int *out_h)
{
    int i;
    int level_counts[64];
    int level_used_width[64];
    int max_level = 0;
    int max_cols = 1;
    float scale = diagram->scale;
    int margin_x = (int)(56.0f * scale);
    int margin_y = (int)(44.0f * scale);
    int sibling_gap = (int)(34.0f * scale);
    int row_gap = (int)(40.0f * scale);
    int level_max_width[64];
    int level_max_height[64];
    int level_y[64];
    int total_height = margin_y;

    memset(level_counts, 0, sizeof(level_counts));
    memset(level_used_width, 0, sizeof(level_used_width));
    memset(level_max_width, 0, sizeof(level_max_width));
    memset(level_max_height, 0, sizeof(level_max_height));
    memset(level_y, 0, sizeof(level_y));

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
        level_used_width[node->level] += node->width;
        if(node->width > level_max_width[node->level])
            level_max_width[node->level] = node->width;
        if(node->height > level_max_height[node->level])
            level_max_height[node->level] = node->height;
    }

    for(i = 0; i <= max_level; ++i)
    {
        if(level_counts[i] > max_cols)
            max_cols = level_counts[i];
        if(level_counts[i] > 1)
            level_used_width[i] += sibling_gap * (level_counts[i] - 1);
    }

    *out_w = margin_x * 2 + max_cols * (int)(190.0f * scale);
    for(i = 0; i <= max_level; ++i)
    {
        if(level_used_width[i] + margin_x * 2 > *out_w)
            *out_w = level_used_width[i] + margin_x * 2;
    }
    if(diagram->title[0] != '\0')
        total_height += (int)(34.0f * scale);

    for(i = 0; i <= max_level; ++i)
    {
        level_y[i] = total_height + level_max_height[i] / 2;
        total_height += level_max_height[i];
        if(i < max_level)
            total_height += row_gap;
    }
    *out_h = total_height + margin_y;

    for(i = 0; i < diagram->node_count; ++i)
    {
        node_t *node = &diagram->nodes[i];
        int j;
        int x_cursor = (*out_w - level_used_width[node->level]) / 2;

        for(j = 0; j < node->slot; ++j)
        {
            int k;
            for(k = 0; k < diagram->node_count; ++k)
            {
                if(diagram->nodes[k].level == node->level &&
                   diagram->nodes[k].slot == j)
                {
                    x_cursor += diagram->nodes[k].width + sibling_gap;
                    break;
                }
            }
        }

        node->x = x_cursor + node->width / 2;
        node->y = level_y[node->level];
    }

    {
        int min_extent = *out_w;
        int max_extent = 0;
        int expand_left = 0;
        int expand_right = 0;

        for(i = 0; i < diagram->node_count; ++i)
        {
            const node_t *node = &diagram->nodes[i];
            int left = node->x - node->width / 2 - margin_x / 2;
            int right = node->x + node->width / 2 + margin_x / 2;
            int label_w = text_width(node->label);
            int label_left = node->x - label_w / 2 - 12;
            int label_right = node->x + label_w / 2 + 12;
            if(left < min_extent)
                min_extent = left;
            if(label_left < min_extent)
                min_extent = label_left;
            if(right > max_extent)
                max_extent = right;
            if(label_right > max_extent)
                max_extent = label_right;
        }

        for(i = 0; i < diagram->edge_count; ++i)
        {
            const edge_t *edge = &diagram->edges[i];
            const node_t *from = &diagram->nodes[edge->from];
            const node_t *to = &diagram->nodes[edge->to];
            int label_w = text_width(edge->label);

            if(edge->label[0] != '\0')
            {
                int label_left;
                int label_right;
                if(to->level > from->level && from->type == NODE_DECISION &&
                   to->x != from->x)
                {
                    int dir = to->x > from->x ? 1 : -1;
                    int exit_x = from->x + dir * (from->width / 2);
                    int branch_x = exit_x + dir * max_i32(18, from->width / 6);
                    int label_x = branch_x - label_w / 2;
                    if(dir > 0)
                        label_x -= max_i32(8, label_w / 4);
                    else
                        label_x += max_i32(8, label_w / 4);
                    label_left = label_x - 8;
                    label_right = label_x + label_w + 8;
                }
                else
                {
                    int center_x = (from->x + to->x) / 2;
                    label_left = center_x - label_w / 2 - 8;
                    label_right = center_x + label_w / 2 + 8;
                }
                if(label_left < min_extent)
                    min_extent = label_left;
                if(label_right > max_extent)
                    max_extent = label_right;
            }

            if(to->level <= from->level)
            {
                int dir = to->x >= from->x ? 1 : -1;
                int bend_x = dir > 0
                                 ? max_i32(from->x + from->width / 2 + 40,
                                           to->x + to->width / 2 + 40)
                                 : min_i32(from->x - from->width / 2 - 40,
                                           to->x - to->width / 2 - 40);
                if(bend_x - margin_x / 2 < min_extent)
                    min_extent = bend_x - margin_x / 2;
                if(bend_x + margin_x / 2 > max_extent)
                    max_extent = bend_x + margin_x / 2;
            }
        }

        if(min_extent < 0)
            expand_left = -min_extent;
        if(max_extent > *out_w)
            expand_right = max_extent - *out_w;

        if(expand_left > 0 || expand_right > 0)
        {
            for(i = 0; i < diagram->node_count; ++i)
                diagram->nodes[i].x += expand_left;
            *out_w += expand_left + expand_right;
        }
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

static int abs_i32(int v)
{
    return v < 0 ? -v : v;
}

static int aa_px(int v)
{
    return v * UML_AA_SCALE;
}

static int aa_stroke(int v)
{
    int scaled = v * UML_AA_SCALE;
    return scaled > 0 ? scaled : 1;
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

static image_t downsample_image(const image_t *src, int out_w, int out_h)
{
    image_t out;
    int y;

    out.width = out_w;
    out.height = out_h;
    out.pixels = (unsigned char *)calloc((size_t)out_w * (size_t)out_h * 4u, 1u);
    if(!out.pixels)
    {
        out.width = 0;
        out.height = 0;
        return out;
    }

    for(y = 0; y < out_h; ++y)
    {
        int x;
        for(x = 0; x < out_w; ++x)
        {
            int sx0 = x * UML_AA_SCALE;
            int sy0 = y * UML_AA_SCALE;
            int yy;
            int r = 0;
            int g = 0;
            int b = 0;
            int a = 0;
            for(yy = 0; yy < UML_AA_SCALE; ++yy)
            {
                int xx;
                for(xx = 0; xx < UML_AA_SCALE; ++xx)
                {
                    size_t idx =
                        ((size_t)(sy0 + yy) * (size_t)src->width +
                         (size_t)(sx0 + xx)) *
                        4u;
                    r += src->pixels[idx + 0];
                    g += src->pixels[idx + 1];
                    b += src->pixels[idx + 2];
                    a += src->pixels[idx + 3];
                }
            }
            {
                size_t out_idx =
                    ((size_t)y * (size_t)out.width + (size_t)x) * 4u;
                int denom = UML_AA_SCALE * UML_AA_SCALE;
                out.pixels[out_idx + 0] = (unsigned char)(r / denom);
                out.pixels[out_idx + 1] = (unsigned char)(g / denom);
                out.pixels[out_idx + 2] = (unsigned char)(b / denom);
                out.pixels[out_idx + 3] = (unsigned char)(a / denom);
            }
        }
    }

    return out;
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
    fill_rect(img, x + radius, y, x + w - radius, y + thickness, color);
    fill_rect(img, x + radius, y + h - thickness, x + w - radius, y + h,
              color);
    fill_rect(img, x, y + radius, x + thickness, y + h - radius, color);
    fill_rect(img, x + w - thickness, y + radius, x + w, y + h - radius,
              color);
    stroke_circle(img, x + radius, y + radius, radius, thickness, color);
    stroke_circle(img, x + w - radius, y + radius, radius, thickness, color);
    stroke_circle(img, x + radius, y + h - radius, radius, thickness, color);
    stroke_circle(img, x + w - radius, y + h - radius, radius, thickness,
                  color);
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
    int dx = x1 - x0;
    int dy = y1 - y0;
    int steps = max_i32(abs(dx), abs(dy));
    int half = thickness / 2;
    int step;

    if(steps == 0)
    {
        int ox;
        int oy;
        for(oy = -half; oy <= half; ++oy)
        {
            for(ox = -half; ox <= half; ++ox)
                set_pixel(img, x0 + ox, y0 + oy, color);
        }
        return;
    }

    for(step = 0; step <= steps; ++step)
    {
        int px = x0 + (dx * step) / steps;
        int py = y0 + (dy * step) / steps;
        int ox;
        int oy;
        for(oy = -half; oy <= half; ++oy)
        {
            for(ox = -half; ox <= half; ++ox)
                set_pixel(img, px + ox, py + oy, color);
        }
    }
}

static void draw_arrow_head(image_t *img, int tip_x, int tip_y, int dx, int dy, color_t color)
{
    int len = aa_px(8);
    int wing = aa_px(4);
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
    draw_line(img, tip_x, tip_y, lx, ly, UML_STROKE, color);
    draw_line(img, tip_x, tip_y, rx, ry, UML_STROKE, color);
}

static void draw_text(image_t *img, int x, int y, const char *text, color_t color)
{
    if(ensure_font_loaded())
    {
        float scale = stbtt_ScaleForPixelHeight(&g_font.info,
                                                UML_TEXT_SIZE * (float)UML_AA_SCALE);
        float pen_x = (float)aa_px(x);
        int baseline = aa_px(y) + (int)(g_font.ascent * scale);
        const unsigned char *p = (const unsigned char *)text;

        while(*p != '\0')
        {
            int cp = (int)*p++;
            int advance;
            int lsb;
            int x0;
            int y0;
            int x1;
            int y1;
            int glyph_w;
            int glyph_h;

            stbtt_GetCodepointHMetrics(&g_font.info, cp, &advance, &lsb);
            stbtt_GetCodepointBitmapBox(&g_font.info, cp, scale, scale,
                                        &x0, &y0, &x1, &y1);
            glyph_w = x1 - x0;
            glyph_h = y1 - y0;
            if(glyph_w > 0 && glyph_h > 0)
            {
                unsigned char *bitmap =
                    (unsigned char *)calloc((size_t)glyph_w * (size_t)glyph_h, 1u);
                int gy;
                if(bitmap)
                {
                    stbtt_MakeCodepointBitmap(&g_font.info, bitmap, glyph_w,
                                              glyph_h, glyph_w, scale, scale,
                                              cp);
                    for(gy = 0; gy < glyph_h; ++gy)
                    {
                        int gx;
                        for(gx = 0; gx < glyph_w; ++gx)
                        {
                            unsigned char alpha = bitmap[gy * glyph_w + gx];
                            if(alpha > 0)
                            {
                                color_t shaded = color;
                                shaded.a = (color.a * alpha) / 255;
                                set_pixel(img, (int)pen_x + x0 + gx,
                                          baseline + y0 + gy, shaded);
                            }
                        }
                    }
                    free(bitmap);
                }
            }

            pen_x += (float)advance * scale;
            if(*p != '\0')
            {
                int kern = stbtt_GetCodepointKernAdvance(&g_font.info, cp,
                                                         (int)*p);
                pen_x += (float)kern * scale;
            }
        }
        return;
    }

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
        quads = stb_easy_font_print(0.0f, 0.0f, (char *)text, rgba,
                                    buffer, (int)sizeof(buffer));
        for(i = 0; i < quads; ++i)
        {
            easy_font_vertex_t *v = (easy_font_vertex_t *)buffer + i * 4;
            float scale = (float)UML_AA_SCALE * 1.25f;
            float min_xf = (float)aa_px(x) + v[0].x * scale;
            float max_xf = (float)aa_px(x) + v[0].x * scale;
            float min_yf = (float)aa_px(y) + v[0].y * scale;
            float max_yf = (float)aa_px(y) + v[0].y * scale;
            int j;
            int min_x;
            int max_x;
            int min_y;
            int max_y;
            for(j = 1; j < 4; ++j)
            {
                float sx = (float)aa_px(x) + v[j].x * scale;
                float sy = (float)aa_px(y) + v[j].y * scale;
                if(sx < min_xf)
                    min_xf = sx;
                if(sx > max_xf)
                    max_xf = sx;
                if(sy < min_yf)
                    min_yf = sy;
                if(sy > max_yf)
                    max_yf = sy;
            }
            min_x = (int)floorf(min_xf);
            max_x = (int)ceilf(max_xf);
            min_y = (int)floorf(min_yf);
            max_y = (int)ceilf(max_yf);
            fill_rect(img, min_x, min_y, max_x, max_y, color);
        }
    }
}

static int text_width(const char *text)
{
    if(ensure_font_loaded())
    {
        float scale = stbtt_ScaleForPixelHeight(&g_font.info,
                                                UML_TEXT_SIZE * (float)UML_AA_SCALE);
        float width = 0.0f;
        const unsigned char *p = (const unsigned char *)text;
        while(*p != '\0')
        {
            int cp = (int)*p++;
            int advance;
            int lsb;
            stbtt_GetCodepointHMetrics(&g_font.info, cp, &advance, &lsb);
            width += (float)advance * scale;
            if(*p != '\0')
                width += (float)stbtt_GetCodepointKernAdvance(&g_font.info, cp,
                                                              (int)*p) *
                         scale;
        }
        return (int)ceilf(width / (float)UML_AA_SCALE);
    }
    return (int)ceilf((float)stb_easy_font_width((char *)text) * 1.25f);
}

static void draw_title(image_t *img, const char *title)
{
    color_t title_color = color_rgba(15, 23, 42, 255);
    int x = (img->width / UML_AA_SCALE) / 2 - text_width(title) / 2;
    draw_text(img, x, 26, title, title_color);
}

static void draw_soft_box(image_t *img, int x, int y, int w, int h, int radius,
                          color_t fill, color_t border)
{
    int ax = aa_px(x);
    int ay = aa_px(y);
    int aw = aa_px(w);
    int ah = aa_px(h);
    int border_w = aa_stroke(UML_STROKE);
    int outer_radius = min_i32(aa_px(radius), min_i32(aw, ah) / 2);
    int inner_w = aw - border_w * 2;
    int inner_h = ah - border_w * 2;

    fill_rounded_rect(img, ax, ay, aw, ah, outer_radius, border);
    if(inner_w > 0 && inner_h > 0)
    {
        int inner_radius = outer_radius - border_w;
        if(inner_radius < 0)
            inner_radius = 0;
        fill_rounded_rect(img, ax + border_w, ay + border_w, inner_w, inner_h,
                          inner_radius, fill);
    }
}

static void draw_node(image_t *img, const diagram_t *diagram, const node_t *node)
{
    int x = node->x - node->width / 2;
    int y = node->y - node->height / 2;
    int label_x = node->x - text_width(node->label) / 2;
    int label_y = node->y - text_height() / 2 - 1;
    color_t border = activity_border_color();
    color_t text = activity_text_color();
    color_t fill = activity_fill_color(node->fill);

    if(node->type == NODE_START)
    {
        fill_circle(img, aa_px(node->x), aa_px(node->y), aa_px(node->width / 2),
                    border);
        stroke_circle(img, aa_px(node->x), aa_px(node->y),
                      aa_px(node->width / 2), aa_stroke(UML_STROKE),
                      border);
    }
    else if(node->type == NODE_END)
    {
        fill_circle(img, aa_px(node->x), aa_px(node->y), aa_px(node->width / 2),
                    color_rgba(255, 255, 255, 255));
        stroke_circle(img, aa_px(node->x), aa_px(node->y),
                      aa_px(node->width / 2), aa_stroke(UML_STROKE),
                      border);
        fill_circle(img, aa_px(node->x), aa_px(node->y),
                    aa_px(node->width / 2 - 10), border);
    }
    else if(node->type == NODE_DECISION)
    {
        fill_diamond(img, aa_px(node->x), aa_px(node->y), aa_px(node->width),
                     aa_px(node->height), mix_color(fill, color_rgba(255, 255, 255, 255), 80));
        stroke_diamond(img, aa_px(node->x), aa_px(node->y), aa_px(node->width),
                       aa_px(node->height), aa_stroke(UML_STROKE),
                       border);
    }
    else
    {
        draw_soft_box(img, x, y, node->width, node->height,
                      (int)(diagram->box_radius * diagram->scale), fill,
                      border);
    }

    if(node->label[0] != '\0')
        draw_text(img, label_x, label_y, node->label, text);
}

static void draw_arc_segment(image_t *img, float cx, float cy, float radius,
                             float start_angle, float end_angle, color_t color)
{
    int i;
    int steps =
        max_i32(6, (int)ceilf(fabsf(end_angle - start_angle) * radius / 14.0f));
    int prev_x = (int)roundf(cx + cosf(start_angle) * radius);
    int prev_y = (int)roundf(cy + sinf(start_angle) * radius);
    for(i = 1; i <= steps; ++i)
    {
        float t = (float)i / (float)steps;
        float angle = start_angle + (end_angle - start_angle) * t;
        int x = (int)roundf(cx + cosf(angle) * radius);
        int y = (int)roundf(cy + sinf(angle) * radius);
        draw_line(img, prev_x, prev_y, x, y, aa_stroke(UML_STROKE), color);
        prev_x = x;
        prev_y = y;
    }
}

static void draw_polyline(image_t *img, int *xs, int *ys, int count, int radius,
                          color_t color)
{
    int i;
    int last_x;
    int last_y;

    if(count < 2)
        return;

    last_x = aa_px(xs[0]);
    last_y = aa_px(ys[0]);

    for(i = 1; i < count - 1; ++i)
    {
        int prev_x = xs[i - 1];
        int prev_y = ys[i - 1];
        int cur_x = xs[i];
        int cur_y = ys[i];
        int next_x = xs[i + 1];
        int next_y = ys[i + 1];
        int dx1 = cur_x - prev_x;
        int dy1 = cur_y - prev_y;
        int dx2 = next_x - cur_x;
        int dy2 = next_y - cur_y;
        int len1 = abs_i32(dx1) + abs_i32(dy1);
        int len2 = abs_i32(dx2) + abs_i32(dy2);
        int corner = min_i32(radius, min_i32(len1 / 2, len2 / 2));

        if(corner <= 0 || (dx1 != 0 && dx2 != 0) || (dy1 != 0 && dy2 != 0))
        {
            draw_line(img, last_x, last_y, aa_px(cur_x), aa_px(cur_y),
                      aa_stroke(UML_STROKE), color);
            last_x = aa_px(cur_x);
            last_y = aa_px(cur_y);
            continue;
        }

        {
            int dir1_x = dx1 == 0 ? 0 : (dx1 > 0 ? 1 : -1);
            int dir1_y = dy1 == 0 ? 0 : (dy1 > 0 ? 1 : -1);
            int dir2_x = dx2 == 0 ? 0 : (dx2 > 0 ? 1 : -1);
            int dir2_y = dy2 == 0 ? 0 : (dy2 > 0 ? 1 : -1);
            int in_x = cur_x - dir1_x * corner;
            int in_y = cur_y - dir1_y * corner;
            int out_x = cur_x + dir2_x * corner;
            int out_y = cur_y + dir2_y * corner;
            float center_x = (float)aa_px(cur_x - dir1_x * corner + dir2_x * corner);
            float center_y = (float)aa_px(cur_y - dir1_y * corner + dir2_y * corner);
            float start_angle =
                atan2f((float)(aa_px(in_y) - center_y), (float)(aa_px(in_x) - center_x));
            float end_angle =
                atan2f((float)(aa_px(out_y) - center_y), (float)(aa_px(out_x) - center_x));
            float delta = end_angle - start_angle;

            while(delta > (float)M_PI)
                delta -= (float)(M_PI * 2.0);
            while(delta < (float)-M_PI)
                delta += (float)(M_PI * 2.0);
            end_angle = start_angle + delta;

            draw_line(img, last_x, last_y, aa_px(in_x), aa_px(in_y),
                      aa_stroke(UML_STROKE), color);
            draw_arc_segment(img, center_x, center_y, (float)aa_px(corner),
                             start_angle, end_angle, color);
            last_x = aa_px(out_x);
            last_y = aa_px(out_y);
        }
    }

    draw_line(img, last_x, last_y, aa_px(xs[count - 1]), aa_px(ys[count - 1]),
              aa_stroke(UML_STROKE), color);
    if(count >= 2)
        draw_arrow_head(img, aa_px(xs[count - 1]), aa_px(ys[count - 1]),
                        aa_px(xs[count - 1] - xs[count - 2]),
                        aa_px(ys[count - 1] - ys[count - 2]), color);
}

static void draw_edge_label(image_t *img, int x, int y, const char *text, color_t color)
{
    color_t bg = color_rgba(255, 255, 255, 230);
    int w;
    int h;
    if(text[0] == '\0')
        return;
    w = text_width(text);
    h = text_height();
    fill_rect(img, aa_px(x - 6), aa_px(y - 4), aa_px(x + w + 6),
              aa_px(y + h + 4), bg);
    draw_text(img, x, y, text, color);
}

static int effective_edge_radius(const diagram_t *diagram)
{
    int configured = (int)(diagram->edge_radius * diagram->scale);
    int box_based = (int)(diagram->box_radius * diagram->scale * 0.5f);
    int radius = configured;

    if(box_based > 0 && (radius <= 0 || radius > box_based))
        radius = box_based;
    if(radius < 2)
        radius = 2;
    return radius;
}

static int is_primary_converging_edge(const diagram_t *diagram, const edge_t *edge,
                                      int *out_peer_count)
{
    int i;
    int peer_count = 0;
    int primary_from = edge->from;

    for(i = 0; i < diagram->edge_count; ++i)
    {
        const edge_t *candidate = &diagram->edges[i];
        if(candidate->to != edge->to)
            continue;
        if(diagram->nodes[candidate->from].level !=
           diagram->nodes[edge->from].level)
            continue;
        if(candidate->to <= candidate->from)
            continue;
        peer_count++;
        if(diagram->nodes[candidate->from].x < diagram->nodes[primary_from].x)
            primary_from = candidate->from;
    }

    if(out_peer_count)
        *out_peer_count = peer_count;
    return edge->from == primary_from;
}

static void draw_edge(image_t *img, const diagram_t *diagram, const edge_t *edge)
{
    const node_t *from = &diagram->nodes[edge->from];
    const node_t *to = &diagram->nodes[edge->to];
    color_t edge_color = activity_border_color();
    int xs[6];
    int ys[6];
    int count = 0;
    int radius = effective_edge_radius(diagram);

    if(to->level > from->level)
    {
        int sx = from->x;
        int sy = from->y + from->height / 2;
        int tx = to->x;
        int ty = to->y - to->height / 2;
        int label_x;
        int label_y;

        if(from->type == NODE_DECISION && tx != from->x)
        {
            int dir = tx > from->x ? 1 : -1;
            int exit_x = from->x + dir * (from->width / 2);
            int exit_y = from->y;
            int branch_x = exit_x + dir * max_i32(12, from->width / 8);
            int mid_y = ty - max_i32(16, radius * 3);

            xs[count] = exit_x;
            ys[count++] = exit_y;
            xs[count] = branch_x;
            ys[count++] = exit_y;
            xs[count] = branch_x;
            ys[count++] = mid_y;
            xs[count] = tx;
            ys[count++] = mid_y;
            xs[count] = tx;
            ys[count++] = ty;

            draw_polyline(img, xs, ys, count, radius, edge_color);
            label_x = exit_x + dir * max_i32(8, text_width(edge->label) / 3) -
                      text_width(edge->label) / 2;
            if(dir > 0)
                label_x -= 4;
            else
                label_x += 4;
            label_y = exit_y - text_height() - 8;
            draw_edge_label(img, label_x, label_y, edge->label, edge_color);
            return;
        }
        else
        {
            int mid_y = sy + (ty - sy) / 2;
            int peer_count = 0;
            int is_primary = is_primary_converging_edge(diagram, edge, &peer_count);
            int path_radius = radius;
            label_y = min_i32(mid_y - text_height() - 12, ty - text_height() - 8);
            if(peer_count > 1)
            {
                int merge_y = ty - max_i32(16, radius * 3);
                int primary_x = sx;
                int i;
                path_radius = 0;

                for(i = 0; i < diagram->edge_count; ++i)
                {
                    const edge_t *candidate = &diagram->edges[i];
                    if(candidate->to != edge->to)
                        continue;
                    if(diagram->nodes[candidate->from].level !=
                       diagram->nodes[edge->from].level)
                        continue;
                    if(candidate->to <= candidate->from)
                        continue;
                    if(diagram->nodes[candidate->from].x < primary_x)
                        primary_x = diagram->nodes[candidate->from].x;
                }

                xs[count] = sx;
                ys[count++] = sy;
                xs[count] = sx;
                ys[count++] = merge_y;
                if(sx != primary_x)
                {
                    xs[count] = primary_x;
                    ys[count++] = merge_y;
                }
                if(is_primary)
                {
                    xs[count] = tx;
                    ys[count++] = merge_y;
                    xs[count] = tx;
                    ys[count++] = ty;
                }
            }
            else
            {
                xs[count] = sx;
                ys[count++] = sy;
                xs[count] = sx;
                ys[count++] = mid_y;
                xs[count] = tx;
                ys[count++] = mid_y;
                xs[count] = tx;
                ys[count++] = ty;
            }
            draw_polyline(img, xs, ys, count, path_radius, edge_color);
            label_x = (sx + tx) / 2 - text_width(edge->label) / 2;
            draw_edge_label(img, label_x, label_y, edge->label, edge_color);
            return;
        }
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
        int label_y = top_y - text_height() - 10;

        if(from->type == NODE_DECISION)
        {
            sx = from->x + dir * (from->width / 2);
            sy = from->y;
        }
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
        draw_polyline(img, xs, ys, count, radius, edge_color);
        draw_edge_label(img, bend_x - text_width(edge->label) / 2, label_y,
                        edge->label, edge_color);
    }
}

static void compute_sequence_layout(sequence_diagram_t *diagram, int *out_w,
                                    int *out_h)
{
    int i;
    float scale = diagram->scale;
    int top = (int)(70.0f * scale);
    int participant_gap = (int)(142.0f * scale);
    int margin_x = (int)(48.0f * scale);
    int message_gap = (int)(56.0f * scale);

    for(i = 0; i < diagram->participant_count; ++i)
    {
        participant_t *p = &diagram->participants[i];
        p->width = max_i32((int)(120.0f * scale),
                           (int)((float)text_width(p->label) * scale + 24.0f * scale));
        p->x = margin_x + p->width / 2 + i * participant_gap;
    }

    *out_w = margin_x * 2;
    for(i = 0; i < diagram->participant_count; ++i)
    {
        int right = diagram->participants[i].x + diagram->participants[i].width / 2;
        if(right + margin_x > *out_w)
            *out_w = right + margin_x;
    }

    *out_h = top + (int)(52.0f * scale) + diagram->message_count * message_gap +
             (int)(52.0f * scale);
    if(diagram->title[0] != '\0')
        *out_h += (int)(24.0f * scale);

    for(i = 0; i < diagram->message_count; ++i)
        diagram->messages[i].y = top + (int)(38.0f * scale) + i * message_gap +
                                 (diagram->title[0] != '\0' ? (int)(24.0f * scale) : 0);
}

static void draw_vertical_line(image_t *img, int x, int y0, int y1,
                               color_t color)
{
    draw_line(img, aa_px(x), aa_px(y0), aa_px(x), aa_px(y1),
              aa_stroke(UML_STROKE), color);
}

static void draw_participant(image_t *img, const participant_t *p, int top_y,
                             int bottom_y)
{
    int box_h = text_height() + 16;
    int border = UML_STROKE;
    int x = p->x - p->width / 2;
    int label_x = p->x - text_width(p->label) / 2;
    fill_rect(img, aa_px(x), aa_px(top_y), aa_px(x + p->width),
              aa_px(top_y + box_h), p->fill);
    fill_rect(img, aa_px(x), aa_px(top_y), aa_px(x + p->width),
              aa_px(top_y + border), p->stroke);
    fill_rect(img, aa_px(x), aa_px(top_y + box_h - border),
              aa_px(x + p->width), aa_px(top_y + box_h), p->stroke);
    fill_rect(img, aa_px(x), aa_px(top_y), aa_px(x + border),
              aa_px(top_y + box_h), p->stroke);
    fill_rect(img, aa_px(x + p->width - border), aa_px(top_y),
              aa_px(x + p->width), aa_px(top_y + box_h), p->stroke);
    draw_text(img, label_x, top_y + (box_h - text_height()) / 2, p->label,
              p->text);
    draw_vertical_line(img, p->x, top_y + box_h, bottom_y, p->stroke);
}

static void draw_sequence_arrow(image_t *img, int x0, int x1, int y,
                                color_t color)
{
    draw_line(img, aa_px(x0), aa_px(y), aa_px(x1), aa_px(y),
              aa_stroke(UML_STROKE), color);
    draw_arrow_head(img, aa_px(x1), aa_px(y), aa_px(x1 - x0), 0, color);
}

static void draw_sequence_message(image_t *img,
                                  const sequence_diagram_t *diagram,
                                  const message_t *m)
{
    const participant_t *from = &diagram->participants[m->from];
    const participant_t *to = &diagram->participants[m->to];
    int start_x = from->x;
    int end_x = to->x;
    int label_x = min_i32(start_x, end_x) + (abs(end_x - start_x) - text_width(m->label)) / 2;

    if(m->from == m->to)
    {
        int right = from->x + from->width / 2 + 48;
        int y2 = m->y + 28;
        draw_line(img, aa_px(from->x), aa_px(m->y), aa_px(right), aa_px(m->y),
                  aa_stroke(UML_STROKE), m->color);
        draw_line(img, aa_px(right), aa_px(m->y), aa_px(right), aa_px(y2),
                  aa_stroke(UML_STROKE), m->color);
        draw_line(img, aa_px(right), aa_px(y2), aa_px(from->x), aa_px(y2),
                  aa_stroke(UML_STROKE), m->color);
        draw_arrow_head(img, aa_px(from->x), aa_px(y2), aa_px(from->x - right),
                        0, m->color);
        draw_edge_label(img, from->x + 12, m->y - 26, m->label, m->color);
        return;
    }

    draw_sequence_arrow(img, start_x, end_x, m->y, m->color);
    draw_edge_label(img, label_x, m->y - 26, m->label, m->color);
}

static int render_sequence_file(const char *input_path, const char *output_path,
                                const char *text)
{
    sequence_diagram_t diagram;
    image_t image;
    int i;
    int top_y;
    int logical_w;
    int logical_h;

    if(!parse_sequence_text(&diagram, text))
        return 1;
    compute_sequence_layout(&diagram, &image.width, &image.height);
    logical_w = image.width;
    logical_h = image.height;
    {
        image.width = logical_w * UML_AA_SCALE;
        image.height = logical_h * UML_AA_SCALE;
    }
    image.pixels = (unsigned char *)calloc((size_t)image.width * (size_t)image.height * 4u, 1u);
    if(!image.pixels)
    {
        set_errorf("out of memory");
        return 1;
    }

    clear_image(&image, color_rgba(248, 250, 252, 255));
    if(diagram.title[0] != '\0')
        draw_title(&image, diagram.title);
    top_y = 40 + (diagram.title[0] != '\0' ? 24 : 0);

    for(i = 0; i < diagram.participant_count; ++i)
        draw_participant(&image, &diagram.participants[i], top_y,
                         logical_h - 36);
    for(i = 0; i < diagram.message_count; ++i)
        draw_sequence_message(&image, &diagram, &diagram.messages[i]);
    {
        image_t out = downsample_image(&image, image.width / UML_AA_SCALE,
                                       image.height / UML_AA_SCALE);
        free(image.pixels);
        if(!out.pixels)
        {
            set_errorf("out of memory");
            return 1;
        }
        image = out;
    }

    if(!ensure_parent_dirs(output_path))
    {
        free(image.pixels);
        return 1;
    }
    if(stbi_write_png(output_path, image.width, image.height, 4, image.pixels,
                      image.width * 4) == 0)
    {
        free(image.pixels);
        set_errorf("failed to write png: %s", output_path);
        return 1;
    }
    free(image.pixels);
    (void)input_path;
    return 0;
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

    if(diagram.kind == DIAGRAM_SEQUENCE)
    {
        int rc = render_sequence_file(input_path, output_path, text);
        free(text);
        return rc;
    }
    free(text);

    compute_node_sizes(&diagram);
    compute_layout(&diagram, &image.width, &image.height);
    {
        int logical_w = image.width;
        int logical_h = image.height;
        image.width = logical_w * UML_AA_SCALE;
        image.height = logical_h * UML_AA_SCALE;
    }
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
        draw_node(&image, &diagram, &diagram.nodes[i]);
    {
        image_t out = downsample_image(&image, image.width / UML_AA_SCALE,
                                       image.height / UML_AA_SCALE);
        free(image.pixels);
        if(!out.pixels)
        {
            set_errorf("out of memory");
            return 1;
        }
        image = out;
    }

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
