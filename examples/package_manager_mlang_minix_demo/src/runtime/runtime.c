#include <stdint.h>

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

enum {
    UART_IRQ_ID = 33u,
    UART_RXIM = 1u << 4,
    UART_RTIM = 1u << 6,
    UART_RX_IRQ_MASK = UART_RXIM | UART_RTIM,
    UART_FR_RXFE = 1u << 4,
    UART_FR_TXFF = 1u << 5,
    UART_FIFO_SIZE = 256u
};

static volatile uint8_t uart_rx_fifo[UART_FIFO_SIZE];
static volatile uint32_t uart_rx_head = 0u;
static volatile uint32_t uart_rx_tail = 0u;

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
