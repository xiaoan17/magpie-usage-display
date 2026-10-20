# ESP-Mosaico / ESP32-S31 fork

This is a derivative port of [peterroe/magpie-usage-display](https://github.com/peterroe/magpie-usage-display),
which builds on [jayyao97/codex-usage-display](https://github.com/jayyao97/codex-usage-display).
The product UI, logo, BLE protocol and companion foundation are upstream work, not original work of this fork.
Original licenses and copyright notices are preserved. See [Attribution](ATTRIBUTION.md).

This fork adds the [ESP-Mosaico ESP-IDF port](ports/esp-mosaico/README.md),
[porting/debugging notes](docs/esp-mosaico-porting.md), and [validation evidence](docs/esp-mosaico-validation.md).
The original Waveshare / Arduino sources and instructions are retained below.

---

# Magpie Usage Display

[简体中文](README.md) | [English](README.en.md)

Display local Magpie gateway usage and health on a 480 × 480 AMOLED board. A
macOS companion reads Magpie's local HTTP API and sends compact snapshots to an
ESP32-S3 over encrypted BLE. The board stores no API keys, account credentials,
or request content.

> This is an unofficial community project based on
> [jayyao97/codex-usage-display](https://github.com/jayyao97/codex-usage-display).

## Features

- Magpie branding and gateway connection state
- Four swipeable pages:
  - **Overview**: success rate, calls, errors, and lowest remaining quota
  - **Requests**: latest model, status, latency, average TTFT, and today's tokens
  - **Sessions**: 30-day sessions, total tokens, estimated cost, and top model
  - **Context**: cache hit rate and input/output/cache/reasoning token mix
- Refresh every 15 seconds and `STALE` status after 60 seconds without data
- Screen sleep after 60 seconds of inactivity
- BLE Secure Connections, bonding, and automatic reconnect
- Device actions to focus Magpie or refresh data

The Context page only uses metrics exposed by Magpie's public gateway API; it
does not reproduce the private scoring used by the macOS app.

## Requirements

- [Waveshare ESP32-S3-Touch-AMOLED-2.16](https://www.waveshare.com/esp32-s3-touch-amoled-2.16.htm)
- A data-capable USB-C cable
- macOS, Python 3.9+, and a running Magpie installation
- PlatformIO; Python 3.11 is recommended for firmware builds

Only this board and macOS have been verified.

## Data path

```text
Magpie http://127.0.0.1:3425
  ├─ /v1/magpie/quotas
  ├─ /v1/magpie/usage
  └─ /v1/magpie/usage/requests
                 |
                 v
       macOS Python Companion
                 |  encrypted BLE
                 v
       ESP32-S3 + LVGL + AMOLED
```

Override the default endpoint with `MAGPIE_URL` or `--magpie-url`.

## Install

Clone the repository:

```bash
git clone https://github.com/peterroe/magpie-usage-display.git
cd magpie-usage-display
```

Create an isolated PlatformIO environment:

```bash
python3 -m venv .pio-venv
source .pio-venv/bin/activate
python -m pip install --upgrade pip platformio
```

Build the firmware:

```bash
pio run
```

The image is written to:

```text
.pio/build/esp32-s3-touch-amoled-216/firmware.bin
```

Connect the board and find its serial port:

```bash
ls /dev/cu.*
```

Upload using the detected `/dev/cu.usbmodem...` port:

```bash
pio run -t upload --upload-port /dev/cu.usbmodem11201
```

If the port is missing, disconnect USB, hold the left BOOT button, reconnect
USB, wait two seconds, release BOOT, and retry.

## Run the companion

Verify Magpie data without BLE:

```bash
./companion/run.sh --once
```

Connect to the display:

```bash
./companion/run.sh
```

The script creates `companion/.venv` and installs `bleak` when needed. Allow
Terminal to use Bluetooth when macOS asks.

An unbonded device shows a six-digit passkey. Enter it in the macOS pairing
dialog. Bonded devices reconnect automatically without showing a code.

To clear pairing, hold the left BOOT button for about three seconds while the
firmware is running. Do not hold BOOT during power-on unless you want download
mode.

## Background service

After completing manual pairing:

```bash
python3 companion/install_launch_agent.py
```

Remove it with:

```bash
python3 companion/install_launch_agent.py --uninstall
```

Logs are stored in `~/Library/Logs/MagpieUsageDisplay/`. The LaunchAgent may
need separate Bluetooth permission in macOS System Settings.

## Controls

| Position | Button | Action |
|---|---|---|
| Left | BOOT / GPIO0 | Short press toggles the screen; hold 3 seconds to clear BLE bonds |
| Center | PWR / AXP2101 | Hold 6 seconds to cut main power |
| Right | IO18 / Focus | Short press cycles data pages; hold 1 second for quick actions |

- Swipe left or right to switch pages.
- You can also press the right Focus key to cycle through pages without swiping.
- If the screen is off, the first Focus press only wakes it; press again to switch pages.
- Touching a sleeping screen wakes it without activating a control.
- BLE continues updating while the screen is off.

## Troubleshooting

- **No serial port:** use a data cable, connect directly to the Mac, or enter
  BOOT download mode as described above.
- **`OFFLINE`:** start `./companion/run.sh`.
- **`STALE`:** check Magpie with `curl http://127.0.0.1:3425/` and run
  `./companion/run.sh --once`.
- **Device logs:** run `pio device monitor`; exit with `Control-C` before
  uploading firmware.

## Development

```bash
python3 -m unittest discover -s companion/tests -v
pio run
```

See [docs/BLE_PROTOCOL.md](docs/BLE_PROTOCOL.md) for protocol v4.

## Security and limitations

- The ESP32 stores no Magpie credentials, request bodies, or session content.
- BLE status and command characteristics require encryption.
- Only `focus_magpie` and `refresh` are accepted; arbitrary commands and
  prompts are rejected.
- The companion currently supports macOS only.
- Firmware updates require USB; OTA is not implemented.

See [SECURITY.md](SECURITY.md) for vulnerability reporting.

## License and trademarks

Licensed under the [MIT License](LICENSE). Original-project and third-party font
notices are retained in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

The Magpie name and logo belong to their respective owner. Their use here only
identifies compatibility and does not imply endorsement.
