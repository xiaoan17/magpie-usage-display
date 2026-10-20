# ESP-Mosaico / ESP32-S31 适配

本目录是 [peterroe/magpie-usage-display](https://github.com/peterroe/magpie-usage-display)
的硬件移植，保留上游界面、协议与电脑端方案。请先阅读[来源声明](../../ATTRIBUTION.md)。

**真机状态（2026-10-20）**：ESP-Mosaico CoreBoard v1.2、ESP32-S31 rev v0.0、
CO5300 480×480 AMOLED、CST9220 触摸已验证；用户确认最终颜色和文字恢复正常。
v1.0 仅按官方引脚表提供分支，未做真机验证。

## 目录与日常使用

- `main/`：板级适配、界面、NimBLE 协议、应用循环。
- `components/`：官方 CST9220 组件及派生 USB 控制台，保留各自许可证。
- `patches/`：本次实际使用的 ESP-IDF 链接修复。
- `tests/`：RGB565 测试及真实 UI 代码的离线渲染工具。
- [移植过程与踩坑](../../docs/esp-mosaico-porting.md)、[验证记录](../../docs/esp-mosaico-validation.md)。

电脑需要运行 Magpie；显示屏通过加密 BLE 连接，不需要 Wi-Fi。
每 15 秒同步。闲置 60 秒熄屏，触摸或短按中间 AI 按钮唤醒；左右滑动切页，
亮屏时短按 AI 按钮也可切页，长按 AI 打开快捷操作。
正常运行时短按 BOOT 切换亮灭，长按约 3 秒清除配对；上电时按住 BOOT 会进入下载模式。

## 可复现环境

实际使用 ESP-IDF commit `e4df0c12f70daf0a7958e586e223c519fa9a1576`，
`tools/cmake/version.cmake` 声明 6.2.0，但 `git describe` / 应用元数据仍可能显示
`v6.1-dev-7975-ge4df0c12f7-dirty`。以提交和补丁为准，不把版本字符串当成兼容性证明。
使用 ESP-IDF 对应的 Python 3.12 环境、预览目标 `esp32s31`。

先安装支持该目标的 ESP-IDF 及工具链，并在测试过的提交上应用补丁：

```sh
# 在仓库根目录；IDF_PATH 指向上述提交的 ESP-IDF checkout。
git -C "$IDF_PATH" apply --check "$PWD/ports/esp-mosaico/patches/esp-idf-psram-linker.patch"
git -C "$IDF_PATH" apply "$PWD/ports/esp-mosaico/patches/esp-idf-psram-linker.patch"
. "$IDF_PATH/export.sh"
idf.py --preview -C ports/esp-mosaico set-target esp32s31 build
```

已应用过的补丁不用重复应用；其他 IDF 版本需要先核对对应链接脚本。
`dependencies.lock` 锁定实际使用的组件版本。项目将生成的 `sdkconfig` 放在 `build/`。

## 刷写边界

这是独立 ESP-IDF 应用，**不采用 ESP-Iris Recovery 分区布局**。
首次安装会替换原应用和分区方案；应先按设备现有固件的流程备份完整 Flash。
本仓库不提供设备整机备份、NVS 或配对数据。不要混用此项目与 Mosaico Recovery 的固件包。

完成备份并确认要安装本项目后，在 ROM 下载模式下选择实际串口：

```sh
idf.py --preview -C ports/esp-mosaico -p <ROM_PORT> flash
```

后续只有在**完整分区表与设备一致**时，才可以只更新应用区：

```sh
python -m esptool --chip esp32s31 --port <ROM_PORT> --baud 460800 \
  --before no-reset --after hard-reset --no-stub \
  write-flash 0x10000 ports/esp-mosaico/build/magpie_mosaico.bin
```

USB-CDC 正常应用进入下载模式时会重新枚举，串口名称可能变化。
遇到 `Device not configured`，先重新枚举并核对同一硬件序列号，不能继续用失效的端口。
正常应用串口的 RTS/DTR 是复位协议，随意打开/关闭串口可能让设备复位或进入下载模式。

## 电脑端与首次配对

在**仓库根目录**运行：

```sh
./companion/run.sh --once  # 先确认本机 Magpie 数据可读
./companion/run.sh         # 连接显示屏，按提示完成六位码配对
```

首次配对完成并验证数据后，退出手动进程，再安装当前用户的登录启动项：

```sh
companion/.venv/bin/python companion/install_launch_agent.py
```

日志位于 `~/Library/Logs/MagpieUsageDisplay/companion.log`。
卸载用同一脚本加 `--uninstall`。不要同时运行两个 Companion 争抢同一设备。

## 本地验证

```sh
python3 -m unittest discover -s companion/tests -t .
cc -std=c11 -Wall -Wextra -Werror -Iports/esp-mosaico/main \
  ports/esp-mosaico/tests/rgb565_transport_test.c -o /tmp/rgb565-test
/tmp/rgb565-test

# 先完成一次 IDF 依赖解析，让 managed_components/lvgl__lvgl 存在。
cmake -S ports/esp-mosaico/tests/ui-preview -B /tmp/magpie-ui-build
cmake --build /tmp/magpie-ui-build --target preview -j8
mkdir -p /tmp/magpie-ui-frames
/tmp/magpie-ui-build/preview /tmp/magpie-ui-frames
```

离线预览使用明确的合成样例数据，不是实时用量或真机截图。
