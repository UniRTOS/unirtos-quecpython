#include <string.h>

#include "py/runtime.h"
#include "py/gc.h"
#include "py/mpthread.h"
#include "py/mperrno.h"
#include "qosa_sys.h"

#if MICROPY_PY_THREAD

// qosa_dev.h pulls AT headers that are deliberately absent from the qstr
// preprocessing include set.  Keep the small memory-query ABI local instead.
typedef struct _qpy_ram_info_t {
    qosa_uint32_t free_ram_size;
    qosa_uint32_t total_ram_size;
    qosa_uint32_t max_free_ram_size;
} qpy_ram_info_t;
extern int qosa_dev_get_memory_size(qpy_ram_info_t *ram_info_ptr);

#define QPY_THREAD_STACK_MARGIN (1024)

typedef struct _qpy_thread_t {
    qosa_task_t id;
    mp_state_thread_t *state;
    void *arg;
    void *stack;
    size_t stack_len;
    int ready;
    struct _qpy_thread_t *next;
} qpy_thread_t;

typedef struct _qpy_thread_entry_t {
    void *(*entry)(void *);
    void *arg;
    qpy_thread_t *thread;
} qpy_thread_entry_t;

static qosa_mutex_t qpy_thread_mutex = QOSA_NULL;
static qpy_thread_t *qpy_threads;
static void *qpy_main_stack;
static size_t qpy_main_stack_len;

static qpy_thread_t *qpy_thread_find_current(void) {
    qosa_task_t current = QOSA_NULL;
    qosa_task_get_current_ref(&current);
    for (qpy_thread_t *th = qpy_threads; th != QOSA_NULL; th = th->next) {
        if (th->id == current) {
            return th;
        }
    }
    return QOSA_NULL;
}

void qpy_thread_register_main_stack(void *stack_base, size_t stack_len) {
    qpy_main_stack = stack_base;
    qpy_main_stack_len = stack_len;
}

void mp_thread_unirtos_begin_atomic_section(void) {
    if (qpy_thread_mutex != QOSA_NULL) {
        qosa_mutex_lock(qpy_thread_mutex, QOSA_WAIT_FOREVER);
    }
}

void mp_thread_unirtos_end_atomic_section(void) {
    if (qpy_thread_mutex != QOSA_NULL) {
        qosa_mutex_unlock(qpy_thread_mutex);
    }
}

void mp_thread_init(void) {
    qosa_mutex_create(&qpy_thread_mutex);

    qpy_thread_t *main_thread = qosa_malloc(sizeof(qpy_thread_t));
    if (main_thread == QOSA_NULL) {
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("thread init"));
    }
    memset(main_thread, 0, sizeof(*main_thread));
    qosa_task_get_current_ref(&main_thread->id);
    main_thread->state = &mp_state_ctx.thread;
    main_thread->stack = qpy_main_stack;
    main_thread->stack_len = qpy_main_stack_len;
    main_thread->ready = 1;
    main_thread->next = QOSA_NULL;
    qpy_threads = main_thread;
    mp_thread_set_state(&mp_state_ctx.thread);
}

void mp_thread_deinit(void) {
    mp_thread_unirtos_begin_atomic_section();
    qpy_thread_t *th = qpy_threads;
    qpy_threads = QOSA_NULL;
    mp_thread_unirtos_end_atomic_section();

    while (th != QOSA_NULL) {
        qpy_thread_t *next = th->next;
        qosa_free(th);
        th = next;
    }
    if (qpy_thread_mutex != QOSA_NULL) {
        qosa_mutex_delete(qpy_thread_mutex);
        qpy_thread_mutex = QOSA_NULL;
    }
}

void mp_thread_gc_others(void) {
    qosa_task_t current = QOSA_NULL;
    qosa_task_get_current_ref(&current);

    mp_thread_unirtos_begin_atomic_section();
    for (qpy_thread_t *th = qpy_threads; th != QOSA_NULL; th = th->next) {
        gc_collect_root(&th->arg, 1);
        if (th->state != QOSA_NULL) {
            gc_collect_root((void **)&th->state, 1);
        }
        if (th->ready && th->id != current && th->stack != QOSA_NULL && th->stack_len > 0) {
            gc_collect_root((void **)th->stack, th->stack_len / sizeof(void *));
        }
    }
    mp_thread_unirtos_end_atomic_section();
}

mp_state_thread_t *mp_thread_get_state(void) {
    qpy_thread_t *th = qpy_thread_find_current();
    if (th != QOSA_NULL && th->state != QOSA_NULL) {
        return th->state;
    }
    return &mp_state_ctx.thread;
}

void mp_thread_set_state(mp_state_thread_t *state) {
    qpy_thread_t *th = qpy_thread_find_current();
    if (th != QOSA_NULL) {
        th->state = state;
    }
}

mp_uint_t mp_thread_get_id(void) {
    qosa_task_t current = QOSA_NULL;
    qosa_task_get_current_ref(&current);
    return (mp_uint_t)(uintptr_t)current;
}

mp_uint_t mp_thread_legacy_get_ident(void) {
    return (mp_uint_t)(uintptr_t)qpy_thread_find_current();
}

mp_uint_t mp_thread_legacy_get_node_from_task_id(mp_uint_t task_id) {
    mp_uint_t node = 0;
    mp_thread_unirtos_begin_atomic_section();
    for (qpy_thread_t *th = qpy_threads; th != QOSA_NULL; th = th->next) {
        if ((mp_uint_t)(uintptr_t)th->id == task_id) {
            node = (mp_uint_t)(uintptr_t)th;
            break;
        }
    }
    mp_thread_unirtos_end_atomic_section();
    return node;
}

bool mp_thread_legacy_is_running(mp_uint_t node) {
    bool found = false;
    mp_thread_unirtos_begin_atomic_section();
    for (qpy_thread_t *th = qpy_threads; th != QOSA_NULL; th = th->next) {
        // Legacy threadIsRunning reports a node as soon as it has been
        // registered, including the brief interval before its entry runs.
        if ((mp_uint_t)(uintptr_t)th == node) {
            found = true;
            break;
        }
    }
    mp_thread_unirtos_end_atomic_section();
    return found;
}

int mp_thread_legacy_stop(mp_uint_t node) {
    qpy_thread_t *target = QOSA_NULL;
    qosa_task_t task = QOSA_NULL;
    mp_thread_unirtos_begin_atomic_section();
    qpy_thread_t *prev = QOSA_NULL;
    for (qpy_thread_t *th = qpy_threads; th != QOSA_NULL; prev = th, th = th->next) {
        if ((mp_uint_t)(uintptr_t)th == node) {
            target = th;
            task = th->id;
            if (prev == QOSA_NULL) {
                qpy_threads = th->next;
            } else {
                prev->next = th->next;
            }
            th->ready = 0;
            break;
        }
    }
    mp_thread_unirtos_end_atomic_section();
    if (target == QOSA_NULL) {
        return -1;
    }
    if (target->state == &mp_state_ctx.thread) {
        mp_thread_unirtos_begin_atomic_section();
        target->next = qpy_threads;
        target->ready = 1;
        qpy_threads = target;
        mp_thread_unirtos_end_atomic_section();
        return -2;
    }
    if (qosa_task_delete(task) != QOSA_ERROR_OK) {
        mp_thread_unirtos_begin_atomic_section();
        target->next = qpy_threads;
        target->ready = 1;
        qpy_threads = target;
        mp_thread_unirtos_end_atomic_section();
        return -1;
    }
    return 0;
}

mp_uint_t mp_thread_legacy_get_heap_size(void) {
    qpy_ram_info_t ram = {0};
    return qosa_dev_get_memory_size(&ram) == 0 ? ram.free_ram_size : 0;
}

void mp_thread_start(void) {
    mp_thread_unirtos_begin_atomic_section();
    qpy_thread_t *th = qpy_thread_find_current();
    if (th != QOSA_NULL) {
        th->state = mp_thread_get_state();
        th->ready = 1;
    }
    mp_thread_unirtos_end_atomic_section();
}

static void qpy_thread_wrapper(void *ctx) {
    qpy_thread_entry_t *entry = (qpy_thread_entry_t *)ctx;
    qosa_task_t self = QOSA_NULL;
    qosa_task_get_current_ref(&self);
    if (entry != QOSA_NULL) {
        entry->thread->id = self;
        entry->entry(entry->arg);
        mp_thread_finish();
        qosa_free(entry);
    }
    if (self != QOSA_NULL) {
        qosa_task_delete(self);
    }
    for (;;) {
        qosa_task_sleep_ms(1000);
    }
}

mp_uint_t mp_thread_create_named(void *(*entry)(void *), void *arg, size_t *stack_size, const char *name) {
    if (*stack_size == 0) {
        *stack_size = CONFIG_QPY_THREAD_STACK_SIZE;
    }
    if (*stack_size < (2 * QPY_THREAD_STACK_MARGIN)) {
        *stack_size = 2 * QPY_THREAD_STACK_MARGIN;
    }

    qpy_thread_t *th = qosa_malloc(sizeof(qpy_thread_t));
    qpy_thread_entry_t *entry_ctx = qosa_malloc(sizeof(qpy_thread_entry_t));
    void *stack = qosa_malloc(*stack_size);
    void *tcb = qosa_malloc(qosa_task_get_tcb_min_size());
    if (th == QOSA_NULL || entry_ctx == QOSA_NULL || stack == QOSA_NULL || tcb == QOSA_NULL) {
        if (th != QOSA_NULL) {
            qosa_free(th);
        }
        if (entry_ctx != QOSA_NULL) {
            qosa_free(entry_ctx);
        }
        if (stack != QOSA_NULL) {
            qosa_free(stack);
        }
        if (tcb != QOSA_NULL) {
            qosa_free(tcb);
        }
        mp_raise_msg(&mp_type_MemoryError, MP_ERROR_TEXT("thread create"));
    }

    memset(th, 0, sizeof(*th));
    entry_ctx->entry = entry;
    entry_ctx->arg = arg;
    entry_ctx->thread = th;
    th->arg = arg;
    th->stack = stack;
    th->stack_len = *stack_size;

    mp_thread_unirtos_begin_atomic_section();
    th->next = qpy_threads;
    qpy_threads = th;
    mp_thread_unirtos_end_atomic_section();

    int ret = qosa_task_create_static(&th->id, stack, (qosa_uint32_t)*stack_size, tcb, qosa_task_get_tcb_min_size(),
        QOSA_PRIORITY_NORMAL, (char *)name, qpy_thread_wrapper, entry_ctx);
    if (ret != QOSA_ERROR_OK) {
        mp_thread_unirtos_begin_atomic_section();
        if (qpy_threads == th) {
            qpy_threads = th->next;
        } else {
            for (qpy_thread_t *scan = qpy_threads; scan != QOSA_NULL; scan = scan->next) {
                if (scan->next == th) {
                    scan->next = th->next;
                    break;
                }
            }
        }
        mp_thread_unirtos_end_atomic_section();
        qosa_free(entry_ctx);
        qosa_free(stack);
        qosa_free(tcb);
        qosa_free(th);
        mp_raise_OSError(MP_EAGAIN);
    }

    *stack_size -= QPY_THREAD_STACK_MARGIN;
    return (mp_uint_t)(uintptr_t)th->id;
}

mp_uint_t mp_thread_create(void *(*entry)(void *), void *arg, size_t *stack_size) {
    return mp_thread_create_named(entry, arg, stack_size, "qpy_thread");
}

void mp_thread_finish(void) {
    qosa_task_t current = QOSA_NULL;
    qosa_task_get_current_ref(&current);
    mp_thread_unirtos_begin_atomic_section();
    qpy_thread_t *prev = QOSA_NULL;
    qpy_thread_t *th = qpy_threads;
    while (th != QOSA_NULL) {
        if (th->id == current) {
            th->ready = 0;
            if (prev == QOSA_NULL) {
                qpy_threads = th->next;
            } else {
                prev->next = th->next;
            }
            break;
        }
        prev = th;
        th = th->next;
    }
    mp_thread_unirtos_end_atomic_section();
}

void mp_thread_mutex_init(mp_thread_mutex_t *mutex) {
    qosa_mutex_create(mutex);
}

int mp_thread_mutex_lock(mp_thread_mutex_t *mutex, int wait) {
    int ret = wait ? qosa_mutex_lock(*mutex, QOSA_WAIT_FOREVER) : qosa_mutex_try_lock(*mutex);
    if (ret == QOSA_ERROR_OK) {
        return 1;
    }
    if (!wait && ret == QOSA_ERROR_MUTEX_EBUSY_ERR) {
        return 0;
    }
    return -MP_EIO;
}

void mp_thread_mutex_unlock(mp_thread_mutex_t *mutex) {
    qosa_mutex_unlock(*mutex);
}

#if MICROPY_PY_THREAD_RECURSIVE_MUTEX
void mp_thread_recursive_mutex_init(mp_thread_recursive_mutex_t *mutex) {
    qosa_mutex_create(mutex);
}

int mp_thread_recursive_mutex_lock(mp_thread_recursive_mutex_t *mutex, int wait) {
    return mp_thread_mutex_lock(mutex, wait);
}

void mp_thread_recursive_mutex_unlock(mp_thread_recursive_mutex_t *mutex) {
    mp_thread_mutex_unlock(mutex);
}
#endif

#endif
