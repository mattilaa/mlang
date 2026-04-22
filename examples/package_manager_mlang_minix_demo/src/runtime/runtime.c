#include <stdint.h>
#include <stddef.h>

void runtime_uart_putc(int32_t ch);
void runtime_write_str(const char* text);
void runtime_write_dec_i64(int64_t value);

static volatile uint32_t* const UART_DR = (volatile uint32_t*)0x09000000u;
static volatile uint32_t* const UART_FR = (volatile uint32_t*)0x09000018u;
static volatile uint32_t* const UART_IFLS = (volatile uint32_t*)0x09000034u;
static volatile uint32_t* const UART_IMSC = (volatile uint32_t*)0x09000038u;
static volatile uint32_t* const UART_MIS = (volatile uint32_t*)0x09000040u;
static volatile uint32_t* const UART_ICR = (volatile uint32_t*)0x09000044u;

static volatile uint32_t* const GICD_CTLR = (volatile uint32_t*)0x08000000u;
static volatile uint32_t* const GICD_ISENABLER1 = (volatile uint32_t*)0x08000104u;
static volatile uint32_t* const GICD_ICPENDR1 = (volatile uint32_t*)0x08000284u;
static volatile uint8_t* const GICD_IPRIORITYR = (volatile uint8_t*)0x08000400u;
static volatile uint8_t* const GICD_ITARGETSR = (volatile uint8_t*)0x08000800u;

static volatile uint32_t* const GICC_CTLR = (volatile uint32_t*)0x08010000u;
static volatile uint32_t* const GICC_PMR = (volatile uint32_t*)0x08010004u;
static volatile uint32_t* const GICC_IAR = (volatile uint32_t*)0x0801000cu;
static volatile uint32_t* const GICC_EOIR = (volatile uint32_t*)0x08010010u;

extern const unsigned char _binary_gnu_rootfs_tar_start[];
extern const unsigned char _binary_gnu_rootfs_tar_end[];

extern const unsigned char _binary_user_hello_elf_start[];
extern const unsigned char _binary_user_hello_elf_end[];
extern const unsigned char _binary_user_initramfs_tar_start[];
extern const unsigned char _binary_user_initramfs_tar_end[];
extern unsigned char __user_stack_top[];

void runtime_enter_user(uint64_t entry, uint64_t user_sp, uint64_t argc, uint64_t argv);
void runtime_exit_to_kernel(void);

enum {
    UART_IRQ_ID = 33u,
    UART_RXIM = 1u << 4,
    UART_RTIM = 1u << 6,
    UART_RX_IRQ_MASK = UART_RXIM | UART_RTIM,
    UART_FR_RXFE = 1u << 4,
    UART_FR_TXFF = 1u << 5,
    UART_FIFO_SIZE = 256u
};

enum {
    ROOTFS_LIST_SEEN_MAX = 4096,
    ROOTFS_NAME_MAX = 128
};

static volatile uint8_t uart_rx_fifo[UART_FIFO_SIZE];
static volatile uint32_t uart_rx_head = 0u;
static volatile uint32_t uart_rx_tail = 0u;

struct TarHeader {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[12];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
    char pad[12];
};

static void uart_putc_raw(uint8_t ch)
{
    while((*UART_FR & UART_FR_TXFF) != 0u) {
    }
    *UART_DR = (uint32_t)ch;
}

static void uart_putc(uint8_t ch)
{
    if(ch == '\n') {
        uart_putc_raw('\r');
    }
    uart_putc_raw(ch);
}

static void uart_rx_push(uint8_t ch)
{
    uint32_t next = (uart_rx_head + 1u) & (UART_FIFO_SIZE - 1u);
    if(next != uart_rx_tail) {
        uart_rx_fifo[uart_rx_head] = ch;
        uart_rx_head = next;
    }
}

static int uart_rx_pop(void)
{
    if(uart_rx_head == uart_rx_tail) {
        return -1;
    }
    uint8_t ch = uart_rx_fifo[uart_rx_tail];
    uart_rx_tail = (uart_rx_tail + 1u) & (UART_FIFO_SIZE - 1u);
    return (int)ch;
}

static void uart_drain_rx(void)
{
    while((*UART_FR & UART_FR_RXFE) == 0u) {
        uart_rx_push((uint8_t)(*UART_DR & 0xffu));
    }
}

static size_t cstr_len(const char* s)
{
    size_t n = 0u;
    if(!s) {
        return 0u;
    }
    while(s[n] != '\0') {
        ++n;
    }
    return n;
}

static int cstr_eq(const char* a, const char* b)
{
    size_t i = 0u;
    if(!a || !b) {
        return 0;
    }
    while(a[i] != '\0' && b[i] != '\0') {
        if(a[i] != b[i]) {
            return 0;
        }
        ++i;
    }
    return a[i] == b[i];
}

static int cstr_starts_with(const char* s, const char* prefix)
{
    size_t i = 0u;
    if(!s || !prefix) {
        return 0;
    }
    while(prefix[i] != '\0') {
        if(s[i] != prefix[i]) {
            return 0;
        }
        ++i;
    }
    return 1;
}

static int cstr_contains(const char* s, const char* needle)
{
    size_t i;
    size_t needle_len = cstr_len(needle);
    size_t len = cstr_len(s);
    if(needle_len == 0u || needle_len > len) {
        return 0;
    }
    for(i = 0u; i + needle_len <= len; ++i) {
        size_t j = 0u;
        while(j < needle_len && s[i + j] == needle[j]) {
            ++j;
        }
        if(j == needle_len) {
            return 1;
        }
    }
    return 0;
}

static int is_printable_text_byte(unsigned char ch)
{
    return ch == '\n' || ch == '\r' || ch == '\t' || (ch >= 32u && ch < 127u);
}

static int looks_binary_file(const unsigned char* data, size_t size)
{
    size_t sample = size < 1024u ? size : 1024u;
    size_t nontext = 0u;
    size_t i;
    for(i = 0u; i < sample; ++i) {
        if(data[i] == 0u) {
            return 1;
        }
        if(!is_printable_text_byte(data[i])) {
            ++nontext;
        }
    }
    return sample > 0u && nontext > (sample / 8u);
}

static size_t tar_octal_to_size(const char* field, size_t width)
{
    size_t value = 0u;
    size_t i = 0u;
    while(i < width && (field[i] == ' ' || field[i] == '\0')) {
        ++i;
    }
    for(; i < width; ++i) {
        char ch = field[i];
        if(ch < '0' || ch > '7') {
            break;
        }
        value = (value << 3u) + (size_t)(ch - '0');
    }
    return value;
}

static int tar_is_zero_block(const unsigned char* block)
{
    size_t i;
    for(i = 0u; i < 512u; ++i) {
        if(block[i] != 0u) {
            return 0;
        }
    }
    return 1;
}

static int tar_entry_type(const struct TarHeader* hdr)
{
    if(hdr->typeflag == '5') {
        return 2;
    }
    if(hdr->typeflag == '2') {
        return 3;
    }
    return 1;
}

static void tar_compose_name(const struct TarHeader* hdr, char* out, size_t out_size)
{
    size_t pos = 0u;
    size_t i = 0u;
    if(out_size == 0u) {
        return;
    }
    if(hdr->prefix[0] != '\0') {
        while(i < sizeof(hdr->prefix) && hdr->prefix[i] != '\0' && pos + 1u < out_size) {
            out[pos++] = hdr->prefix[i++];
        }
        if(pos + 1u < out_size) {
            out[pos++] = '/';
        }
    }
    i = 0u;
    while(i < sizeof(hdr->name) && hdr->name[i] != '\0' && pos + 1u < out_size) {
        out[pos++] = hdr->name[i++];
    }
    out[pos] = '\0';
}

static void normalize_rootfs_path(const char* raw, char* out, size_t out_size)
{
    size_t in = 0u;
    size_t out_pos = 0u;
    if(out_size == 0u) {
        return;
    }

    while(raw[in] == ' ') {
        ++in;
    }
    out[out_pos++] = '/';

    while(raw[in] != '\0') {
        char segment[ROOTFS_NAME_MAX];
        size_t seg_len = 0u;
        size_t i;

        while(raw[in] == '/') {
            ++in;
        }
        if(raw[in] == '\0') {
            break;
        }
        while(raw[in] != '\0' && raw[in] != '/' && seg_len + 1u < sizeof(segment)) {
            segment[seg_len++] = raw[in++];
        }
        segment[seg_len] = '\0';

        if(cstr_eq(segment, ".") || segment[0] == '\0') {
            continue;
        }
        if(cstr_eq(segment, "..")) {
            if(out_pos > 1u) {
                --out_pos;
                while(out_pos > 1u && out[out_pos - 1u] != '/') {
                    --out_pos;
                }
                out[out_pos] = '\0';
            }
            continue;
        }

        if(out_pos > 1u && out_pos + 1u < out_size) {
            out[out_pos++] = '/';
        }
        for(i = 0u; i < seg_len && out_pos + 1u < out_size; ++i) {
            out[out_pos++] = segment[i];
        }
        out[out_pos] = '\0';
    }

    if(out_pos == 1u) {
        out[1] = '\0';
    }
}

static void tar_normalized_name(const struct TarHeader* hdr, char* out, size_t out_size)
{
    char raw[256];
    tar_compose_name(hdr, raw, sizeof(raw));
    normalize_rootfs_path(raw, out, out_size);
}

static const unsigned char* rootfs_start(void)
{
    return _binary_gnu_rootfs_tar_start;
}

static const unsigned char* rootfs_end(void)
{
    return _binary_gnu_rootfs_tar_end;
}

static int rootfs_has_archive(void)
{
    return rootfs_end() > rootfs_start() && !tar_is_zero_block(rootfs_start());
}

static int rootfs_skip_name(const char* name)
{
    return cstr_eq(name, "/") ||
           cstr_eq(name, "/.") ||
           cstr_starts_with(name, "/._") ||
           cstr_contains(name, "/._") ||
           cstr_contains(name, "/PaxHeader");
}

static void rootfs_parent_path(const char* path, char* out, size_t out_size)
{
    size_t len;
    size_t i;
    if(out_size == 0u) {
        return;
    }
    len = cstr_len(path);
    if(len <= 1u) {
        out[0] = '/';
        out[1] = '\0';
        return;
    }
    for(i = 0u; i + 1u < out_size && i < len; ++i) {
        out[i] = path[i];
    }
    out[i < out_size ? i : out_size - 1u] = '\0';
    while(i > 1u && out[i - 1u] != '/') {
        --i;
        out[i] = '\0';
    }
    if(i > 1u) {
        out[i - 1u] = '\0';
    } else {
        out[0] = '/';
        out[1] = '\0';
    }
}

static void rootfs_join_relative(const char* base, const char* relative, char* out, size_t out_size)
{
    char parent[256];
    char combined[256];
    size_t pos = 0u;
    size_t i = 0u;
    if(relative[0] == '/') {
        normalize_rootfs_path(relative, out, out_size);
        return;
    }
    rootfs_parent_path(base, parent, sizeof(parent));
    if(out_size == 0u) {
        return;
    }
    while(parent[pos] != '\0' && pos + 1u < sizeof(combined)) {
        combined[pos] = parent[pos];
        ++pos;
    }
    if(pos == 0u) {
        combined[pos++] = '/';
    }
    if(pos > 1u && pos + 1u < sizeof(combined)) {
        combined[pos++] = '/';
    }
    while(relative[i] != '\0' && pos + 1u < sizeof(combined)) {
        combined[pos++] = relative[i++];
    }
    combined[pos] = '\0';
    normalize_rootfs_path(combined, out, out_size);
}

static const unsigned char* tar_next_entry(const unsigned char* p, const unsigned char* end)
{
    const struct TarHeader* hdr;
    size_t size;
    size_t blocks;
    if(p + 512u > end) {
        return end;
    }
    hdr = (const struct TarHeader*)p;
    size = tar_octal_to_size(hdr->size, sizeof(hdr->size));
    blocks = ((size + 511u) / 512u) + 1u;
    p += blocks * 512u;
    if(p > end) {
        return end;
    }
    return p;
}

static int rootfs_find_entry(const char* path,
                             const struct TarHeader** out_hdr,
                             const unsigned char** out_data,
                             size_t* out_size)
{
    const unsigned char* p = rootfs_start();
    const unsigned char* end = rootfs_end();
    while(p + 512u <= end) {
        const struct TarHeader* hdr = (const struct TarHeader*)p;
        char name[256];
        if(tar_is_zero_block(p)) {
            break;
        }
        tar_normalized_name(hdr, name, sizeof(name));
        if(rootfs_skip_name(name)) {
            p = tar_next_entry(p, end);
            continue;
        }
        if(cstr_eq(name, path)) {
            if(out_hdr) {
                *out_hdr = hdr;
            }
            if(out_data) {
                *out_data = p + 512u;
            }
            if(out_size) {
                *out_size = tar_octal_to_size(hdr->size, sizeof(hdr->size));
            }
            return 1;
        }
        p = tar_next_entry(p, end);
    }
    return 0;
}

static int rootfs_resolve_path(const char* path,
                               char* resolved,
                               size_t resolved_size,
                               const struct TarHeader** out_hdr,
                               const unsigned char** out_data,
                               size_t* out_size)
{
    char normalized[256];
    char current[256];
    size_t in = 1u;
    int depth = 0;

    normalize_rootfs_path(path, normalized, sizeof(normalized));
    current[0] = '/';
    current[1] = '\0';

    while(normalized[in] != '\0') {
        char segment[ROOTFS_NAME_MAX];
        char candidate[256];
        const struct TarHeader* hdr = NULL;
        size_t seg_len = 0u;
        size_t pos = 0u;
        size_t i = 0u;

        while(normalized[in] == '/') {
            ++in;
        }
        if(normalized[in] == '\0') {
            break;
        }
        while(normalized[in] != '\0' && normalized[in] != '/' && seg_len + 1u < sizeof(segment)) {
            segment[seg_len++] = normalized[in++];
        }
        segment[seg_len] = '\0';

        while(current[pos] != '\0' && pos + 1u < sizeof(candidate)) {
            candidate[pos] = current[pos];
            ++pos;
        }
        if(pos > 1u && pos + 1u < sizeof(candidate)) {
            candidate[pos++] = '/';
        }
        for(i = 0u; i < seg_len && pos + 1u < sizeof(candidate); ++i) {
            candidate[pos++] = segment[i];
        }
        candidate[pos] = '\0';

        if(rootfs_find_entry(candidate, &hdr, NULL, NULL) && hdr && tar_entry_type(hdr) == 3) {
            if(depth++ >= 8) {
                break;
            }
            rootfs_join_relative(candidate, hdr->linkname, current, sizeof(current));
            continue;
        }
        normalize_rootfs_path(candidate, current, sizeof(current));
    }

    while(depth < 8) {
        const struct TarHeader* hdr = NULL;
        if(!rootfs_find_entry(current, &hdr, NULL, NULL) || !hdr || tar_entry_type(hdr) != 3) {
            break;
        }
        rootfs_join_relative(current, hdr->linkname, current, sizeof(current));
        ++depth;
    }

    normalize_rootfs_path(current, resolved, resolved_size);
    return rootfs_find_entry(resolved, out_hdr, out_data, out_size);
}

static int rootfs_child_name(const char* target, const char* full, char* child, size_t child_size)
{
    const char* rest = full;
    size_t i = 0u;
    size_t j = 0u;
    if(cstr_eq(target, "/")) {
        if(full[0] != '/') {
            return 0;
        }
        rest = full + 1;
    } else {
        size_t target_len = cstr_len(target);
        if(!cstr_starts_with(full, target) || full[target_len] != '/') {
            return 0;
        }
        rest = full + target_len + 1u;
    }
    if(rest[0] == '\0') {
        return 0;
    }
    while(rest[i] != '\0' && rest[i] != '/' && j + 1u < child_size) {
        child[j++] = rest[i++];
    }
    child[j] = '\0';
    return child[0] != '\0';
}

static int seen_name_contains(char seen[][ROOTFS_NAME_MAX], size_t count, const char* name)
{
    size_t i;
    for(i = 0u; i < count; ++i) {
        if(cstr_eq(seen[i], name)) {
            return 1;
        }
    }
    return 0;
}

static void seen_name_add(char seen[][ROOTFS_NAME_MAX], size_t* count, const char* name)
{
    size_t i = 0u;
    if(*count >= ROOTFS_LIST_SEEN_MAX) {
        return;
    }
    while(name[i] != '\0' && i + 1u < ROOTFS_NAME_MAX) {
        seen[*count][i] = name[i];
        ++i;
    }
    seen[*count][i] = '\0';
    *count += 1u;
}

int32_t runtime_rootfs_try_ls(const char* line, int32_t start)
{
    const unsigned char* p;
    const unsigned char* end;
    const struct TarHeader* hdr = NULL;
    char target[256];
    char name[256];
    char child[ROOTFS_NAME_MAX];
    char seen[ROOTFS_LIST_SEEN_MAX][ROOTFS_NAME_MAX];
    size_t seen_count = 0u;
    int printed = 0;
    int found_exact = 0;

    if(!rootfs_has_archive()) {
        return 0;
    }

    normalize_rootfs_path(line + start, target, sizeof(target));
    if(rootfs_resolve_path(target, target, sizeof(target), &hdr, NULL, NULL)) {
        if(hdr && tar_entry_type(hdr) == 1) {
            runtime_write_str("fs: not a directory: ");
            runtime_write_str(target);
            runtime_write_str("\n");
            return 1;
        }
        found_exact = 1;
    }

    p = rootfs_start();
    end = rootfs_end();
    while(p + 512u <= end) {
        const struct TarHeader* hdr = (const struct TarHeader*)p;
        if(tar_is_zero_block(p)) {
            break;
        }
        tar_normalized_name(hdr, name, sizeof(name));
        if(rootfs_skip_name(name)) {
            p = tar_next_entry(p, end);
            continue;
        }
        if(rootfs_child_name(target, name, child, sizeof(child)) &&
           !seen_name_contains(seen, seen_count, child)) {
            if(printed) {
                runtime_write_str("  ");
            }
            runtime_write_str(child);
            seen_name_add(seen, &seen_count, child);
            printed = 1;
            found_exact = 1;
        }
        p = tar_next_entry(p, end);
    }

    if(printed) {
        runtime_write_str("\n");
        return 1;
    }
    return found_exact ? 1 : 0;
}

int32_t runtime_rootfs_try_cat(const char* line, int32_t start)
{
    const struct TarHeader* hdr = NULL;
    const unsigned char* data = NULL;
    size_t size = 0u;
    char target[256];
    size_t i;

    if(!rootfs_has_archive()) {
        return 0;
    }

    normalize_rootfs_path(line + start, target, sizeof(target));
    if(!rootfs_resolve_path(target, target, sizeof(target), &hdr, &data, &size)) {
        return 0;
    }

    switch(tar_entry_type(hdr)) {
    case 1:
        if(looks_binary_file(data, size)) {
            runtime_write_str("[binary file ");
            runtime_write_str(target);
            runtime_write_str(", ");
            runtime_write_dec_i64((int64_t)size);
            runtime_write_str(" bytes]\n");
            return 1;
        }
        for(i = 0u; i < size; ++i) {
            runtime_uart_putc((int32_t)data[i]);
        }
        if(size == 0u || data[size - 1u] != '\n') {
            runtime_write_str("\n");
        }
        return 1;
    case 2:
        runtime_write_str("fs: not a file: ");
        runtime_write_str(target);
        runtime_write_str("\n");
        return 1;
    default:
        return 0;
    }
}

static void gic_enable_uart_irq(void)
{
    GICD_IPRIORITYR[UART_IRQ_ID] = 0x80u;
    GICD_ITARGETSR[UART_IRQ_ID] = 0x01u;
    *GICD_ICPENDR1 = 1u << (UART_IRQ_ID - 32u);
    *GICD_ISENABLER1 = 1u << (UART_IRQ_ID - 32u);
}

void runtime_init_interrupts(void)
{
    uart_rx_head = 0u;
    uart_rx_tail = 0u;

    *GICD_CTLR = 0u;
    *GICC_CTLR = 0u;

    gic_enable_uart_irq();
    *GICC_PMR = 0xffu;
    *GICC_CTLR = 1u;
    *GICD_CTLR = 1u;

    *UART_ICR = 0x7ffu;
    *UART_IFLS = 0u;
    *UART_IMSC = UART_RX_IRQ_MASK;

    __asm__ __volatile__("dsb sy");
    __asm__ __volatile__("isb");
    __asm__ __volatile__("msr daifclr, #2");
}

int32_t runtime_uart_getc(void)
{
    for(;;) {
        int ch = uart_rx_pop();
        if(ch >= 0) {
            return ch;
        }
        __asm__ __volatile__("wfi");
    }
}

void runtime_uart_putc(int32_t ch)
{
    uart_putc((uint8_t)ch);
}

void runtime_write_str(const char* text)
{
    if(!text) {
        return;
    }
    while(*text != '\0') {
        uart_putc((uint8_t)*text++);
    }
}

void runtime_write_hex64(int64_t value)
{
    uint64_t v = (uint64_t)value;
    for(int shift = 60; shift >= 0; shift -= 4) {
        uint8_t nibble = (uint8_t)((v >> shift) & 0xFu);
        uart_putc((uint8_t)(nibble < 10 ? ('0' + nibble) : ('a' + (nibble - 10))));
    }
}

void runtime_write_dec_i32(int32_t value)
{
    char buf[16];
    int pos = 0;
    uint32_t mag;
    if(value < 0) {
        uart_putc('-');
        mag = (uint32_t)(-(int64_t)value);
    } else {
        mag = (uint32_t)value;
    }
    do {
        buf[pos++] = (char)('0' + (mag % 10u));
        mag /= 10u;
    } while(mag != 0u);
    while(pos > 0) {
        uart_putc((uint8_t)buf[--pos]);
    }
}

void runtime_write_dec_i64(int64_t value)
{
    char buf[24];
    int pos = 0;
    uint64_t mag;
    if(value < 0) {
        uart_putc('-');
        mag = (uint64_t)(-value);
    } else {
        mag = (uint64_t)value;
    }
    do {
        buf[pos++] = (char)('0' + (mag % 10u));
        mag /= 10u;
    } while(mag != 0u);
    while(pos > 0) {
        uart_putc((uint8_t)buf[--pos]);
    }
}

void runtime_handle_irq(void)
{
    uint32_t iar = *GICC_IAR;
    uint32_t irq_id = iar & 0x3ffu;

    if(irq_id == UART_IRQ_ID) {
        uint32_t mis = *UART_MIS;
        if((mis & UART_RX_IRQ_MASK) != 0u) {
            uart_drain_rx();
            *UART_ICR = mis & UART_RX_IRQ_MASK;
        }
    }

    *GICC_EOIR = iar;
}

void runtime_unhandled_exception(void)
{
    uint64_t esr;
    uint64_t elr;
    uint64_t far;

    __asm__ __volatile__("mrs %0, esr_el1" : "=r"(esr));
    __asm__ __volatile__("mrs %0, elr_el1" : "=r"(elr));
    __asm__ __volatile__("mrs %0, far_el1" : "=r"(far));

    runtime_write_str("panic: unhandled exception esr=0x");
    runtime_write_hex64((int64_t)esr);
    runtime_write_str(" elr=0x");
    runtime_write_hex64((int64_t)elr);
    runtime_write_str(" far=0x");
    runtime_write_hex64((int64_t)far);
    runtime_write_str("\n");

    for(;;) {
        __asm__ __volatile__("wfe");
    }
}

enum {
    SYS_ioctl = 29,
    SYS_openat = 56,
    SYS_close = 57,
    SYS_lseek = 62,
    SYS_read = 63,
    SYS_write = 64,
    SYS_writev = 66,
    SYS_readlinkat = 78,
    SYS_newfstatat = 79,
    SYS_fstat = 80,
    SYS_exit = 93,
    SYS_exit_group = 94,
    SYS_set_tid_address = 96,
    SYS_set_robust_list = 99,
    SYS_clock_gettime = 113,
    SYS_rt_sigaction = 134,
    SYS_rt_sigprocmask = 135,
    SYS_uname = 160,
    SYS_getpid = 172,
    SYS_getuid = 174,
    SYS_geteuid = 175,
    SYS_getgid = 176,
    SYS_getegid = 177,
    SYS_brk = 214,
    SYS_munmap = 215,
    SYS_mmap = 222,
    SYS_mprotect = 226,
    SYS_madvise = 233,
    SYS_prlimit64 = 261,
    SYS_getrandom = 278
};

enum {
    EPERM_ERR = -1,
    EBADF_ERR = -9,
    EFAULT_ERR = -14,
    EINVAL_ERR = -22,
    ENOENT_ERR = -2,
    EISDIR_ERR = -21,
    ENOSYS_ERR = -38,
    ENOMEM_ERR = -12
};

#define USER_FD_MAX 32
#define USER_FD_BASE 3
#define USER_HEAP_SIZE (4u * 1024u * 1024u)

struct user_fd_entry {
    int in_use;
    const unsigned char* data;
    uint64_t size;
    uint64_t offset;
    uint8_t is_dir;
};

static struct user_fd_entry user_fds[USER_FD_MAX];

__attribute__((aligned(4096))) static unsigned char user_heap[USER_HEAP_SIZE];
static uint64_t user_brk_start;
static uint64_t user_brk_current;
static uint64_t user_brk_end;

struct iovec_k {
    const void* iov_base;
    uint64_t iov_len;
};

static void user_state_reset(void)
{
    size_t i;
    for(i = 0u; i < USER_FD_MAX; ++i) {
        user_fds[i].in_use = 0;
        user_fds[i].data = NULL;
        user_fds[i].size = 0u;
        user_fds[i].offset = 0u;
        user_fds[i].is_dir = 0u;
    }
    user_brk_start = (uint64_t)(uintptr_t)user_heap;
    user_brk_current = user_brk_start;
    user_brk_end = user_brk_start + USER_HEAP_SIZE;
}

static struct user_fd_entry* user_fd_alloc(int* fd_out)
{
    int i;
    for(i = 0; i < (int)USER_FD_MAX; ++i) {
        if(!user_fds[i].in_use) {
            user_fds[i].in_use = 1;
            if(fd_out) {
                *fd_out = i + USER_FD_BASE;
            }
            return &user_fds[i];
        }
    }
    if(fd_out) {
        *fd_out = -1;
    }
    return NULL;
}

static struct user_fd_entry* user_fd_get(int fd)
{
    int idx = fd - USER_FD_BASE;
    if(idx < 0 || idx >= (int)USER_FD_MAX) {
        return NULL;
    }
    if(!user_fds[idx].in_use) {
        return NULL;
    }
    return &user_fds[idx];
}

static int user_tar_find_file(const char* path,
                              const unsigned char** data_out,
                              uint64_t* size_out,
                              uint8_t* typeflag_out)
{
    const unsigned char* p = _binary_user_initramfs_tar_start;
    const unsigned char* end = _binary_user_initramfs_tar_end;
    char normalized[256];

    normalize_rootfs_path(path, normalized, sizeof(normalized));

    while(p + 512u <= end) {
        const struct TarHeader* hdr = (const struct TarHeader*)p;
        char name[256];
        uint64_t size;
        if(tar_is_zero_block(p)) {
            break;
        }
        tar_normalized_name(hdr, name, sizeof(name));
        size = tar_octal_to_size(hdr->size, sizeof(hdr->size));
        if(cstr_eq(name, normalized)) {
            if(data_out) {
                *data_out = p + 512u;
            }
            if(size_out) {
                *size_out = size;
            }
            if(typeflag_out) {
                *typeflag_out = (uint8_t)hdr->typeflag;
            }
            return 1;
        }
        p += (((size + 511u) / 512u) + 1u) * 512u;
    }
    return 0;
}

static int64_t sys_write(int64_t fd, const void* buf, int64_t count)
{
    const unsigned char* p = (const unsigned char*)buf;
    int64_t i;
    if(!p || count < 0) {
        return EFAULT_ERR;
    }
    if(fd == 1 || fd == 2) {
        for(i = 0; i < count; ++i) {
            uart_putc(p[i]);
        }
        return count;
    }
    return EBADF_ERR;
}

static int64_t sys_writev(int64_t fd, const void* iov_user, int64_t iovcnt)
{
    const struct iovec_k* iov = (const struct iovec_k*)iov_user;
    int64_t total = 0;
    int64_t i;
    int64_t rc;
    if(!iov || iovcnt < 0) {
        return EFAULT_ERR;
    }
    for(i = 0; i < iovcnt; ++i) {
        rc = sys_write(fd, iov[i].iov_base, (int64_t)iov[i].iov_len);
        if(rc < 0) {
            return total > 0 ? total : rc;
        }
        total += rc;
    }
    return total;
}

static int64_t sys_openat(int64_t dirfd, const char* path, int64_t flags)
{
    const unsigned char* data = NULL;
    uint64_t size = 0u;
    uint8_t typeflag = 0u;
    int fd_num;
    struct user_fd_entry* f;

    (void)dirfd;
    (void)flags;
    if(!path) {
        return EFAULT_ERR;
    }
    if(!user_tar_find_file(path, &data, &size, &typeflag)) {
        return ENOENT_ERR;
    }
    if(typeflag == '5') {
        return EISDIR_ERR;
    }
    f = user_fd_alloc(&fd_num);
    if(!f) {
        return ENOMEM_ERR;
    }
    f->data = data;
    f->size = size;
    f->offset = 0u;
    f->is_dir = 0u;
    return fd_num;
}

static int64_t sys_read(int64_t fd, void* buf, int64_t count)
{
    struct user_fd_entry* f;
    unsigned char* out = (unsigned char*)buf;
    uint64_t remain;
    int64_t i;
    if(!buf || count < 0) {
        return EFAULT_ERR;
    }
    if(fd == 0) {
        return 0;
    }
    f = user_fd_get((int)fd);
    if(!f) {
        return EBADF_ERR;
    }
    remain = f->size > f->offset ? f->size - f->offset : 0u;
    if((uint64_t)count > remain) {
        count = (int64_t)remain;
    }
    for(i = 0; i < count; ++i) {
        out[i] = f->data[f->offset + (uint64_t)i];
    }
    f->offset += (uint64_t)count;
    return count;
}

static int64_t sys_close(int64_t fd)
{
    struct user_fd_entry* f = user_fd_get((int)fd);
    if(!f) {
        return EBADF_ERR;
    }
    f->in_use = 0;
    f->data = NULL;
    f->size = 0u;
    f->offset = 0u;
    f->is_dir = 0u;
    return 0;
}

static int64_t sys_lseek(int64_t fd, int64_t offset, int64_t whence)
{
    struct user_fd_entry* f = user_fd_get((int)fd);
    int64_t new_off;
    if(!f) {
        return EBADF_ERR;
    }
    if(whence == 0) {
        new_off = offset;
    } else if(whence == 1) {
        new_off = (int64_t)f->offset + offset;
    } else if(whence == 2) {
        new_off = (int64_t)f->size + offset;
    } else {
        return EINVAL_ERR;
    }
    if(new_off < 0) {
        return EINVAL_ERR;
    }
    f->offset = (uint64_t)new_off;
    return new_off;
}

struct user_stat_buf {
    uint64_t st_dev;
    uint64_t st_ino;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    uint64_t __pad1;
    int64_t  st_size;
    int32_t  st_blksize;
    int32_t  __pad2;
    int64_t  st_blocks;
    int64_t  st_atime;
    uint64_t st_atime_nsec;
    int64_t  st_mtime;
    uint64_t st_mtime_nsec;
    int64_t  st_ctime;
    uint64_t st_ctime_nsec;
    uint32_t __unused[2];
};

static void fill_stat(struct user_stat_buf* st, uint64_t size, int is_dir)
{
    size_t i;
    unsigned char* bytes = (unsigned char*)st;
    for(i = 0u; i < sizeof(*st); ++i) {
        bytes[i] = 0u;
    }
    st->st_mode = is_dir ? (0040000u | 0755u) : (0100000u | 0644u);
    st->st_nlink = 1u;
    st->st_size = (int64_t)size;
    st->st_blksize = 512;
    st->st_blocks = (int64_t)((size + 511u) / 512u);
}

static int64_t sys_fstat(int64_t fd, void* statbuf)
{
    struct user_fd_entry* f;
    if(!statbuf) {
        return EFAULT_ERR;
    }
    f = user_fd_get((int)fd);
    if(!f) {
        return EBADF_ERR;
    }
    fill_stat((struct user_stat_buf*)statbuf, f->size, f->is_dir);
    return 0;
}

static int64_t sys_newfstatat(int64_t dirfd, const char* path, void* statbuf, int64_t flags)
{
    const unsigned char* data = NULL;
    uint64_t size = 0u;
    uint8_t typeflag = 0u;
    (void)dirfd;
    (void)flags;
    if(!path || !statbuf) {
        return EFAULT_ERR;
    }
    if(!user_tar_find_file(path, &data, &size, &typeflag)) {
        return ENOENT_ERR;
    }
    fill_stat((struct user_stat_buf*)statbuf, size, typeflag == '5');
    return 0;
}

static int64_t sys_ioctl(int64_t fd, int64_t cmd, int64_t arg)
{
    (void)fd;
    if(cmd == 0x5413) {
        struct winsize_k { uint16_t ws_row; uint16_t ws_col; uint16_t ws_xp; uint16_t ws_yp; };
        struct winsize_k* ws = (struct winsize_k*)(uintptr_t)arg;
        if(!ws) {
            return EFAULT_ERR;
        }
        ws->ws_row = 24;
        ws->ws_col = 80;
        ws->ws_xp = 0;
        ws->ws_yp = 0;
        return 0;
    }
    return EINVAL_ERR;
}

static int64_t sys_brk(uint64_t addr)
{
    if(addr == 0u) {
        return (int64_t)user_brk_current;
    }
    if(addr < user_brk_start || addr > user_brk_end) {
        return (int64_t)user_brk_current;
    }
    user_brk_current = addr;
    return (int64_t)user_brk_current;
}

static int64_t sys_mmap(uint64_t addr, uint64_t len, int64_t prot, int64_t flags, int64_t fd, int64_t off)
{
    uint64_t aligned_len;
    uint64_t block;
    (void)addr;
    (void)prot;
    (void)flags;
    (void)off;
    if(fd != -1) {
        return ENOSYS_ERR;
    }
    if(len == 0u) {
        return EINVAL_ERR;
    }
    aligned_len = (len + 4095u) & ~((uint64_t)4095u);
    if(user_brk_current + aligned_len > user_brk_end) {
        return ENOMEM_ERR;
    }
    block = user_brk_current;
    user_brk_current += aligned_len;
    return (int64_t)block;
}

static int64_t sys_clock_gettime(int64_t clock_id, void* ts_out)
{
    struct user_timespec { int64_t tv_sec; int64_t tv_nsec; };
    struct user_timespec* ts = (struct user_timespec*)ts_out;
    uint64_t cntfrq;
    uint64_t counter;
    uint64_t seconds;
    uint64_t frac_ns;
    (void)clock_id;
    if(!ts) {
        return EFAULT_ERR;
    }
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r"(cntfrq));
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(counter));
    if(cntfrq == 0u) {
        cntfrq = 62500000u;
    }
    seconds = counter / cntfrq;
    frac_ns = ((counter % cntfrq) * 1000000000u) / cntfrq;
    ts->tv_sec = (int64_t)seconds;
    ts->tv_nsec = (int64_t)frac_ns;
    return 0;
}

static int64_t sys_uname(void* buf)
{
    struct utsname_k {
        char sysname[65];
        char nodename[65];
        char release[65];
        char version[65];
        char machine[65];
        char domainname[65];
    };
    struct utsname_k* u = (struct utsname_k*)buf;
    size_t i;
    unsigned char* bytes;
    const char* fields[6] = {
        "MLNIX",
        "mlnix",
        "0.3.0",
        "#1 M3 SMP PREEMPT",
        "aarch64",
        "localdomain"
    };
    char* slots[6];
    if(!u) {
        return EFAULT_ERR;
    }
    bytes = (unsigned char*)u;
    for(i = 0u; i < sizeof(*u); ++i) {
        bytes[i] = 0u;
    }
    slots[0] = u->sysname;
    slots[1] = u->nodename;
    slots[2] = u->release;
    slots[3] = u->version;
    slots[4] = u->machine;
    slots[5] = u->domainname;
    for(i = 0u; i < 6u; ++i) {
        const char* src = fields[i];
        size_t j = 0u;
        while(src[j] != '\0' && j < 64u) {
            slots[i][j] = src[j];
            ++j;
        }
        slots[i][j] = '\0';
    }
    return 0;
}

static int64_t sys_getrandom(void* buf, uint64_t len, int64_t flags)
{
    unsigned char* out = (unsigned char*)buf;
    uint64_t i;
    uint64_t counter;
    (void)flags;
    if(!out) {
        return EFAULT_ERR;
    }
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(counter));
    for(i = 0u; i < len; ++i) {
        counter = counter * 6364136223846793005ull + 1442695040888963407ull;
        out[i] = (unsigned char)(counter >> 33);
    }
    return (int64_t)len;
}

static void sys_exit_group_impl(int64_t code)
{
    runtime_write_str("[user] exit_group code=");
    runtime_write_dec_i64(code);
    runtime_write_str("\n");
    runtime_exit_to_kernel();
    for(;;) {
        __asm__ __volatile__("wfe");
    }
}

int64_t runtime_syscall_dispatch(int64_t nr,
                                 int64_t a0,
                                 int64_t a1,
                                 int64_t a2,
                                 int64_t a3,
                                 int64_t a4,
                                 int64_t a5)
{
    (void)a4;
    (void)a5;
    switch(nr) {
    case SYS_write:
        return sys_write(a0, (const void*)a1, a2);
    case SYS_writev:
        return sys_writev(a0, (const void*)a1, a2);
    case SYS_read:
        return sys_read(a0, (void*)a1, a2);
    case SYS_openat:
        return sys_openat(a0, (const char*)a1, a2);
    case SYS_close:
        return sys_close(a0);
    case SYS_lseek:
        return sys_lseek(a0, a1, a2);
    case SYS_fstat:
        return sys_fstat(a0, (void*)a1);
    case SYS_newfstatat:
        return sys_newfstatat(a0, (const char*)a1, (void*)a2, a3);
    case SYS_ioctl:
        return sys_ioctl(a0, a1, a2);
    case SYS_brk:
        return sys_brk((uint64_t)a0);
    case SYS_mmap:
        return sys_mmap((uint64_t)a0, (uint64_t)a1, a2, a3, a4, a5);
    case SYS_munmap:
    case SYS_mprotect:
    case SYS_madvise:
        return 0;
    case SYS_clock_gettime:
        return sys_clock_gettime(a0, (void*)a1);
    case SYS_uname:
        return sys_uname((void*)a0);
    case SYS_getrandom:
        return sys_getrandom((void*)a0, (uint64_t)a1, a2);
    case SYS_set_tid_address:
    case SYS_set_robust_list:
    case SYS_rt_sigaction:
    case SYS_rt_sigprocmask:
        return 0;
    case SYS_getpid:
        return 2;
    case SYS_getuid:
    case SYS_geteuid:
    case SYS_getgid:
    case SYS_getegid:
        return 0;
    case SYS_prlimit64:
    case SYS_readlinkat:
        return 0;
    case SYS_exit:
    case SYS_exit_group:
        sys_exit_group_impl(a0);
        return 0;
    default:
        runtime_write_str("[user] unknown syscall nr=");
        runtime_write_dec_i64(nr);
        runtime_write_str("\n");
        return ENOSYS_ERR;
    }
}

void runtime_user_fault(uint64_t esr, uint64_t elr, uint64_t far)
{
    runtime_write_str("[user] fault esr=0x");
    runtime_write_hex64((int64_t)esr);
    runtime_write_str(" elr=0x");
    runtime_write_hex64((int64_t)elr);
    runtime_write_str(" far=0x");
    runtime_write_hex64((int64_t)far);
    runtime_write_str("\n");
    runtime_exit_to_kernel();
    for(;;) {
        __asm__ __volatile__("wfe");
    }
}

struct Elf64_Ehdr {
    unsigned char e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64_Phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
};

enum {
    ELF_PT_LOAD = 1u,
    ELF_EM_AARCH64 = 183u,
    ELF_CLASS64 = 2u
};

static int load_elf64_image(const unsigned char* image,
                            size_t image_size,
                            uint64_t* entry_out)
{
    const struct Elf64_Ehdr* eh;
    const struct Elf64_Phdr* ph;
    uint16_t i;

    if(image_size < sizeof(struct Elf64_Ehdr)) {
        return -1;
    }
    eh = (const struct Elf64_Ehdr*)image;
    if(eh->e_ident[0] != 0x7fu || eh->e_ident[1] != 'E' ||
       eh->e_ident[2] != 'L' || eh->e_ident[3] != 'F') {
        return -2;
    }
    if(eh->e_ident[4] != ELF_CLASS64) {
        return -3;
    }
    if(eh->e_machine != ELF_EM_AARCH64) {
        return -4;
    }
    if(eh->e_phoff == 0u || eh->e_phnum == 0u) {
        return -5;
    }
    if(eh->e_phoff + (uint64_t)eh->e_phnum * eh->e_phentsize > image_size) {
        return -6;
    }

    ph = (const struct Elf64_Phdr*)(image + eh->e_phoff);
    for(i = 0u; i < eh->e_phnum; ++i) {
        const struct Elf64_Phdr* seg = &ph[i];
        unsigned char* dst;
        const unsigned char* src;
        uint64_t j;
        if(seg->p_type != ELF_PT_LOAD) {
            continue;
        }
        if(seg->p_offset + seg->p_filesz > image_size) {
            return -7;
        }
        dst = (unsigned char*)(uintptr_t)seg->p_vaddr;
        src = image + seg->p_offset;
        for(j = 0u; j < seg->p_filesz; ++j) {
            dst[j] = src[j];
        }
        for(j = seg->p_filesz; j < seg->p_memsz; ++j) {
            dst[j] = 0u;
        }
    }

    __asm__ __volatile__("dsb ish");
    __asm__ __volatile__("ic iallu");
    __asm__ __volatile__("dsb ish");
    __asm__ __volatile__("isb");

    *entry_out = eh->e_entry;
    return 0;
}

void runtime_exec_hello_user(void)
{
    const unsigned char* image = _binary_user_hello_elf_start;
    size_t image_size = (size_t)(_binary_user_hello_elf_end - _binary_user_hello_elf_start);
    uint64_t entry = 0u;
    int rc;

    runtime_write_str("[kernel] loading user ELF, size=");
    runtime_write_dec_i64((int64_t)image_size);
    runtime_write_str(" bytes\n");

    rc = load_elf64_image(image, image_size, &entry);
    if(rc != 0) {
        runtime_write_str("[kernel] ELF load failed rc=");
        runtime_write_dec_i64(rc);
        runtime_write_str("\n");
        return;
    }

    runtime_write_str("[kernel] entry=0x");
    runtime_write_hex64((int64_t)entry);
    runtime_write_str(" handing off to EL0...\n");

    user_state_reset();
    runtime_enter_user(entry, (uint64_t)__user_stack_top, 0u, 0u);
    runtime_write_str("[kernel] returned from EL0\n");
}

void __mlang_std_exceptions_pop_frame(int64_t frame)
{
    (void)frame;
}

int64_t __mlang_std_exceptions_push_frame(void)
{
    return 0;
}

void* __mlang_std_exceptions_frame_env(int64_t frame)
{
    static int64_t env[32];
    (void)frame;
    return env;
}

int _setjmp(void* env)
{
    (void)env;
    return 0;
}

void __mlang_std_exceptions_rethrow_current(void)
{
    runtime_write_str("panic: unexpected exception path in freestanding kernel\n");
    for(;;) {
        __asm__ __volatile__("wfe");
    }
}
