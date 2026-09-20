# unirtos-quecpython

[中文](README.zh.md) | English

`unirtos-quecpython` is the independently versioned UniRTOS library that provides the QuecPython runtime for EG800ZCN_LA applications. Use it through the UniRTOS library workflow; application and demo repositories should declare the dependency instead of copying the runtime source.

## Feature Description

The library packages the following as a single static library:

- MicroPython `v1.28.0` and the Quectel UniRTOS port
- QuecPython C modules, QOSA wrappers, QSTR generation, and frozen modules
- LittleFS support and the initial `customer_app2.bin` CUST image
- The `EG800ZCN_LA` board configuration and partition overlay

The runtime registers `quecpython_init()` in `.unirtos_app_init`; an application that declares this library does not create a second MicroPython task or call the initializer itself. It also does not provide product application code, peripheral examples, an LCD driver, or another board profile. Those belong in a demo or product repository.

## Compatibility

| Item | Supported version |
| --- | --- |
| Library | `1.1.0` |
| MicroPython baseline | `v1.28.0` |
| UniRTOS SDK | `1.0.5` |
| unirtos-cli | `1.0.20` |
| Board | `EG800ZCN_LA` |

The current library accepts only `EG800ZCN_LA`. The actual CMake target is `unirtos-quecpython`; `quecpython` remains a compatibility alias.

## Quick Start

### 1. Install the UniRTOS toolchain

- [Development Preparation](https://www.quectel.com.cn/unirtos/docs?docs_page=快速上手/开发准备/开发准备.html)
- [Install the Cross-Compilation Toolchain](https://www.quectel.com.cn/unirtos/docs?docs_page=快速上手/环境搭建/环境搭建.html)
- Install Python 3, Git, and `unirtos-cli`

Confirm that the CLI is available:

```bash
python --version
git --version
unirtos-cli version
```

### 2. Declare the library in an application

Add the library to the application's `env_config.json`:

```json
{
  "build": { "module": "EG800ZCN_LA" },
  "sdk": { "version": "1.0.5" },
  "libraries": {
    "list": [
      { "name": "unirtos-quecpython", "version": "1.1.0" }
    ]
  }
}
```

Then fetch the SDK and library and build the application:

```bash
unirtos-cli env-setup
unirtos-cli build
```

For an end-to-end reference project, use [`unirtos-quecpython-demos`](https://github.com/UniRTOS/unirtos-quecpython-demos):

```bash
unirtos-cli ls-demos
unirtos-cli new -r unirtos-quecpython-demos -v 1.0.0
cd unirtos-quecpython-demos-1.0.0
unirtos-cli env-setup
unirtos-cli build
```

## Build and Packaging Notes

The base `gccout.7z` used with this library must declare the following flash layout: OpenCPU `0x90000`, CUST `0x48000`, and LFS `0x79000`. Configuration stops with a clear error if the archive does not match.

When replacing `gccout.7z`, remove the extracted cache before rebuilding:

```bash
unirtos-cli clean
unirtos-cli build -m EG800ZCN_LA -v <firmware-version>
```

`-m` selects the board and `-v` names the resulting firmware release. They do not change the library's supported SDK or board.

The release directory is `qos_build/release/<firmware-version>/`. It includes `ap_application.bin`, `customer_app2.bin`, `at_command.hbinpkg`, and the FlashTool download configuration. The component adds the CUST image to the SDK package as `PKGFLX_CUST`; no image needs to be copied into `qos_tools`.

## Runtime Profile

- UART7 REPL at 115200 baud
- 512 KiB Python GC heap, 32 KiB main task stack, and 8 KiB Python thread stack
- LittleFS user filesystem with a separate 288 KiB initial CUST image
- Frozen `_boot.py` and the board-selected frozen module set

The EG800ZCN_LA profile enables `uos`, `machine`, `utime`, `usocket`, `net`, `sim`, `dataCall`, `sms`, `fota`, `atcmd`, `misc`, `modem`, `ostimer`, `ujson`, `ubinascii`, `ustruct`, `urandom`, `uerrno`, `uselect`, `ucollections`, and `math`. Its `machine` integration covers Pin, Timer, RTC, WDT, ExtInt, Key, I2C, SPI, and UART. Audio, LVGL, camera, BLE/BT, Ethernet, GNSS, and Wi-Fi are outside this library's scope.

## Public C API

Include `quecpython.h` when an application needs to inspect runtime state:

```c
void quecpython_init(void);
int quecpython_is_running(void);
int quecpython_exec_string(const char *code);
```

`quecpython_init()` is idempotent. The runtime starts automatically, so normal applications do not call it. `quecpython_exec_string()` returns `-1` before the runtime is ready or for invalid input; it must not be called directly from an arbitrary UniRTOS task without first marshalling work to the QuecPython runtime task.

## Repository Layout

```text
boards/EG800ZCN_LA/       board configuration, feature matrix, and CUST image
include/                  public C API
ports/quectel/            Quectel port, QOSA wrappers, C modules, frozen modules
third_party/micropython/  pinned MicroPython source
```

All copied upstream sources, QSTR files, frozen modules, and generated headers are emitted to the consuming application's build directory. The library does not modify UniRTOS SDK source files or call `add_apps_libraries()`.

## Common Commands

```bash
# List available library versions
unirtos-cli ls-libs

# Refresh SDK and declared external libraries
unirtos-cli env-setup

# Remove build output and the extracted gccout cache
unirtos-cli clean
```

## Technical Community

Technical Community: https://forumschinese.quectel.com/c/66-category/66

## Contribution Guidelines

- Run `env-setup`, `build`, and `clean` before submitting a change.
- Keep board, SDK, CLI, and MicroPython version changes explicit in this README.
- Do not add generated build output or an SDK copy to this repository.
- When changing the flash layout, CUST image, or runtime startup path, update the validation record and complete an EG800ZCN_LA hardware regression.

## Licenses

MicroPython is MIT licensed in `third_party/micropython/LICENSE`. LittleFS is BSD-3-Clause licensed and retains its original SPDX and copyright notices. See `THIRD_PARTY_NOTICES.md` for details.
