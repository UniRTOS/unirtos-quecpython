#ifndef MICROPY_INCLUDED_QUECTEL_UNIRTOS_MPHALPORT_H
#define MICROPY_INCLUDED_QUECTEL_UNIRTOS_MPHALPORT_H

#include <stdint.h>
#include "py/obj.h"

void mp_hal_stdio_init(void);
int mp_hal_stdin_rx_chr(void);
void mp_hal_stdout_tx_str(const char *str);
mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len);
void mp_hal_stdio_wake(void);
uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags);
mp_uint_t mp_hal_ticks_ms(void);
mp_uint_t mp_hal_ticks_us(void);
mp_uint_t mp_hal_ticks_cpu(void);
void mp_hal_delay_ms(mp_uint_t ms);
void mp_hal_delay_us(mp_uint_t us);
uint32_t mp_hal_get_random(void);
void mp_hal_set_interrupt_char(int c);

#define mp_hal_stdout_tx_strn_cooked(str, len) mp_hal_stdout_tx_strn((str), (len))
#define MICROPY_BEGIN_ATOMIC_SECTION() (mp_thread_unirtos_begin_atomic_section(), 0)
#define MICROPY_END_ATOMIC_SECTION(state) do { (void)(state); mp_thread_unirtos_end_atomic_section(); } while (0)

#endif
