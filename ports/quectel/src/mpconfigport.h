#ifndef MICROPY_INCLUDED_QUECTEL_UNIRTOS_MPCONFIGPORT_H
#define MICROPY_INCLUDED_QUECTEL_UNIRTOS_MPCONFIGPORT_H

#include <stdint.h>
#if __has_include("qcm_proj_config.h")
#include "qcm_proj_config.h"
#endif
#include "mpconfigboard.h"

uint32_t mp_hal_get_random(void);

#ifndef CONFIG_QPY_REPL_PORT
#define CONFIG_QPY_REPL_PORT MICROPY_QPY_REPL_PORT
#endif
#ifndef CONFIG_QPY_REPL_BAUD
#define CONFIG_QPY_REPL_BAUD MICROPY_QPY_REPL_BAUD
#endif
#ifndef CONFIG_QPY_GC_HEAP_SIZE
#define CONFIG_QPY_GC_HEAP_SIZE MICROPY_QPY_GC_HEAP_SIZE
#endif
#ifndef CONFIG_QPY_MAIN_TASK_STACK_SIZE
#define CONFIG_QPY_MAIN_TASK_STACK_SIZE MICROPY_QPY_MAIN_TASK_STACK_SIZE
#endif
#ifndef CONFIG_QPY_THREAD_STACK_SIZE
#define CONFIG_QPY_THREAD_STACK_SIZE MICROPY_QPY_THREAD_STACK_SIZE
#endif
#ifndef CONFIG_QPY_USRFS_AUTO_FORMAT
#define CONFIG_QPY_USRFS_AUTO_FORMAT MICROPY_QPY_USRFS_AUTO_FORMAT
#endif

#define MICROPY_CONFIG_ROM_LEVEL (MICROPY_CONFIG_ROM_LEVEL_EXTRA_FEATURES)
#define MICROPY_ENABLE_COMPILER (1)
#define MICROPY_ENABLE_GC (1)
#define MICROPY_ENABLE_FINALISER (1)
#define MICROPY_ENABLE_PYSTACK (0)
#define MICROPY_ENABLE_SCHEDULER (1)
#define MICROPY_SCHEDULER_DEPTH (8)
#define MICROPY_ENABLE_EXTERNAL_IMPORT (1)
#define MICROPY_READER_POSIX (0)
#define MICROPY_READER_VFS (1)
#define MICROPY_HELPER_REPL (1)
#define MICROPY_REPL_AUTO_INDENT (1)
#define MICROPY_KBD_EXCEPTION (1)
#define MICROPY_ALLOC_PATH_MAX (192)
#define MICROPY_USE_INTERNAL_PRINTF (0)
#define MICROPY_FLOAT_IMPL (MICROPY_FLOAT_IMPL_FLOAT)
#define MICROPY_LONGINT_IMPL (MICROPY_LONGINT_IMPL_MPZ)
#define MICROPY_OBJ_REPR (MICROPY_OBJ_REPR_C)
#define MICROPY_ERROR_REPORTING (MICROPY_ERROR_REPORTING_NORMAL)
#define MICROPY_MALLOC_USES_ALLOCATED_SIZE (0)
#define MP_SSIZE_MAX (0x7fffffff)
#define MICROPY_EVENT_POLL_HOOK do { } while (0);

#define MICROPY_PY_SYS (1)
#define MICROPY_PY_SYS_PATH (1)
#define MICROPY_PY_SYS_ARGV (1)
#define MICROPY_PY_GC (1)
#define MICROPY_PY_IO (1)
#define MICROPY_PY_IO_FILEIO (1)
#define MICROPY_PY_COLLECTIONS (MICROPY_QPY_MODULE_UCOLLECTIONS)
#define MICROPY_PY_COLLECTIONS_DEQUE (MICROPY_QPY_MODULE_UCOLLECTIONS)
#define MICROPY_PY_COLLECTIONS_ORDEREDDICT (MICROPY_QPY_MODULE_UCOLLECTIONS)
#define MICROPY_PY_ERRNO (MICROPY_QPY_MODULE_UERRNO)
#define MICROPY_PY_ERRNO_ERRORCODE (MICROPY_QPY_MODULE_UERRNO)
#define MICROPY_PY_STRUCT (MICROPY_QPY_MODULE_USTRUCT)
#define MICROPY_PY_MATH (MICROPY_QPY_MODULE_MATH)
#define MICROPY_PY_MATH_CONSTANTS (MICROPY_QPY_MODULE_MATH)
#define MICROPY_PY_JSON (MICROPY_QPY_MODULE_UJSON)
#define MICROPY_PY_JSON_SEPARATORS (0)
#define MICROPY_PY_BINASCII (MICROPY_QPY_MODULE_UBINASCII)
#define MICROPY_PY_BINASCII_CRC32 (0)
#define MICROPY_PY_RANDOM (MICROPY_QPY_MODULE_URANDOM)
#define MICROPY_PY_RANDOM_EXTRA_FUNCS (MICROPY_QPY_MODULE_URANDOM)
#define MICROPY_PY_RANDOM_SEED_INIT_FUNC (mp_hal_get_random())
#define MICROPY_PY_SELECT (MICROPY_QPY_MODULE_USELECT)
#define MICROPY_PY_SELECT_SELECT (0)
#define MICROPY_PY_SELECT_POSIX_OPTIMISATIONS (0)
#define MICROPY_PY_OS (0)
#define MICROPY_PY_TIME (MICROPY_QPY_MODULE_UTIME)
#define MICROPY_PY_TIME_TICKS (MICROPY_QPY_MODULE_UTIME)
#define MICROPY_PY_TIME_GMTIME_LOCALTIME_MKTIME (MICROPY_QPY_MODULE_UTIME)
#define MICROPY_PY_TIME_TIME_TIME_NS (MICROPY_QPY_MODULE_UTIME)
#define MICROPY_QPY_LEGACY_UTIME_COMPAT (1)
#define MICROPY_PY_TIME_INCLUDEFILE "qpy_time_port.h"
#define MICROPY_QPY_LEGACY_SYS_COMPAT (1)
#undef MICROPY_PY_SYS_PLATFORM
#define MICROPY_PY_SYS_PLATFORM MICROPY_HW_BOARD_NAME
#define MICROPY_PY_SYS_PS1_PS2 (0)
#define MICROPY_PY_THREAD (1)
#define MICROPY_PY_THREAD_GIL (1)
#define MICROPY_PY_THREAD_RECURSIVE_MUTEX (1)
#define MICROPY_PY_MACHINE (0)
#define MICROPY_PY_BUILTINS_HELP (1)
#define MICROPY_PY_BUILTINS_OPEN (1)
#define MICROPY_PY_BUILTINS_MEMORYVIEW (1)
#define MICROPY_PY_UCTYPES (0)
#define MICROPY_PY_UZLIB (1)


#define MICROPY_PORT_ROOT_POINTERS

extern const struct _mp_obj_module_t mp_module_uos_qosa;
extern const struct _mp_obj_module_t mp_module_machine_qosa;
extern const struct _mp_obj_module_t mp_module_time;
extern const struct _mp_obj_module_t mp_module_json;
extern const struct _mp_obj_module_t mp_module_binascii;
extern const struct _mp_obj_module_t mp_module_random;
extern const struct _mp_obj_module_t mp_module_struct;
extern const struct _mp_obj_module_t mp_module_errno;
extern const struct _mp_obj_module_t mp_module_select;
extern const struct _mp_obj_module_t mp_module_collections;
extern const struct _mp_obj_module_t mp_module_math;
extern const struct _mp_obj_module_t mp_module_uzlib;
extern const struct _mp_obj_module_t mp_module_atcmd;
extern const struct _mp_obj_module_t mp_module_example;
extern const struct _mp_obj_module_t mp_module_modem;
extern const struct _mp_obj_module_t mp_module_net;
extern const struct _mp_obj_module_t mp_module_sim;
extern const struct _mp_obj_module_t mp_module_sms;
extern const struct _mp_obj_module_t mp_module_dial;
extern const struct _mp_obj_module_t mp_module_misc;
extern const struct _mp_obj_module_t mp_module_usocket;
extern const struct _mp_obj_fun_builtin_var_t mp_builtin_open_obj;

#if MICROPY_QPY_MODULE_UOS
#define QPY_BUILTIN_MODULE_UOS \
    { MP_ROM_QSTR(MP_QSTR_uos), MP_ROM_PTR(&mp_module_uos_qosa) }, \
    { MP_ROM_QSTR(MP_QSTR_os), MP_ROM_PTR(&mp_module_uos_qosa) },
#else
#define QPY_BUILTIN_MODULE_UOS
#endif

#if MICROPY_QPY_MODULE_UTIME
#define QPY_BUILTIN_MODULE_UTIME \
    { MP_ROM_QSTR(MP_QSTR_utime), MP_ROM_PTR(&mp_module_time) }, \
    { MP_ROM_QSTR(MP_QSTR_time), MP_ROM_PTR(&mp_module_time) },
#else
#define QPY_BUILTIN_MODULE_UTIME
#endif

#if MICROPY_QPY_MODULE_MACHINE
#define QPY_BUILTIN_MODULE_MACHINE \
    { MP_ROM_QSTR(MP_QSTR_machine), MP_ROM_PTR(&mp_module_machine_qosa) },
#else
#define QPY_BUILTIN_MODULE_MACHINE
#endif

#if MICROPY_QPY_MODULE_ATCMD
#define QPY_BUILTIN_MODULE_ATCMD \
    { MP_ROM_QSTR(MP_QSTR_atcmd), MP_ROM_PTR(&mp_module_atcmd) },
#else
#define QPY_BUILTIN_MODULE_ATCMD
#endif

#if MICROPY_QPY_MODULE_EXAMPLE
#define QPY_BUILTIN_MODULE_EXAMPLE \
    { MP_ROM_QSTR(MP_QSTR_example), MP_ROM_PTR(&mp_module_example) },
#else
#define QPY_BUILTIN_MODULE_EXAMPLE
#endif

#if MICROPY_QPY_MODULE_USOCKET
#define QPY_BUILTIN_MODULE_USOCKET \
    { MP_ROM_QSTR(MP_QSTR_usocket), MP_ROM_PTR(&mp_module_usocket) }, \
    { MP_ROM_QSTR(MP_QSTR_socket), MP_ROM_PTR(&mp_module_usocket) },
#else
#define QPY_BUILTIN_MODULE_USOCKET
#endif

#if MICROPY_QPY_MODULE_MODEM
#define QPY_BUILTIN_MODULE_MODEM \
    { MP_ROM_QSTR(MP_QSTR_modem), MP_ROM_PTR(&mp_module_modem) },
#else
#define QPY_BUILTIN_MODULE_MODEM
#endif

#if MICROPY_QPY_MODULE_NET
#define QPY_BUILTIN_MODULE_NET \
    { MP_ROM_QSTR(MP_QSTR_net), MP_ROM_PTR(&mp_module_net) },
#else
#define QPY_BUILTIN_MODULE_NET
#endif

#if MICROPY_QPY_MODULE_SIM
#define QPY_BUILTIN_MODULE_SIM \
    { MP_ROM_QSTR(MP_QSTR_sim), MP_ROM_PTR(&mp_module_sim) },
#else
#define QPY_BUILTIN_MODULE_SIM
#endif

#if MICROPY_QPY_MODULE_SMS
#define QPY_BUILTIN_MODULE_SMS \
    { MP_ROM_QSTR(MP_QSTR_sms), MP_ROM_PTR(&mp_module_sms) },
#else
#define QPY_BUILTIN_MODULE_SMS
#endif

#if MICROPY_QPY_MODULE_DATACALL
#define QPY_BUILTIN_MODULE_DATACALL \
    { MP_ROM_QSTR(MP_QSTR_dial), MP_ROM_PTR(&mp_module_dial) },
#else
#define QPY_BUILTIN_MODULE_DATACALL
#endif

#if MICROPY_QPY_MODULE_MISC
#define QPY_BUILTIN_MODULE_MISC \
    { MP_ROM_QSTR(MP_QSTR_misc), MP_ROM_PTR(&mp_module_misc) },
#else
#define QPY_BUILTIN_MODULE_MISC
#endif

#if MICROPY_QPY_MODULE_UJSON
#define QPY_BUILTIN_MODULE_UJSON \
    { MP_ROM_QSTR(MP_QSTR_ujson), MP_ROM_PTR(&mp_module_json) }, \
    { MP_ROM_QSTR(MP_QSTR_json), MP_ROM_PTR(&mp_module_json) },
#else
#define QPY_BUILTIN_MODULE_UJSON
#endif

#if MICROPY_QPY_MODULE_UBINASCII
#define QPY_BUILTIN_MODULE_UBINASCII \
    { MP_ROM_QSTR(MP_QSTR_ubinascii), MP_ROM_PTR(&mp_module_binascii) }, \
    { MP_ROM_QSTR(MP_QSTR_binascii), MP_ROM_PTR(&mp_module_binascii) },
#else
#define QPY_BUILTIN_MODULE_UBINASCII
#endif

#if MICROPY_QPY_MODULE_USTRUCT
#define QPY_BUILTIN_MODULE_USTRUCT \
    { MP_ROM_QSTR(MP_QSTR_ustruct), MP_ROM_PTR(&mp_module_struct) }, \
    { MP_ROM_QSTR(MP_QSTR_struct), MP_ROM_PTR(&mp_module_struct) },
#else
#define QPY_BUILTIN_MODULE_USTRUCT
#endif

#if MICROPY_QPY_MODULE_URANDOM
#define QPY_BUILTIN_MODULE_URANDOM \
    { MP_ROM_QSTR(MP_QSTR_urandom), MP_ROM_PTR(&mp_module_random) }, \
    { MP_ROM_QSTR(MP_QSTR_random), MP_ROM_PTR(&mp_module_random) },
#else
#define QPY_BUILTIN_MODULE_URANDOM
#endif

#if MICROPY_QPY_MODULE_UERRNO
#define QPY_BUILTIN_MODULE_UERRNO \
    { MP_ROM_QSTR(MP_QSTR_uerrno), MP_ROM_PTR(&mp_module_errno) }, \
    { MP_ROM_QSTR(MP_QSTR_errno), MP_ROM_PTR(&mp_module_errno) },
#else
#define QPY_BUILTIN_MODULE_UERRNO
#endif

#if MICROPY_QPY_MODULE_USELECT
#define QPY_BUILTIN_MODULE_USELECT \
    { MP_ROM_QSTR(MP_QSTR_uselect), MP_ROM_PTR(&mp_module_select) }, \
    { MP_ROM_QSTR(MP_QSTR_select), MP_ROM_PTR(&mp_module_select) },
#else
#define QPY_BUILTIN_MODULE_USELECT
#endif

#if MICROPY_QPY_MODULE_UCOLLECTIONS
#define QPY_BUILTIN_MODULE_UCOLLECTIONS \
    { MP_ROM_QSTR(MP_QSTR_ucollections), MP_ROM_PTR(&mp_module_collections) }, \
    { MP_ROM_QSTR(MP_QSTR_collections), MP_ROM_PTR(&mp_module_collections) },
#else
#define QPY_BUILTIN_MODULE_UCOLLECTIONS
#endif

#if MICROPY_QPY_MODULE_MATH
#define QPY_BUILTIN_MODULE_MATH \
    { MP_ROM_QSTR(MP_QSTR_math), MP_ROM_PTR(&mp_module_math) },
#else
#define QPY_BUILTIN_MODULE_MATH
#endif

#define QPY_BUILTIN_MODULE_UZLIB \
    { MP_ROM_QSTR(MP_QSTR_uzlib), MP_ROM_PTR(&mp_module_uzlib) },
#define MICROPY_PORT_BUILTIN_MODULES \
    QPY_BUILTIN_MODULE_UOS \
    QPY_BUILTIN_MODULE_UTIME \
    QPY_BUILTIN_MODULE_MACHINE \
    QPY_BUILTIN_MODULE_UJSON \
    QPY_BUILTIN_MODULE_UBINASCII \
    QPY_BUILTIN_MODULE_USTRUCT \
    QPY_BUILTIN_MODULE_URANDOM \
    QPY_BUILTIN_MODULE_UERRNO \
    QPY_BUILTIN_MODULE_USELECT \
    QPY_BUILTIN_MODULE_UCOLLECTIONS \
    QPY_BUILTIN_MODULE_MATH \
    QPY_BUILTIN_MODULE_UZLIB \
    QPY_BUILTIN_MODULE_ATCMD \
    QPY_BUILTIN_MODULE_EXAMPLE \
    QPY_BUILTIN_MODULE_MODEM \
    QPY_BUILTIN_MODULE_NET \
    QPY_BUILTIN_MODULE_SIM \
    QPY_BUILTIN_MODULE_SMS \
    QPY_BUILTIN_MODULE_DATACALL \
    QPY_BUILTIN_MODULE_USOCKET \
    QPY_BUILTIN_MODULE_MISC
#define MICROPY_PORT_BUILTINS \
    { MP_ROM_QSTR(MP_QSTR_open), MP_ROM_PTR(&mp_builtin_open_obj) },

#define MP_STATE_PORT MP_STATE_VM

typedef int mp_int_t;
typedef unsigned int mp_uint_t;
typedef long mp_off_t;

#endif
