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
static int parse_string_value(const char *text, char *out, size_t out_size);
static int parse_bool_value(const char *text, int fallback);

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
    DIAGRAM_SEQUENCE = 1,
    DIAGRAM_CLASS = 2,
    DIAGRAM_PACKAGE = 3
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
    int bold;
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
    float start_radius;
    float end_radius;
    float title_size;
    int title_bold;
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
    int bold;
} participant_t;

typedef struct
{
    int from;
    int to;
    char label[160];
    color_t color;
    int y;
    int bold;
} message_t;

typedef struct
{
    char title[160];
    float scale;
    float box_radius;
    float edge_radius;
    float arrow_size;
    float title_size;
    int title_bold;
    participant_t participants[24];
    int participant_count;
    message_t messages[256];
    int message_count;
} sequence_diagram_t;

typedef enum
{
    CLASS_ASSOCIATION = 0,
    CLASS_AGGREGATION = 1,
    CLASS_COMPOSITION = 2,
    CLASS_GENERALIZATION = 3,
    CLASS_REALIZATION = 4,
    CLASS_DEPENDENCY = 5
} class_association_kind_t;

typedef struct
{
    char id[64];
    char name[160];
    int x;
    int y;
    int width;
    int height;
    char attributes[24][160];
    int attribute_count;
    char methods[24][160];
    int method_count;
    color_t fill;
    color_t header_fill;
    color_t stroke;
    color_t text;
    int bold;
} class_box_t;

typedef struct
{
    int from;
    int to;
    class_association_kind_t kind;
    char from_multiplicity[32];
    char to_multiplicity[32];
    char label[80];
    color_t color;
    int bold;
} class_association_t;

typedef struct
{
    char title[160];
    float scale;
    float box_radius;
    float edge_radius;
    float title_size;
    int title_bold;
    class_box_t classes[48];
    int class_count;
    class_association_t associations[96];
    int association_count;
} class_diagram_t;

typedef enum
{
    PACKAGE_CONTAINER = 0,
    PACKAGE_PACKAGE = 1,
    PACKAGE_MODEL = 2
} package_element_kind_t;

typedef struct
{
    char id[64];
    package_element_kind_t kind;
    int parent;
    char label[160];
    char stereotype[64];
    int x;
    int y;
    int width;
    int height;
    color_t fill;
    color_t header_fill;
    color_t stroke;
    color_t text;
    int bold;
} package_element_t;

typedef enum
{
    ANCHOR_AUTO = 0,
    ANCHOR_TOP = 1,
    ANCHOR_RIGHT = 2,
    ANCHOR_BOTTOM = 3,
    ANCHOR_LEFT = 4
} anchor_side_t;

typedef struct
{
    int from;
    int to;
    char label[96];
    color_t color;
    int bold;
    int point_count;
    int points_x[16];
    int points_y[16];
    int corner_radius;
    anchor_side_t from_side;
    anchor_side_t to_side;
} package_dependency_t;

typedef struct
{
    char title[160];
    float scale;
    float box_radius;
    float edge_radius;
    float title_size;
    int title_bold;
    package_element_t elements[64];
    int element_count;
    package_dependency_t dependencies[128];
    int dependency_count;
} package_diagram_t;

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
    char buffer[32];
    const char *value = text;
    int r;
    int g;
    int b;

    if(!text || text[0] == '\0')
    {
        *out = fallback;
        return 1;
    }
    if(!parse_string_value(text, buffer, sizeof(buffer)))
        return 0;
    value = buffer;
    if(value[0] != '#' || strlen(value) != 7)
        return 0;
    r = parse_hex_byte(value + 1);
    g = parse_hex_byte(value + 3);
    b = parse_hex_byte(value + 5);
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
    if(eq_ci(text, "class"))
    {
        *out = DIAGRAM_CLASS;
        return 1;
    }
    if(eq_ci(text, "package"))
    {
        *out = DIAGRAM_PACKAGE;
        return 1;
    }
    return 0;
}

static int parse_package_element_kind(const char *text,
                                      package_element_kind_t *out)
{
    if(eq_ci(text, "container"))
    {
        *out = PACKAGE_CONTAINER;
        return 1;
    }
    if(eq_ci(text, "package"))
    {
        *out = PACKAGE_PACKAGE;
        return 1;
    }
    if(eq_ci(text, "model"))
    {
        *out = PACKAGE_MODEL;
        return 1;
    }
    return 0;
}

static int parse_anchor_side(const char *text, anchor_side_t *out)
{
    if(eq_ci(text, "auto"))
    {
        *out = ANCHOR_AUTO;
        return 1;
    }
    if(eq_ci(text, "top"))
    {
        *out = ANCHOR_TOP;
        return 1;
    }
    if(eq_ci(text, "right"))
    {
        *out = ANCHOR_RIGHT;
        return 1;
    }
    if(eq_ci(text, "bottom"))
    {
        *out = ANCHOR_BOTTOM;
        return 1;
    }
    if(eq_ci(text, "left"))
    {
        *out = ANCHOR_LEFT;
        return 1;
    }
    return 0;
}

static int parse_class_association_kind(const char *text,
                                        class_association_kind_t *out)
{
    if(eq_ci(text, "association"))
    {
        *out = CLASS_ASSOCIATION;
        return 1;
    }
    if(eq_ci(text, "aggregation"))
    {
        *out = CLASS_AGGREGATION;
        return 1;
    }
    if(eq_ci(text, "composition"))
    {
        *out = CLASS_COMPOSITION;
        return 1;
    }
    if(eq_ci(text, "generalization") || eq_ci(text, "inheritance"))
    {
        *out = CLASS_GENERALIZATION;
        return 1;
    }
    if(eq_ci(text, "realization"))
    {
        *out = CLASS_REALIZATION;
        return 1;
    }
    if(eq_ci(text, "dependency"))
    {
        *out = CLASS_DEPENDENCY;
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

static int parse_bool_value(const char *text, int fallback)
{
    char buffer[32];
    if(!text || text[0] == '\0')
        return fallback;
    if(!parse_string_value(text, buffer, sizeof(buffer)))
        return fallback;
    if(eq_ci(buffer, "true") || eq_ci(buffer, "yes") || eq_ci(buffer, "1"))
        return 1;
    if(eq_ci(buffer, "false") || eq_ci(buffer, "no") || eq_ci(buffer, "0"))
        return 0;
    return fallback;
}

static int parse_int_value(const char *text, int fallback)
{
    char buffer[64];
    char *end = NULL;
    long value;
    if(!text || text[0] == '\0')
        return fallback;
    if(!parse_string_value(text, buffer, sizeof(buffer)))
        return fallback;
    value = strtol(buffer, &end, 10);
    if(end == buffer || (end && *end != '\0'))
        return fallback;
    return (int)value;
}

static int parse_point_array(const char *text, int *xs, int *ys, int max_points,
                             int *out_count)
{
    char buffer[2048];
    char *cursor;
    int count = 0;

    if(!parse_string_value(text, buffer, sizeof(buffer)))
        return 0;
    trim_in_place(buffer);
    if(buffer[0] != '[')
        return 0;
    cursor = buffer + 1;
    while(*cursor != '\0')
    {
        char *start;
        char *comma;
        char *end;
        long x;
        long y;
        while(*cursor != '\0' &&
              (isspace((unsigned char)*cursor) || *cursor == ','))
            cursor++;
        if(*cursor == ']')
            break;
        if(*cursor != '"' || count >= max_points)
            return 0;
        start = ++cursor;
        end = strchr(start, '"');
        if(!end)
            return 0;
        *end = '\0';
        comma = strchr(start, ',');
        if(!comma)
            return 0;
        *comma = '\0';
        x = strtol(start, NULL, 10);
        y = strtol(comma + 1, NULL, 10);
        xs[count] = (int)x;
        ys[count] = (int)y;
        count++;
        cursor = end + 1;
    }
    *out_count = count;
    return 1;
}

static int parse_string_array(const char *text, char out[][160], int max_items,
                              int *out_count)
{
    char buffer[2048];
    char *cursor;
    int count = 0;

    if(!parse_string_value(text, buffer, sizeof(buffer)))
        return 0;
    trim_in_place(buffer);
    if(buffer[0] != '[')
        return 0;
    cursor = buffer + 1;
    while(*cursor != '\0')
    {
        char *start;
        char *end;
        while(*cursor != '\0' && (isspace((unsigned char)*cursor) || *cursor == ','))
            cursor++;
        if(*cursor == ']')
            break;
        if(*cursor != '"' || count >= max_items)
            return 0;
        start = ++cursor;
        end = strchr(start, '"');
        if(!end)
            return 0;
        *end = '\0';
        snprintf(out[count], 160, "%s", start);
        count++;
        cursor = end + 1;
        while(*cursor != '\0' && isspace((unsigned char)*cursor))
            cursor++;
        if(*cursor == ',')
            cursor++;
        else if(*cursor == ']')
            break;
    }
    *out_count = count;
    return 1;
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

static int find_class_index(const class_diagram_t *diagram, const char *id)
{
    int i;
    for(i = 0; i < diagram->class_count; ++i)
    {
        if(strcmp(diagram->classes[i].id, id) == 0)
            return i;
    }
    return -1;
}

static int find_package_element_index(const package_diagram_t *diagram,
                                      const char *id)
{
    int i;
    for(i = 0; i < diagram->element_count; ++i)
    {
        if(strcmp(diagram->elements[i].id, id) == 0)
            return i;
    }
    return -1;
}

typedef struct
{
    color_t action_fill;
    color_t action_stroke;
    color_t action_text;
    int action_bold;
    color_t decision_fill;
    color_t decision_stroke;
    color_t decision_text;
    int decision_bold;
    color_t start_fill;
    color_t start_stroke;
    color_t start_text;
    float start_radius;
    int start_bold;
    color_t end_fill;
    color_t end_stroke;
    color_t end_text;
    float end_radius;
    int end_bold;
    color_t edge_color;
    color_t participant_fill;
    color_t participant_stroke;
    color_t participant_text;
    int participant_bold;
    color_t message_color;
    int message_bold;
    color_t class_fill;
    color_t class_header_fill;
    color_t class_stroke;
    color_t class_text;
    int class_bold;
    color_t association_color;
    int association_bold;
    color_t container_fill;
    color_t container_header_fill;
    color_t container_stroke;
    color_t container_text;
    int container_bold;
    color_t package_fill;
    color_t package_header_fill;
    color_t package_stroke;
    color_t package_text;
    int package_bold;
    color_t model_fill;
    color_t model_header_fill;
    color_t model_stroke;
    color_t model_text;
    int model_bold;
    color_t dependency_color;
    int dependency_bold;
} style_defaults_t;

typedef struct
{
    char id[64];
    node_type_t type;
    int has_type;
    char label[160];
    int has_label;
    color_t fill;
    int has_fill;
    color_t stroke;
    int has_stroke;
    color_t text;
    int has_text;
} activity_node_input_t;

typedef struct
{
    char from_id[64];
    char to_id[64];
    char label[64];
    color_t color;
    int has_color;
} activity_edge_input_t;

typedef struct
{
    char id[64];
    char label[160];
    int has_label;
    color_t fill;
    int has_fill;
    color_t stroke;
    int has_stroke;
    color_t text;
    int has_text;
} participant_input_t;

typedef struct
{
    char from_id[64];
    char to_id[64];
    char label[160];
    color_t color;
    int has_color;
} message_input_t;

typedef struct
{
    char id[64];
    char name[160];
    int has_name;
    int x;
    int has_x;
    int y;
    int has_y;
    char attributes[24][160];
    int attribute_count;
    char methods[24][160];
    int method_count;
    color_t fill;
    int has_fill;
    color_t header_fill;
    int has_header_fill;
    color_t stroke;
    int has_stroke;
    color_t text;
    int has_text;
    int bold;
    int has_bold;
} class_input_t;

typedef struct
{
    char from_id[64];
    char to_id[64];
    class_association_kind_t kind;
    int has_kind;
    char from_multiplicity[32];
    char to_multiplicity[32];
    char label[80];
    color_t color;
    int has_color;
    int bold;
    int has_bold;
} class_association_input_t;

typedef struct
{
    char id[64];
    char parent_id[64];
    package_element_kind_t kind;
    int has_kind;
    char label[160];
    int has_label;
    char stereotype[64];
    int has_stereotype;
    int x;
    int has_x;
    int y;
    int has_y;
    int width;
    int has_width;
    int height;
    int has_height;
    color_t fill;
    int has_fill;
    color_t header_fill;
    int has_header_fill;
    color_t stroke;
    int has_stroke;
    color_t text;
    int has_text;
    int bold;
    int has_bold;
} package_element_input_t;

typedef struct
{
    char from_id[64];
    char to_id[64];
    char label[96];
    color_t color;
    int has_color;
    int bold;
    int has_bold;
    int point_count;
    int points_x[16];
    int points_y[16];
    int corner_radius;
    int has_corner_radius;
    anchor_side_t from_side;
    int has_from_side;
    anchor_side_t to_side;
    int has_to_side;
} package_dependency_input_t;

typedef enum
{
    SECTION_NONE = 0,
    SECTION_SETTINGS = 1,
    SECTION_PROPERTIES = 2,
    SECTION_NODE_ITEM = 3,
    SECTION_EDGE_ITEM = 4,
    SECTION_PARTICIPANT_ITEM = 5,
    SECTION_MESSAGE_ITEM = 6,
    SECTION_CLASS_ITEM = 7,
    SECTION_ASSOCIATION_ITEM = 8,
    SECTION_ELEMENT_ITEM = 9,
    SECTION_DEPENDENCY_ITEM = 10
} section_kind_t;

static void init_style_defaults(style_defaults_t *defaults)
{
    defaults->action_fill = color_rgba(219, 234, 254, 255);
    defaults->action_stroke = color_rgba(37, 99, 235, 255);
    defaults->action_text = color_rgba(15, 23, 42, 255);
    defaults->action_bold = 0;
    defaults->decision_fill = color_rgba(253, 230, 138, 255);
    defaults->decision_stroke = color_rgba(217, 119, 6, 255);
    defaults->decision_text = color_rgba(17, 24, 39, 255);
    defaults->decision_bold = 0;
    defaults->start_fill = color_rgba(34, 197, 94, 255);
    defaults->start_stroke = color_rgba(22, 101, 52, 255);
    defaults->start_text = color_rgba(255, 255, 255, 255);
    defaults->start_radius = 10.0f;
    defaults->start_bold = 0;
    defaults->end_fill = color_rgba(192, 132, 252, 255);
    defaults->end_stroke = color_rgba(124, 58, 237, 255);
    defaults->end_text = color_rgba(255, 255, 255, 255);
    defaults->end_radius = 10.0f;
    defaults->end_bold = 0;
    defaults->edge_color = color_rgba(71, 85, 105, 255);
    defaults->participant_fill = color_rgba(219, 234, 254, 255);
    defaults->participant_stroke = color_rgba(37, 99, 235, 255);
    defaults->participant_text = color_rgba(15, 23, 42, 255);
    defaults->participant_bold = 0;
    defaults->message_color = color_rgba(71, 85, 105, 255);
    defaults->message_bold = 0;
    defaults->class_fill = color_rgba(255, 255, 255, 255);
    defaults->class_header_fill = color_rgba(244, 244, 245, 255);
    defaults->class_stroke = color_rgba(24, 24, 27, 255);
    defaults->class_text = color_rgba(24, 24, 27, 255);
    defaults->class_bold = 1;
    defaults->association_color = color_rgba(82, 82, 91, 255);
    defaults->association_bold = 0;
    defaults->container_fill = color_rgba(255, 255, 255, 255);
    defaults->container_header_fill = color_rgba(255, 255, 255, 255);
    defaults->container_stroke = color_rgba(39, 39, 42, 255);
    defaults->container_text = color_rgba(17, 24, 39, 255);
    defaults->container_bold = 1;
    defaults->package_fill = color_rgba(255, 255, 255, 255);
    defaults->package_header_fill = color_rgba(248, 250, 252, 255);
    defaults->package_stroke = color_rgba(63, 63, 70, 255);
    defaults->package_text = color_rgba(17, 24, 39, 255);
    defaults->package_bold = 1;
    defaults->model_fill = color_rgba(255, 255, 255, 255);
    defaults->model_header_fill = color_rgba(248, 250, 252, 255);
    defaults->model_stroke = color_rgba(63, 63, 70, 255);
    defaults->model_text = color_rgba(17, 24, 39, 255);
    defaults->model_bold = 1;
    defaults->dependency_color = color_rgba(113, 113, 122, 255);
    defaults->dependency_bold = 0;
}

static int looks_like_sectioned_text(const char *text)
{
    const char *cursor = text;
    while(*cursor != '\0')
    {
        const char *line = cursor;
        const char *newline = strchr(cursor, '\n');
        char buf[512];
        size_t len;
        if(newline)
            len = (size_t)(newline - line);
        else
            len = strlen(line);
        if(len >= sizeof(buf))
            len = sizeof(buf) - 1;
        memcpy(buf, line, len);
        buf[len] = '\0';
        trim_in_place(buf);
        if(buf[0] != '\0' && buf[0] != '#')
            return buf[0] == '[' || strchr(buf, '=') != NULL;
        if(!newline)
            break;
        cursor = newline + 1;
    }
    return 0;
}

static void strip_inline_comment(char *line)
{
    int in_string = 0;
    while(*line != '\0')
    {
        if(*line == '"')
            in_string = !in_string;
        else if(*line == '#' && !in_string)
        {
            *line = '\0';
            break;
        }
        line++;
    }
}

static int split_key_value(char *line, char **key, char **value)
{
    char *eq = strchr(line, '=');
    if(!eq)
        return 0;
    *eq = '\0';
    *key = line;
    *value = eq + 1;
    trim_in_place(*key);
    trim_in_place(*value);
    return (*key)[0] != '\0';
}

static int parse_string_value(const char *text, char *out, size_t out_size)
{
    char buffer[512];
    size_t len;
    if(!text)
        return 0;
    len = strlen(text);
    if(len >= sizeof(buffer))
        len = sizeof(buffer) - 1;
    memcpy(buffer, text, len);
    buffer[len] = '\0';
    trim_in_place(buffer);
    if(buffer[0] == '"' && len >= 2 && buffer[strlen(buffer) - 1] == '"')
    {
        size_t inner_len = strlen(buffer) - 2;
        if(inner_len >= out_size)
            inner_len = out_size - 1;
        memcpy(out, buffer + 1, inner_len);
        out[inner_len] = '\0';
        return 1;
    }
    snprintf(out, out_size, "%s", buffer);
    return 1;
}

static int parse_section_header(const char *line, section_kind_t *section)
{
    if(strcmp(line, "[settings]") == 0)
    {
        *section = SECTION_SETTINGS;
        return 1;
    }
    if(strcmp(line, "[properties]") == 0)
    {
        *section = SECTION_PROPERTIES;
        return 1;
    }
    if(strcmp(line, "[[nodes]]") == 0)
    {
        *section = SECTION_NODE_ITEM;
        return 1;
    }
    if(strcmp(line, "[[edges]]") == 0)
    {
        *section = SECTION_EDGE_ITEM;
        return 1;
    }
    if(strcmp(line, "[[participants]]") == 0)
    {
        *section = SECTION_PARTICIPANT_ITEM;
        return 1;
    }
    if(strcmp(line, "[[messages]]") == 0)
    {
        *section = SECTION_MESSAGE_ITEM;
        return 1;
    }
    if(strcmp(line, "[[classes]]") == 0)
    {
        *section = SECTION_CLASS_ITEM;
        return 1;
    }
    if(strcmp(line, "[[associations]]") == 0)
    {
        *section = SECTION_ASSOCIATION_ITEM;
        return 1;
    }
    if(strcmp(line, "[[elements]]") == 0)
    {
        *section = SECTION_ELEMENT_ITEM;
        return 1;
    }
    if(strcmp(line, "[[dependencies]]") == 0)
    {
        *section = SECTION_DEPENDENCY_ITEM;
        return 1;
    }
    return 0;
}

static int apply_property_color(style_defaults_t *defaults, const char *key,
                                const char *value)
{
    if(eq_ci(key, "action_fill"))
        return parse_color(value, defaults->action_fill, &defaults->action_fill);
    if(eq_ci(key, "action_stroke"))
        return parse_color(value, defaults->action_stroke, &defaults->action_stroke);
    if(eq_ci(key, "action_text"))
        return parse_color(value, defaults->action_text, &defaults->action_text);
    if(eq_ci(key, "action_bold"))
    {
        defaults->action_bold = parse_bool_value(value, defaults->action_bold);
        return 1;
    }
    if(eq_ci(key, "decision_fill"))
        return parse_color(value, defaults->decision_fill, &defaults->decision_fill);
    if(eq_ci(key, "decision_stroke"))
        return parse_color(value, defaults->decision_stroke, &defaults->decision_stroke);
    if(eq_ci(key, "decision_text"))
        return parse_color(value, defaults->decision_text, &defaults->decision_text);
    if(eq_ci(key, "decision_bold"))
    {
        defaults->decision_bold =
            parse_bool_value(value, defaults->decision_bold);
        return 1;
    }
    if(eq_ci(key, "start_fill"))
        return parse_color(value, defaults->start_fill, &defaults->start_fill);
    if(eq_ci(key, "start_stroke"))
        return parse_color(value, defaults->start_stroke, &defaults->start_stroke);
    if(eq_ci(key, "start_text"))
        return parse_color(value, defaults->start_text, &defaults->start_text);
    if(eq_ci(key, "start_radius"))
    {
        defaults->start_radius = parse_option_value(value, defaults->start_radius);
        return 1;
    }
    if(eq_ci(key, "start_bold"))
    {
        defaults->start_bold = parse_bool_value(value, defaults->start_bold);
        return 1;
    }
    if(eq_ci(key, "end_fill"))
        return parse_color(value, defaults->end_fill, &defaults->end_fill);
    if(eq_ci(key, "end_stroke"))
        return parse_color(value, defaults->end_stroke, &defaults->end_stroke);
    if(eq_ci(key, "end_text"))
        return parse_color(value, defaults->end_text, &defaults->end_text);
    if(eq_ci(key, "end_radius"))
    {
        defaults->end_radius = parse_option_value(value, defaults->end_radius);
        return 1;
    }
    if(eq_ci(key, "end_bold"))
    {
        defaults->end_bold = parse_bool_value(value, defaults->end_bold);
        return 1;
    }
    if(eq_ci(key, "edge_color"))
        return parse_color(value, defaults->edge_color, &defaults->edge_color);
    if(eq_ci(key, "participant_fill"))
        return parse_color(value, defaults->participant_fill, &defaults->participant_fill);
    if(eq_ci(key, "participant_stroke"))
        return parse_color(value, defaults->participant_stroke, &defaults->participant_stroke);
    if(eq_ci(key, "participant_text"))
        return parse_color(value, defaults->participant_text, &defaults->participant_text);
    if(eq_ci(key, "participant_bold"))
    {
        defaults->participant_bold =
            parse_bool_value(value, defaults->participant_bold);
        return 1;
    }
    if(eq_ci(key, "message_color"))
        return parse_color(value, defaults->message_color, &defaults->message_color);
    if(eq_ci(key, "message_bold"))
    {
        defaults->message_bold = parse_bool_value(value, defaults->message_bold);
        return 1;
    }
    if(eq_ci(key, "class_fill"))
        return parse_color(value, defaults->class_fill, &defaults->class_fill);
    if(eq_ci(key, "class_header_fill"))
        return parse_color(value, defaults->class_header_fill,
                           &defaults->class_header_fill);
    if(eq_ci(key, "class_stroke"))
        return parse_color(value, defaults->class_stroke, &defaults->class_stroke);
    if(eq_ci(key, "class_text"))
        return parse_color(value, defaults->class_text, &defaults->class_text);
    if(eq_ci(key, "class_bold"))
    {
        defaults->class_bold = parse_bool_value(value, defaults->class_bold);
        return 1;
    }
    if(eq_ci(key, "association_color"))
        return parse_color(value, defaults->association_color,
                           &defaults->association_color);
    if(eq_ci(key, "association_bold"))
    {
        defaults->association_bold =
            parse_bool_value(value, defaults->association_bold);
        return 1;
    }
    if(eq_ci(key, "container_fill"))
        return parse_color(value, defaults->container_fill,
                           &defaults->container_fill);
    if(eq_ci(key, "container_header_fill"))
        return parse_color(value, defaults->container_header_fill,
                           &defaults->container_header_fill);
    if(eq_ci(key, "container_stroke"))
        return parse_color(value, defaults->container_stroke,
                           &defaults->container_stroke);
    if(eq_ci(key, "container_text"))
        return parse_color(value, defaults->container_text,
                           &defaults->container_text);
    if(eq_ci(key, "container_bold"))
    {
        defaults->container_bold =
            parse_bool_value(value, defaults->container_bold);
        return 1;
    }
    if(eq_ci(key, "package_fill"))
        return parse_color(value, defaults->package_fill, &defaults->package_fill);
    if(eq_ci(key, "package_header_fill"))
        return parse_color(value, defaults->package_header_fill,
                           &defaults->package_header_fill);
    if(eq_ci(key, "package_stroke"))
        return parse_color(value, defaults->package_stroke,
                           &defaults->package_stroke);
    if(eq_ci(key, "package_text"))
        return parse_color(value, defaults->package_text, &defaults->package_text);
    if(eq_ci(key, "package_bold"))
    {
        defaults->package_bold =
            parse_bool_value(value, defaults->package_bold);
        return 1;
    }
    if(eq_ci(key, "model_fill"))
        return parse_color(value, defaults->model_fill, &defaults->model_fill);
    if(eq_ci(key, "model_header_fill"))
        return parse_color(value, defaults->model_header_fill,
                           &defaults->model_header_fill);
    if(eq_ci(key, "model_stroke"))
        return parse_color(value, defaults->model_stroke, &defaults->model_stroke);
    if(eq_ci(key, "model_text"))
        return parse_color(value, defaults->model_text, &defaults->model_text);
    if(eq_ci(key, "model_bold"))
    {
        defaults->model_bold =
            parse_bool_value(value, defaults->model_bold);
        return 1;
    }
    if(eq_ci(key, "dependency_color"))
        return parse_color(value, defaults->dependency_color,
                           &defaults->dependency_color);
    if(eq_ci(key, "dependency_bold"))
    {
        defaults->dependency_bold =
            parse_bool_value(value, defaults->dependency_bold);
        return 1;
    }
    return 0;
}

static void apply_node_defaults(node_t *node, const activity_node_input_t *input,
                                const style_defaults_t *defaults)
{
    node->type = input->type;
    snprintf(node->id, sizeof(node->id), "%s", input->id);
    snprintf(node->label, sizeof(node->label), "%s", input->label);
    if(node->type == NODE_ACTION)
    {
        node->fill = input->has_fill ? input->fill : defaults->action_fill;
        node->stroke =
            input->has_stroke ? input->stroke : defaults->action_stroke;
        node->text = input->has_text ? input->text : defaults->action_text;
        node->bold = defaults->action_bold;
    }
    else if(node->type == NODE_DECISION)
    {
        node->fill = input->has_fill ? input->fill : defaults->decision_fill;
        node->stroke =
            input->has_stroke ? input->stroke : defaults->decision_stroke;
        node->text = input->has_text ? input->text : defaults->decision_text;
        node->bold = defaults->decision_bold;
    }
    else if(node->type == NODE_START)
    {
        node->fill = input->has_fill ? input->fill : defaults->start_fill;
        node->stroke =
            input->has_stroke ? input->stroke : defaults->start_stroke;
        node->text = input->has_text ? input->text : defaults->start_text;
        node->bold = defaults->start_bold;
    }
    else
    {
        node->fill = input->has_fill ? input->fill : defaults->end_fill;
        node->stroke =
            input->has_stroke ? input->stroke : defaults->end_stroke;
        node->text = input->has_text ? input->text : defaults->end_text;
        node->bold = defaults->end_bold;
    }
}

static int detect_diagram_kind_from_text(const char *text, diagram_kind_t *kind)
{
    char *owned;
    char *cursor;
    section_kind_t section = SECTION_NONE;

    *kind = DIAGRAM_ACTIVITY;
    owned = (char *)malloc(strlen(text) + 1);
    if(!owned)
        return 1;
    strcpy(owned, text);
    cursor = owned;
    while(*cursor != '\0')
    {
        char *line = cursor;
        char *newline = strchr(cursor, '\n');
        if(newline)
        {
            *newline = '\0';
            cursor = newline + 1;
        }
        else
            cursor += strlen(cursor);
        trim_in_place(line);
        strip_inline_comment(line);
        trim_in_place(line);
        if(line[0] == '\0' || line[0] == '#')
            continue;
        if(!looks_like_sectioned_text(text))
        {
            if(strncmp(line, "diagram|sequence", 16) == 0)
                *kind = DIAGRAM_SEQUENCE;
            break;
        }
        if(parse_section_header(line, &section))
            continue;
        if(section == SECTION_SETTINGS)
        {
            char *key;
            char *value;
            char parsed[64];
            if(split_key_value(line, &key, &value) && eq_ci(key, "diagram") &&
               parse_string_value(value, parsed, sizeof(parsed)) &&
               parse_diagram_kind(parsed, kind))
                break;
        }
    }
    free(owned);
    return 1;
}

static int parse_activity_sectioned(diagram_t *diagram, const char *text)
{
    char *owned = NULL;
    char *cursor;
    int line_no = 0;
    section_kind_t section = SECTION_NONE;
    style_defaults_t defaults;
    activity_node_input_t node_inputs[128];
    activity_edge_input_t edge_inputs[256];
    int node_input_count = 0;
    int edge_input_count = 0;
    int current_node = -1;
    int current_edge = -1;
    int i;

    memset(diagram, 0, sizeof(*diagram));
    init_style_defaults(&defaults);
    diagram->scale = 1.0f;
    diagram->kind = DIAGRAM_ACTIVITY;
    diagram->box_radius = 8.0f;
    diagram->edge_radius = 5.0f;
    diagram->start_radius = defaults.start_radius;
    diagram->end_radius = defaults.end_radius;
    diagram->title_size = 18.0f;
    diagram->title_bold = 1;
    memset(node_inputs, 0, sizeof(node_inputs));
    memset(edge_inputs, 0, sizeof(edge_inputs));

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
        char *key;
        char *value;

        line_no++;
        if(newline)
        {
            *newline = '\0';
            cursor = newline + 1;
        }
        else
            cursor += strlen(cursor);

        trim_in_place(line);
        strip_inline_comment(line);
        trim_in_place(line);
        if(line[0] == '\0')
            continue;
        if(parse_section_header(line, &section))
        {
            if(section == SECTION_NODE_ITEM)
            {
                if(node_input_count >= 128)
                {
                    free(owned);
                    set_errorf("too many nodes");
                    return 0;
                }
                current_node = node_input_count++;
                current_edge = -1;
                memset(&node_inputs[current_node], 0, sizeof(node_inputs[0]));
            }
            else if(section == SECTION_EDGE_ITEM)
            {
                if(edge_input_count >= 256)
                {
                    free(owned);
                    set_errorf("too many edges");
                    return 0;
                }
                current_edge = edge_input_count++;
                current_node = -1;
                memset(&edge_inputs[current_edge], 0, sizeof(edge_inputs[0]));
            }
            else
            {
                current_node = -1;
                current_edge = -1;
            }
            continue;
        }
        if(!split_key_value(line, &key, &value))
        {
            free(owned);
            set_errorf("line %d: expected key = value", line_no);
            return 0;
        }

        if(section == SECTION_SETTINGS)
        {
            char parsed[160];
            if(eq_ci(key, "diagram"))
            {
                if(!parse_string_value(value, parsed, sizeof(parsed)) ||
                   !parse_diagram_kind(parsed, &diagram->kind))
                {
                    free(owned);
                    set_errorf("line %d: invalid diagram type", line_no);
                    return 0;
                }
            }
            else if(eq_ci(key, "title"))
            {
                parse_string_value(value, diagram->title, sizeof(diagram->title));
            }
            else if(eq_ci(key, "scale"))
                diagram->scale = parse_scale_value(value, 1.0f);
            else if(eq_ci(key, "title_size"))
                diagram->title_size = parse_option_value(value, 18.0f);
            else if(eq_ci(key, "title_bold"))
                diagram->title_bold = parse_bool_value(value, 1);
            else if(eq_ci(key, "box_radius"))
                diagram->box_radius = parse_option_value(value, 8.0f);
            else if(eq_ci(key, "edge_radius"))
                diagram->edge_radius = parse_option_value(value, 5.0f);
            else
            {
                free(owned);
                set_errorf("line %d: unknown settings key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        if(section == SECTION_PROPERTIES)
        {
            if(!apply_property_color(&defaults, key, value))
            {
                free(owned);
                set_errorf("line %d: unknown properties key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        if(section == SECTION_NODE_ITEM && current_node >= 0)
        {
            activity_node_input_t *node = &node_inputs[current_node];
            char parsed[160];
            if(eq_ci(key, "id"))
                parse_string_value(value, node->id, sizeof(node->id));
            else if(eq_ci(key, "type"))
            {
                if(!parse_string_value(value, parsed, sizeof(parsed)) ||
                   !parse_node_type(parsed, &node->type))
                {
                    free(owned);
                    set_errorf("line %d: invalid node type", line_no);
                    return 0;
                }
                node->has_type = 1;
            }
            else if(eq_ci(key, "label"))
            {
                parse_string_value(value, node->label, sizeof(node->label));
                node->has_label = 1;
            }
            else if(eq_ci(key, "fill"))
                node->has_fill = parse_color(value, defaults.action_fill, &node->fill);
            else if(eq_ci(key, "stroke"))
                node->has_stroke = parse_color(value, defaults.action_stroke, &node->stroke);
            else if(eq_ci(key, "text"))
                node->has_text = parse_color(value, defaults.action_text, &node->text);
            else
            {
                free(owned);
                set_errorf("line %d: unknown node key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        if(section == SECTION_EDGE_ITEM && current_edge >= 0)
        {
            activity_edge_input_t *edge = &edge_inputs[current_edge];
            if(eq_ci(key, "from"))
                parse_string_value(value, edge->from_id, sizeof(edge->from_id));
            else if(eq_ci(key, "to"))
                parse_string_value(value, edge->to_id, sizeof(edge->to_id));
            else if(eq_ci(key, "label"))
                parse_string_value(value, edge->label, sizeof(edge->label));
            else if(eq_ci(key, "color"))
                edge->has_color = parse_color(value, defaults.edge_color, &edge->color);
            else
            {
                free(owned);
                set_errorf("line %d: unknown edge key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        free(owned);
        set_errorf("line %d: key outside a supported section", line_no);
        return 0;
    }

    free(owned);
    if(diagram->kind != DIAGRAM_ACTIVITY)
    {
        set_errorf("sectioned activity parser received non-activity diagram");
        return 0;
    }
    diagram->start_radius = defaults.start_radius;
    diagram->end_radius = defaults.end_radius;
    if(node_input_count == 0)
    {
        set_errorf("diagram contains no nodes");
        return 0;
    }

    diagram->node_count = node_input_count;
    for(i = 0; i < node_input_count; ++i)
    {
        if(node_inputs[i].id[0] == '\0' || !node_inputs[i].has_type)
        {
            set_errorf("node %d is missing required id/type", i + 1);
            return 0;
        }
        if(find_node_index(diagram, node_inputs[i].id) >= 0)
        {
            set_errorf("duplicate node id '%s'", node_inputs[i].id);
            return 0;
        }
        apply_node_defaults(&diagram->nodes[i], &node_inputs[i], &defaults);
    }

    diagram->edge_count = edge_input_count;
    for(i = 0; i < edge_input_count; ++i)
    {
        edge_t *edge = &diagram->edges[i];
        edge->from = find_node_index(diagram, edge_inputs[i].from_id);
        edge->to = find_node_index(diagram, edge_inputs[i].to_id);
        if(edge->from < 0 || edge->to < 0)
        {
            set_errorf("edge %d references unknown node", i + 1);
            return 0;
        }
        snprintf(edge->label, sizeof(edge->label), "%s", edge_inputs[i].label);
        edge->color =
            edge_inputs[i].has_color ? edge_inputs[i].color : defaults.edge_color;
    }

    return 1;
}

static int parse_sequence_sectioned(sequence_diagram_t *diagram, const char *text)
{
    char *owned = NULL;
    char *cursor;
    int line_no = 0;
    section_kind_t section = SECTION_NONE;
    style_defaults_t defaults;
    participant_input_t participant_inputs[24];
    message_input_t message_inputs[256];
    int participant_input_count = 0;
    int message_input_count = 0;
    int current_participant = -1;
    int current_message = -1;
    int i;

    memset(diagram, 0, sizeof(*diagram));
    init_style_defaults(&defaults);
    diagram->scale = 1.0f;
    diagram->box_radius = 8.0f;
    diagram->edge_radius = 0.0f;
    diagram->arrow_size = 12.0f;
    diagram->title_size = 18.0f;
    diagram->title_bold = 1;
    memset(participant_inputs, 0, sizeof(participant_inputs));
    memset(message_inputs, 0, sizeof(message_inputs));

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
        char *key;
        char *value;

        line_no++;
        if(newline)
        {
            *newline = '\0';
            cursor = newline + 1;
        }
        else
            cursor += strlen(cursor);

        trim_in_place(line);
        strip_inline_comment(line);
        trim_in_place(line);
        if(line[0] == '\0')
            continue;
        if(parse_section_header(line, &section))
        {
            if(section == SECTION_PARTICIPANT_ITEM)
            {
                if(participant_input_count >= 24)
                {
                    free(owned);
                    set_errorf("too many participants");
                    return 0;
                }
                current_participant = participant_input_count++;
                current_message = -1;
                memset(&participant_inputs[current_participant], 0,
                       sizeof(participant_inputs[0]));
            }
            else if(section == SECTION_MESSAGE_ITEM)
            {
                if(message_input_count >= 256)
                {
                    free(owned);
                    set_errorf("too many messages");
                    return 0;
                }
                current_message = message_input_count++;
                current_participant = -1;
                memset(&message_inputs[current_message], 0,
                       sizeof(message_inputs[0]));
            }
            else
            {
                current_participant = -1;
                current_message = -1;
            }
            continue;
        }
        if(!split_key_value(line, &key, &value))
        {
            free(owned);
            set_errorf("line %d: expected key = value", line_no);
            return 0;
        }

        if(section == SECTION_SETTINGS)
        {
            char parsed[160];
            if(eq_ci(key, "diagram"))
            {
                if(!parse_string_value(value, parsed, sizeof(parsed)) ||
                   !eq_ci(parsed, "sequence"))
                {
                    free(owned);
                    set_errorf("line %d: sequence input requires diagram = \"sequence\"",
                               line_no);
                    return 0;
                }
            }
            else if(eq_ci(key, "title"))
                parse_string_value(value, diagram->title, sizeof(diagram->title));
            else if(eq_ci(key, "scale"))
                diagram->scale = parse_scale_value(value, 1.0f);
            else if(eq_ci(key, "title_size"))
                diagram->title_size = parse_option_value(value, 18.0f);
            else if(eq_ci(key, "title_bold"))
                diagram->title_bold = parse_bool_value(value, 1);
            else if(eq_ci(key, "box_radius"))
                diagram->box_radius = parse_option_value(value, 8.0f);
            else if(eq_ci(key, "edge_radius"))
                diagram->edge_radius = parse_option_value(value, 0.0f);
            else if(eq_ci(key, "arrow_size"))
                diagram->arrow_size = parse_option_value(value, 12.0f);
            else
            {
                free(owned);
                set_errorf("line %d: unknown settings key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        if(section == SECTION_PROPERTIES)
        {
            if(!apply_property_color(&defaults, key, value))
            {
                free(owned);
                set_errorf("line %d: unknown properties key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        if(section == SECTION_PARTICIPANT_ITEM && current_participant >= 0)
        {
            participant_input_t *p = &participant_inputs[current_participant];
            if(eq_ci(key, "id"))
                parse_string_value(value, p->id, sizeof(p->id));
            else if(eq_ci(key, "label"))
            {
                parse_string_value(value, p->label, sizeof(p->label));
                p->has_label = 1;
            }
            else if(eq_ci(key, "fill"))
                p->has_fill =
                    parse_color(value, defaults.participant_fill, &p->fill);
            else if(eq_ci(key, "stroke"))
                p->has_stroke =
                    parse_color(value, defaults.participant_stroke, &p->stroke);
            else if(eq_ci(key, "text"))
                p->has_text =
                    parse_color(value, defaults.participant_text, &p->text);
            else
            {
                free(owned);
                set_errorf("line %d: unknown participant key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        if(section == SECTION_MESSAGE_ITEM && current_message >= 0)
        {
            message_input_t *m = &message_inputs[current_message];
            if(eq_ci(key, "from"))
                parse_string_value(value, m->from_id, sizeof(m->from_id));
            else if(eq_ci(key, "to"))
                parse_string_value(value, m->to_id, sizeof(m->to_id));
            else if(eq_ci(key, "label"))
                parse_string_value(value, m->label, sizeof(m->label));
            else if(eq_ci(key, "color"))
                m->has_color =
                    parse_color(value, defaults.message_color, &m->color);
            else
            {
                free(owned);
                set_errorf("line %d: unknown message key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        free(owned);
        set_errorf("line %d: key outside a supported section", line_no);
        return 0;
    }

    free(owned);
    if(participant_input_count == 0)
    {
        set_errorf("sequence diagram contains no participants");
        return 0;
    }

    diagram->participant_count = participant_input_count;
    for(i = 0; i < participant_input_count; ++i)
    {
        participant_t *p;
        if(participant_inputs[i].id[0] == '\0')
        {
            set_errorf("participant %d is missing required id", i + 1);
            return 0;
        }
        if(find_participant_index(diagram, participant_inputs[i].id) >= 0)
        {
            set_errorf("duplicate participant id '%s'", participant_inputs[i].id);
            return 0;
        }
        p = &diagram->participants[i];
        snprintf(p->id, sizeof(p->id), "%s", participant_inputs[i].id);
        snprintf(p->label, sizeof(p->label), "%s",
                 participant_inputs[i].has_label ? participant_inputs[i].label
                                                 : participant_inputs[i].id);
        p->fill = participant_inputs[i].has_fill ? participant_inputs[i].fill
                                                 : defaults.participant_fill;
        p->stroke =
            participant_inputs[i].has_stroke ? participant_inputs[i].stroke
                                             : defaults.participant_stroke;
        p->text = participant_inputs[i].has_text ? participant_inputs[i].text
                                                 : defaults.participant_text;
        p->bold = defaults.participant_bold;
    }

    diagram->message_count = message_input_count;
    for(i = 0; i < message_input_count; ++i)
    {
        message_t *m = &diagram->messages[i];
        m->from = find_participant_index(diagram, message_inputs[i].from_id);
        m->to = find_participant_index(diagram, message_inputs[i].to_id);
        if(m->from < 0 || m->to < 0)
        {
            set_errorf("message %d references unknown participant", i + 1);
            return 0;
        }
        snprintf(m->label, sizeof(m->label), "%s", message_inputs[i].label);
        m->color = message_inputs[i].has_color ? message_inputs[i].color
                                               : defaults.message_color;
        m->bold = defaults.message_bold;
    }

    return 1;
}

static int parse_class_sectioned(class_diagram_t *diagram, const char *text)
{
    char *owned = NULL;
    char *cursor;
    int line_no = 0;
    section_kind_t section = SECTION_NONE;
    style_defaults_t defaults;
    class_input_t class_inputs[48];
    class_association_input_t association_inputs[96];
    int class_input_count = 0;
    int association_input_count = 0;
    int current_class = -1;
    int current_association = -1;
    int i;

    memset(diagram, 0, sizeof(*diagram));
    init_style_defaults(&defaults);
    diagram->scale = 1.0f;
    diagram->box_radius = 4.0f;
    diagram->edge_radius = 0.0f;
    diagram->title_size = 18.0f;
    diagram->title_bold = 1;
    memset(class_inputs, 0, sizeof(class_inputs));
    memset(association_inputs, 0, sizeof(association_inputs));

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
        char *key;
        char *value;

        line_no++;
        if(newline)
        {
            *newline = '\0';
            cursor = newline + 1;
        }
        else
            cursor += strlen(cursor);

        trim_in_place(line);
        strip_inline_comment(line);
        trim_in_place(line);
        if(line[0] == '\0')
            continue;
        if(parse_section_header(line, &section))
        {
            if(section == SECTION_CLASS_ITEM)
            {
                if(class_input_count >= 48)
                {
                    free(owned);
                    set_errorf("too many classes");
                    return 0;
                }
                current_class = class_input_count++;
                current_association = -1;
                memset(&class_inputs[current_class], 0, sizeof(class_inputs[0]));
            }
            else if(section == SECTION_ASSOCIATION_ITEM)
            {
                if(association_input_count >= 96)
                {
                    free(owned);
                    set_errorf("too many associations");
                    return 0;
                }
                current_association = association_input_count++;
                current_class = -1;
                memset(&association_inputs[current_association], 0,
                       sizeof(association_inputs[0]));
            }
            else
            {
                current_class = -1;
                current_association = -1;
            }
            continue;
        }
        if(!split_key_value(line, &key, &value))
        {
            free(owned);
            set_errorf("line %d: expected key = value", line_no);
            return 0;
        }

        if(section == SECTION_SETTINGS)
        {
            char parsed[160];
            if(eq_ci(key, "diagram"))
            {
                if(!parse_string_value(value, parsed, sizeof(parsed)) ||
                   !eq_ci(parsed, "class"))
                {
                    free(owned);
                    set_errorf("line %d: class input requires diagram = \"class\"",
                               line_no);
                    return 0;
                }
            }
            else if(eq_ci(key, "title"))
                parse_string_value(value, diagram->title, sizeof(diagram->title));
            else if(eq_ci(key, "title_size"))
                diagram->title_size = parse_option_value(value, 18.0f);
            else if(eq_ci(key, "title_bold"))
                diagram->title_bold = parse_bool_value(value, 1);
            else if(eq_ci(key, "scale"))
                diagram->scale = parse_scale_value(value, 1.0f);
            else if(eq_ci(key, "box_radius"))
                diagram->box_radius = parse_option_value(value, 4.0f);
            else
            {
                free(owned);
                set_errorf("line %d: unknown settings key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        if(section == SECTION_PROPERTIES)
        {
            if(!apply_property_color(&defaults, key, value))
            {
                free(owned);
                set_errorf("line %d: unknown properties key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        if(section == SECTION_CLASS_ITEM && current_class >= 0)
        {
            class_input_t *cls = &class_inputs[current_class];
            char parsed[160];
            if(eq_ci(key, "id"))
                parse_string_value(value, cls->id, sizeof(cls->id));
            else if(eq_ci(key, "name"))
            {
                parse_string_value(value, cls->name, sizeof(cls->name));
                cls->has_name = 1;
            }
            else if(eq_ci(key, "x"))
            {
                cls->x = parse_int_value(value, 0);
                cls->has_x = 1;
            }
            else if(eq_ci(key, "y"))
            {
                cls->y = parse_int_value(value, 0);
                cls->has_y = 1;
            }
            else if(eq_ci(key, "attributes"))
            {
                if(!parse_string_array(value, cls->attributes, 24,
                                       &cls->attribute_count))
                {
                    free(owned);
                    set_errorf("line %d: invalid attributes array", line_no);
                    return 0;
                }
            }
            else if(eq_ci(key, "methods"))
            {
                if(!parse_string_array(value, cls->methods, 24,
                                       &cls->method_count))
                {
                    free(owned);
                    set_errorf("line %d: invalid methods array", line_no);
                    return 0;
                }
            }
            else if(eq_ci(key, "fill"))
                cls->has_fill = parse_color(value, defaults.class_fill, &cls->fill);
            else if(eq_ci(key, "header_fill"))
                cls->has_header_fill =
                    parse_color(value, defaults.class_header_fill,
                                &cls->header_fill);
            else if(eq_ci(key, "stroke"))
                cls->has_stroke =
                    parse_color(value, defaults.class_stroke, &cls->stroke);
            else if(eq_ci(key, "text"))
                cls->has_text = parse_color(value, defaults.class_text, &cls->text);
            else if(eq_ci(key, "bold"))
            {
                cls->bold = parse_bool_value(value, defaults.class_bold);
                cls->has_bold = 1;
            }
            else
            {
                parse_string_value(value, parsed, sizeof(parsed));
                free(owned);
                set_errorf("line %d: unknown class key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        if(section == SECTION_ASSOCIATION_ITEM && current_association >= 0)
        {
            class_association_input_t *assoc =
                &association_inputs[current_association];
            char parsed[80];
            if(eq_ci(key, "from"))
                parse_string_value(value, assoc->from_id, sizeof(assoc->from_id));
            else if(eq_ci(key, "to"))
                parse_string_value(value, assoc->to_id, sizeof(assoc->to_id));
            else if(eq_ci(key, "kind"))
            {
                if(!parse_string_value(value, parsed, sizeof(parsed)) ||
                   !parse_class_association_kind(parsed, &assoc->kind))
                {
                    free(owned);
                    set_errorf("line %d: invalid association kind", line_no);
                    return 0;
                }
                assoc->has_kind = 1;
            }
            else if(eq_ci(key, "from_multiplicity"))
                parse_string_value(value, assoc->from_multiplicity,
                                   sizeof(assoc->from_multiplicity));
            else if(eq_ci(key, "to_multiplicity"))
                parse_string_value(value, assoc->to_multiplicity,
                                   sizeof(assoc->to_multiplicity));
            else if(eq_ci(key, "label"))
                parse_string_value(value, assoc->label, sizeof(assoc->label));
            else if(eq_ci(key, "color"))
                assoc->has_color =
                    parse_color(value, defaults.association_color, &assoc->color);
            else if(eq_ci(key, "bold"))
            {
                assoc->bold = parse_bool_value(value, defaults.association_bold);
                assoc->has_bold = 1;
            }
            else
            {
                free(owned);
                set_errorf("line %d: unknown association key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        free(owned);
        set_errorf("line %d: key outside a supported section", line_no);
        return 0;
    }
    free(owned);

    if(class_input_count == 0)
    {
        set_errorf("class diagram contains no classes");
        return 0;
    }

    diagram->class_count = class_input_count;
    for(i = 0; i < class_input_count; ++i)
    {
        class_box_t *cls = &diagram->classes[i];
        if(class_inputs[i].id[0] == '\0')
        {
            set_errorf("class %d is missing required id", i + 1);
            return 0;
        }
        if(find_class_index(diagram, class_inputs[i].id) >= 0)
        {
            set_errorf("duplicate class id '%s'", class_inputs[i].id);
            return 0;
        }
        snprintf(cls->id, sizeof(cls->id), "%s", class_inputs[i].id);
        snprintf(cls->name, sizeof(cls->name), "%s",
                 class_inputs[i].has_name ? class_inputs[i].name
                                          : class_inputs[i].id);
        cls->x = class_inputs[i].has_x ? class_inputs[i].x : 80 + (i % 3) * 260;
        cls->y = class_inputs[i].has_y ? class_inputs[i].y : 100 + (i / 3) * 220;
        cls->attribute_count = class_inputs[i].attribute_count;
        cls->method_count = class_inputs[i].method_count;
        memcpy(cls->attributes, class_inputs[i].attributes, sizeof(cls->attributes));
        memcpy(cls->methods, class_inputs[i].methods, sizeof(cls->methods));
        cls->fill = class_inputs[i].has_fill ? class_inputs[i].fill
                                             : defaults.class_fill;
        cls->header_fill = class_inputs[i].has_header_fill
                               ? class_inputs[i].header_fill
                               : defaults.class_header_fill;
        cls->stroke = class_inputs[i].has_stroke ? class_inputs[i].stroke
                                                 : defaults.class_stroke;
        cls->text = class_inputs[i].has_text ? class_inputs[i].text
                                             : defaults.class_text;
        cls->bold = class_inputs[i].has_bold ? class_inputs[i].bold
                                             : defaults.class_bold;
    }

    diagram->association_count = association_input_count;
    for(i = 0; i < association_input_count; ++i)
    {
        class_association_t *assoc = &diagram->associations[i];
        assoc->from = find_class_index(diagram, association_inputs[i].from_id);
        assoc->to = find_class_index(diagram, association_inputs[i].to_id);
        if(assoc->from < 0 || assoc->to < 0)
        {
            set_errorf("association %d references unknown class", i + 1);
            return 0;
        }
        assoc->kind = association_inputs[i].has_kind ? association_inputs[i].kind
                                                     : CLASS_ASSOCIATION;
        snprintf(assoc->from_multiplicity, sizeof(assoc->from_multiplicity), "%s",
                 association_inputs[i].from_multiplicity);
        snprintf(assoc->to_multiplicity, sizeof(assoc->to_multiplicity), "%s",
                 association_inputs[i].to_multiplicity);
        snprintf(assoc->label, sizeof(assoc->label), "%s",
                 association_inputs[i].label);
        assoc->color = association_inputs[i].has_color
                           ? association_inputs[i].color
                           : defaults.association_color;
        assoc->bold = association_inputs[i].has_bold
                          ? association_inputs[i].bold
                          : defaults.association_bold;
    }

    return 1;
}

static int parse_package_sectioned(package_diagram_t *diagram, const char *text)
{
    char *owned = NULL;
    char *cursor;
    int line_no = 0;
    section_kind_t section = SECTION_NONE;
    style_defaults_t defaults;
    package_element_input_t element_inputs[64];
    package_dependency_input_t dependency_inputs[128];
    int element_input_count = 0;
    int dependency_input_count = 0;
    int current_element = -1;
    int current_dependency = -1;
    int i;

    memset(diagram, 0, sizeof(*diagram));
    init_style_defaults(&defaults);
    diagram->scale = 1.0f;
    diagram->box_radius = 4.0f;
    diagram->edge_radius = 10.0f;
    diagram->title_size = 18.0f;
    diagram->title_bold = 1;
    memset(element_inputs, 0, sizeof(element_inputs));
    memset(dependency_inputs, 0, sizeof(dependency_inputs));

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
        char *key;
        char *value;

        line_no++;
        if(newline)
        {
            *newline = '\0';
            cursor = newline + 1;
        }
        else
            cursor += strlen(cursor);

        trim_in_place(line);
        strip_inline_comment(line);
        trim_in_place(line);
        if(line[0] == '\0')
            continue;
        if(parse_section_header(line, &section))
        {
            if(section == SECTION_ELEMENT_ITEM)
            {
                if(element_input_count >= 64)
                {
                    free(owned);
                    set_errorf("too many package elements");
                    return 0;
                }
                current_element = element_input_count++;
                current_dependency = -1;
                memset(&element_inputs[current_element], 0,
                       sizeof(element_inputs[0]));
            }
            else if(section == SECTION_DEPENDENCY_ITEM)
            {
                if(dependency_input_count >= 128)
                {
                    free(owned);
                    set_errorf("too many package dependencies");
                    return 0;
                }
                current_dependency = dependency_input_count++;
                current_element = -1;
                memset(&dependency_inputs[current_dependency], 0,
                       sizeof(dependency_inputs[0]));
            }
            else
            {
                current_element = -1;
                current_dependency = -1;
            }
            continue;
        }
        if(!split_key_value(line, &key, &value))
        {
            free(owned);
            set_errorf("line %d: expected key = value", line_no);
            return 0;
        }

        if(section == SECTION_SETTINGS)
        {
            char parsed[160];
            if(eq_ci(key, "diagram"))
            {
                if(!parse_string_value(value, parsed, sizeof(parsed)) ||
                   !eq_ci(parsed, "package"))
                {
                    free(owned);
                    set_errorf("line %d: package input requires diagram = \"package\"",
                               line_no);
                    return 0;
                }
            }
            else if(eq_ci(key, "title"))
                parse_string_value(value, diagram->title, sizeof(diagram->title));
            else if(eq_ci(key, "title_size"))
                diagram->title_size = parse_option_value(value, 18.0f);
            else if(eq_ci(key, "title_bold"))
                diagram->title_bold = parse_bool_value(value, 1);
            else if(eq_ci(key, "scale"))
                diagram->scale = parse_scale_value(value, 1.0f);
            else if(eq_ci(key, "box_radius"))
                diagram->box_radius = parse_option_value(value, 4.0f);
            else if(eq_ci(key, "edge_radius"))
                diagram->edge_radius = parse_option_value(value, 10.0f);
            else
            {
                free(owned);
                set_errorf("line %d: unknown settings key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        if(section == SECTION_PROPERTIES)
        {
            if(!apply_property_color(&defaults, key, value))
            {
                free(owned);
                set_errorf("line %d: unknown properties key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        if(section == SECTION_ELEMENT_ITEM && current_element >= 0)
        {
            package_element_input_t *el = &element_inputs[current_element];
            char parsed[160];
            if(eq_ci(key, "id"))
                parse_string_value(value, el->id, sizeof(el->id));
            else if(eq_ci(key, "parent"))
                parse_string_value(value, el->parent_id, sizeof(el->parent_id));
            else if(eq_ci(key, "kind"))
            {
                if(!parse_string_value(value, parsed, sizeof(parsed)) ||
                   !parse_package_element_kind(parsed, &el->kind))
                {
                    free(owned);
                    set_errorf("line %d: invalid element kind", line_no);
                    return 0;
                }
                el->has_kind = 1;
            }
            else if(eq_ci(key, "label"))
            {
                parse_string_value(value, el->label, sizeof(el->label));
                el->has_label = 1;
            }
            else if(eq_ci(key, "stereotype"))
            {
                parse_string_value(value, el->stereotype, sizeof(el->stereotype));
                el->has_stereotype = 1;
            }
            else if(eq_ci(key, "x"))
            {
                el->x = parse_int_value(value, 0);
                el->has_x = 1;
            }
            else if(eq_ci(key, "y"))
            {
                el->y = parse_int_value(value, 0);
                el->has_y = 1;
            }
            else if(eq_ci(key, "width"))
            {
                el->width = parse_int_value(value, 0);
                el->has_width = 1;
            }
            else if(eq_ci(key, "height"))
            {
                el->height = parse_int_value(value, 0);
                el->has_height = 1;
            }
            else if(eq_ci(key, "fill"))
                el->has_fill = parse_color(value, defaults.package_fill, &el->fill);
            else if(eq_ci(key, "header_fill"))
                el->has_header_fill =
                    parse_color(value, defaults.package_header_fill,
                                &el->header_fill);
            else if(eq_ci(key, "stroke"))
                el->has_stroke = parse_color(value, defaults.package_stroke,
                                             &el->stroke);
            else if(eq_ci(key, "text"))
                el->has_text = parse_color(value, defaults.package_text, &el->text);
            else if(eq_ci(key, "bold"))
            {
                el->bold = parse_bool_value(value, defaults.package_bold);
                el->has_bold = 1;
            }
            else
            {
                free(owned);
                set_errorf("line %d: unknown element key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        if(section == SECTION_DEPENDENCY_ITEM && current_dependency >= 0)
        {
            package_dependency_input_t *dep = &dependency_inputs[current_dependency];
            char parsed[64];
            if(eq_ci(key, "from"))
                parse_string_value(value, dep->from_id, sizeof(dep->from_id));
            else if(eq_ci(key, "to"))
                parse_string_value(value, dep->to_id, sizeof(dep->to_id));
            else if(eq_ci(key, "label"))
                parse_string_value(value, dep->label, sizeof(dep->label));
            else if(eq_ci(key, "from_side"))
            {
                if(!parse_string_value(value, parsed, sizeof(parsed)) ||
                   !parse_anchor_side(parsed, &dep->from_side))
                {
                    free(owned);
                    set_errorf("line %d: invalid from_side", line_no);
                    return 0;
                }
                dep->has_from_side = 1;
            }
            else if(eq_ci(key, "to_side"))
            {
                if(!parse_string_value(value, parsed, sizeof(parsed)) ||
                   !parse_anchor_side(parsed, &dep->to_side))
                {
                    free(owned);
                    set_errorf("line %d: invalid to_side", line_no);
                    return 0;
                }
                dep->has_to_side = 1;
            }
            else if(eq_ci(key, "color"))
                dep->has_color =
                    parse_color(value, defaults.dependency_color, &dep->color);
            else if(eq_ci(key, "bold"))
            {
                dep->bold = parse_bool_value(value, defaults.dependency_bold);
                dep->has_bold = 1;
            }
            else if(eq_ci(key, "waypoints"))
            {
                if(!parse_point_array(value, dep->points_x, dep->points_y, 16,
                                      &dep->point_count))
                {
                    free(owned);
                    set_errorf("line %d: invalid dependency waypoint array",
                               line_no);
                    return 0;
                }
            }
            else if(eq_ci(key, "corner_radius"))
            {
                dep->corner_radius = parse_int_value(value, 0);
                dep->has_corner_radius = 1;
            }
            else
            {
                free(owned);
                set_errorf("line %d: unknown dependency key '%s'", line_no, key);
                return 0;
            }
            continue;
        }

        free(owned);
        set_errorf("line %d: key outside a supported section", line_no);
        return 0;
    }
    free(owned);

    if(element_input_count == 0)
    {
        set_errorf("package diagram contains no elements");
        return 0;
    }

    diagram->element_count = element_input_count;
    for(i = 0; i < element_input_count; ++i)
    {
        package_element_t *el = &diagram->elements[i];
        const package_element_input_t *in = &element_inputs[i];
        color_t fill = defaults.package_fill;
        color_t header_fill = defaults.package_header_fill;
        color_t stroke = defaults.package_stroke;
        color_t text = defaults.package_text;
        int bold = defaults.package_bold;

        if(in->id[0] == '\0')
        {
            set_errorf("package element %d is missing required id", i + 1);
            return 0;
        }
        if(find_package_element_index(diagram, in->id) >= 0)
        {
            set_errorf("duplicate package element id '%s'", in->id);
            return 0;
        }
        if(!in->has_kind)
        {
            set_errorf("package element '%s' is missing required kind", in->id);
            return 0;
        }
        if(in->kind == PACKAGE_CONTAINER)
        {
            fill = defaults.container_fill;
            header_fill = defaults.container_header_fill;
            stroke = defaults.container_stroke;
            text = defaults.container_text;
            bold = defaults.container_bold;
        }
        else if(in->kind == PACKAGE_MODEL)
        {
            fill = defaults.model_fill;
            header_fill = defaults.model_header_fill;
            stroke = defaults.model_stroke;
            text = defaults.model_text;
            bold = defaults.model_bold;
        }

        snprintf(el->id, sizeof(el->id), "%s", in->id);
        el->kind = in->kind;
        el->parent = -1;
        snprintf(el->label, sizeof(el->label), "%s",
                 in->has_label ? in->label : in->id);
        snprintf(el->stereotype, sizeof(el->stereotype), "%s",
                 in->has_stereotype ? in->stereotype : "");
        el->x = in->has_x ? in->x : 40 + (i % 3) * 220;
        el->y = in->has_y ? in->y : 80 + (i / 3) * 180;
        el->width = in->has_width ? in->width : 180;
        el->height = in->has_height ? in->height : 120;
        el->fill = in->has_fill ? in->fill : fill;
        el->header_fill = in->has_header_fill ? in->header_fill : header_fill;
        el->stroke = in->has_stroke ? in->stroke : stroke;
        el->text = in->has_text ? in->text : text;
        el->bold = in->has_bold ? in->bold : bold;
    }

    for(i = 0; i < element_input_count; ++i)
    {
        if(element_inputs[i].parent_id[0] != '\0')
        {
            int parent = find_package_element_index(diagram, element_inputs[i].parent_id);
            if(parent < 0)
            {
                set_errorf("package element '%s' references unknown parent '%s'",
                           diagram->elements[i].id, element_inputs[i].parent_id);
                return 0;
            }
            diagram->elements[i].parent = parent;
        }
    }

    diagram->dependency_count = dependency_input_count;
    for(i = 0; i < dependency_input_count; ++i)
    {
        package_dependency_t *dep = &diagram->dependencies[i];
        const package_dependency_input_t *in = &dependency_inputs[i];
        dep->from = find_package_element_index(diagram, in->from_id);
        dep->to = find_package_element_index(diagram, in->to_id);
        if(dep->from < 0 || dep->to < 0)
        {
            set_errorf("package dependency %d references unknown element", i + 1);
            return 0;
        }
        snprintf(dep->label, sizeof(dep->label), "%s", in->label);
        dep->color = in->has_color ? in->color : defaults.dependency_color;
        dep->bold = in->has_bold ? in->bold : defaults.dependency_bold;
        dep->point_count = in->point_count;
        memcpy(dep->points_x, in->points_x, sizeof(dep->points_x));
        memcpy(dep->points_y, in->points_y, sizeof(dep->points_y));
        dep->corner_radius = in->has_corner_radius
                                 ? in->corner_radius
                                 : (int)diagram->edge_radius;
        dep->from_side = in->has_from_side ? in->from_side : ANCHOR_AUTO;
        dep->to_side = in->has_to_side ? in->to_side : ANCHOR_AUTO;
    }

    return 1;
}

static int parse_diagram_text(diagram_t *diagram, const char *text)
{
    if(looks_like_sectioned_text(text))
        return parse_activity_sectioned(diagram, text);

    char *owned = NULL;
    char *cursor;
    int line_no = 0;

    memset(diagram, 0, sizeof(*diagram));
    diagram->scale = 1.0f;
    diagram->kind = DIAGRAM_ACTIVITY;
    diagram->box_radius = 8.0f;
    diagram->edge_radius = 14.0f;
    diagram->start_radius = 10.0f;
    diagram->end_radius = 10.0f;
    diagram->title_size = 18.0f;
    diagram->title_bold = 1;
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

        if(eq_ci(fields[0], "title_size"))
        {
            if(count < 2)
            {
                free(owned);
                set_errorf("line %d: title_size requires one field", line_no);
                return 0;
            }
            trim_in_place(fields[1]);
            diagram->title_size = parse_option_value(fields[1], 18.0f);
            continue;
        }

        if(eq_ci(fields[0], "title_bold"))
        {
            if(count < 2)
            {
                free(owned);
                set_errorf("line %d: title_bold requires one field", line_no);
                return 0;
            }
            trim_in_place(fields[1]);
            diagram->title_bold = parse_bool_value(fields[1], 1);
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

        if(eq_ci(fields[0], "arrow_size"))
        {
            if(diagram->kind == DIAGRAM_SEQUENCE)
                continue;
            free(owned);
            set_errorf("line %d: arrow_size is only supported for sequence diagrams",
                       line_no);
            return 0;
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
    if(looks_like_sectioned_text(text))
        return parse_sequence_sectioned(diagram, text);

    char *owned = NULL;
    char *cursor;
    int line_no = 0;

    memset(diagram, 0, sizeof(*diagram));
    diagram->scale = 1.0f;
    diagram->box_radius = 0.0f;
    diagram->edge_radius = 0.0f;
    diagram->arrow_size = 8.0f;
    diagram->title_size = 18.0f;
    diagram->title_bold = 1;
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

        if(eq_ci(fields[0], "title_size"))
        {
            if(count < 2)
            {
                free(owned);
                set_errorf("line %d: title_size requires one field", line_no);
                return 0;
            }
            trim_in_place(fields[1]);
            diagram->title_size = parse_option_value(fields[1], 18.0f);
            continue;
        }

        if(eq_ci(fields[0], "title_bold"))
        {
            if(count < 2)
            {
                free(owned);
                set_errorf("line %d: title_bold requires one field", line_no);
                return 0;
            }
            trim_in_place(fields[1]);
            diagram->title_bold = parse_bool_value(fields[1], 1);
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

        if(eq_ci(fields[0], "arrow_size"))
        {
            if(count < 2)
            {
                free(owned);
                set_errorf("line %d: arrow_size requires one field", line_no);
                return 0;
            }
            trim_in_place(fields[1]);
            diagram->arrow_size = parse_option_value(fields[1], 8.0f);
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

static int text_height_for_size(float size)
{
    return (int)ceilf(size > 0.0f ? size : UML_TEXT_SIZE);
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
            int radius = (int)((node->type == NODE_START ? diagram->start_radius
                                                         : diagram->end_radius) *
                               scale);
            if(radius < 4)
                radius = 4;
            node->width = radius * 2;
            node->height = radius * 2;
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
        total_height += text_height_for_size(diagram->title_size) + (int)(16.0f * scale);

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

static void draw_dashed_line(image_t *img, int x0, int y0, int x1, int y1,
                             int thickness, int dash_len, int gap_len,
                             color_t color)
{
    int dx = x1 - x0;
    int dy = y1 - y0;
    float length = sqrtf((float)(dx * dx + dy * dy));
    float ux;
    float uy;
    float pos = 0.0f;

    if(length < 0.001f)
        return;

    ux = (float)dx / length;
    uy = (float)dy / length;
    while(pos < length)
    {
        float next = pos + (float)dash_len;
        int sx;
        int sy;
        int ex;
        int ey;
        if(next > length)
            next = length;
        sx = (int)roundf((float)x0 + ux * pos);
        sy = (int)roundf((float)y0 + uy * pos);
        ex = (int)roundf((float)x0 + ux * next);
        ey = (int)roundf((float)y0 + uy * next);
        draw_line(img, sx, sy, ex, ey, thickness, color);
        pos = next + (float)gap_len;
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
    draw_line(img, tip_x, tip_y, lx, ly, aa_stroke(UML_STROKE), color);
    draw_line(img, tip_x, tip_y, rx, ry, aa_stroke(UML_STROKE), color);
}

static void draw_arrow_head_sized(image_t *img, int tip_x, int tip_y, int dx,
                                  int dy, float size, color_t color)
{
    int len = aa_px((int)size);
    int wing = aa_px(max_i32(2, (int)(size * 0.5f)));
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
    draw_line(img, tip_x, tip_y, lx, ly, aa_stroke(UML_STROKE), color);
    draw_line(img, tip_x, tip_y, rx, ry, aa_stroke(UML_STROKE), color);
}

static int text_width_styled(const char *text, float size)
{
    if(ensure_font_loaded())
    {
        float scale = stbtt_ScaleForPixelHeight(&g_font.info,
                                                size * (float)UML_AA_SCALE);
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
    return (int)ceilf((float)stb_easy_font_width((char *)text) *
                      (size / 11.0f));
}

static void draw_text_styled(image_t *img, int x, int y, const char *text,
                             color_t color, float size, int bold)
{
    if(ensure_font_loaded())
    {
        float scale =
            stbtt_ScaleForPixelHeight(&g_font.info, size * (float)UML_AA_SCALE);
        int pass_count = bold ? 2 : 1;
        int pass;
        for(pass = 0; pass < pass_count; ++pass)
        {
            float pen_x = (float)aa_px(x) + (float)pass;
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
                    unsigned char *bitmap = (unsigned char *)calloc(
                        (size_t)glyph_w * (size_t)glyph_h, 1u);
                    int gy;
                    if(bitmap)
                    {
                        stbtt_MakeCodepointBitmap(&g_font.info, bitmap, glyph_w,
                                                  glyph_h, glyph_w, scale,
                                                  scale, cp);
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
        int pass;
        int pass_count = bold ? 2 : 1;
        float scale = (float)UML_AA_SCALE * (size / 11.0f);

        rgba[0] = (unsigned char)color.r;
        rgba[1] = (unsigned char)color.g;
        rgba[2] = (unsigned char)color.b;
        rgba[3] = (unsigned char)color.a;
        quads = stb_easy_font_print(0.0f, 0.0f, (char *)text, rgba,
                                    buffer, (int)sizeof(buffer));
        for(pass = 0; pass < pass_count; ++pass)
        {
            for(i = 0; i < quads; ++i)
            {
                easy_font_vertex_t *v = (easy_font_vertex_t *)buffer + i * 4;
                float min_xf = (float)aa_px(x) + v[0].x * scale + (float)pass;
                float max_xf = (float)aa_px(x) + v[0].x * scale + (float)pass;
                float min_yf = (float)aa_px(y) + v[0].y * scale;
                float max_yf = (float)aa_px(y) + v[0].y * scale;
                int j;
                int min_x;
                int max_x;
                int min_y;
                int max_y;
                for(j = 1; j < 4; ++j)
                {
                    float sx =
                        (float)aa_px(x) + v[j].x * scale + (float)pass;
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
}

static int text_width(const char *text)
{
    return text_width_styled(text, UML_TEXT_SIZE);
}

static void draw_text(image_t *img, int x, int y, const char *text, color_t color)
{
    draw_text_styled(img, x, y, text, color, UML_TEXT_SIZE, 0);
}

static void draw_title(image_t *img, const char *title, float size, int bold)
{
    color_t title_color = color_rgba(15, 23, 42, 255);
    int x = (img->width / UML_AA_SCALE) / 2 - text_width_styled(title, size) / 2;
    draw_text_styled(img, x, 26, title, title_color, size, bold);
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
        int outer_radius = aa_px(node->width / 2);
        int inner_radius = max_i32(aa_px(3), (int)(outer_radius * 0.58f));
        fill_circle(img, aa_px(node->x), aa_px(node->y), aa_px(node->width / 2),
                    color_rgba(255, 255, 255, 255));
        stroke_circle(img, aa_px(node->x), aa_px(node->y),
                      aa_px(node->width / 2), aa_stroke(UML_STROKE),
                      border);
        fill_circle(img, aa_px(node->x), aa_px(node->y), inner_radius, border);
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
        draw_text_styled(img, label_x, label_y, node->label, text,
                         UML_TEXT_SIZE, node->bold);
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

        if(i == count - 2)
            corner = 0;

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

static void draw_dashed_arc_segment(image_t *img, float cx, float cy, float radius,
                                    float start_angle, float end_angle,
                                    color_t color)
{
    int i;
    int steps =
        max_i32(8, (int)ceilf(fabsf(end_angle - start_angle) * radius / 10.0f));
    float prev_x = cx + cosf(start_angle) * radius;
    float prev_y = cy + sinf(start_angle) * radius;
    for(i = 1; i <= steps; ++i)
    {
        float t0 = (float)(i - 1) / (float)steps;
        float t1 = (float)i / (float)steps;
        float a1 = start_angle + (end_angle - start_angle) * t1;
        float cur_x = cx + cosf(a1) * radius;
        float cur_y = cy + sinf(a1) * radius;
        if(((int)(t0 * 12.0f)) % 2 == 0)
        {
            draw_line(img, (int)roundf(prev_x), (int)roundf(prev_y),
                      (int)roundf(cur_x), (int)roundf(cur_y),
                      aa_stroke(UML_STROKE), color);
        }
        prev_x = cur_x;
        prev_y = cur_y;
    }
}

static void draw_dashed_polyline(image_t *img, int *xs, int *ys, int count,
                                 int radius, color_t color)
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
            draw_dashed_line(img, last_x, last_y, aa_px(cur_x), aa_px(cur_y),
                             aa_stroke(UML_STROKE), aa_px(7), aa_px(5), color);
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

            draw_dashed_line(img, last_x, last_y, aa_px(in_x), aa_px(in_y),
                             aa_stroke(UML_STROKE), aa_px(7), aa_px(5), color);
            draw_dashed_arc_segment(img, center_x, center_y, (float)aa_px(corner),
                                    start_angle, end_angle, color);
            last_x = aa_px(out_x);
            last_y = aa_px(out_y);
        }
    }
    draw_dashed_line(img, last_x, last_y, aa_px(xs[count - 1]), aa_px(ys[count - 1]),
                     aa_stroke(UML_STROKE), aa_px(7), aa_px(5), color);
    draw_arrow_head_sized(img, aa_px(xs[count - 1]), aa_px(ys[count - 1]),
                          aa_px(xs[count - 1] - xs[count - 2]),
                          aa_px(ys[count - 1] - ys[count - 2]), 8.0f, color);
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
        *out_h += text_height_for_size(diagram->title_size) + (int)(10.0f * scale);

    for(i = 0; i < diagram->message_count; ++i)
        diagram->messages[i].y = top + (int)(38.0f * scale) + i * message_gap +
                                 (diagram->title[0] != '\0'
                                      ? text_height_for_size(diagram->title_size) + (int)(10.0f * scale)
                                      : 0);
}

static void draw_vertical_line(image_t *img, int x, int y0, int y1,
                               color_t color)
{
    draw_line(img, aa_px(x), aa_px(y0), aa_px(x), aa_px(y1),
              aa_stroke(UML_STROKE), color);
}

static void draw_participant(image_t *img, const participant_t *p, int top_y,
                             int bottom_y, int box_radius)
{
    int box_h = text_height() + 16;
    int x = p->x - p->width / 2;
    int label_x = p->x - text_width(p->label) / 2;
    draw_soft_box(img, x, top_y, p->width, box_h, box_radius, p->fill,
                  p->stroke);
    draw_text_styled(img, label_x, top_y + (box_h - text_height()) / 2,
                     p->label, p->text, UML_TEXT_SIZE, p->bold);
    draw_vertical_line(img, p->x, top_y + box_h, bottom_y, p->stroke);
}

static void draw_sequence_arrow(image_t *img, int x0, int x1, int y,
                                float arrow_size, color_t color)
{
    draw_line(img, aa_px(x0), aa_px(y), aa_px(x1), aa_px(y),
              aa_stroke(UML_STROKE), color);
    draw_arrow_head_sized(img, aa_px(x1), aa_px(y), aa_px(x1 - x0), 0,
                          arrow_size, color);
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
        draw_arrow_head_sized(img, aa_px(from->x), aa_px(y2),
                              aa_px(from->x - right), 0,
                              diagram->arrow_size, m->color);
        draw_text_styled(img, from->x + 12, m->y - 26, m->label, m->color,
                         UML_TEXT_SIZE, m->bold);
        return;
    }

    draw_sequence_arrow(img, start_x, end_x, m->y, diagram->arrow_size,
                        m->color);
    draw_text_styled(img, label_x, m->y - 26, m->label, m->color,
                     UML_TEXT_SIZE, m->bold);
}

static void compute_class_layout(class_diagram_t *diagram, int *out_w, int *out_h)
{
    int i;
    int max_right = 0;
    int max_bottom = 0;
    int min_left = 1000000;
    int min_top = 1000000;
    int padding_x = 14;
    int title_h = text_height() + 16;
    int row_h = text_height() + 6;

    for(i = 0; i < diagram->class_count; ++i)
    {
        class_box_t *cls = &diagram->classes[i];
        int j;
        int content_w = text_width_styled(cls->name, UML_TEXT_SIZE);
        int attr_h = cls->attribute_count > 0 ? cls->attribute_count * row_h + 10 : 20;
        int method_h = cls->method_count > 0 ? cls->method_count * row_h + 10 : 20;
        for(j = 0; j < cls->attribute_count; ++j)
        {
            int w = text_width(cls->attributes[j]);
            if(w > content_w)
                content_w = w;
        }
        for(j = 0; j < cls->method_count; ++j)
        {
            int w = text_width(cls->methods[j]);
            if(w > content_w)
                content_w = w;
        }
        cls->width = max_i32(180, content_w + padding_x * 2);
        cls->height = title_h + attr_h + method_h;
        if(cls->x < min_left)
            min_left = cls->x;
        if(cls->y < min_top)
            min_top = cls->y;
        if(cls->x + cls->width > max_right)
            max_right = cls->x + cls->width;
        if(cls->y + cls->height > max_bottom)
            max_bottom = cls->y + cls->height;
    }

    if(min_left > 40 || min_top > 60)
    {
        int shift_x = min_left > 40 ? 0 : 40 - min_left;
        int shift_y = min_top > 80 ? 0 : 80 - min_top;
        for(i = 0; i < diagram->class_count; ++i)
        {
            diagram->classes[i].x += shift_x;
            diagram->classes[i].y += shift_y;
        }
        max_right += shift_x;
        max_bottom += shift_y;
        min_left += shift_x;
        min_top += shift_y;
    }

    *out_w = max_right + 60;
    *out_h = max_bottom + 60;
    if(diagram->title[0] != '\0')
        *out_h += text_height_for_size(diagram->title_size) + 12;
}

static void fill_triangle(image_t *img, int x0, int y0, int x1, int y1, int x2,
                          int y2, color_t color)
{
    int min_x = min_i32(x0, min_i32(x1, x2));
    int max_x = max_i32(x0, max_i32(x1, x2));
    int min_y = min_i32(y0, min_i32(y1, y2));
    int max_y = max_i32(y0, max_i32(y1, y2));
    int x;
    int y;
    for(y = min_y; y <= max_y; ++y)
    {
        for(x = min_x; x <= max_x; ++x)
        {
            int w0 = (x1 - x0) * (y - y0) - (y1 - y0) * (x - x0);
            int w1 = (x2 - x1) * (y - y1) - (y2 - y1) * (x - x1);
            int w2 = (x0 - x2) * (y - y2) - (y0 - y2) * (x - x2);
            if((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
                set_pixel(img, x, y, color);
        }
    }
}

static void draw_diamond_marker(image_t *img, int x0, int y0, int x1, int y1,
                                class_association_kind_t kind, color_t color)
{
    float ux;
    float uy;
    float px;
    float py;
    float len = (float)aa_px(10);
    float half = (float)aa_px(6);
    float mag = sqrtf((float)((x1 - x0) * (x1 - x0) + (y1 - y0) * (y1 - y0)));
    int ax;
    int ay;
    int bx;
    int by;
    int cx;
    int cy;
    int dx;
    int dy;

    if(mag < 0.001f)
        return;
    ux = (float)(x1 - x0) / mag;
    uy = (float)(y1 - y0) / mag;
    px = -uy;
    py = ux;

    ax = x0;
    ay = y0;
    bx = (int)roundf((float)x0 + ux * len * 0.5f + px * half);
    by = (int)roundf((float)y0 + uy * len * 0.5f + py * half);
    cx = (int)roundf((float)x0 + ux * len);
    cy = (int)roundf((float)y0 + uy * len);
    dx = (int)roundf((float)x0 + ux * len * 0.5f - px * half);
    dy = (int)roundf((float)y0 + uy * len * 0.5f - py * half);

    if(kind == CLASS_COMPOSITION)
    {
        fill_triangle(img, ax, ay, bx, by, cx, cy, color);
        fill_triangle(img, ax, ay, cx, cy, dx, dy, color);
    }
    else
    {
        fill_triangle(img, ax, ay, bx, by, cx, cy, color_rgba(255, 255, 255, 255));
        fill_triangle(img, ax, ay, cx, cy, dx, dy, color_rgba(255, 255, 255, 255));
    }
    draw_line(img, ax, ay, bx, by, aa_stroke(UML_STROKE), color);
    draw_line(img, bx, by, cx, cy, aa_stroke(UML_STROKE), color);
    draw_line(img, cx, cy, dx, dy, aa_stroke(UML_STROKE), color);
    draw_line(img, dx, dy, ax, ay, aa_stroke(UML_STROKE), color);
}

static void draw_open_triangle_marker(image_t *img, int tip_x, int tip_y, int dx,
                                      int dy, float len_px, float wing_px,
                                      color_t color)
{
    float mag = sqrtf((float)(dx * dx + dy * dy));
    float ux;
    float uy;
    float px;
    float py;
    int bx;
    int by;
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
    bx = (int)roundf((float)tip_x - ux * len_px);
    by = (int)roundf((float)tip_y - uy * len_px);
    lx = (int)roundf((float)bx + px * wing_px);
    ly = (int)roundf((float)by + py * wing_px);
    rx = (int)roundf((float)bx - px * wing_px);
    ry = (int)roundf((float)by - py * wing_px);
    draw_line(img, tip_x, tip_y, lx, ly, aa_stroke(UML_STROKE), color);
    draw_line(img, tip_x, tip_y, rx, ry, aa_stroke(UML_STROKE), color);
    draw_line(img, lx, ly, rx, ry, aa_stroke(UML_STROKE), color);
}

static void class_anchor_points(const class_box_t *from, const class_box_t *to,
                                int *x0, int *y0, int *x1, int *y1)
{
    int from_cx = from->x + from->width / 2;
    int from_cy = from->y + from->height / 2;
    int to_cx = to->x + to->width / 2;
    int to_cy = to->y + to->height / 2;
    int dx = to_cx - from_cx;
    int dy = to_cy - from_cy;

    if(abs_i32(dx) > abs_i32(dy))
    {
        *x0 = dx >= 0 ? from->x + from->width : from->x;
        *y0 = from_cy;
        *x1 = dx >= 0 ? to->x : to->x + to->width;
        *y1 = to_cy;
    }
    else
    {
        *x0 = from_cx;
        *y0 = dy >= 0 ? from->y + from->height : from->y;
        *x1 = to_cx;
        *y1 = dy >= 0 ? to->y : to->y + to->height;
    }
}

static void draw_class_box(image_t *img, const class_diagram_t *diagram,
                           const class_box_t *cls)
{
    int title_h = text_height() + 16;
    int row_h = text_height() + 6;
    int attr_h = cls->attribute_count > 0 ? cls->attribute_count * row_h + 10 : 20;
    int title_y = cls->y + (title_h - text_height()) / 2;
    int title_x = cls->x + cls->width / 2 - text_width(cls->name) / 2;
    int y = cls->y + title_h + 8;
    int i;
    color_t shadow = color_rgba(15, 23, 42, 55);
    int radius = (int)(diagram->box_radius * diagram->scale);

    draw_soft_box(img, cls->x + 4, cls->y + 4, cls->width, cls->height,
                  radius, shadow, shadow);
    draw_soft_box(img, cls->x, cls->y, cls->width, cls->height,
                  radius, cls->fill, cls->stroke);
    fill_rounded_rect(img, aa_px(cls->x), aa_px(cls->y), aa_px(cls->width),
                      aa_px(title_h), aa_px(radius), cls->header_fill);
    fill_rect(img, aa_px(cls->x), aa_px(cls->y + radius), aa_px(cls->x + cls->width),
              aa_px(cls->y + title_h), cls->header_fill);
    draw_line(img, aa_px(cls->x), aa_px(cls->y), aa_px(cls->x + cls->width),
              aa_px(cls->y), aa_stroke(UML_STROKE), cls->stroke);
    draw_line(img, aa_px(cls->x), aa_px(cls->y), aa_px(cls->x),
              aa_px(cls->y + title_h), aa_stroke(UML_STROKE), cls->stroke);
    draw_line(img, aa_px(cls->x + cls->width), aa_px(cls->y),
              aa_px(cls->x + cls->width), aa_px(cls->y + title_h),
              aa_stroke(UML_STROKE), cls->stroke);
    draw_line(img, aa_px(cls->x), aa_px(cls->y + title_h), aa_px(cls->x + cls->width),
              aa_px(cls->y + title_h), aa_stroke(UML_STROKE), cls->stroke);
    draw_line(img, aa_px(cls->x), aa_px(cls->y + title_h + attr_h),
              aa_px(cls->x + cls->width), aa_px(cls->y + title_h + attr_h),
              aa_stroke(UML_STROKE), cls->stroke);
    draw_text_styled(img, title_x, title_y, cls->name, cls->text, UML_TEXT_SIZE,
                     1);
    for(i = 0; i < cls->attribute_count; ++i)
    {
        draw_text_styled(img, cls->x + 10, y, cls->attributes[i], cls->text,
                         UML_TEXT_SIZE, 0);
        y += row_h;
    }
    y = cls->y + title_h + attr_h + 8;
    for(i = 0; i < cls->method_count; ++i)
    {
        draw_text_styled(img, cls->x + 10, y, cls->methods[i], cls->text,
                         UML_TEXT_SIZE, 0);
        y += row_h;
    }
}

static void draw_class_association(image_t *img, const class_diagram_t *diagram,
                                   const class_association_t *assoc)
{
    const class_box_t *from = &diagram->classes[assoc->from];
    const class_box_t *to = &diagram->classes[assoc->to];
    int x0;
    int y0;
    int x1;
    int y1;
    int start_x;
    int start_y;
    int end_x;
    int end_y;
    float ux;
    float uy;
    float px;
    float py;
    float mag;
    int label_x;
    int label_y;

    class_anchor_points(from, to, &x0, &y0, &x1, &y1);
    start_x = aa_px(x0);
    start_y = aa_px(y0);
    end_x = aa_px(x1);
    end_y = aa_px(y1);

    if(assoc->kind == CLASS_AGGREGATION || assoc->kind == CLASS_COMPOSITION)
    {
        mag = sqrtf((float)((end_x - start_x) * (end_x - start_x) +
                            (end_y - start_y) * (end_y - start_y)));
        if(mag > 0.001f)
        {
            ux = (float)(end_x - start_x) / mag;
            uy = (float)(end_y - start_y) / mag;
            start_x = (int)roundf((float)start_x + ux * aa_px(10));
            start_y = (int)roundf((float)start_y + uy * aa_px(10));
            draw_diamond_marker(img, aa_px(x0), aa_px(y0), end_x, end_y,
                                assoc->kind, assoc->color);
        }
    }

    if(assoc->kind == CLASS_GENERALIZATION || assoc->kind == CLASS_REALIZATION)
    {
        int tip_x = end_x;
        int tip_y = end_y;
        int back_x;
        int back_y;
        mag = sqrtf((float)((end_x - start_x) * (end_x - start_x) +
                            (end_y - start_y) * (end_y - start_y)));
        if(mag > 0.001f)
        {
            ux = (float)(end_x - start_x) / mag;
            uy = (float)(end_y - start_y) / mag;
            back_x = (int)roundf((float)tip_x - ux * aa_px(14));
            back_y = (int)roundf((float)tip_y - uy * aa_px(14));
            if(assoc->kind == CLASS_REALIZATION)
                draw_dashed_line(img, start_x, start_y, back_x, back_y,
                                 aa_stroke(UML_STROKE), aa_px(7), aa_px(5),
                                 assoc->color);
            else
                draw_line(img, start_x, start_y, back_x, back_y,
                          aa_stroke(UML_STROKE), assoc->color);
            draw_open_triangle_marker(img, tip_x, tip_y, end_x - start_x,
                                      end_y - start_y, (float)aa_px(14),
                                      (float)aa_px(7), assoc->color);
        }
    }
    else if(assoc->kind == CLASS_DEPENDENCY)
    {
        int tip_x = end_x;
        int tip_y = end_y;
        int back_x;
        int back_y;
        mag = sqrtf((float)((end_x - start_x) * (end_x - start_x) +
                            (end_y - start_y) * (end_y - start_y)));
        if(mag > 0.001f)
        {
            ux = (float)(end_x - start_x) / mag;
            uy = (float)(end_y - start_y) / mag;
            back_x = (int)roundf((float)tip_x - ux * aa_px(9));
            back_y = (int)roundf((float)tip_y - uy * aa_px(9));
            draw_dashed_line(img, start_x, start_y, back_x, back_y,
                             aa_stroke(UML_STROKE), aa_px(7), aa_px(5),
                             assoc->color);
            draw_arrow_head_sized(img, tip_x, tip_y, end_x - start_x,
                                  end_y - start_y, 8.0f, assoc->color);
        }
    }
    else
    {
        draw_line(img, start_x, start_y, end_x, end_y, aa_stroke(UML_STROKE),
                  assoc->color);
    }

    mag = sqrtf((float)((end_x - start_x) * (end_x - start_x) +
                        (end_y - start_y) * (end_y - start_y)));
    if(mag > 0.001f)
    {
        ux = (float)(end_x - start_x) / mag;
        uy = (float)(end_y - start_y) / mag;
        px = -uy;
        py = ux;
        if(assoc->from_multiplicity[0] != '\0')
        {
            draw_text_styled(img,
                             x0 + (int)roundf(px * 10.0f / (float)UML_AA_SCALE) -
                                 text_width(assoc->from_multiplicity) / 2,
                             y0 + (int)roundf(py * 10.0f / (float)UML_AA_SCALE) - 8,
                             assoc->from_multiplicity, assoc->color, UML_TEXT_SIZE,
                             assoc->bold);
        }
        if(assoc->to_multiplicity[0] != '\0')
        {
            draw_text_styled(img,
                             x1 + (int)roundf(px * 10.0f / (float)UML_AA_SCALE) -
                                 text_width(assoc->to_multiplicity) / 2,
                             y1 + (int)roundf(py * 10.0f / (float)UML_AA_SCALE) - 8,
                             assoc->to_multiplicity, assoc->color, UML_TEXT_SIZE,
                             assoc->bold);
        }
    }

    if(assoc->label[0] != '\0')
    {
        label_x = (x0 + x1) / 2 - text_width(assoc->label) / 2;
        label_y = (y0 + y1) / 2 - text_height() - 8;
        draw_text_styled(img, label_x, label_y, assoc->label, assoc->color,
                         UML_TEXT_SIZE, assoc->bold);
    }
}

static void compute_package_layout(package_diagram_t *diagram, int *out_w, int *out_h)
{
    int i;
    int pass;
    int max_right = 0;
    int max_bottom = 0;
    int title_offset = 0;

    if(diagram->title[0] != '\0')
        title_offset = text_height_for_size(diagram->title_size) + 12;

    for(i = 0; i < diagram->element_count; ++i)
    {
        package_element_t *el = &diagram->elements[i];
        int min_w = max_i32(140, text_width(el->label) + 40);
        if(el->stereotype[0] != '\0')
            min_w = max_i32(min_w, text_width(el->stereotype) + text_width(el->label) + 56);
        if(el->width < min_w)
            el->width = min_w;
        if(el->height < 70)
            el->height = 70;
        el->y += title_offset;
    }

    for(pass = 0; pass < 4; ++pass)
    {
        for(i = 0; i < diagram->element_count; ++i)
        {
            package_element_t *el = &diagram->elements[i];
            int j;
            int child_count = 0;
            int min_x = 1000000;
            int min_y = 1000000;
            int max_x = -1000000;
            int max_y = -1000000;
            int pad_x = el->kind == PACKAGE_CONTAINER ? 35 : 45;
            int pad_top = el->kind == PACKAGE_CONTAINER ? 18 : 28;
            int pad_bottom = el->kind == PACKAGE_CONTAINER ? 28 : 30;
            int tab_h = el->kind == PACKAGE_CONTAINER ? 28 : 24;
            int needed_w;
            int needed_h;
            int explicit_w = el->width;
            int explicit_h = el->height;
            float group_cx;
            float group_cy;

            for(j = 0; j < diagram->element_count; ++j)
            {
                const package_element_t *child = &diagram->elements[j];
                if(child->parent != i)
                    continue;
                child_count++;
                if(child->x < min_x)
                    min_x = child->x;
                if(child->y < min_y)
                    min_y = child->y;
                if(child->x + child->width > max_x)
                    max_x = child->x + child->width;
                if(child->y + child->height > max_y)
                    max_y = child->y + child->height;
            }
            if(child_count == 0)
                continue;

            needed_w = (max_x - min_x) + pad_x * 2;
            needed_h = (max_y - min_y) + tab_h + pad_top + pad_bottom;
            if(explicit_w < needed_w)
                explicit_w = needed_w;
            if(explicit_h < needed_h)
                explicit_h = needed_h;

            group_cx = ((float)min_x + (float)max_x) * 0.5f;
            group_cy = ((float)min_y + (float)max_y) * 0.5f;

            el->width = explicit_w;
            el->height = explicit_h;
            el->x = (int)roundf(group_cx - (float)el->width * 0.5f);
            el->y = (int)roundf(group_cy -
                                ((float)(el->height + tab_h + pad_top - pad_bottom) *
                                 0.5f));
        }
    }

    for(i = 0; i < diagram->element_count; ++i)
    {
        package_element_t *el = &diagram->elements[i];
        if(el->x + el->width > max_right)
            max_right = el->x + el->width;
        if(el->y + el->height > max_bottom)
            max_bottom = el->y + el->height;
    }
    for(i = 0; i < diagram->dependency_count; ++i)
    {
        const package_dependency_t *dep = &diagram->dependencies[i];
        int j;
        for(j = 0; j < dep->point_count; ++j)
        {
            if(dep->points_x[j] > max_right)
                max_right = dep->points_x[j];
            if(dep->points_y[j] + title_offset > max_bottom)
                max_bottom = dep->points_y[j] + title_offset;
        }
    }
    *out_w = max_right + 70;
    *out_h = max_bottom + 70;
}

static void package_anchor_for_side(const package_element_t *el,
                                    anchor_side_t side, int *x, int *y)
{
    switch(side)
    {
        case ANCHOR_TOP:
            *x = el->x + el->width / 2;
            *y = el->y;
            return;
        case ANCHOR_RIGHT:
            *x = el->x + el->width;
            *y = el->y + el->height / 2;
            return;
        case ANCHOR_BOTTOM:
            *x = el->x + el->width / 2;
            *y = el->y + el->height;
            return;
        case ANCHOR_LEFT:
            *x = el->x;
            *y = el->y + el->height / 2;
            return;
        default:
            *x = el->x + el->width / 2;
            *y = el->y + el->height / 2;
            return;
    }
}

static void package_anchor_points(const package_element_t *from,
                                  const package_element_t *to,
                                  anchor_side_t from_side,
                                  anchor_side_t to_side, int *x0, int *y0,
                                  int *x1, int *y1)
{
    int from_cx = from->x + from->width / 2;
    int from_cy = from->y + from->height / 2;
    int to_cx = to->x + to->width / 2;
    int to_cy = to->y + to->height / 2;
    int dx = to_cx - from_cx;
    int dy = to_cy - from_cy;

    if(from_side != ANCHOR_AUTO)
        package_anchor_for_side(from, from_side, x0, y0);
    if(to_side != ANCHOR_AUTO)
        package_anchor_for_side(to, to_side, x1, y1);
    if(from_side != ANCHOR_AUTO && to_side != ANCHOR_AUTO)
        return;

    if(from_side == ANCHOR_AUTO && to_side == ANCHOR_AUTO && abs_i32(dx) > abs_i32(dy))
    {
        *x0 = dx >= 0 ? from->x + from->width : from->x;
        *y0 = from_cy;
        *x1 = dx >= 0 ? to->x : to->x + to->width;
        *y1 = to_cy;
    }
    else
    {
        if(from_side == ANCHOR_AUTO)
        {
            *x0 = from_cx;
            *y0 = dy >= 0 ? from->y + from->height : from->y;
        }
        if(to_side == ANCHOR_AUTO)
        {
            *x1 = to_cx;
            *y1 = dy >= 0 ? to->y : to->y + to->height;
        }
    }
}

static void draw_package_frame(image_t *img, int x, int y, int w, int h,
                               int radius, int tab_w, int tab_h, color_t fill,
                               color_t header_fill, color_t stroke)
{
    fill_rect(img, aa_px(x), aa_px(y), aa_px(x + w), aa_px(y + h), fill);
    draw_line(img, aa_px(x), aa_px(y), aa_px(x + tab_w), aa_px(y),
              aa_stroke(UML_STROKE), stroke);
    draw_line(img, aa_px(x + tab_w), aa_px(y), aa_px(x + tab_w), aa_px(y + tab_h),
              aa_stroke(UML_STROKE), stroke);
    draw_line(img, aa_px(x + tab_w), aa_px(y + tab_h), aa_px(x + w),
              aa_px(y + tab_h), aa_stroke(UML_STROKE), stroke);
    draw_line(img, aa_px(x + w), aa_px(y + tab_h), aa_px(x + w), aa_px(y + h),
              aa_stroke(UML_STROKE), stroke);
    draw_line(img, aa_px(x + w), aa_px(y + h), aa_px(x), aa_px(y + h),
              aa_stroke(UML_STROKE), stroke);
    draw_line(img, aa_px(x), aa_px(y + h), aa_px(x), aa_px(y),
              aa_stroke(UML_STROKE), stroke);
    fill_rect(img, aa_px(x), aa_px(y), aa_px(x + tab_w), aa_px(y + tab_h),
              header_fill);
    draw_line(img, aa_px(x), aa_px(y + tab_h), aa_px(x + tab_w), aa_px(y + tab_h),
              aa_stroke(UML_STROKE), stroke);
    (void)radius;
}

static void draw_package_element(image_t *img, const package_diagram_t *diagram,
                                 const package_element_t *el)
{
    int tab_h = el->kind == PACKAGE_CONTAINER ? 28 : 24;
    int tab_w = max_i32(80, text_width(el->label) + 18);
    int label_y = el->y + 7;
    char header[256];

    if(el->stereotype[0] != '\0')
        snprintf(header, sizeof(header), "<<%s>> %s", el->stereotype, el->label);
    else
        snprintf(header, sizeof(header), "%s", el->label);

    tab_w = max_i32(tab_w, text_width(header) + 18);
    draw_package_frame(img, el->x, el->y, el->width, el->height,
                       (int)(diagram->box_radius * diagram->scale), tab_w, tab_h,
                       el->fill, el->header_fill, el->stroke);
    draw_text_styled(img, el->x + 8, label_y, header, el->text, UML_TEXT_SIZE,
                     el->bold);
}

static void draw_package_dependency(image_t *img, const package_diagram_t *diagram,
                                    const package_dependency_t *dep)
{
    const package_element_t *from = &diagram->elements[dep->from];
    const package_element_t *to = &diagram->elements[dep->to];
    int xs[20];
    int ys[20];
    int count = 0;
    int x0;
    int y0;
    int x1;
    int y1;
    int i;

    package_anchor_points(from, to, dep->from_side, dep->to_side, &x0, &y0,
                          &x1, &y1);
    xs[count] = x0;
    ys[count++] = y0;
    for(i = 0; i < dep->point_count && count < 19; ++i)
    {
        xs[count] = dep->points_x[i];
        ys[count] = dep->points_y[i] + (diagram->title[0] != '\0'
                                            ? text_height_for_size(diagram->title_size) + 12
                                            : 0);
        count++;
    }
    xs[count] = x1;
    ys[count++] = y1;

    draw_dashed_polyline(img, xs, ys, count, dep->corner_radius, dep->color);
    if(dep->label[0] != '\0')
    {
        int label_x = (xs[0] + xs[count - 1]) / 2 - text_width(dep->label) / 2;
        int label_y = (ys[0] + ys[count - 1]) / 2 - text_height() - 8;
        draw_edge_label(img, label_x, label_y, dep->label, dep->color);
    }
}

static int render_package_file(const char *input_path, const char *output_path,
                               const char *text)
{
    package_diagram_t diagram;
    image_t image;
    int logical_w;
    int logical_h;
    int i;

    if(!parse_package_sectioned(&diagram, text))
        return 1;
    compute_package_layout(&diagram, &logical_w, &logical_h);

    image.width = logical_w * UML_AA_SCALE;
    image.height = logical_h * UML_AA_SCALE;
    image.pixels = (unsigned char *)calloc((size_t)image.width *
                                               (size_t)image.height * 4u,
                                           1u);
    if(!image.pixels)
    {
        set_errorf("out of memory");
        return 1;
    }

    clear_image(&image, color_rgba(248, 250, 252, 255));
    if(diagram.title[0] != '\0')
        draw_title(&image, diagram.title, diagram.title_size, diagram.title_bold);
    for(i = 0; i < diagram.element_count; ++i)
        draw_package_element(&image, &diagram, &diagram.elements[i]);
    for(i = 0; i < diagram.dependency_count; ++i)
        draw_package_dependency(&image, &diagram, &diagram.dependencies[i]);
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

static int render_class_file(const char *input_path, const char *output_path,
                             const char *text)
{
    class_diagram_t diagram;
    image_t image;
    int logical_w;
    int logical_h;
    int i;
    int title_offset = 0;

    if(!parse_class_sectioned(&diagram, text))
        return 1;
    compute_class_layout(&diagram, &logical_w, &logical_h);
    if(diagram.title[0] != '\0')
        title_offset = text_height_for_size(diagram.title_size) + 12;
    for(i = 0; i < diagram.class_count; ++i)
        diagram.classes[i].y += title_offset;

    image.width = logical_w * UML_AA_SCALE;
    image.height = logical_h * UML_AA_SCALE;
    image.pixels = (unsigned char *)calloc((size_t)image.width * (size_t)image.height * 4u, 1u);
    if(!image.pixels)
    {
        set_errorf("out of memory");
        return 1;
    }

    clear_image(&image, color_rgba(248, 250, 252, 255));
    if(diagram.title[0] != '\0')
        draw_title(&image, diagram.title, diagram.title_size, diagram.title_bold);
    for(i = 0; i < diagram.association_count; ++i)
        draw_class_association(&image, &diagram, &diagram.associations[i]);
    for(i = 0; i < diagram.class_count; ++i)
        draw_class_box(&image, &diagram, &diagram.classes[i]);
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
        draw_title(&image, diagram.title, diagram.title_size, diagram.title_bold);
    top_y = 40 + (diagram.title[0] != '\0'
                      ? text_height_for_size(diagram.title_size) + 10
                      : 0);

    for(i = 0; i < diagram.participant_count; ++i)
        draw_participant(&image, &diagram.participants[i], top_y,
                         logical_h - 36,
                         (int)(diagram.box_radius > 0.0f ? diagram.box_radius
                                                         : 8.0f));
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
    image_t image;
    diagram_kind_t kind;
    diagram_t diagram;
    int i;

    g_last_error[0] = '\0';
    text = read_text_file(input_path);
    if(!text)
        return 1;

    detect_diagram_kind_from_text(text, &kind);
    if(kind == DIAGRAM_SEQUENCE)
    {
        int rc = render_sequence_file(input_path, output_path, text);
        free(text);
        return rc;
    }
    if(kind == DIAGRAM_CLASS)
    {
        int rc = render_class_file(input_path, output_path, text);
        free(text);
        return rc;
    }
    if(kind == DIAGRAM_PACKAGE)
    {
        int rc = render_package_file(input_path, output_path, text);
        free(text);
        return rc;
    }

    if(!parse_diagram_text(&diagram, text))
    {
        free(text);
        return 1;
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
        draw_title(&image, diagram.title, diagram.title_size, diagram.title_bold);

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
