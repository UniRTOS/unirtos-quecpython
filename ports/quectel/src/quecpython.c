#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "py/compile.h"
#include "py/runtime.h"
#include "py/objstr.h"
#include "py/gc.h"
#include "py/lexer.h"
#include "py/stackctrl.h"
#include "py/mphal.h"

extern void qpy_machine_uart_reset_all(void);
#include "py/mpthread.h"
#include "shared/readline/readline.h"
#include "shared/runtime/pyexec.h"
#include "qosa_sys.h"
#include "qosa_virtual_file.h"
#include "unirtos_app_init_registry.h"
#include "quecpython.h"
#include "mphalport.h"
#include "qpy_path.h"
#include "qpy_usrfs.h"

static uint8_t qpy_heap[CONFIG_QPY_GC_HEAP_SIZE] __attribute__((aligned(8)));
static uint8_t qpy_main_stack[(CONFIG_QPY_MAIN_TASK_STACK_SIZE + 7) & ~7] __attribute__((aligned(8)));
static qosa_task_t qpy_main_task = QOSA_NULL;
static void *qpy_stack_top;
static volatile int qpy_runtime_ready;
static char qpy_saved_dir[256];

static void qpy_save_softreset_vars(void) {
    mp_map_elem_t *elem = mp_map_lookup(&mp_globals_get()->map, MP_OBJ_NEW_QSTR(MP_QSTR_DIR), MP_MAP_LOOKUP);
    if (elem == NULL || !mp_obj_is_str_or_bytes(elem->value)) {
        qpy_saved_dir[0] = '\0';
        return;
    }

    size_t len = 0;
    const char *dir = mp_obj_str_get_data(elem->value, &len);
    if (len >= sizeof(qpy_saved_dir)) {
        len = sizeof(qpy_saved_dir) - 1;
    }
    memcpy(qpy_saved_dir, dir, len);
    qpy_saved_dir[len] = '\0';
}

static void qpy_restore_softreset_vars(void) {
    if (qpy_saved_dir[0] != '\0') {
        mp_store_global(MP_QSTR_DIR, mp_obj_new_str(qpy_saved_dir, strlen(qpy_saved_dir)));
    }
}
static void qpy_init_sys_path(void) {
    #if MICROPY_PY_SYS_PATH
    mp_sys_path = mp_obj_new_list(0, NULL);
    mp_obj_list_append(mp_sys_path, MP_OBJ_NEW_QSTR(MP_QSTR_));
    #if !MICROPY_QPY_LEGACY_SYS_COMPAT
    mp_obj_list_append(mp_sys_path, mp_obj_new_str(".frozen", 7));
    mp_obj_list_append(mp_sys_path, mp_obj_new_str("/usr", 4));
    #endif
    #endif
    #if MICROPY_PY_SYS_ARGV
    mp_obj_list_init(MP_OBJ_TO_PTR(mp_sys_argv), 0);
    #endif
}

static void qpy_run_boot(void) {
    qpy_path_init();
    #if defined(CONFIG_QPY_FROZEN_MODULES)
    pyexec_frozen_module("_boot.py", false);
    #endif
}

static void qpy_run_app_fota_boot(void) {
    #if MICROPY_QPY_FROZEN_APP_FOTA
    pyexec_frozen_module("app_fota_boot.py", false);
    #endif
}

static void qpy_main(void *arg) {
    (void)arg;
    qpy_stack_top = qpy_main_stack + sizeof(qpy_main_stack);
    qpy_thread_register_main_stack(qpy_main_stack, sizeof(qpy_main_stack));
    mp_stack_set_top(qpy_stack_top);
    mp_stack_set_limit(sizeof(qpy_main_stack) - 1024);

    mp_hal_stdio_init();
    if (!qpy_stdio_is_ready()) {
        mp_printf(&mp_plat_print, "QuecPython UART7 REPL initialization failed.\r\n");
        return;
    }

    if (qpy_usrfs_init() != 0) {
        mp_printf(&mp_plat_print, "QuecPython /usr CUST filesystem initialization failed.\r\n");
        return;
    }

    for (;;) {
        qpy_machine_uart_reset_all();
        gc_init(qpy_heap, qpy_heap + sizeof(qpy_heap));
        mp_thread_init();
        mp_init();
        qpy_init_sys_path();
        qpy_restore_softreset_vars();
        readline_init0();

        mp_printf(&mp_plat_print, "\r\nQuecPython on %s with MicroPython v1.28.0\r\n", MICROPY_HW_BOARD_NAME);

        qpy_run_boot();
        qpy_run_app_fota_boot();
        pyexec_file_if_exists("/usr/main.py");
        mp_printf(&mp_plat_print, "Entering friendly REPL. Use Ctrl-D to soft reset.\r\n");
        qpy_runtime_ready = 1;
        pyexec_friendly_repl();

        mp_printf(&mp_plat_print, "\r\nMPY: soft reboot\r\n");
        qpy_save_softreset_vars();
        qpy_runtime_ready = 0;
        qpy_machine_uart_reset_all();
        mp_deinit();
        mp_thread_deinit();
    }
}

void quecpython_init(void) {
    if (qpy_main_task != QOSA_NULL) {
        printf("QuecPython runtime is already initialized.\r\n");
        return;
    }
    void *tcb = qosa_malloc(qosa_task_get_tcb_min_size());
    if (tcb == QOSA_NULL) {
        printf("QuecPython task control block allocation failed.\r\n");
        return;
    }
    qosa_task_t task = QOSA_NULL;
    if (qosa_task_create_static(&task, qpy_main_stack, sizeof(qpy_main_stack), tcb,
            qosa_task_get_tcb_min_size(), QOSA_PRIORITY_NORMAL, "quecpython", qpy_main, QOSA_NULL) != QOSA_ERROR_OK) {
        qosa_free(tcb);
        printf("QuecPython runtime task creation failed.\r\n");
        return;
    }
    qpy_main_task = task;
}

int quecpython_is_running(void) {
    return qpy_main_task != QOSA_NULL && qpy_runtime_ready;
}

void gc_collect(void) {
    void *sp;
    gc_collect_start();
    if (qpy_stack_top != QOSA_NULL) {
        gc_collect_root(&sp, ((uintptr_t)qpy_stack_top - (uintptr_t)&sp) / sizeof(uintptr_t));
    }
    #if MICROPY_PY_THREAD
    mp_thread_gc_others();
    #endif
    gc_collect_end();
}

void nlr_jump_fail(void *val) {
    mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(val));
    for (;;) {
        qosa_task_sleep_ms(1000);
    }
}

void MP_NORETURN __fatal_error(const char *msg) {
    mp_printf(&mp_plat_print, "fatal: %s\r\n", msg);
    for (;;) {
        qosa_task_sleep_ms(1000);
    }
}

#ifndef NDEBUG
void MP_WEAK __assert_func(const char *file, int line, const char *func, const char *expr) {
    mp_printf(&mp_plat_print, "assert %s:%d %s %s\r\n", file, line, func, expr);
    __fatal_error("assert");
}
#endif

UNIRTOS_APP_EXPORT(700, "quecpython", quecpython_init);
