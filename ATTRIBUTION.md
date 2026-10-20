# 来源与贡献声明 / Attribution

**本仓库是基于他人开源工作的派生项目，不是从零独立完成的原创项目。**
请首先感谢以下项目的作者。本 fork 保留上游 Git 历史、原始许可证和文件内版权声明。

## 直接上游与更早来源

1. **[peterroe/magpie-usage-display](https://github.com/peterroe/magpie-usage-display)**：本 fork 的直接上游。
   Magpie 产品思路、四页界面、图标、BLE v4 协议、电脑端数据采集、同步服务和大量实现来自该仓库。
   本次适配基线为 [`5940d479afeb89affc04d13c01f3f644b3f28cc7`](https://github.com/peterroe/magpie-usage-display/tree/5940d479afeb89affc04d13c01f3f644b3f28cc7)。
2. **[jayyao97/codex-usage-display](https://github.com/jayyao97/codex-usage-display)**：直接上游 README 声明的更早基础项目。
   根目录 MIT License 保留原文及 `Copyright (c) 2026 Jay Yao`，没有改成当前 fork 作者的名字。
3. **[esp-mosaico/esp-mosaico-bsp](https://github.com/esp-mosaico/esp-mosaico-bsp)**：板型识别、引脚表、CO5300 面板初始化、刷新区域对齐和触摸配置的官方依据。
   对照版本：`05e067de0613a0aa3fe2af42e75616804decbf4b`；关键文件是
   `components/esp-mosaico-bsp/include/bsp/esp_mosaico.h` 与 `onboard/display.c`。
4. **[esp-mosaico/esp-mosaico-claw](https://github.com/esp-mosaico/esp-mosaico-claw)**：
   `ports/esp-mosaico/components/usb_console/` 派生自其
   `boards/esp_mosaico/components/usb_hs_console/`，对照版本
   `903fe11ec3ca4a8881afef104e53a8da040896ac`。保留 Espressif Apache-2.0 文件头；
   本适配增加应用启动包装及无缓冲日志输出等集成修改。
5. **[Espressif esp-iot-solution CST9220 驱动](https://github.com/espressif/esp-iot-solution/tree/8eee17c20da66048faa20c683ffeb80bfa85d723/components/display/lcd_touch/esp_lcd_touch_cst9220)**：
   `ports/esp-mosaico/components/esp_lcd_touch_cst9220/` 为原样保留的官方 0.1.1 组件，
   含源码、版本元数据、测试源码及 Apache-2.0 许可证，不是本 fork 原创。
6. **ESP-IDF、NimBLE、LVGL、Bleak 及其他依赖**：平台、蓝牙栈、字体/渲染和电脑端蓝牙能力由这些项目提供。
   详见 [第三方声明](THIRD_PARTY_NOTICES.md) 和依赖清单。

## 本 fork 增加了什么

- 将原先面向 Waveshare ESP32-S3 / Arduino 的方案适配为 ESP-Mosaico ESP32-S31 / ESP-IDF 应用。
- 实现 Mosaico 板型引脚、屏幕、触摸和 USB 控制台集成；移植协议到 NimBLE。
- 修复本地移植过程中的蓝牙长度输出参数误用、认证能力冲突、引脚映射错误、触摸初始化缺失、
  RGB565 传输字节序及绘图缓冲区生命周期问题。
- 增加电脑端首次配对等待、相关回归测试、RGB565 测试和四页离线渲染工具。
- 记录可复现版本、ESP-IDF 链接脚本补丁、真机验证过程及未覆盖范围。

这些修复描述的是**本次 ESP32-S31 移植遇到的问题**，不表示原作者针对原硬件的代码存在同样问题。
实现和调试由仓库维护者使用 Codex 协助完成；硬件亮屏、触摸及最终颜色/文字由维护者实际观察确认。
不将上游的界面设计、协议、图标、电脑端框架或官方驱动归为本 fork 的原创。

## 许可证边界

- 保留根目录 [MIT License](LICENSE)；基于上游 MIT 代码的移植部分继续按 MIT 分发。
- 官方 Mosaico 硬件代码的派生部分、USB 控制台和 CST9220 组件遵循其 Apache-2.0 声明；
  全文见 [LICENSES/Apache-2.0.txt](LICENSES/Apache-2.0.txt) 和组件原许可证。
- 字体的 OFL 声明继续保留。不能用根目录 MIT 声明覆盖第三方文件的许可证。
- 本 fork 不是 Magpie、Espressif 或上游作者的官方发行版，也不代表这些作者为本适配背书。
