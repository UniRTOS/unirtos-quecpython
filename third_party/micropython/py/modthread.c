/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2016 Damien P. George on behalf of Pycom Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdio.h>
#include <string.h>

#include "py/runtime.h"
#include "py/mperrno.h"
#include "py/mphal.h"

#if MICROPY_PY_THREAD

#include "py/mpthread.h"

#if MICROPY_DEBUG_VERBOSE // print debugging info
#define DEBUG_PRINT (1)
#define DEBUG_printf DEBUG_printf
#else // don't print debugging info
#define DEBUG_PRINT (0)
#define DEBUG_printf(...) (void)0
#endif

/****************************************************************/
// Lock object

static const mp_obj_type_t mp_type_thread_lock;

typedef struct _mp_obj_thread_lock_t {
    mp_obj_base_t base;
    mp_thread_mutex_t mutex;
    volatile bool locked;
} mp_obj_thread_lock_t;

static mp_obj_thread_lock_t *mp_obj_new_thread_lock(void) {
    mp_obj_thread_lock_t *self = mp_obj_malloc_with_finaliser(mp_obj_thread_lock_t, &mp_type_thread_lock);
    mp_thread_mutex_init(&self->mutex);
    self->locked = false;
    return self;
}

static mp_obj_t thread_lock_acquire(size_t n_args, const mp_obj_t *args) {
    mp_obj_thread_lock_t *self = MP_OBJ_TO_PTR(args[0]);
    if (self->mutex == QOSA_NULL) {
        return mp_const_false;
    }
    bool wait = true;
    if (n_args > 1) {
        wait = mp_obj_get_int(args[1]);
        // TODO support timeout arg
    }
    MP_THREAD_GIL_EXIT();
    int ret = mp_thread_mutex_lock(&self->mutex, wait);
    MP_THREAD_GIL_ENTER();
    if (ret == 0) {
        return mp_const_false;
    } else if (ret == 1) {
        self->locked = true;
        return mp_const_true;
    } else {
        mp_raise_OSError(-ret);
    }
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(thread_lock_acquire_obj, 1, 3, thread_lock_acquire);

static mp_obj_t thread_lock_release(mp_obj_t self_in) {
    mp_obj_thread_lock_t *self = MP_OBJ_TO_PTR(self_in);
    if (!self->locked) {
        mp_raise_msg(&mp_type_RuntimeError, NULL);
    }
    self->locked = false;
    MP_THREAD_GIL_EXIT();
    mp_thread_mutex_unlock(&self->mutex);
    MP_THREAD_GIL_ENTER();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(thread_lock_release_obj, thread_lock_release);

static mp_obj_t thread_lock_locked(mp_obj_t self_in) {
    mp_obj_thread_lock_t *self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_bool(self->locked);
}
static MP_DEFINE_CONST_FUN_OBJ_1(thread_lock_locked_obj, thread_lock_locked);

static mp_obj_t thread_lock___exit__(size_t n_args, const mp_obj_t *args) {
    (void)n_args; // unused
    return thread_lock_release(args[0]);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(thread_lock___exit___obj, 4, 4, thread_lock___exit__);

static mp_obj_t mod_thread_delete_lock(mp_obj_t lock_in) {
    if (!mp_obj_is_type(lock_in, &mp_type_thread_lock)) {
        mp_raise_msg(&mp_type_TypeError, MP_ERROR_TEXT("The argument isn't a lock."));
    }
    mp_obj_thread_lock_t *lock = MP_OBJ_TO_PTR(lock_in);
    if (lock->mutex != QOSA_NULL) {
        qosa_mutex_delete(lock->mutex);
        lock->mutex = QOSA_NULL;
        lock->locked = false;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_thread_delete_lock_obj, mod_thread_delete_lock);

static const mp_rom_map_elem_t thread_lock_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&mod_thread_delete_lock_obj) },
    { MP_ROM_QSTR(MP_QSTR_acquire), MP_ROM_PTR(&thread_lock_acquire_obj) },
    { MP_ROM_QSTR(MP_QSTR_release), MP_ROM_PTR(&thread_lock_release_obj) },
    { MP_ROM_QSTR(MP_QSTR_locked), MP_ROM_PTR(&thread_lock_locked_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&thread_lock_acquire_obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__), MP_ROM_PTR(&thread_lock___exit___obj) },
};

static MP_DEFINE_CONST_DICT(thread_lock_locals_dict, thread_lock_locals_dict_table);

static MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_thread_lock,
    MP_QSTR_lock,
    MP_TYPE_FLAG_NONE,
    locals_dict, &thread_lock_locals_dict
    );

/****************************************************************/
// _thread module

typedef struct _mp_obj_thread_semphore_t {
    mp_obj_base_t base;
    qosa_sem_t semphore;
    uint32_t maxcount;
    volatile uint32_t count;
} mp_obj_thread_semphore_t;

static const mp_obj_type_t mp_type_thread_semphore;

static mp_obj_thread_semphore_t *mp_obj_new_thread_semphore(uint32_t maxcount) {
    mp_obj_thread_semphore_t *self = mp_obj_malloc_with_finaliser(mp_obj_thread_semphore_t, &mp_type_thread_semphore);
    if (qosa_sem_create_ex(&self->semphore, maxcount, maxcount) != QOSA_ERROR_OK) {
        mp_raise_OSError(MP_EIO);
    }
    self->maxcount = maxcount;
    self->count = maxcount;
    return self;
}

static mp_obj_t thread_semphore_acquire(size_t n_args, const mp_obj_t *args) {
    mp_obj_thread_semphore_t *self = MP_OBJ_TO_PTR(args[0]);
    if (self->semphore == QOSA_NULL) {
        return mp_const_false;
    }
    qosa_uint32_t wait = QOSA_WAIT_FOREVER;
    if (n_args > 1) {
        wait = (qosa_uint32_t)mp_obj_get_int(args[1]);
    }
    MP_THREAD_GIL_EXIT();
    int ret = qosa_sem_wait(self->semphore, wait);
    MP_THREAD_GIL_ENTER();
    if (ret != QOSA_ERROR_OK) {
        return mp_const_false;
    }
    if (self->count > 0) {
        --self->count;
    }
    return mp_const_true;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(thread_semphore_acquire_obj, 1, 3, thread_semphore_acquire);

static mp_obj_t thread_semphore_release(mp_obj_t self_in) {
    mp_obj_thread_semphore_t *self = MP_OBJ_TO_PTR(self_in);
    if (self->semphore == QOSA_NULL || self->count >= self->maxcount) {
        mp_raise_msg(&mp_type_RuntimeError, NULL);
    }
    MP_THREAD_GIL_EXIT();
    int ret = qosa_sem_release(self->semphore);
    MP_THREAD_GIL_ENTER();
    if (ret != QOSA_ERROR_OK) {
        mp_raise_OSError(MP_EIO);
    }
    ++self->count;
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(thread_semphore_release_obj, thread_semphore_release);

static mp_obj_t mod_thread_delete_semphore(mp_obj_t sem_in) {
    if (!mp_obj_is_type(sem_in, &mp_type_thread_semphore)) {
        mp_raise_msg(&mp_type_TypeError, MP_ERROR_TEXT("The argument isn't a semphore."));
    }
    mp_obj_thread_semphore_t *sem = MP_OBJ_TO_PTR(sem_in);
    if (sem->semphore != QOSA_NULL) {
        qosa_sem_delete(sem->semphore);
        sem->semphore = QOSA_NULL;
        sem->count = 0;
        sem->maxcount = 0;
    }
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_thread_delete_semphore_obj, mod_thread_delete_semphore);

static mp_obj_t thread_semphore_get_count(mp_obj_t self_in) {
    mp_obj_thread_semphore_t *self = MP_OBJ_TO_PTR(self_in);
    static const qstr semphore_count_info_fields[] = { MP_QSTR_maxCnt, MP_QSTR_curCnt };
    mp_obj_t values[2] = { mp_obj_new_int_from_uint(self->maxcount), mp_obj_new_int_from_uint(self->count) };
    return MP_OBJ_FROM_PTR(mp_obj_new_attrtuple(semphore_count_info_fields, 2, values));
}
static MP_DEFINE_CONST_FUN_OBJ_1(thread_semphore_get_count_obj, thread_semphore_get_count);

static const mp_rom_map_elem_t thread_semphore_locals_dict_table[] = {
    { MP_ROM_QSTR(MP_QSTR___del__), MP_ROM_PTR(&mod_thread_delete_semphore_obj) },
    { MP_ROM_QSTR(MP_QSTR_acquire), MP_ROM_PTR(&thread_semphore_acquire_obj) },
    { MP_ROM_QSTR(MP_QSTR_release), MP_ROM_PTR(&thread_semphore_release_obj) },
    { MP_ROM_QSTR(MP_QSTR_getCnt), MP_ROM_PTR(&thread_semphore_get_count_obj) },
};
static MP_DEFINE_CONST_DICT(thread_semphore_locals_dict, thread_semphore_locals_dict_table);
static MP_DEFINE_CONST_OBJ_TYPE(
    mp_type_thread_semphore,
    MP_QSTR_semphore,
    MP_TYPE_FLAG_NONE,
    locals_dict, &thread_semphore_locals_dict
    );

static size_t thread_stack_size = 2048 * sizeof(uint32_t);

static mp_obj_t mod_thread_get_ident(void) {
    return mp_obj_new_int_from_uint(mp_thread_legacy_get_ident());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_thread_get_ident_obj, mod_thread_get_ident);

static mp_obj_t mod_thread_stack_size(size_t n_args, const mp_obj_t *args) {
    if (n_args != 0) {
        mp_int_t stack_words = mp_obj_get_int(args[0]);
        if (stack_words < 0 || stack_words * (mp_int_t)sizeof(uint32_t) < 4096) {
            return mp_obj_new_int(-1);
        }
        thread_stack_size = (size_t)stack_words * sizeof(uint32_t);
        return mp_obj_new_int(0);
    }
    return mp_obj_new_int_from_uint(thread_stack_size / sizeof(uint32_t));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_thread_stack_size_obj, 0, 1, mod_thread_stack_size);

typedef struct _thread_entry_args_t {
    mp_obj_dict_t *dict_locals;
    mp_obj_dict_t *dict_globals;
    size_t stack_size;
    mp_obj_t fun;
    size_t n_args;
    size_t n_kw;
    bool auto_resume;
    mp_obj_t args[];
} thread_entry_args_t;

static void *thread_entry(void *args_in) {
    // Execution begins here for a new thread.  We do not have the GIL.

    thread_entry_args_t *args = (thread_entry_args_t *)args_in;

    mp_state_thread_t ts;
    mp_thread_init_state(&ts, args->stack_size, args->dict_locals, args->dict_globals);

    #if MICROPY_ENABLE_PYSTACK
    // TODO threading and pystack is not fully supported, for now just make a small stack
    mp_obj_t mini_pystack[128];
    mp_pystack_init(mini_pystack, &mini_pystack[128]);
    #endif

    MP_THREAD_GIL_ENTER();

    // signal that we are set up and running
    mp_thread_start();

    // TODO set more thread-specific state here:
    //  cur_exception (root pointer)

    DEBUG_printf("[thread] start ts=%p args=%p stack=%p\n", &ts, &args, MP_STATE_THREAD(stack_top));

    do {
        nlr_buf_t nlr;
        nlr.ret_val = NULL;
        if (nlr_push(&nlr) == 0) {
            mp_call_function_n_kw(args->fun, args->n_args, args->n_kw, args->args);
            nlr_pop();
            break;
        }
        mp_obj_base_t *exc = (mp_obj_base_t *)nlr.ret_val;
        if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(exc->type), MP_OBJ_FROM_PTR(&mp_type_SystemExit))) {
            break;
        }
        mp_printf(MICROPY_ERROR_PRINTER, "Unhandled exception in thread started by ");
        mp_obj_print_helper(MICROPY_ERROR_PRINTER, args->fun, PRINT_REPR);
        mp_printf(MICROPY_ERROR_PRINTER, "\n");
        mp_obj_print_exception(MICROPY_ERROR_PRINTER, MP_OBJ_FROM_PTR(exc));
        if (args->auto_resume) {
            mp_printf(&mp_plat_print, "\nThe thread will automatically resumed after 500 milliseconds.\n");
            mp_hal_delay_ms(500);
        }
    } while (args->auto_resume);

    DEBUG_printf("[thread] finish ts=%p\n", &ts);

    // signal that we are finished
    mp_thread_finish();

    MP_THREAD_GIL_EXIT();

    return NULL;
}

static mp_obj_t mod_thread_start_new_thread(size_t n_args, const mp_obj_t *args) {
    // This structure holds the Python function and arguments for thread entry.
    // We copy all arguments into this structure to keep ownership of them.
    // We must be very careful about root pointers because this pointer may
    // disappear from our address space before the thread is created.
    thread_entry_args_t *th_args;

    // get positional arguments
    size_t pos_args_len;
    mp_obj_t *pos_args_items;
    mp_obj_get_array(args[1], &pos_args_len, &pos_args_items);

    const char *thread_name = "mp_thread";
    if (n_args == 3 && mp_obj_is_str(args[2])) {
        thread_name = mp_obj_str_get_str(args[2]);
    }

    // Preserve the legacy positional/name, keyword and auto-resume forms.
    if (n_args == 2 || n_args == 3) {
        // just position arguments
        th_args = m_new_obj_var(thread_entry_args_t, args, mp_obj_t, pos_args_len);
        th_args->n_kw = 0;
        th_args->auto_resume = false;
    } else {
        if (!mp_obj_is_type(args[2], &mp_type_dict) && !mp_obj_is_bool(args[2])) {
            mp_raise_TypeError(MP_ERROR_TEXT("expecting a dict for keyword args or a bool for thread auto resume flag"));
        }
        if (mp_obj_is_type(args[2], &mp_type_dict)) {
            mp_map_t *map = &((mp_obj_dict_t *)MP_OBJ_TO_PTR(args[2]))->map;
            th_args = m_new_obj_var(thread_entry_args_t, args, mp_obj_t, pos_args_len + 2 * map->used);
            th_args->n_kw = map->used;
            th_args->auto_resume = false;
            for (size_t i = 0, n = pos_args_len; i < map->alloc; ++i) {
                if (mp_map_slot_is_filled(map, i)) {
                    th_args->args[n++] = map->table[i].key;
                    th_args->args[n++] = map->table[i].value;
                }
            }
        } else {
            th_args = m_new_obj_var(thread_entry_args_t, args, mp_obj_t, pos_args_len);
            th_args->n_kw = 0;
            th_args->auto_resume = mp_obj_is_true(args[2]);
        }
    }
    if (n_args == 4 && mp_obj_is_type(args[2], &mp_type_dict)) {
        if (!mp_obj_is_bool(args[3])) {
            mp_raise_TypeError(MP_ERROR_TEXT("expecting a bool for thread auto resume flag"));
        }
        th_args->auto_resume = mp_obj_is_true(args[3]);
    }

    // copy across the positional arguments
    th_args->n_args = pos_args_len;
    memcpy(th_args->args, pos_args_items, pos_args_len * sizeof(mp_obj_t));

    // pass our locals and globals into the new thread
    th_args->dict_locals = mp_locals_get();
    th_args->dict_globals = mp_globals_get();

    // set the stack size to use
    th_args->stack_size = thread_stack_size;

    // set the function for thread entry
    th_args->fun = args[0];

    // spawn the thread!
    mp_uint_t task_id = mp_thread_create_named(thread_entry, th_args, &th_args->stack_size, thread_name);
    return mp_obj_new_int_from_uint(mp_thread_legacy_get_node_from_task_id(task_id));
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(mod_thread_start_new_thread_obj, 2, 5, mod_thread_start_new_thread);

static mp_obj_t mod_thread_exit(void) {
    mp_raise_type(&mp_type_SystemExit);
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_thread_exit_obj, mod_thread_exit);

static mp_obj_t mod_thread_allocate_lock(void) {
    return MP_OBJ_FROM_PTR(mp_obj_new_thread_lock());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_thread_allocate_lock_obj, mod_thread_allocate_lock);

static mp_obj_t mod_thread_allocate_semphore(mp_obj_t maxcount_in) {
    return MP_OBJ_FROM_PTR(mp_obj_new_thread_semphore((uint32_t)mp_obj_get_int_truncated(maxcount_in)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_thread_allocate_semphore_obj, mod_thread_allocate_semphore);

static mp_obj_t mod_thread_get_heap_size(void) {
    return mp_obj_new_int_from_uint(mp_thread_legacy_get_heap_size());
}
static MP_DEFINE_CONST_FUN_OBJ_0(mod_thread_get_heap_size_obj, mod_thread_get_heap_size);

static mp_obj_t mod_thread_stop_thread(mp_obj_t node_in) {
    mp_uint_t node = mp_obj_get_int_truncated(node_in);
    if (node == 0) {
        node = mp_thread_legacy_get_ident();
    }
    int ret = mp_thread_legacy_stop(node);
    if (ret == -2) {
        mp_raise_ValueError(MP_ERROR_TEXT("Deleting the main thread is not allowed!"));
    }
    if (ret != 0) {
        mp_raise_msg_varg(&mp_type_ValueError, MP_ERROR_TEXT("No thread with thread id %lu was found!"), (uint32_t)node);
    }
    return mp_obj_new_int(0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_thread_stop_thread_obj, mod_thread_stop_thread);

static mp_obj_t mod_thread_is_running(mp_obj_t node_in) {
    return mp_obj_new_bool(mp_thread_legacy_is_running(mp_obj_get_int_truncated(node_in)));
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_thread_is_running_obj, mod_thread_is_running);

static const mp_rom_map_elem_t mp_module_thread_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR__thread) },
    { MP_ROM_QSTR(MP_QSTR_LockType), MP_ROM_PTR(&mp_type_thread_lock) },
    { MP_ROM_QSTR(MP_QSTR_get_ident), MP_ROM_PTR(&mod_thread_get_ident_obj) },
    { MP_ROM_QSTR(MP_QSTR_stack_size), MP_ROM_PTR(&mod_thread_stack_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_start_new_thread), MP_ROM_PTR(&mod_thread_start_new_thread_obj) },
    { MP_ROM_QSTR(MP_QSTR_exit), MP_ROM_PTR(&mod_thread_exit_obj) },
    { MP_ROM_QSTR(MP_QSTR_allocate_lock), MP_ROM_PTR(&mod_thread_allocate_lock_obj) },
    { MP_ROM_QSTR(MP_QSTR_allocate_semphore), MP_ROM_PTR(&mod_thread_allocate_semphore_obj) },
    { MP_ROM_QSTR(MP_QSTR_allocate_semaphore), MP_ROM_PTR(&mod_thread_allocate_semphore_obj) },
    { MP_ROM_QSTR(MP_QSTR_delete_lock), MP_ROM_PTR(&mod_thread_delete_lock_obj) },
    { MP_ROM_QSTR(MP_QSTR_delete_semphore), MP_ROM_PTR(&mod_thread_delete_semphore_obj) },
    { MP_ROM_QSTR(MP_QSTR_delete_semaphore), MP_ROM_PTR(&mod_thread_delete_semphore_obj) },
    { MP_ROM_QSTR(MP_QSTR_get_heap_size), MP_ROM_PTR(&mod_thread_get_heap_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_stop_thread), MP_ROM_PTR(&mod_thread_stop_thread_obj) },
    { MP_ROM_QSTR(MP_QSTR_threadIsRunning), MP_ROM_PTR(&mod_thread_is_running_obj) },
};

static MP_DEFINE_CONST_DICT(mp_module_thread_globals, mp_module_thread_globals_table);

const mp_obj_module_t mp_module_thread = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_thread_globals,
};

MP_REGISTER_MODULE(MP_QSTR__thread, mp_module_thread);

#endif // MICROPY_PY_THREAD
