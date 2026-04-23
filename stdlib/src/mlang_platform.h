/*
 * mlang_platform.h — Platform compatibility shims for the mlang stdlib.
 *
 * Provides portable alternatives for POSIX-only constructs so that the
 * stdlib C sources compile on both POSIX (Linux/macOS) and Windows (MSVC).
 */
#ifndef MLANG_PLATFORM_H
#define MLANG_PLATFORM_H

/* ── Thread-local storage ────────────────────────────────────────────── */
#ifdef _MSC_VER
  /* MSVC does not support C11 _Thread_local or GCC __thread in C mode. */
  #define MLANG_THREAD_LOCAL __declspec(thread)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
  #define MLANG_THREAD_LOCAL _Thread_local
#else
  #define MLANG_THREAD_LOCAL __thread
#endif

/* ── gettimeofday shim (for <sys/time.h>) ────────────────────────────── */
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
  /* Include winsock2.h before windows.h so it owns 'struct timeval' and
   * avoids the older winsock.h being pulled in by windows.h. */
  #include <winsock2.h>
  #include <windows.h>
  #include <stdint.h>

  static inline int gettimeofday(struct timeval* tp, void* tzp)
  {
      (void)tzp;
      FILETIME ft;
      GetSystemTimeAsFileTime(&ft);
      /* FILETIME is 100-ns intervals since 1601-01-01; convert to Unix epoch. */
      uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
      t -= 116444736000000000ULL; /* Jan 1, 1601 → Jan 1, 1970 */
      tp->tv_sec  = (long)(t / 10000000ULL);
      tp->tv_usec = (long)((t % 10000000ULL) / 10);
      return 0;
  }
#else
  #include <sys/time.h>
#endif

/* ── <unistd.h> shim ────────────────────────────────────────────────── */
#ifdef _WIN32
  #include <io.h>
  #include <direct.h>
  #include <process.h>
  #ifndef STDIN_FILENO
    #define STDIN_FILENO  0
    #define STDOUT_FILENO 1
    #define STDERR_FILENO 2
  #endif
  #define isatty  _isatty
  #define fileno  _fileno
  #define read    _read
  #define write   _write
  #define getcwd  _getcwd
  #define chdir   _chdir
  #define access  _access
  #define F_OK    0
  #define R_OK    4
  #define W_OK    2
  #define usleep(us) Sleep(((us) + 999) / 1000)
  #define sleep(s)   Sleep((s) * 1000)
  #define pipe(fds)  _pipe((fds), 4096, 0)
  #define popen      _popen
  #define pclose     _pclose
  #define unlink     _unlink
  #define rmdir      _rmdir
  /* mkdir on Windows takes only path (no mode). */
  #define mkdir(p, m) _mkdir(p)

  #ifndef PATH_MAX
    #define PATH_MAX MAX_PATH
  #endif

  /* MSVC does not define ssize_t in C mode. */
  #include <BaseTsd.h>
  #ifndef _SSIZE_T_DEFINED
    typedef SSIZE_T ssize_t;
    #define _SSIZE_T_DEFINED
  #endif

  /* EINTR is not used on Windows; define if missing so guarded code compiles. */
  #ifndef EINTR
    #define EINTR 4
  #endif
#else
  #include <unistd.h>
#endif

/* ── <sys/stat.h> additions for Windows ─────────────────────────────── */
#ifdef _WIN32
  #include <sys/stat.h>
  #ifndef S_ISDIR
    #define S_ISDIR(m)  (((m) & _S_IFMT) == _S_IFDIR)
  #endif
  #ifndef S_ISREG
    #define S_ISREG(m)  (((m) & _S_IFMT) == _S_IFREG)
  #endif
  /* Treat lstat as stat (no symlink semantics needed for our uses). */
  #define lstat stat
#endif

/* ── <dirent.h> emulation for Windows ───────────────────────────────── */
#ifdef _WIN32
  #include <stdlib.h>
  #include <string.h>

  struct dirent
  {
      char d_name[MAX_PATH];
  };

  typedef struct mlang_dir_s
  {
      HANDLE h;
      WIN32_FIND_DATAA fd;
      int first;
      int eof;
      struct dirent cur;
  } DIR;

  static inline DIR* opendir(const char* path)
  {
      if(!path)
          return NULL;
      char pat[MAX_PATH];
      size_t n = strlen(path);
      if(n + 3 >= sizeof(pat))
          return NULL;
      memcpy(pat, path, n);
      if(n > 0 && path[n-1] != '\\' && path[n-1] != '/')
          pat[n++] = '\\';
      pat[n++] = '*';
      pat[n]   = '\0';

      DIR* d = (DIR*)malloc(sizeof(DIR));
      if(!d)
          return NULL;
      d->h = FindFirstFileA(pat, &d->fd);
      if(d->h == INVALID_HANDLE_VALUE)
      {
          free(d);
          return NULL;
      }
      d->first = 1;
      d->eof = 0;
      return d;
  }

  static inline struct dirent* readdir(DIR* d)
  {
      if(!d || d->eof)
          return NULL;
      if(d->first)
      {
          d->first = 0;
      }
      else
      {
          if(!FindNextFileA(d->h, &d->fd))
          {
              d->eof = 1;
              return NULL;
          }
      }
      strncpy(d->cur.d_name, d->fd.cFileName, sizeof(d->cur.d_name) - 1);
      d->cur.d_name[sizeof(d->cur.d_name) - 1] = '\0';
      return &d->cur;
  }

  static inline int closedir(DIR* d)
  {
      if(!d)
          return -1;
      if(d->h != INVALID_HANDLE_VALUE)
          FindClose(d->h);
      free(d);
      return 0;
  }
#endif

/* ── <sys/wait.h> shim ──────────────────────────────────────────────── */
#ifdef _WIN32
  #ifndef WIFEXITED
    #define WIFEXITED(status)   (((status) & 0x7f) == 0)
  #endif
  #ifndef WEXITSTATUS
    #define WEXITSTATUS(status) (((status) >> 8) & 0xff)
  #endif
#else
  #include <sys/wait.h>
#endif

/* ── pthread shim (minimal, for mutex + thread basics) ──────────────── */
#ifdef _WIN32
  #include <windows.h>

  typedef HANDLE            mlang_thread_t;
  typedef CRITICAL_SECTION  mlang_mutex_t;

  static inline int mlang_mutex_init(mlang_mutex_t* m)
  { InitializeCriticalSection(m); return 0; }

  static inline int mlang_mutex_destroy(mlang_mutex_t* m)
  { DeleteCriticalSection(m); return 0; }

  static inline int mlang_mutex_lock(mlang_mutex_t* m)
  { EnterCriticalSection(m); return 0; }

  static inline int mlang_mutex_unlock(mlang_mutex_t* m)
  { LeaveCriticalSection(m); return 0; }

  static inline int mlang_mutex_trylock(mlang_mutex_t* m)
  { return TryEnterCriticalSection(m) ? 0 : -1; }

  /* call_once */
  typedef INIT_ONCE mlang_once_t;
  #define MLANG_ONCE_INIT INIT_ONCE_STATIC_INIT
  typedef void (*mlang_once_func_t)(void);
  static BOOL CALLBACK mlang_once_trampoline_(PINIT_ONCE io, PVOID param, PVOID* ctx)
  { (void)io; (void)ctx; ((mlang_once_func_t)param)(); return TRUE; }
  static inline void mlang_call_once(mlang_once_t* o, mlang_once_func_t f)
  { (void)InitOnceExecuteOnce(o, mlang_once_trampoline_, (PVOID)f, NULL); }

  /* Condition variable (SRWLOCK-free, works with CRITICAL_SECTION) */
  typedef CONDITION_VARIABLE mlang_cond_t;

  static inline int mlang_cond_init(mlang_cond_t* c)
  { InitializeConditionVariable(c); return 0; }

  static inline int mlang_cond_destroy(mlang_cond_t* c)
  { (void)c; return 0; }

  static inline int mlang_cond_wait(mlang_cond_t* c, mlang_mutex_t* m)
  { return SleepConditionVariableCS(c, m, INFINITE) ? 0 : -1; }

  /* Returns 0 on signal, 1 on timeout, -1 on error. */
  static inline int mlang_cond_wait_for_ms(mlang_cond_t* c, mlang_mutex_t* m, long long timeout_ms)
  {
      if(timeout_ms < 0) timeout_ms = 0;
      DWORD ms = (timeout_ms > (long long)0xFFFFFFFEUL) ? 0xFFFFFFFEUL : (DWORD)timeout_ms;
      if(SleepConditionVariableCS(c, m, ms))
          return 0;
      return (GetLastError() == ERROR_TIMEOUT) ? 1 : -1;
  }

  static inline int mlang_cond_signal(mlang_cond_t* c)
  { WakeConditionVariable(c); return 0; }

  static inline int mlang_cond_broadcast(mlang_cond_t* c)
  { WakeAllConditionVariable(c); return 0; }

  /* Portable thread entry: define functions as
   *     MLANG_THREAD_RETURN MLANG_THREAD_CALL fn(void* arg) { ... return MLANG_THREAD_RETURN_VALUE; }
   * and pass them to mlang_thread_create unchanged on both platforms. */
  #define MLANG_THREAD_RETURN        DWORD
  #define MLANG_THREAD_CALL          WINAPI
  #define MLANG_THREAD_RETURN_VALUE  ((DWORD)0)
  typedef DWORD (WINAPI *mlang_thread_func_t)(LPVOID);

  static inline int mlang_thread_create(mlang_thread_t* t,
                                         mlang_thread_func_t fn, void* arg)
  {
      *t = CreateThread(NULL, 0, fn, arg, 0, NULL);
      return (*t == NULL) ? -1 : 0;
  }

  static inline int mlang_thread_join(mlang_thread_t t)
  {
      WaitForSingleObject(t, INFINITE);
      CloseHandle(t);
      return 0;
  }

  /* pthread_mutex compatibility (statically initializable via SRWLOCK). */
  typedef SRWLOCK pthread_mutex_t;
  #define PTHREAD_MUTEX_INITIALIZER SRWLOCK_INIT
  static inline int pthread_mutex_init(pthread_mutex_t* m, void* attr)
  { (void)attr; InitializeSRWLock(m); return 0; }
  static inline int pthread_mutex_destroy(pthread_mutex_t* m)
  { (void)m; return 0; }
  static inline int pthread_mutex_lock(pthread_mutex_t* m)
  { AcquireSRWLockExclusive(m); return 0; }
  static inline int pthread_mutex_unlock(pthread_mutex_t* m)
  { ReleaseSRWLockExclusive(m); return 0; }
  static inline int pthread_mutex_trylock(pthread_mutex_t* m)
  { return TryAcquireSRWLockExclusive(m) ? 0 : -1; /* EBUSY-ish */ }

  /* Wait for input on a CRT file descriptor with a timeout (POSIX poll-ish).
   * Returns 1 if readable, 0 on timeout, -1 on error. */
  static inline int mlang_poll_fd_in_ms(int fd, int timeout_ms)
  {
      HANDLE h = (HANDLE)_get_osfhandle(fd);
      if(h == INVALID_HANDLE_VALUE)
          return -1;
      DWORD type = GetFileType(h);
      if(type == FILE_TYPE_PIPE)
      {
          int waited = 0;
          for(;;)
          {
              DWORD avail = 0;
              if(!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL))
                  return (GetLastError() == ERROR_BROKEN_PIPE) ? 1 : -1;
              if(avail > 0)
                  return 1;
              if(timeout_ms == 0)
                  return 0;
              DWORD step = 10;
              if(timeout_ms > 0 && (int)step > (timeout_ms - waited))
                  step = (DWORD)(timeout_ms - waited);
              Sleep(step);
              if(timeout_ms > 0)
              {
                  waited += (int)step;
                  if(waited >= timeout_ms)
                      return 0;
              }
          }
      }
      else if(type == FILE_TYPE_CHAR)
      {
          DWORD ms = (timeout_ms < 0) ? INFINITE : (DWORD)timeout_ms;
          DWORD r = WaitForSingleObject(h, ms);
          if(r == WAIT_OBJECT_0) return 1;
          if(r == WAIT_TIMEOUT)  return 0;
          return -1;
      }
      /* Disk files / unknown: assume always readable. */
      return 1;
  }
#else
  #include <pthread.h>

  typedef pthread_t   mlang_thread_t;
  typedef pthread_mutex_t mlang_mutex_t;

  #define mlang_mutex_init(m)    pthread_mutex_init((m), NULL)
  #define mlang_mutex_destroy(m) pthread_mutex_destroy(m)
  #define mlang_mutex_lock(m)    pthread_mutex_lock(m)
  #define mlang_mutex_unlock(m)  pthread_mutex_unlock(m)
  #define mlang_mutex_trylock(m) pthread_mutex_trylock(m)

  typedef pthread_once_t mlang_once_t;
  #define MLANG_ONCE_INIT PTHREAD_ONCE_INIT
  typedef void (*mlang_once_func_t)(void);
  #define mlang_call_once(o, f) ((void)pthread_once((o), (f)))

  #include <errno.h>
  #include <time.h>
  typedef pthread_cond_t mlang_cond_t;

  #define mlang_cond_init(c)      pthread_cond_init((c), NULL)
  #define mlang_cond_destroy(c)   pthread_cond_destroy(c)
  #define mlang_cond_wait(c, m)   pthread_cond_wait((c), (m))
  #define mlang_cond_signal(c)    pthread_cond_signal(c)
  #define mlang_cond_broadcast(c) pthread_cond_broadcast(c)

  static inline int mlang_cond_wait_for_ms(mlang_cond_t* c, mlang_mutex_t* m, long long timeout_ms)
  {
      if(timeout_ms < 0) timeout_ms = 0;
      struct timespec ts;
      (void)clock_gettime(CLOCK_REALTIME, &ts);
      long long add_ns = timeout_ms * 1000000LL;
      ts.tv_sec  += (time_t)(add_ns / 1000000000LL);
      ts.tv_nsec += (long)(add_ns % 1000000000LL);
      if(ts.tv_nsec >= 1000000000L) { ts.tv_sec += 1; ts.tv_nsec -= 1000000000L; }
      int rc = pthread_cond_timedwait(c, m, &ts);
      if(rc == 0)         return 0;
      if(rc == ETIMEDOUT) return 1;
      return -1;
  }

  #define MLANG_THREAD_RETURN        void*
  #define MLANG_THREAD_CALL          /* nothing */
  #define MLANG_THREAD_RETURN_VALUE  ((void*)0)
  typedef void* (*mlang_thread_func_t)(void*);

  static inline int mlang_thread_create(mlang_thread_t* t,
                                         mlang_thread_func_t fn, void* arg)
  {
      return pthread_create(t, NULL, fn, arg);
  }

  static inline int mlang_thread_join(mlang_thread_t t)
  {
      return pthread_join(t, NULL);
  }

  /* poll() helper, mirror of the Windows shim. */
  #include <poll.h>
  static inline int mlang_poll_fd_in_ms(int fd, int timeout_ms)
  {
      struct pollfd pfd;
      pfd.fd = fd;
      pfd.events = POLLIN;
      pfd.revents = 0;
      int pr = poll(&pfd, 1, timeout_ms);
      if(pr == 0)  return 0;
      if(pr < 0)   return -1;
      return ((pfd.revents & POLLIN) != 0) ? 1 : -1;
  }
#endif

/* ── clock_gettime shim ─────────────────────────────────────────────── */
#ifdef _WIN32
  #include <stdint.h>
  #include <time.h>
  #ifndef CLOCK_REALTIME
    #define CLOCK_REALTIME  0
  #endif
  #ifndef CLOCK_MONOTONIC
    #define CLOCK_MONOTONIC 1
  #endif

  /* MSVC's <time.h> already defines struct timespec (C11). */
  static inline int mlang_clock_gettime(int clk, struct timespec* ts)
  {
      if(!ts)
          return -1;
      if(clk == CLOCK_MONOTONIC)
      {
          static LARGE_INTEGER s_freq;
          static int s_have_freq = 0;
          LARGE_INTEGER now;
          if(!s_have_freq) { QueryPerformanceFrequency(&s_freq); s_have_freq = 1; }
          QueryPerformanceCounter(&now);
          int64_t freq = s_freq.QuadPart ? s_freq.QuadPart : 1;
          int64_t sec  = now.QuadPart / freq;
          int64_t rem  = now.QuadPart - sec * freq;
          ts->tv_sec  = (time_t)sec;
          ts->tv_nsec = (long)((rem * 1000000000LL) / freq);
          return 0;
      }
      /* CLOCK_REALTIME */
      FILETIME ft;
      GetSystemTimeAsFileTime(&ft);
      uint64_t t = ((uint64_t)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
      t -= 116444736000000000ULL; /* 1601 → 1970 */
      ts->tv_sec  = (time_t)(t / 10000000ULL);
      ts->tv_nsec = (long)((t % 10000000ULL) * 100);
      return 0;
  }
  #define clock_gettime(c, ts) mlang_clock_gettime((c), (ts))
#endif

/* ── Sleep with nanosecond input (millisecond precision on Windows) ── */
#ifdef _WIN32
  static inline void mlang_sleep_ns(long long ns)
  {
      if(ns <= 0)
          return;
      long long ms = (ns + 999999LL) / 1000000LL;
      while(ms > 0)
      {
          DWORD chunk = (ms > (long long)0xFFFFFFFEUL) ? 0xFFFFFFFEUL : (DWORD)ms;
          Sleep(chunk);
          ms -= chunk;
      }
  }
#else
  #include <time.h>
  #include <errno.h>
  static inline void mlang_sleep_ns(long long ns)
  {
      if(ns <= 0)
          return;
      struct timespec req;
      req.tv_sec  = (time_t)(ns / 1000000000LL);
      req.tv_nsec = (long)(ns % 1000000000LL);
      while(nanosleep(&req, &req) != 0)
      {
          if(errno != EINTR)
              break;
      }
  }
#endif

/* ── Atomic int shim (relaxed load/store) ───────────────────────────── */
#ifdef _MSC_VER
  /* MSVC C mode lacks <stdatomic.h>; use Interlocked intrinsics. */
  typedef volatile LONG mlang_atomic_int;
  #define mlang_atomic_store(p, v)  InterlockedExchange((p), (LONG)(v))
  #define mlang_atomic_load(p)      InterlockedOr((p), 0)

  /* 64-bit atomic for size_t / uint64_t sequence numbers. */
  typedef volatile LONG64 mlang_atomic_size_t;
  #define mlang_atomic_size_store(p, v)  InterlockedExchange64((p), (LONG64)(v))
  #define mlang_atomic_size_load(p)      ((size_t)InterlockedOr64((p), 0))
#else
  #include <stdatomic.h>
  typedef atomic_int mlang_atomic_int;
  #define mlang_atomic_store(p, v)  atomic_store((p), (v))
  #define mlang_atomic_load(p)      atomic_load((p))

  typedef _Atomic size_t mlang_atomic_size_t;
  #define mlang_atomic_size_store(p, v)  atomic_store((p), (v))
  #define mlang_atomic_size_load(p)      atomic_load((p))
#endif

/* ── Sockets ────────────────────────────────────────────────────────── */
#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
  #include <basetsd.h>
  #pragma comment(lib, "ws2_32.lib")

  #ifndef _SSIZE_T_DEFINED
    typedef SSIZE_T ssize_t;
    #define _SSIZE_T_DEFINED
  #endif
  typedef long suseconds_t;

  typedef SOCKET mlang_socket_t;
  #define MLANG_INVALID_SOCKET INVALID_SOCKET
  #define mlang_close_socket(s) closesocket((SOCKET)(s))
  #define mlang_socket_errno()  WSAGetLastError()

  static mlang_once_t g_mlang_wsa_once = MLANG_ONCE_INIT;
  static void mlang_wsa_init(void)
  {
      WSADATA wsa;
      (void)WSAStartup(MAKEWORD(2, 2), &wsa);
  }
  static inline void mlang_socket_startup(void)
  {
      mlang_call_once(&g_mlang_wsa_once, mlang_wsa_init);
  }

  static inline int mlang_set_socket_nonblocking(mlang_socket_t s, int enabled)
  {
      u_long mode = enabled ? 1u : 0u;
      return (ioctlsocket(s, FIONBIO, &mode) == 0) ? 0 : -1;
  }

  static inline void mlang_socket_strerror(char* buf, size_t n, int err)
  {
      if(!buf || n == 0)
          return;
      DWORD r = FormatMessageA(
          FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
          NULL, (DWORD)err, 0, buf, (DWORD)n, NULL);
      if(r == 0)
          (void)snprintf(buf, n, "winsock error %d", err);
      else
      {
          /* trim trailing CRLF / period from FormatMessage output */
          while(r > 0 && (buf[r-1] == '\r' || buf[r-1] == '\n' ||
                          buf[r-1] == '.'  || buf[r-1] == ' '))
              buf[--r] = '\0';
      }
  }
#else
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <fcntl.h>
  #include <errno.h>

  typedef int mlang_socket_t;
  #define MLANG_INVALID_SOCKET (-1)
  #define mlang_close_socket(s) close((int)(s))
  #define mlang_socket_errno()  errno
  static inline void mlang_socket_startup(void) { }

  static inline int mlang_set_socket_nonblocking(mlang_socket_t s, int enabled)
  {
      int flags = fcntl(s, F_GETFL, 0);
      if(flags < 0)
          return -1;
      if(enabled)
          flags |= O_NONBLOCK;
      else
          flags &= ~O_NONBLOCK;
      return fcntl(s, F_SETFL, flags) == 0 ? 0 : -1;
  }

  static inline void mlang_socket_strerror(char* buf, size_t n, int err)
  {
      if(!buf || n == 0) return;
      const char* m = strerror(err);
      (void)snprintf(buf, n, "%s", m ? m : "unknown");
  }
#endif

#endif /* MLANG_PLATFORM_H */
