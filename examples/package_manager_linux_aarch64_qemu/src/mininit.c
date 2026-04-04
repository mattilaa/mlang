typedef unsigned long ulong;
typedef long slong;

#define SYS_READ 63
#define SYS_WRITE 64
#define SYS_REBOOT 142

#define LINUX_REBOOT_MAGIC1 0xfee1dead
#define LINUX_REBOOT_MAGIC2 672274793
#define LINUX_REBOOT_CMD_RESTART 0x1234567
#define LINUX_REBOOT_CMD_POWER_OFF 0x4321fedc

static slong sys_call3(slong number, slong a0, slong a1, slong a2)
{
    register slong x0 asm("x0") = a0;
    register slong x1 asm("x1") = a1;
    register slong x2 asm("x2") = a2;
    register slong x8 asm("x8") = number;
    asm volatile("svc #0" : "+r"(x0) : "r"(x1), "r"(x2), "r"(x8) : "memory");
    return x0;
}

static slong sys_call4(slong number, slong a0, slong a1, slong a2, slong a3)
{
    register slong x0 asm("x0") = a0;
    register slong x1 asm("x1") = a1;
    register slong x2 asm("x2") = a2;
    register slong x3 asm("x3") = a3;
    register slong x8 asm("x8") = number;
    asm volatile("svc #0"
                 : "+r"(x0)
                 : "r"(x1), "r"(x2), "r"(x3), "r"(x8)
                 : "memory");
    return x0;
}

static slong sys_read(slong fd, void* buf, ulong len)
{
    return sys_call3(SYS_READ, fd, (slong)buf, (slong)len);
}

static slong sys_write(slong fd, const void* buf, ulong len)
{
    return sys_call3(SYS_WRITE, fd, (slong)buf, (slong)len);
}

static slong sys_reboot(slong cmd)
{
    return sys_call4(SYS_REBOOT, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,
                     cmd, 0);
}

static ulong string_length(const char* text)
{
    ulong len = 0;
    while(text[len] != '\0')
        ++len;
    return len;
}

static void write_fd(slong fd, const char* text)
{
    (void)sys_write(fd, text, string_length(text));
}

static void write_text(const char* text)
{
    write_fd(1, text);
    write_fd(2, text);
}

static int string_equals(const char* a, const char* b)
{
    while(*a != '\0' && *b != '\0')
    {
        if(*a != *b)
            return 0;
        ++a;
        ++b;
    }
    return *a == '\0' && *b == '\0';
}

static int string_starts_with(const char* text, const char* prefix)
{
    while(*prefix != '\0')
    {
        if(*text != *prefix)
            return 0;
        ++text;
        ++prefix;
    }
    return 1;
}

static void trim_line(char* line)
{
    ulong i = 0;
    while(line[i] != '\0')
    {
        if(line[i] == '\r' || line[i] == '\n')
        {
            line[i] = '\0';
            return;
        }
        ++i;
    }
}

static void print_help(void)
{
    write_text(
        "Available commands:\n"
        "  help      Show this help\n"
        "  hello     Print the example banner\n"
        "  uname     Print a fixed kernel/userland description\n"
        "  clear     Clear the terminal\n"
        "  reboot    Reboot the guest\n"
        "  poweroff  Power off the guest\n");
}

static void print_banner(void)
{
    write_text(
        "\n"
        "mlang pkg Linux AArch64 QEMU example booted\n"
        "Tiny userspace console is running as /init\n"
        "Type 'help' for built-in commands.\n");
}

static void run_command(const char* line)
{
    if(line[0] == '\0')
        return;
    if(string_equals(line, "help"))
    {
        print_help();
        return;
    }
    if(string_equals(line, "hello"))
    {
        print_banner();
        return;
    }
    if(string_equals(line, "uname"))
    {
        write_text("Linux mlang-example 6.x aarch64 minimal-userspace\n");
        return;
    }
    if(string_equals(line, "clear"))
    {
        write_text("\033[2J\033[H");
        return;
    }
    if(string_equals(line, "reboot"))
    {
        write_text("Rebooting guest...\n");
        (void)sys_reboot(LINUX_REBOOT_CMD_RESTART);
        write_text("Reboot syscall failed; halting.\n");
        for(;;)
            asm volatile("wfe");
    }
    if(string_equals(line, "poweroff"))
    {
        write_text("Powering off guest...\n");
        (void)sys_reboot(LINUX_REBOOT_CMD_POWER_OFF);
        write_text("Poweroff syscall failed; halting.\n");
        for(;;)
            asm volatile("wfe");
    }
    if(string_starts_with(line, "echo "))
    {
        write_text(line + 5);
        write_text("\n");
        return;
    }
    write_text("Unknown command. Type 'help'.\n");
}

__attribute__((noreturn)) void _start(void)
{
    char line[128];
    ulong cursor = 0;

    print_banner();
    write_text("\nconsole> ");

    for(;;)
    {
        char ch = '\0';
        const slong got = sys_read(0, &ch, 1);
        if(got <= 0)
            continue;

        if(ch == '\r' || ch == '\n')
        {
            write_text("\n");
            line[cursor] = '\0';
            trim_line(line);
            run_command(line);
            cursor = 0;
            line[0] = '\0';
            write_text("\nconsole> ");
            continue;
        }

        if((ch == 0x7f || ch == '\b') && cursor > 0)
        {
            --cursor;
            line[cursor] = '\0';
            write_text("\b \b");
            continue;
        }

        if(cursor + 1 >= sizeof(line))
            continue;

        if(ch >= 32 && ch <= 126)
        {
            line[cursor++] = ch;
            line[cursor] = '\0';
            (void)sys_write(1, &ch, 1);
        }
    }
}
