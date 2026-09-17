# unirtos-quecpython

QuecPython external component for UniRTOS. The component packages the MicroPython runtime, the Quectel UniRTOS port, board configuration, frozen Python modules, and LittleFS into one independently versioned static library.

## Version matrix

| Item | Version |
| --- | --- |
| Component | `1.1.0` |
| MicroPython | `v1.28.0` |
| UniRTOS SDK | `1.0.5` |
| unirtos-cli | `1.0.20` |
| Supported board | `EG800ZCN_LA` |

Only `EG800ZCN_LA` is accepted by the v1.1.0 CMake configuration. The component is compiled as the `unirtos-quecpython` target; `quecpython` remains as a CMake alias for source compatibility.

## Use from an application

Declare the component in `env_config.json`:

```json
{
  "build": {
    "module": "EG800ZCN_LA"
  },
  "sdk": {
    "version": "1.0.5"
  },
  "libraries": {
    "list": [
      {
        "name": "unirtos-quecpython",
        "version": "1.1.0"
      }
    ]
  }
}
```

Then run:

```text
unirtos-cli env-setup
unirtos-cli clean
unirtos-cli build
```

No application-side initialization call is required. `UNIRTOS_APP_EXPORT(700, "quecpython", quecpython_init)` places the startup entry in `.unirtos_app_init`, so the UniRTOS application registry starts QuecPython automatically.

The `EG800ZCN_LA` board directory also carries the 288 KiB `customer_app2.bin` first-boot LittleFS image. Its SHA-256 is recorded in [`boards/EG800ZCN_LA/customer_app2.sha256`](boards/EG800ZCN_LA/customer_app2.sha256). After the SDK creates its base package, the component automatically repackages it with this image as `PKGFLX_CUST` and copies the raw image to the release directory. This does not require staging an image under `qos_tools`.

The component never modifies SDK source files. Its EG800ZCN_LA partition overlay applies the required OpenCPU `0x90000`, CUST `0x48000`, and LFS `0x79000` values only while compiling the QuecPython port; packaging also rejects a `gccout` that does not declare that layout.

## Public C API

Include `quecpython.h` and link the application against `unirtos-quecpython` when a direct dependency is needed.

```c
void quecpython_init(void);
int quecpython_is_running(void);
```

`quecpython_init()` is idempotent. `quecpython_is_running()` becomes true only after the UART7 REPL, `/usr` filesystem, and startup scripts have initialized.

## Runtime profile

- REPL: UART7 at 115200 baud.
- Python GC heap: 512 KiB.
- Main task stack: 32 KiB.
- Python thread stack: 8 KiB.
- User filesystem: LittleFS, with a separate 288 KiB initial CUST image. A mount failure preserves existing partition data by default; define `MICROPY_QPY_USRFS_AUTO_FORMAT=1` only for an explicitly authorized recovery build.
- Startup and filesystem bootstrap: frozen `_boot.py` plus the board-selected frozen module set.

The exact linked code/data size and free runtime heap must be recorded from the release build and target smoke test; the values above are configuration budgets, not measured consumption.

Reference software build (SDK 1.0.5, EG800ZCN_LA, minimal component-link application):

| Artifact/section | Size |
| --- | ---: |
| Firmware `.text` | 559,204 bytes |
| Firmware `.data` | 1,004 bytes |
| Firmware `.bss` | 568,104 bytes |
| Packaged firmware with CUST image | 3,423,933 bytes |

These figures include the SDK, enabled QURL/file dependencies, the component, and the link application; they are not an isolated component-size measurement.

## Module matrix

The EG800ZCN_LA profile enables `uos`, `machine`, `utime`, `usocket`, `net`, `sim`, `dataCall`, `sms`, `fota`, `atcmd`, `misc`, `modem`, `ostimer`, `ujson`, `ubinascii`, `ustruct`, `urandom`, `uerrno`, `uselect`, `ucollections`, and `math`.

The `machine` profile enables Pin, Timer, RTC, WDT, ExtInt, Key, I2C, SPI, and UART integration. Audio, LVGL, camera, BLE/BT, Ethernet, GNSS, and Wi-Fi are outside the v1.1.0 component scope.

## Repository layout

```text
boards/EG800ZCN_LA/       board configuration, feature matrix, and CUST image
include/                  stable public C API
ports/quectel/            Quectel port, QOSA wrappers, C modules, frozen modules
third_party/micropython/  pinned MicroPython v1.28.0 source
```

All QSTR, frozen-module, copied upstream, and generated header artifacts are emitted under the consuming application's build directory. The component does not reference the legacy SDK directory tree and does not call `add_apps_libraries()`.

## Release checks

Run `powershell -ExecutionPolicy Bypass -File scripts/verify-source.ps1` before publishing. A release tag also requires two clean, reproducible builds, map-file confirmation of `__unirtos_app_init_quecpython_init`, the UART7/device smoke matrix, and an end-to-end project created from the demo manifest. The UART import regression script is [`tests/compat_smoke.py`](tests/compat_smoke.py); it is intentionally not frozen into release firmware.

Before flashing, verify each FlashTool download configuration against the packaged images: `powershell -ExecutionPolicy Bypass -File scripts/verify-flash-config.ps1 -IniPath <quec_download_*.ini> -ImageDataJson <release>/imagedata.json`. The check fails when a `pkgflx*` burn address does not match the address recorded for its image.

## Licenses

This repository's QuecPython port, board configuration, demo integration, and project scripts are MIT licensed under [`LICENSE`](LICENSE). MicroPython is distributed under its own MIT license in `third_party/micropython/LICENSE`. LittleFS is BSD-3-Clause licensed; its source files retain the original SPDX and copyright notices. See `THIRD_PARTY_NOTICES.md`.
