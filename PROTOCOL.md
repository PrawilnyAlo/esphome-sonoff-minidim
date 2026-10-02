# Sonoff MINI-DIM (Matter) - ESP32-C3 ↔ HC32L021 protocol

Reverse engineered by sniffing both UART lines of an original unit (board `CON_MINI-DIM_WIFI V1.0`, 2025.08.10,
HC32 firmware reported as 1.1.2) while operating it from Home Assistant (Matter) and the eWeLink app.

## Hardware

- **ESP32-C3** - Wi-Fi/Matter, buttons, LED. Secure Boot v2 + flash encryption, cannot be reflashed.
- **HC32L021C8UB** (QFN20, Cortex-M0+) - drives the power stage: two MOSFETs (`PWM1` / `PWM2` on the PCB),
  zero-cross sync, leading/trailing edge, power metering, calibration storage.

| ESP32-C3 | Function |
|---|---|
| GPIO18 | UART TX → HC32 (`CLK_HC` pad, HC32 PA14) |
| GPIO19 | UART RX ← HC32 (`DIO_HC` pad, HC32 PA13) |
| GPIO10 | HC32 RESETB (`RST_HC` pad), active low |
| GPIO9 | On-board button |
| GPIO4 | S1 input (230 V through divider) |
| GPIO5 | S2 input (230 V through divider) |
| GPIO3 | Blue status LED |
| GPIO20 / GPIO21 | U0RXD / U0TXD service pads |

The same two HC32 pins are its SWD port (`CLK_HC` / `DIO_HC`); the original firmware has an `ota_slave`
partition, so the ESP can also reflash the HC32. This is not needed for normal operation.

## Physical layer

115200 baud, 8N1, 3.3 V. Lines are silent when nothing happens - there is no heartbeat.

## Frame

```
A5 00 LEN CMD [DATA ...] SUM
```

| Field | Description |
|---|---|
| `A5` | header |
| `00` | always `00` |
| `LEN` | length of the **whole** frame, header and checksum included |
| `CMD` | command code |
| `SUM` | sum of all previous bytes, low byte |

**Reply / acknowledge code = command code + 0x10**, in both directions (`B6` → `C6`, `BD` → `CD`, `D6` → `E6`).
The original ESP resends a command after ~200 ms without a reply.

## Commands ESP → HC32

| CMD | Data | Reply | Meaning |
|---|---|---|---|
| `B2` | - | `C2 00 <cal> <min> <max>` | read calibration: `cal` 00 = default / 01 = calibrated, range min..max (default `02`..`FF`) |
| `B3` | - | `C3 00 <level>` | read state (probably current logical level) |
| `B4` | `01` | `C4 00 B4` | start auto calibration (progress `E0`, result `B5`) |
| `B6` | `<mode> <level> 00 00 00 <time>` | `C6 00` | set level, see below |
| `B7` | - | `C7 00 <level>` | stop an ongoing fade, returns the level where it stopped |
| `B8` | - | `C8 00` | reset HC32 settings (erases calibration) |
| `B9` | `00` / `01` | `C9 00` | dimming type: `00` leading edge, `01` trailing edge (default) |
| `BA` | `00` `01` `03` `04` | `CA 00` | load type: auto, dimmable LED, incandescent/halogen, dimmable ELV (`02` not used by the app) |
| `BB` | - | `CB …` | read power (see `CB`) |
| `BC` | `<min>` | `CC 00` | write manual calibration minimum |
| `D6` | `<max>` | `E6 00` | write manual calibration maximum |
| `BF` | `00 00 00 00 00 32` | `CF 00` | sent at start-up, constant, meaning unknown |
| `C0` | `00` | - | acknowledge HC32 hello |
| `C5` | `00` | - | acknowledge calibration result |
| `CD` | `00` | - | acknowledge level report |

### `B6` - set level

```
B6  <mode> <level> 00 00 00 <time>
```

- `mode`: `00` = logical level, mapped by HC32 onto the calibrated range; `01` = raw level, calibration bypassed
  (used by the app to preview manual calibration).
- `level`: 0-255, `00` = off. HA sends 1-254, a wall switch "on" sends `FF`.
- `time`: fade duration = `(time + 1) × 0.25 s` (1 s = `03`, 2 s = `07`, 2.5 s = `09`). The app's "Transition Time"
  ends up here; "Switch-Triggered Fade Rate" too, when dimming by holding a momentary switch
  (`B6 00 FF … 13` up / `B6 00 01 … 13` down, then `B7` on release).

## Frames HC32 → ESP

| CMD | Data | Meaning |
|---|---|---|
| `B0` | `01 01 02` | hello after start-up (firmware 1.1.2?), repeated every ~250 ms - 1 s until `C0` |
| `BD` | `<level>` | level report after a fade has finished; ESP answers `CD 00` |
| `B5` | `00 01 00 <min> <max>` | auto calibration finished; ESP answers `C5 00` |
| `CB` | `00 00 <I> <U_hi> <U_lo> <P_hi> <P_lo>` | metering: current in 0.01 A, voltage in 0.01 V, power in 0.01 W (big-endian). Sent by itself for a few seconds after a level change and when consumption changes; also the reply to `BB`. Not acknowledged. |
| `E0` | `00 <percent>` | auto calibration progress, steps of 5 %. Not acknowledged. |

Power readings were checked against the eWeLink app (3.88 W and 4.38 W, exact match).

## Start-up sequence (original firmware)

```
HC32  B0 01 01 02            (repeated until answered)
ESP   C0 00
ESP   B2                     → C2 00 <cal> <min> <max>
ESP   B9 01                  → C9 00
ESP   BA 00                  → CA 00
ESP   BF 00 00 00 00 00 32   → CF 00
ESP   B3                     → C3 00 00
ESP   B6 00 <last level> …   (restores the last level, only if the light was on)
```

## What lives where

- **HC32**: dimming type, load type, calibration (survives power loss), fade execution, metering.
- **ESP**: last brightness, transition time, fade rate, switch mode (rocker / momentary), button and LED logic.
  Changing these in the app sends nothing to the HC32.
