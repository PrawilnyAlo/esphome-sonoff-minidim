# ESPHome component for Sonoff MINI-DIM (Matter)

External [ESPHome](https://esphome.io) component for the **Sonoff MINI-DIM** (MINI Extreme, Matter over Wi-Fi dimmer).

The original ESP32-C3 has Secure Boot and flash encryption enabled, so - like the
[Sonoff MINI R4M](https://devices.esphome.io/devices/Sonoff-MINIR4M/) - it has to be **replaced with a new ESP32-C3**
running ESPHome. The dimming itself is done by a second chip on the board, an HC32L021, which talks to the ESP
over UART. This component implements that protocol - see [PROTOCOL.md](PROTOCOL.md).

## Features

- Light with brightness, fades executed by the HC32 (configurable transition time)
- Power, voltage and current
- Auto calibration and manual min/max calibration (stored in the HC32)
- Leading / trailing edge, load type
- Momentary switch dimming (start / stop fade) with sync back to the light state

## Usage

```yaml
external_components:
  - source: github://PrawilnyAlo/esphome-sonoff-minidim
    components: [sonoff_minidim]

uart:
  id: hc32_uart
  tx_pin: GPIO18
  rx_pin: GPIO19
  baud_rate: 115200

sonoff_minidim:
  id: minidim
  uart_id: hc32_uart
  reset_pin: GPIO10
  update_interval: 60s
  power:
    name: Power
  voltage:
    name: Voltage
  current:
    name: Current

light:
  - platform: sonoff_minidim
    name: Light
    default_transition_length: 0s   # fades are done by the HC32
    gamma_correct: 1.0
```

A full configuration with the button, LED, switch modes and calibration controls is in
[example/sonoff-mini-dim.yaml](example/sonoff-mini-dim.yaml).

### Hub options

| Option | Default | Description |
|---|---|---|
| `uart_id` | | UART at 115200 baud |
| `reset_pin` | | HC32 RESETB (GPIO10). Driven high so the HC32 runs. |
| `reset_on_boot` | `false` | Pulse RESETB on every ESP boot (the light flickers on OTA restarts) |
| `update_interval` | `60s` | How often to ask for power (`BB`); the HC32 also reports by itself after changes |
| `power`, `voltage`, `current` | | Metering sensors |
| `level` | | Last level reported by the HC32 (0-255), diagnostic |
| `calibration_min`, `calibration_max` | | Calibrated range stored in the HC32, diagnostic |
| `calibration_progress` | | Auto calibration progress in %, diagnostic |

### Methods for lambdas

```cpp
id(minidim).set_transition(2.0);            // fade time in seconds, 0.25 s steps
id(minidim).set_dimming_type(1);            // 0 leading edge, 1 trailing edge
id(minidim).set_load_type(0);               // 0 auto, 1 LED, 3 incandescent/halogen, 4 ELV
id(minidim).start_fade(true, 0x13);         // fade up (false = down), rate byte = seconds / 0.25 - 1
id(minidim).stop_fade();
id(minidim).start_auto_calibration();
id(minidim).write_calibration(40, 230);     // manual min / max
id(minidim).factory_reset_hc32();           // erases calibration
id(minidim).request_power();
```

## Replacing the ESP32-C3

Flash ESPHome onto an ESP32-C3 SuperMini over USB, desolder its ESP32-C3 and solder it in place of the original one
on the Sonoff logic board (the small board with the ESP32-C3 and HC32L021, desoldered from the power board).

After flashing, USB stops responding: GPIO18/GPIO19 (USB D-/D+) are used as the UART to the HC32.
Check Wi-Fi before moving the chip; later updates go over OTA or the `TX` / `RX` service pads (`KEY` = GPIO9 to GND
for download mode).

## Status

Protocol captured on a real unit. Running on a MINI-DIM with a replaced ESP32-C3 (ESPHome 2026.9, ESP-IDF).
Feedback and issues welcome.

## Safety

The board is not isolated from mains. Never connect a USB-UART adapter, programmer or computer while it is
powered from 230 V. Flash and test the logic on 3.3 V only.

## License

[MIT](LICENSE)
