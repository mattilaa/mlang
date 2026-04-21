#include <stdint.h>

static volatile uint32_t* const UART_DR = (volatile uint32_t*)0x09000000u;
static volatile uint32_t* const UART_FR = (volatile uint32_t*)0x09000018u;

static void uart_putc_raw(uint8_t ch)
{
    while((*UART_FR & (1u << 5)) != 0u) {
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
