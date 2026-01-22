#ifndef MLANG_RUNTIME_BUILTINS_H
#define MLANG_RUNTIME_BUILTINS_H

/**
 * @file runtime_builtins.h
 * @brief Mlang runtime builtins for threading, mutexes, and atomics.
 */

/**
 * @defgroup runtime_builtins Mlang Runtime Builtins
 * @brief Built-in functions and handle types provided by the compiler.
 *
 * These APIs are not implemented as normal library functions. The compiler
 * recognizes them and lowers them to platform calls (pthreads + libc).
 */

/**
 * @defgroup runtime_handles Handle Types
 * @ingroup runtime_builtins
 * @brief Typed handle wrappers for low-level runtime resources.
 */

/**
 * @brief Generic handle wrapper.
 * @ingroup runtime_handles
 *
 * In Mlang, this is represented as Handle<T> with a hidden raw 64-bit value.
 */
typedef struct MlangHandle MlangHandle;

/**
 * @brief Marker type for thread handles.
 * @ingroup runtime_handles
 */
typedef struct MlangThread MlangThread;

/**
 * @brief Marker type for mutex handles.
 * @ingroup runtime_handles
 */
typedef struct MlangMutex MlangMutex;

/**
 * @brief Marker type for atomic i64 handles.
 * @ingroup runtime_handles
 */
typedef struct MlangAtomic64 MlangAtomic64;

/**
 * @defgroup runtime_threads Threading
 * @ingroup runtime_builtins
 * @brief Thread spawn/join builtins.
 */

/**
 * @brief Spawn a new thread.
 * @ingroup runtime_threads
 *
 * The first argument is the function to run, followed by 0-4 integer or
 * handle arguments. The function must accept the same number of arguments
 * and return void.
 */
MlangHandle mlang_thread_spawn(void* func, long long arg1, long long arg2,
                               long long arg3, long long arg4);

/**
 * @brief Join a thread and return its status.
 * @ingroup runtime_threads
 */
int mlang_thread_join(MlangHandle thread);

/**
 * @defgroup runtime_mutex Mutexes
 * @ingroup runtime_builtins
 * @brief Mutex builtins for protecting shared state.
 */

/**
 * @brief Create a mutex handle.
 * @ingroup runtime_mutex
 */
MlangHandle mlang_mutex_create(void);

/**
 * @brief Lock a mutex.
 * @ingroup runtime_mutex
 */
int mlang_mutex_lock(MlangHandle mutex);

/**
 * @brief Unlock a mutex.
 * @ingroup runtime_mutex
 */
int mlang_mutex_unlock(MlangHandle mutex);

/**
 * @brief Destroy a mutex handle.
 * @ingroup runtime_mutex
 */
int mlang_mutex_destroy(MlangHandle mutex);

/**
 * @defgroup runtime_atomics Atomics
 * @ingroup runtime_builtins
 * @brief Atomic i64 operations.
 */

/**
 * @brief Create an atomic i64 with the given initial value.
 * @ingroup runtime_atomics
 */
MlangHandle mlang_atomic_i64_new(long long initial);

/**
 * @brief Load the current value of an atomic i64.
 * @ingroup runtime_atomics
 */
long long mlang_atomic_i64_load(MlangHandle atomic);

/**
 * @brief Store a new value into an atomic i64.
 * @ingroup runtime_atomics
 */
long long mlang_atomic_i64_store(MlangHandle atomic, long long value);

/**
 * @brief Add a value to an atomic i64 and return the previous value.
 * @ingroup runtime_atomics
 */
long long mlang_atomic_i64_add(MlangHandle atomic, long long delta);

/**
 * @brief Free an atomic i64 handle.
 * @ingroup runtime_atomics
 */
void mlang_atomic_i64_free(MlangHandle atomic);

#endif
