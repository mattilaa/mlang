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
  #include <windows.h>
  #include <stdint.h>

  #ifndef _STRUCT_TIMEVAL
  #define _STRUCT_TIMEVAL
  struct timeval {
      long tv_sec;
      long tv_usec;
  };
  #endif

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

  /* MSVC does not define ssize_t in C mode. */
  #include <BaseTsd.h>
  typedef SSIZE_T ssize_t;

  /* EINTR is not used on Windows; define if missing so guarded code compiles. */
  #ifndef EINTR
    #define EINTR 4
  #endif
#else
  #include <unistd.h>
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
#else
  #include <pthread.h>

  typedef pthread_t   mlang_thread_t;
  typedef pthread_mutex_t mlang_mutex_t;

  #define mlang_mutex_init(m)    pthread_mutex_init((m), NULL)
  #define mlang_mutex_destroy(m) pthread_mutex_destroy(m)
  #define mlang_mutex_lock(m)    pthread_mutex_lock(m)
  #define mlang_mutex_unlock(m)  pthread_mutex_unlock(m)

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
#endif

#endif /* MLANG_PLATFORM_H */
