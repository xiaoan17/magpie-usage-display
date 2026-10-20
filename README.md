# ESP-Mosaico / ESP32-S31 fork

> **基于他人开源工作的适配版，并非从零原创。** 直接 fork 自
> [peterroe/magpie-usage-display](https://github.com/peterroe/magpie-usage-display)，
> 上游进一步基于 [jayyao97/codex-usage-display](https://github.com/jayyao97/codex-usage-display)。
> 原界面、图标、BLE 协议、电脑端采集与同步方案来自上游；本 fork 增加 ESP32-S31 / ESP-Mosaico 适配、修复和验证记录。
> 保留原 MIT 许可证及各组件原有许可证。详见 **[来源与贡献声明](ATTRIBUTION.md)**。

- **ESP-Mosaico 用户**：[构建与使用](ports/esp-mosaico/README.md)
- **移植过程**：[实现方法与踩坑记录](docs/esp-mosaico-porting.md)
- **证据与范围**：[真机验证记录](docs/esp-mosaico-validation.md)
- **原 Waveshare ESP32-S3 用户**：以下原项目说明保持保留；原 `src/` / PlatformIO 路径未改。

2026-10-20：v1.2 真机配对、显示、颜色/文字修复与持续同步通过现场确认。

---

# Magpie Usage Display

[简体中文](README.md) | [English](README.en.md)

将本机 Magpie 网关的用量和运行状态显示在一块 480 × 480 AMOLED 开发板上。
macOS Companion 从 Magpie 本地 HTTP API 读取数据，再通过加密 BLE 发送给
ESP32-S3。开发板不保存 API Key、账户凭据或请求内容。

> 本项目是社区项目，并非 Magpie 官方产品。它基于
> [jayyao97/codex-usage-display](https://github.com/jayyao97/codex-usage-display)
> 改造。

## 实拍

<table>
  <tr>
    <td><img src="docs/photos/overview.jpg" alt="Overview 页面实拍"></td>
    <td><img src="docs/photos/requests.jpg" alt="Requests 页面实拍"></td>
  </tr>
  <tr>
    <td align="center">Overview</td>
    <td align="center">Requests</td>
  </tr>
  <tr>
    <td><img src="docs/photos/sessions.jpg" alt="Sessions 页面实拍"></td>
    <td><img src="docs/photos/context.jpg" alt="Context 页面实拍"></td>
  </tr>
  <tr>
    <td align="center">Sessions</td>
    <td align="center">Context</td>
  </tr>
</table>

## 功能

- Magpie 官方 Logo 和网关连接状态
- 四个可左右滑动的页面：
  - **Overview**：今日成功率、调用数、错误数、最低剩余额度
  - **Requests**：最近模型、状态、耗时、平均 TTFT、今日 Token
  - **Sessions**：30 天会话数、总 Token、估算费用、最常用模型
  - **Context**：缓存命中率及 Input / Output / Cache / Reasoning 构成
- 15 秒刷新数据，60 秒未收到数据时显示 `STALE`
- 60 秒无触摸或按键操作后自动熄屏
- 触摸或 BOOT 键唤醒屏幕
- BLE Secure Connections、六位配对码和自动重连
- 设备快捷操作：聚焦 Magpie、立即刷新

Context 页面只展示 Magpie 公共网关 API 能提供的真实统计，不复制 macOS App
内部尚未公开的 Context 评分。

## 硬件与系统要求

- [Waveshare ESP32-S3-Touch-AMOLED-2.16](https://www.waveshare.com/esp32-s3-touch-amoled-2.16.htm)
- 支持数据传输的 USB-C 线
- macOS
- Python 3.9+；构建 ESP32 固件推荐 Python 3.11
- 已安装并正在运行的 Magpie
- PlatformIO

目前只验证了上述开发板和 macOS。

## 工作原理

```text
Magpie http://127.0.0.1:3425
  ├─ /v1/magpie/quotas
  ├─ /v1/magpie/usage
  └─ /v1/magpie/usage/requests
                 │
                 ▼
       macOS Python Companion
                 │  加密 BLE
                 ▼
       ESP32-S3 + LVGL + AMOLED
```

默认只访问本机 `http://127.0.0.1:3425`。可通过 `MAGPIE_URL` 环境变量或
`--magpie-url` 参数覆盖。

## 1. 获取代码

```bash
git clone https://github.com/peterroe/magpie-usage-display.git
cd magpie-usage-display
```

## 2. 准备 PlatformIO

推荐在项目目录创建独立 Python 环境：

```bash
python3 -m venv .pio-venv
source .pio-venv/bin/activate
python -m pip install --upgrade pip platformio
```

确认使用的是项目环境：

```bash
which pio
pio --version
```

`which pio` 应优先显示项目中的 `.pio-venv/bin/pio`。

## 3. 编译固件

```bash
pio run
```

成功后固件位于：

```text
.pio/build/esp32-s3-touch-amoled-216/firmware.bin
```

## 4. 连接并烧录开发板

用 USB-C 数据线连接开发板，查看串口：

```bash
ls /dev/cu.*
```

找到类似 `/dev/cu.usbmodem11201` 的端口，然后上传：

```bash
pio run -t upload --upload-port /dev/cu.usbmodem11201
```

请将端口替换为你的实际设备。

如果找不到端口或无法连接：

1. 拔下 USB-C。
2. 按住开发板左侧 `BOOT`。
3. 保持按住并重新插入 USB-C。
4. 等待约 2 秒后松开。
5. 重新运行 `ls /dev/cu.*` 和上传命令。

烧录成功后设备会自动重启，未连接 Companion 时显示 `OFFLINE`。

## 5. 验证 Magpie 数据

确保 Magpie 正在运行，然后执行：

```bash
./companion/run.sh --once
```

成功时会输出一行 BLE v4 JSON 快照。该命令只读取数据，不连接蓝牙。

## 6. 启动 Companion

```bash
./companion/run.sh
```

首次运行会自动创建 `companion/.venv` 并安装 `bleak`。macOS 询问蓝牙权限时
请选择允许。

### 首次配对

只有尚未绑定的设备才会显示六位验证码：

1. 开发板显示六位数字。
2. 在 macOS 蓝牙配对框中输入相同数字。
3. 收到第一包数据后状态变为 `ONLINE`。

后续连接会复用绑定密钥，不再要求输入验证码。

需要换电脑或重新配对时，在设备正常运行状态下长按左侧 `BOOT` 约 3 秒。看到
`PAIRING CLEARED` 后设备会重启。不要在上电时按住 BOOT，否则会进入下载模式。

## 7. 后台自动运行

先完成一次手动配对，再停止手动运行的 Companion：

```bash
python3 companion/install_launch_agent.py
```

卸载：

```bash
python3 companion/install_launch_agent.py --uninstall
```

日志位于：

```text
~/Library/Logs/MagpieUsageDisplay/companion.log
~/Library/Logs/MagpieUsageDisplay/companion.log.1
```

LaunchAgent 进程可能需要在“系统设置 → 隐私与安全性 → 蓝牙”中单独授权。

## 操作

面对屏幕、三个实体键位于顶部时：

| 位置 | 按键 | 行为 |
|---|---|---|
| 左 | BOOT / GPIO0 | 短按切换屏幕；运行时长按 3 秒清除 BLE 绑定 |
| 中 | PWR / AXP2101 | 长按 6 秒关闭主电源 |
| 右 | IO18 / Focus | 短按切换数据页面；长按 1 秒打开快捷操作 |

- 左右滑动：切换四个数据页面
- 也可以按右侧 Focus 键逐页循环切换，不用手指滑动
- 屏幕熄灭时按 Focus 键只会唤醒屏幕，再按一次才切换页面
- 触摸熄灭的屏幕：仅唤醒，不触发页面操作
- 60 秒无操作：自动熄屏；BLE 数据仍在后台更新

## 常见问题

### 没有 `/dev/cu.usbmodem...`

- 确认 USB-C 线支持数据传输
- 直接连接 Mac，避免先经过扩展坞
- 换一个 USB 接口
- 按照上面的 BOOT 下载模式步骤重试

### 屏幕显示 `OFFLINE`

确认 Companion 正在运行：

```bash
./companion/run.sh
```

### 屏幕显示 `STALE`

检查 Magpie 和本地 API：

```bash
curl http://127.0.0.1:3425/
./companion/run.sh --once
```

### 查看设备日志

```bash
pio device monitor
```

按 `Control-C` 退出。烧录前需要先关闭串口监视器。

## 开发与测试

```bash
python3 -m unittest discover -s companion/tests -v
pio run
```

BLE v4 协议见 [docs/BLE_PROTOCOL.md](docs/BLE_PROTOCOL.md)。

## 安全与隐私

- ESP32 不保存 Magpie 凭据、请求正文或会话内容
- BLE 状态与命令特征要求加密访问
- 设备只允许 `focus_magpie` 和 `refresh` 两个语义操作
- 不接受任意 shell 命令或 prompt

安全问题请参阅 [SECURITY.md](SECURITY.md)。

## 限制

- Companion 当前仅支持 macOS
- 只验证了 Waveshare ESP32-S3-Touch-AMOLED-2.16
- 固件没有 OTA，升级需要 USB 烧录
- Context 页面使用公共 API 聚合数据，不包含 Magpie App 内部评分

## 许可证与商标

代码使用 [MIT License](LICENSE)。原项目版权和第三方字体声明保留在
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

Magpie 名称和 Logo 属于其各自权利人。本项目对 Logo 的使用仅用于标识与 Magpie
的兼容性，不表示官方背书。
