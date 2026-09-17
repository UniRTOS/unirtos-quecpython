#ifndef MICROPY_INCLUDED_QUECTEL_UNIRTOS_QUECPYTHON_H
#define MICROPY_INCLUDED_QUECTEL_UNIRTOS_QUECPYTHON_H

#include <stddef.h>
#include "include/quecpython.h"

void qpy_thread_register_main_stack(void *stack_base, size_t stack_len);

#endif
