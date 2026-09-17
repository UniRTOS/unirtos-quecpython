# unirtos-quecpython

`unirtos-quecpython` 是 QuecPython 的 UniRTOS 外部组件，组件版本为 `1.1.0`，固定 MicroPython `v1.28.0`，面向 UniRTOS SDK `1.0.5` 和 `unirtos-cli 1.0.20`。首版仅支持 `EG800ZCN_LA`。

应用只需在 `env_config.json` 中声明依赖：

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

执行：

```text
unirtos-cli env-setup
unirtos-cli clean
unirtos-cli build
```

组件实际静态库 target 为 `unirtos-quecpython`，并保留 `quecpython` CMake alias。QuecPython 通过 `.unirtos_app_init` 自动注册，应用不需要主动调用初始化函数。

`EG800ZCN_LA` 板级目录同时携带 288 KiB 的首启空 LittleFS 镜像 `customer_app2.bin`，其 SHA-256 记录在 `boards/EG800ZCN_LA/customer_app2.sha256`。组件会在 SDK 基础包生成后自动以 `PKGFLX_CUST` 重新打包，并将原始镜像复制到 release 目录；无需手工把镜像放入 `qos_tools`。

组件不会修改 SDK 源文件。`EG800ZCN_LA` 分区覆盖头仅在编译 QuecPython port 时应用镜像所需的 OpenCPU `0x90000`、CUST `0x48000`、LFS `0x79000` 布局；若 `gccout` 未声明该布局，打包配置直接报错。

公共 C 接口位于 `include/quecpython.h`：

```c
void quecpython_init(void);
int quecpython_is_running(void);
```

`quecpython_init()` 可重复调用；只有 UART7 REPL、`/usr` 文件系统和启动脚本均完成初始化后，`quecpython_is_running()` 才返回真。运行配置：UART7/115200 REPL、512 KiB GC heap、32 KiB 主任务栈、8 KiB Python 线程栈、LittleFS 用户文件系统，以及独立的 288 KiB CUST 初始镜像。默认挂载失败不会格式化已有分区；仅在明确授权的恢复构建中定义 `MICROPY_QPY_USRFS_AUTO_FORMAT=1`。配置启用 `uos`、`machine`、`utime`、`usocket`、`net`、`sim`、`dataCall`、`sms`、`fota`、`atcmd` 等兼容模块，以及 GPIO、UART、I2C、SPI、Timer、RTC、WDT 等板级能力。

本仓库的 QuecPython port、板级配置、Demo 集成和新增脚本采用根目录 `LICENSE` 中的 MIT 许可证；MicroPython 与 LittleFS 保留各自原始许可证和声明，详见 `THIRD_PARTY_NOTICES.md`。发布前运行 `scripts/verify-source.ps1`，并完成两次干净构建、map 符号检查和 EG800ZCN_LA 实机回归。UART REPL 导入回归脚本位于 `tests/compat_smoke.py`，不会冻结进正式固件。
烧录前用 `scripts/verify-flash-config.ps1` 校验 FlashTool 烧录地址与发布包 `imagedata.json` 记录一致，避免把 `customer_app2.bin` 写到旧地址覆盖应用。
