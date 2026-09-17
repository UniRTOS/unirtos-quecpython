#include <stdint.h>
#include <string.h>

#include "py/mphal.h"
#include "py/mpthread.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "shared/runtime/interrupt_char.h"
#include "qosa_rtc.h"
#include "qosa_sys.h"
#include "qosa_uart.h"

static qosa_sem_t qpy_stdin_sem = QOSA_NULL;
static qosa_uart_port_number_e qpy_stdio_port = (qosa_uart_port_number_e)CONFIG_QPY_REPL_PORT;
static volatile int qpy_stdio_failed;
static volatile int qpy_stdio_ready;
static uint32_t qpy_random_state = 0x51f15e2d;

#if MICROPY_QPY_MODULE_SMS
extern void qpy_sms_poll_pending(void);
#endif
#if MICROPY_QPY_MODULE_DATACALL
extern void qpy_dial_poll_pending(void);
#endif
#if MICROPY_QPY_MACHINE_TIMER
extern void qpy_machine_timer_poll_pending(void);
#endif
#if MICROPY_QPY_MACHINE_RTC
extern void qpy_machine_rtc_poll_pending(void);
#endif
#if MICROPY_QPY_MACHINE_EXTINT
extern void qpy_machine_extint_poll_pending(void);
#endif
#if MICROPY_QPY_MACHINE_UART
extern void qpy_machine_uart_poll_pending(void);
#endif
#if MICROPY_QPY_MACHINE_KEY
extern void qpy_machine_key_poll_pending(void);
#endif
#if MICROPY_QPY_MISC_USB
extern void qpy_misc_usb_poll_pending(void);
#endif
#if MICROPY_QPY_MISC_POWERKEY
extern void qpy_misc_powerkey_poll_pending(void);
#endif
#if MICROPY_QPY_MODULE_OSTIMER
extern void qpy_ostimer_poll_pending(void);
#endif
#if MICROPY_QPY_MODULE_FOTA
extern void qpy_fota_poll_pending(void);
#endif


static void qpy_uart_callback(qosa_uart_cb_param_t *param) {
    if (param != QOSA_NULL && (param->event_id & QOSA_UART_EVENT_RX_INDICATE) && qpy_stdin_sem != QOSA_NULL) {
        qosa_sem_release(qpy_stdin_sem);
    }
}

static int qpy_stdio_read_char(unsigned char *ch) {
    if (!qpy_stdio_ready || qosa_uart_read_available(qpy_stdio_port) <= 0) {
        return 0;
    }
    return qosa_uart_read(qpy_stdio_port, ch, 1) == 1;
}

void mp_hal_stdio_init(void) {
    if (qpy_stdio_ready || qpy_stdio_failed) {
        return;
    }

    if (qosa_sem_create_ex(&qpy_stdin_sem, 0, 8) != QOSA_OK) {
        qpy_stdio_failed = 1;
        return;
    }

    qosa_uart_status_monitor_t monitor = {0};
    monitor.callback = qpy_uart_callback;
    monitor.event_mask = QOSA_UART_EVENT_RX_INDICATE;
    if (qosa_uart_register_cb(qpy_stdio_port, &monitor) != QOSA_UART_SUCCESS) {
        qpy_stdio_failed = 1;
        return;
    }

    qosa_uart_config_t cfg = {0};
    cfg.baudrate = CONFIG_QPY_REPL_BAUD;
    cfg.data_bit = QOSA_UART_DATABIT_8;
    cfg.stop_bit = QOSA_UART_STOP_1;
    cfg.parity_bit = QOSA_UART_PARITY_NONE;
    cfg.flow_ctrl = QOSA_FC_NONE;
    if (qosa_uart_ioctl(qpy_stdio_port, QOSA_UART_IOCTL_SET_DCB_CFG, &cfg) != QOSA_UART_SUCCESS) {
        qpy_stdio_failed = 1;
        return;
    }
    qosa_uart_error_e open_rc = qosa_uart_open(qpy_stdio_port);
    if (open_rc != QOSA_UART_SUCCESS && open_rc != QOSA_UART_OPEN_REPEAT_ERR) {
        qpy_stdio_failed = 1;
        return;
    }

    qpy_stdio_ready = 1;
}

int qpy_stdio_is_ready(void) {
    return qpy_stdio_ready;
}

int mp_hal_stdin_rx_chr(void) {
    unsigned char ch = 0;
    for (;;) {
        if (qpy_stdio_read_char(&ch)) {
            #if MICROPY_KBD_EXCEPTION
            if (ch == mp_interrupt_char) {
                mp_sched_keyboard_interrupt();
                mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
                continue;
            }
            #endif
            return ch;
        }
        MP_THREAD_GIL_EXIT();
        qosa_sem_wait(qpy_stdin_sem, QOSA_WAIT_FOREVER);
        MP_THREAD_GIL_ENTER();
        #if MICROPY_QPY_MODULE_SMS
        qpy_sms_poll_pending();
        #endif
        #if MICROPY_QPY_MODULE_DATACALL
        qpy_dial_poll_pending();
        #endif
        #if MICROPY_QPY_MACHINE_TIMER
        qpy_machine_timer_poll_pending();
        #endif
        #if MICROPY_QPY_MACHINE_RTC
        qpy_machine_rtc_poll_pending();
        #endif
        #if MICROPY_QPY_MACHINE_EXTINT
        qpy_machine_extint_poll_pending();
        #endif
        #if MICROPY_QPY_MACHINE_UART
        qpy_machine_uart_poll_pending();
        #endif
        #if MICROPY_QPY_MACHINE_KEY
        qpy_machine_key_poll_pending();
        #endif
        #if MICROPY_QPY_MISC_USB
        qpy_misc_usb_poll_pending();
        #endif
        #if MICROPY_QPY_MISC_POWERKEY
        qpy_misc_powerkey_poll_pending();
        #endif
        #if MICROPY_QPY_MODULE_OSTIMER
        qpy_ostimer_poll_pending();
        #endif
        #if MICROPY_QPY_MODULE_FOTA
        qpy_fota_poll_pending();
        #endif
        mp_handle_pending(MP_HANDLE_PENDING_CALLBACKS_AND_EXCEPTIONS);
    }
}

void mp_hal_stdio_wake(void) {
    if (qpy_stdin_sem != QOSA_NULL) {
        qosa_sem_release(qpy_stdin_sem);
    }
}

void mp_hal_stdout_tx_str(const char *str) {
    mp_hal_stdout_tx_strn(str, strlen(str));
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, size_t len) {
    size_t remaining = len;
    if (!qpy_stdio_ready) {
        return len;
    }
    while (remaining > 0) {
        unsigned int chunk = remaining > 1024 ? 1024 : (unsigned int)remaining;
        int written = qosa_uart_write(qpy_stdio_port, (unsigned char *)str, chunk);
        if (written <= 0) {
            break;
        }
        str += written;
        remaining -= written;
    }
    return len - remaining;
}

uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    uintptr_t ret = 0;
    if (poll_flags & MP_STREAM_POLL_RD) {
        if (qpy_stdio_ready && qosa_uart_read_available(qpy_stdio_port) > 0) {
            ret |= MP_STREAM_POLL_RD;
        }
    }
    if (poll_flags & MP_STREAM_POLL_WR) {
        ret |= MP_STREAM_POLL_WR;
    }
    return ret;
}

mp_uint_t mp_hal_ticks_ms(void) {
    return qosa_get_system_tick_cnt();
}

mp_uint_t mp_hal_ticks_us(void) {
    return mp_hal_ticks_ms() * 1000U;
}

mp_uint_t mp_hal_ticks_cpu(void) {
    return mp_hal_ticks_us();
}

uint64_t mp_hal_time_ns(void) {
    return (uint64_t)qosa_get_system_time_microseconds() * 1000ULL;
}

void mp_hal_delay_ms(mp_uint_t ms) {
    MP_THREAD_GIL_EXIT();
    qosa_task_sleep_ms((qosa_uint32_t)ms);
    MP_THREAD_GIL_ENTER();
}

void mp_hal_delay_us(mp_uint_t us) {
    mp_hal_delay_ms((us + 999U) / 1000U);
}

uint32_t mp_hal_get_random(void) {
    qpy_random_state ^= mp_hal_ticks_us() + 0x9e3779b9U;
    qpy_random_state = qpy_random_state * 1664525U + 1013904223U;
    return qpy_random_state;
}
