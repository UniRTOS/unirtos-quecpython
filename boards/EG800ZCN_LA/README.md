# EG800ZCN_LA profile

This is the only board profile supported by component v1.1.0.

Runtime budgets and interfaces are defined in `mpconfigboard.h`:

| Setting | Value |
| --- | --- |
| REPL | UART7, 115200 baud |
| stdin ring buffer | 8192 bytes |
| GC heap | 512 KiB |
| main task stack | 32 KiB |
| thread stack | 8 KiB |
| user filesystem | 1 MiB |

The release acceptance matrix covers Pin/GPIO, UART, I2C, SPI, Timer, RTC, WDT, ExtInt, Key, ADC, PWM, power-key, temperature, USB, and USB networking where the corresponding board macro is enabled.

