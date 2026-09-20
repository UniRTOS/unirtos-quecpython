# unirtos-quecpython

中文 | [English](README.md)

`unirtos-quecpython` 是面向 EG800ZCN_LA 的 QuecPython UniRTOS 独立组件库。请通过 UniRTOS 库依赖流程使用它；应用和 Demo 只声明依赖，不复制运行时源码。

## 功能说明

组件以一个静态库提供以下内容：

- MicroPython `v1.28.0` 及 Quectel UniRTOS port
- QuecPython C 模块、QOSA 封装、QSTR 生成和冻结模块
- LittleFS 支持及初始 CUST 镜像 `customer_app2.bin`
- `EG800ZCN_LA` 板级配置和分区覆盖

运行时会将 `quecpython_init()` 注册到 `.unirtos_app_init`。声明本组件的应用不应再创建第二个 MicroPython 任务，也不需要主动调用初始化函数。组件不包含产品应用、外设示例、LCD 驱动或其他板型配置，这些内容应放在 Demo 或产品工程中。

## 兼容性

| 项目 | 支持版本 |
| --- | --- |
| 组件 | `1.1.0` |
| MicroPython 基线 | `v1.28.0` |
| UniRTOS SDK | `1.0.5` |
| unirtos-cli | `1.0.20` |
| 板型 | `EG800ZCN_LA` |

当前版本仅接受 `EG800ZCN_LA`。实际 CMake target 为 `unirtos-quecpython`，保留 `quecpython` 作为兼容 alias。

## 快速开始

### 1. 安装 UniRTOS 工具链

- [开发准备](https://www.quectel.com.cn/unirtos/docs?docs_page=快速上手/开发准备/开发准备.html)
- [交叉编译工具链安装](https://www.quectel.com.cn/unirtos/docs?docs_page=快速上手/环境搭建/环境搭建.html)
- 安装 Python 3、Git 和 `unirtos-cli`

确认 CLI 可用：

```bash
python --version
git --version
unirtos-cli version
```

### 2. 在应用中声明组件

在应用的 `env_config.json` 增加组件依赖：

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

拉取 SDK 和组件并构建：

```bash
unirtos-cli env-setup
unirtos-cli build
```

完整参考工程请使用 [`unirtos-quecpython-demos`](https://github.com/UniRTOS/unirtos-quecpython-demos)：

```bash
unirtos-cli ls-demos
unirtos-cli new -r unirtos-quecpython-demos -v 1.0.0
cd unirtos-quecpython-demos-1.0.0
unirtos-cli env-setup
unirtos-cli build
```

## 构建与打包说明

本组件使用的基础 `gccout.7z` 必须声明 OpenCPU `0x90000`、CUST `0x48000`、LFS `0x79000` 的分区布局；不符合时，配置阶段会直接给出错误。

替换 `gccout.7z` 后，需要先清理已解压的缓存：

```bash
unirtos-cli clean
unirtos-cli build -m EG800ZCN_LA -v <firmware-version>
```

`-m` 用于选择板型，`-v` 用于命名本次发布固件；它们不会改变组件支持的 SDK 或板型。

发布目录为 `qos_build/release/<firmware-version>/`，其中包含 `ap_application.bin`、`customer_app2.bin`、`at_command.hbinpkg` 和 FlashTool 下载配置。组件会将 CUST 镜像以 `PKGFLX_CUST` 加入 SDK 打包结果，无需将镜像手工复制到 `qos_tools`。

## 运行配置

- UART7 REPL，115200 波特率
- 512 KiB Python GC heap、32 KiB 主任务栈、8 KiB Python 线程栈
- LittleFS 用户文件系统和独立的 288 KiB 初始 CUST 镜像
- 冻结的 `_boot.py` 和板级冻结模块集合

EG800ZCN_LA profile 启用 `uos`、`machine`、`utime`、`usocket`、`net`、`sim`、`dataCall`、`sms`、`fota`、`atcmd`、`misc`、`modem`、`ostimer`、`ujson`、`ubinascii`、`ustruct`、`urandom`、`uerrno`、`uselect`、`ucollections` 和 `math`。`machine` 支持 Pin、Timer、RTC、WDT、ExtInt、Key、I2C、SPI 和 UART。Audio、LVGL、camera、BLE/BT、Ethernet、GNSS 和 Wi-Fi 不在当前组件范围内。

## 公共 C 接口

应用需要读取运行状态时可包含 `quecpython.h`：

```c
void quecpython_init(void);
int quecpython_is_running(void);
int quecpython_exec_string(const char *code);
```

`quecpython_init()` 可重复调用。运行时会自动启动，普通应用无需调用它。运行时未就绪或输入非法时 `quecpython_exec_string()` 返回 `-1`；在没有把请求投递到 QuecPython 运行时任务前，不能从任意 UniRTOS 任务中直接调用该接口。

## 仓库结构

```text
boards/EG800ZCN_LA/       板级配置、功能矩阵和 CUST 镜像
include/                  公共 C 接口
ports/quectel/            Quectel port、QOSA 封装、C 模块和冻结模块
third_party/micropython/  固定的 MicroPython 源码
```

上游拷贝代码、QSTR、冻结模块和生成头文件均输出至使用方应用的构建目录。组件不会修改 UniRTOS SDK 源码，也不会调用 `add_apps_libraries()`。

## 常用命令

```bash
# 查看可用组件版本
unirtos-cli ls-libs

# 刷新 SDK 与 env_config.json 中声明的组件
unirtos-cli env-setup

# 清除构建产物和已解压的 gccout 缓存
unirtos-cli clean
```

## 技术社区

技术社区：https://forumschinese.quectel.com/c/66-category/66

## 贡献说明

- 提交前运行 `env-setup`、`build` 和 `clean`。
- 修改板型、SDK、CLI 或 MicroPython 版本时，同步更新本 README。
- 不要将生成的构建产物或 SDK 副本提交到本仓库。
- 修改分区布局、CUST 镜像或运行时启动路径时，更新验证记录并完成 EG800ZCN_LA 实机回归。

## 许可证

MicroPython 使用 MIT 许可证，见 `third_party/micropython/LICENSE`。LittleFS 使用 BSD-3-Clause 许可证，并保留其原始 SPDX 和版权声明。详情见 `THIRD_PARTY_NOTICES.md`。
