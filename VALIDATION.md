# Validation record

Date: 2026-09-16

## Software environment

- unirtos-cli: 1.0.20
- UniRTOS SDK: 1.0.5, fetched into a fresh isolated UniRTOS root
- Target: EG800ZCN_LA
- Toolchain: arm-none-eabi-gcc 10.2.1
- Application: minimal component-link demo

## Partition layout contract

The EG800ZCN_LA package pairs SDK 1.0.5 with a `gccout` that reserves OpenCPU `0x90000` / CUST `0x48000` / LFS `0x79000`; the CUST image is flashed at `0x2E8000`. The component enforces this contract without modifying SDK sources:

- Packaging requires the selected `gccout` `mem_map` to declare the same OpenCPU/CUST/LFS layout.
- `ports/quectel/src/qpy_usrfs.c` includes `boards/EG800ZCN_LA/qpy_partition_layout.h`, which accepts the stock SDK layout or the target layout, then applies the target values only to that translation unit.
- FlashTool download configurations are verified against the packaged `imagedata.json` with `scripts/verify-flash-config.ps1`.

## Passed

- Clean configure and full firmware build completed: 250/250 steps.
- The old layout (`0xC8000/0/0x89000`, CUST address `0x320000`) is gone from compiled code: scanning the code sections of all 221 objects, the only partition constants are in `qpy_usrfs.c.obj`, and they use the new values (`0x2E8000`, `0x48000`).
- Final ELF contains `mp_init`, `mp_execute_bytecode`, `pyexec_friendly_repl`, `mp_qstr_frozen_const_pool`, `quecpython_init`, `quecpython_is_running`, `quecpython_exec_string`, and `__unirtos_app_init_quecpython_init`; the map places the component's `quecpython.c.obj` in `.unirtos_app_init`; the linker `FLASH_AREA` length is `0x00090000`.
- Repackaging with the board's 294,912-byte `customer_app2.bin` (`PKGFLX_CUST`) ran automatically; the raw image SHA-256 `E1CA8F87...` matched the component input byte-for-byte in the build directory, release directory, and offline-extracted copy.
- Final firmware package after removing the concurrent smoke task: 3,426,229 bytes, SHA-256 `254FA53B8E4CC64FA2B2D82A6D81D40E97E5FC7688EA9848AC7F3D024356CCE6`.
- FlashTool `pkg2img` extracted the final package; `imagedata.json` records `customer_app2.bin` at XIP `0xAE8000` (flash `0x2E8000`) and `pkgflx2` regeneration reproduced the same address.
- `scripts/verify-flash-config.ps1` passed for both `quec_download_usb.ini` and `quec_download_usb_incremental.ini` (the incremental ini previously carried a stale `0x2D8000` for `pkgflx2`).
- Generated inputs stayed stable across the clean and incremental builds and match the established reference:
  - `qstrdefs.generated.h`: `0CD4A4934635AD14D9654FA7BEC9E2160C85E0F2C975BB2CDAF5AB0B039ECFB1`
  - `frozen_content.c`: `5F287D5020F081001AB1EEF2A99AE9679DB2900BFF45C458889EA8135DE79DF9`
- Static-library, ELF, and package hashes differ between rebuilds because `mpversion.h`/`frozen_content.c` are regenerated on each configure; sizes are identical and generated inputs are stable, so this is build metadata rather than content nondeterminism.
- Unsupported board configure test failed explicitly with: `unirtos-quecpython v1.1.0 supports only EG800ZCN_LA`.

## Environment notes

- E-SafeNet: keep `unirtos-cli build -j 1`; parallel builds can expose protected files to GCC as raw container bytes.
- Agent shells on this machine may export `MSYS_NO_PATHCONV=1` and `MSYS2_ARG_CONV_EXCL=*`. The Helios `unirtos` shim depends on MSYS path conversion, so run the CLI with both variables removed: `env -u MSYS_NO_PATHCONV -u MSYS2_ARG_CONV_EXCL unirtos-cli build ...`.
- The component does not edit the SDK checkout. `qpy_partition_layout.h` supplies the QuecPython-only partition overlay.

## Pending hardware/release gates

- Flash and boot EG800ZCN_LA firmware. Re-flash `pkgflx0`/`pkgflx1`/`pkgflx2`; the earlier flash that used the stale CUST address `0x2D8000` may have overwritten the application tail.
- Verify UART7 REPL (115200 8N1), public API return behavior, `_boot.py`, LittleFS mount, and script execution.
- Run the legacy import matrix: `uos`, `machine`, `usocket`, `net`, `sim`, `dataCall`, `sms`, `fota`, `atcmd`.
- Smoke-test enabled GPIO/UART/I2C/SPI/Timer/RTC/WDT capabilities.
- Compare the old successful firmware's module list, key behavior, firmware size, and runtime memory.
- Publish component/demo repositories and tags, merge the staged manifests, repeat `ls-demos -> new -r -> env-setup -> build` end-to-end.
