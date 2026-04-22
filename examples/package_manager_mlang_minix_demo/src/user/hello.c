static long sys1(long nr, long a)
{
    register long x0 __asm__("x0") = a;
    register long x8 __asm__("x8") = nr;
    __asm__ volatile("svc #0" : "+r"(x0) : "r"(x8) : "memory");
    return x0;
}

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

static long sys4(long nr, long a, long b, long c, long d)
{
    register long x0 __asm__("x0") = a;
    register long x1 __asm__("x1") = b;
    register long x2 __asm__("x2") = c;
    register long x3 __asm__("x3") = d;
    register long x8 __asm__("x8") = nr;
    __asm__ volatile("svc #0"
                     : "+r"(x0)
                     : "r"(x1), "r"(x2), "r"(x3), "r"(x8)
                     : "memory");
    return x0;
}

static long sys_write(long fd, const void *buf, long len)
{
    return sys3(64, fd, (long)buf, len);
}

static long sys_openat(long dirfd, const char *path, long flags)
{
    return sys4(56, dirfd, (long)path, flags, 0);
}

static long sys_read(long fd, void *buf, long len)
{
    return sys3(63, fd, (long)buf, len);
}

static long sys_close(long fd)
{
    return sys1(57, fd);
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

static void uputs(const char *s)
{
    sys_write(1, s, ustrlen(s));
}

#define AT_FDCWD (-100L)

static void cat_file(const char *path)
{
    char buf[512];
    long fd = sys_openat(AT_FDCWD, path, 0);
    if (fd < 0) {
        uputs("[user] openat failed for ");
        uputs(path);
        uputs("\n");
        return;
    }
    uputs("[user] --- ");
    uputs(path);
    uputs(" ---\n");
    for (;;) {
        long n = sys_read(fd, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        sys_write(1, buf, n);
    }
    sys_close(fd);
}

void _start(void)
{
    uputs("hello from ELF-loaded EL0 binary\n");
    cat_file("/etc/motd");
    cat_file("/etc/hello.txt");
    cat_file("/readme");
    sys_exit_group(0);
}
