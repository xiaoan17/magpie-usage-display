# Third-party notices

This repository contains a generated bitmap subset of Montserrat Medium in
`src/fonts/font_montserrat_100.c`. Montserrat is Copyright 2024 The
Montserrat.Git Project Authors and is licensed under the SIL Open Font
License 1.1. See [`LICENSES/OFL-1.1.txt`](LICENSES/OFL-1.1.txt).

The project downloads but does not vendor the following runtime and build
dependencies:

| Dependency | License |
|---|---|
| ArduinoJson | MIT |
| LVGL | MIT |
| Arduino GFX | BSD 2-Clause |
| SensorLib | MIT |
| XPowersLib | MIT |
| Arduino-ESP32 | LGPL-2.1 |
| Bleak | MIT |
| pioarduino platform-espressif32 | Apache-2.0 |
| PlatformIO Core | Apache-2.0 |

Each dependency remains subject to its own copyright notices and license
terms. Refer to the dependency versions in `platformio.ini`,
`companion/pyproject.toml`, and `companion/requirements.txt`.

## ESP-Mosaico fork additions

See [ATTRIBUTION.md](ATTRIBUTION.md) for upstream contribution boundaries and pinned source revisions.

- `ports/esp-mosaico/components/esp_lcd_touch_cst9220/`: Espressif CST9220 0.1.1, Apache-2.0, vendored with its original license and headers.
- `ports/esp-mosaico/components/usb_console/`: derived from ESP-Mosaico Claw USB console, Apache-2.0; original Espressif notices retained.
- Mosaico pin mapping and panel initialization in `main/board.c`: adapted from the official ESP-Mosaico BSP, Apache-2.0.
- The ESP-IDF linker patch derives from ESP-IDF (Apache-2.0). Full license in `LICENSES/Apache-2.0.txt`.
- The port downloads ESP-IDF, ESP LCD components, TinyUSB, LVGL and NimBLE dependencies under their respective licenses; the lockfile records versions.
- Shared UI/protocol/companion porting retains the upstream MIT license. Generated preview images use synthetic data.
