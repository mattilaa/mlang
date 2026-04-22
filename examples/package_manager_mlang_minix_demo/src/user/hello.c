static long sys3(long nr, long a, long b, long c)
{
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x8 __asm__("x8") = nr;
    __asm__ volatile("svc #0"
                     : "+r"(x0)
                     : "r"(x1), "r"(x2), "r"(x8)
                     : "memory");
    return x0;
}

static long sys_write(long fd, const void *buf, long len)
{
    return sys3(64, fd, (long)buf, len);
}

__attribute__((noreturn)) static void sys_exit_group(int code)
{
    register long x0 __asm__("x0") = code;
    register long x8 __asm__("x8") = 94;
    __asm__ volatile("svc #0" ::"r"(x0), "r"(x8));
    __builtin_unreachable();
}

static long ustrlen(const char *s)
{
    long n = 0;
    while (s[n] != 0) {
        ++n;
    }
    return n;
}

void _start(void)
{
    const char msg[] = "hello from ELF-loaded EL0 binary\n";
    sys_write(1, msg, ustrlen(msg));
    sys_exit_group(0);
}
