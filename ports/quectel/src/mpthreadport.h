#ifndef MICROPY_INCLUDED_QUECTEL_UNIRTOS_MPTHREADPORT_H
#define MICROPY_INCLUDED_QUECTEL_UNIRTOS_MPTHREADPORT_H

#include "qosa_sys.h"

typedef qosa_mutex_t mp_thread_mutex_t;
typedef qosa_mutex_t mp_thread_recursive_mutex_t;

void mp_thread_init(void);
void mp_thread_deinit(void);
void mp_thread_gc_others(void);
void mp_thread_unirtos_begin_atomic_section(void);
void mp_thread_unirtos_end_atomic_section(void);

// Legacy QuecPython _thread uses managed-node addresses as thread IDs.
mp_uint_t mp_thread_legacy_get_ident(void);
mp_uint_t mp_thread_legacy_get_node_from_task_id(mp_uint_t task_id);
bool mp_thread_legacy_is_running(mp_uint_t node);
int mp_thread_legacy_stop(mp_uint_t node);
mp_uint_t mp_thread_legacy_get_heap_size(void);
mp_uint_t mp_thread_create_named(void *(*entry)(void *), void *arg, size_t *stack_size, const char *name);

#endif
