# HC32F460 grblHAL Driver

A grblHAL driver for the HC32F460 SoC, developed and tested on the **Voxelab Aquila X2 / S2** 3D printer controller board (hardware revision **v1.0.2**). Most design decisions — pin assignments, peripheral choices, default motion settings — reflect that specific board.

This is not a finished, upstream-quality port. It started as a bring-up driver for the features I personally needed, and was later extended with additional functionality. Features that could not be verified on real hardware are clearly marked as [untested](#untested-features).

This should **NOT** be used for production equipment and is considered experimental, at best.
Use as your own risk


Note: AI was used extensively for this porting effort.  I have tested as much of it as I can with the hardware I have available, but your results may vary.
---

## Table of Contents

- [Status](#status)
- [Supported Features](#supported-features)
- [Untested Features](#untested-features)
- [Hardware Reference — Voxelab Aquila v1.0.2](#hardware-reference--voxelab-aquila-v102)
- [Board Map](#board-map)
- [Default Motion Settings](#default-motion-settings)
- [Build](#build)
- [Flashing](#flashing)
- [Debugging](#debugging)
- [Repository Layout](#repository-layout)

---

## Status

Confirmed working on `BOARD_VOXELAB_AQUILA_V102`:
- Original bootloader is preserved. Firmware can be installed using SDCard.
- Serial RX/TX via `USART2` (default) or `USART1` via `USE_USART=1` build flag
- Settings persistence: external EEPROM (`EEPROM_ENABLE=1`) or reserved internal flash (`EEPROM_ENABLE=0`)
- `$RST=*` and normal settings reload
- 4-axis step/dir motion using HC32 TimerA-backed step timing (`N_AXIS=3` or `N_AXIS=4`)
- Dual selectable PWM spindles (primary on `PA1` / **Head** (24V), secondary/laser on `PB1` / **BLTouch OUT** (5V [signal only]))
- EXINT-backed hard limits on X, Y, and Z

Known rough edges:

- Step generation is a practical bring-up implementation, not a tuned high-performance backend
- Control inputs on the display header are optional and need hardware validation
- No board-specific polish beyond the current machine map

---

## Supported Features

| Feature | Notes |
|---|---|
| Serial console | `USART2` default; `USART1` via `USE_USART=1` |
| Settings persistence | External EEPROM or reserved internal flash |
| 4-axis step/dir motion | TimerA-backed step timing |
| Primary spindle PWM | `PA1` — board label **Head** |
| Secondary spindle / laser PWM | `PB1` — **BLTouch OUT** header |
| Hard limits (X / Y / Z) | EXINT-backed interrupts |
| Homing inputs | X / Y / Z endstops |
| Probe input | `PA4` — filament sensor header (labelled **SILK** on board) |
| SD card | `SDCARD_ENABLE` |

---

## Untested Features

The following features were added but could not be verified due to lack of hardware:

| Feature | Build Flag | Notes |
|---|---|---|
| `USART1` serial path | `USE_USART=1` | Maps to display header pins `PC0` / `PC1` |
| Control inputs (reset / feed-hold / cycle-start) | `CONTROL_ENABLE` | Maps to `PB14` / `PB13` / `PB12` on display header |
| Fans plugin | `FANS_ENABLE` | `PA0` is the natural Aquila mapping for fan 0 |
| Odometer plugin | `ODOMETER_ENABLE=1` | Requires `EEPROM_ENABLE=1`; internal flash is incompatible as per the plugin |

---

## Hardware Reference — Voxelab Aquila v1.0.2

### USB Host Serial

Provided by a `CH340G` USB-to-serial converter bridging USB to UART on the HC32F460.

| Signal | MCU Pin | Notes |
|---|---|---|
| TX | `PA9` | |
| RX | `PA15` | |

* PA11/PA12 could theoretically be used for native USB, but they're wired to the EEPROM so I couldn't test it.

### SD Card Slot

| Signal | MCU Pin |
|---|---|
| D0 | `PC8` |
| D1 | `PC9` |
| D2 | `PC10` |
| D3 | `PC11` |
| CLK | `PC12` |
| CMD | `PD2` |
| DETECT | `PA10` |

### I²C EEPROM

| Signal | MCU Pin |
|---|---|
| SDA | `PA11` |
| SCL | `PA12` |

### Labeled Board Outputs

| Board Label | MCU Pin | Notes |
|---|---|---|
| **Head** | `PA1` | Primary spindle PWM output |
| **Board** | `PA2` | Available output |
| *(unmarked)* | `PA0` | Two 2.54 mm 1×2 pin headers next to the **CPU** LED; natural mapping for fan 0 |
| **TB** | `PC4` | Thermistor header (not really useful for many things, its circuitry is designed for use with thermistors) |
| **TH** | `PC5` | Thermistor header (not really useful for many things, its circuitry is designed for use with thermistors) |

### BLTouch Header

| Pin Label | MCU Pin | grblHAL Use |
|---|---|---|
| **IN** | `PB0` | Available |
| **OUT** | `PB1` | Secondary spindle / laser PWM (`TMRA1 CH7`) |
* Pin header also provides +5V and GND

### Endstop / Probe Inputs

| Board Label | MCU Pin | grblHAL Use |
|---|---|---|
| **X** | `PA5` | X hard limit (EXINT, normally closed) |
| **Y** | `PA6` | Y hard limit (EXINT, normally closed) |
| **Z** | `PA7` | Z hard limit (EXINT, normally closed) |
| **SILK** | `PA4` | Probe input (normally open) — filament runout sensor header (Pin header also provides +5V and GND) |

### Display Header

```
 ------
| 1  2 |
| 3  4 |
  5  6 |
| 7  8 |
| 9 10 |
 ------
```

| Pin | MCU Pin | grblHAL Use |
|---|---|---|
| 1 | `PC6` | — |
| 2 | `PB2` | — |
| 3 | `PC0` | Serial TX (`USART1`, `USE_USART=1`) [untested] |
| 4 | `PC1` | Serial RX (`USART1`, `USE_USART=1`) [untested] |
| 5 | `PB14` | Cycle-start control input (`CONTROL_ENABLE`) [untested] |
| 6 | `PB13` | Feed-hold control input (`CONTROL_ENABLE`) [untested] |
| 7 | `PB12` | Reset control input (`CONTROL_ENABLE`) [untested] |
| 8 | `PB15` | — |
| 9 | `GND` | |
| 10 | `+5V` | |

---

## Board Map

The default board definition is [`voxelab_aquila_v1_0_2_map.h`](boards/voxelab_aquila_v1_0_2_map.h).

The older [`my_machine_map.h`](boards/my_machine_map.h) is kept as a local scratch template.

### Active Pin Assignments

| Function | MCU Pin | Board Label |
|---|---|---|
| Serial TX (default, `USART2`) | `PA9` | via CH340G |
| Serial RX (default, `USART2`) | `PA15` | via CH340G |
| Serial TX (alt, `USART1`) | `PC0` | Display header pin 3 |
| Serial RX (alt, `USART1`) | `PC1` | Display header pin 4 |
| EEPROM SDA | `PA11` | *(when `EEPROM_ENABLE=1`)* |
| EEPROM SCL | `PA12` | *(when `EEPROM_ENABLE=1`)* |
| X limit | `PA5` | **X** |
| Y limit | `PA6` | **Y** |
| Z limit | `PA7` | **Z** |
| Probe | `PA4` | **SILK** (filament sensor header) |
| Stepper enable (shared) | `PC3` | — |
| X step / dir | `PC2` / `PB9` | — |
| Y step / dir | `PB8` / `PB7` | — |
| Z step / dir | `PB6` / `PB5` | — |
| Primary spindle PWM | `PA1` | **Head** |
| Secondary spindle / laser PWM | `PB1` | BLTouch **OUT** |

### Board Map Notes

- All pins not required for normal operation are designated as `ioport` pins, making them available for dynamic assignment via grblHAL's I/O port interface
- No spindle direction output is used on this map
- `N_SPINDLE=2` — primary spindle is `PA1` on startup
- X, Y and Z limits are normally closed; probe is normally open (pull-up enabled, connect to GND to trigger)
- Hard limits use EXINT-backed interrupts for X, Y, and Z
- `PA0` (**unmarked**) is aliased as `FAN_PIN_HEADER`; `PA1` (**Head**) as `TB_HEAD`; `PA2` (**Board**) as `TB_BOARD`

---

## Default Motion Settings

These are compile-time defaults baked into the board map. After changing them and reflashing, run `$RST=*` to apply.

| Axis | Steps/mm | Max Rate (mm/min) | Acceleration (mm/s²) | Max Travel (mm) |
|---|---|---|---|---|
| X | 1600 | 1200 | 300 | 300 |
| Y | 1600 | 1200 | 300 | 200 |
| Z | 1600 | 800 | 100 | 45 |

---

## Build

### PlatformIO (primary)

```sh
pio run
```

Or from outside the driver directory:

```sh
pio run -d /path/to/HC32F460
```

Default environment: `hc32f460_voxelab_aquila_v102`

Build outputs are placed in `.pio/build/hc32f460_voxelab_aquila_v102/` and also copied to `build/` by the post-build step:

- `build/firmware.elf`
- `build/firmware.bin`
- `build/firmware.map`

### Common Build Flag Overrides

Add these to `build_flags` in `platformio.ini` as needed:

```ini
build_flags =
  ${env.build_flags}
  -DEEPROM_ENABLE=0          ; use internal flash instead of EEPROM
  -DUSE_USART=1              ; switch serial to USART1 on display header
  -DCONTROL_ENABLE=(CONTROL_HALT|CONTROL_FEED_HOLD|CONTROL_CYCLE_START)
  -DPROBE_ENABLE=0
  -DN_SPINDLE=1
  -DESTOP_ENABLE=1
```

```ini
build_flags =
  ${env.build_flags}
  -DN_SPINDLE=2
  -DSPINDLE1_ENABLE=SPINDLE_PWM1_NODIR
```

### Shell Fallback

```sh
./scripts/build-hc32f460.sh
```

---

## Flashing

The firmware is linked for the HC32 bootloader environment already present on this board:
- Format SD Card (must be 16GB or less) as FAT32
- place `firmware.bin` in a folder named `firmware`
- power the board with SD Card inserted and it flashes the firmware. Successful flash will delete the file from SD Card.
* SDCARD_ENABLE is **NOT** required for this to work.

### With pyocd (preferred)

```sh
pip install pyocd
pyocd pack install hc32f460
pyocd load firmware.bin -t hc32f460 -a 0xC000
```

---

## Debugging

Bring-up was done using a Raspberry Pi PicoProbe running `debugprobe_on_pico.uf2`.

SWD wiring:

| PicoProbe | Target |
|---|---|
| `GP2` | `SWCLK` |
| `GP3` | `SWDIO` |
| `GND` | `GND` |

Linux udev rule for the PicoProbe CMSIS-DAP USB device:

```sh
echo 'ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", MODE="660", GROUP="plugdev", TAG+="uaccess"' | \
  sudo tee /etc/udev/rules.d/99-pico-debug.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

OpenOCD config used during bring-up: [`scripts/openocd-hc32f460.cfg`](scripts/openocd-hc32f460.cfg)

---

## Repository Layout

```
.
├── Src/                    # Driver source files
├── Inc/                    # Driver headers
├── boards/                 # Board map headers
│   ├── voxelab_aquila_v1_0_2_map.h   # Default board map
│   └── my_machine_map.h               # Local scratch template
├── grbl/                   # grblHAL core (submodule)
├── sdcard/                 # SD card plugin (submodule)
├── keypad/                 # Keypad plugin (submodule)
├── fans/                   # Fans plugin (submodule)
├── odometer/               # Odometer plugin (submodule)
├── SDK/                    # HC32 SDK sources (vendored)
├── platformio_boards/      # PlatformIO custom board definition
└── scripts/                # Helper scripts and OpenOCD config
```

Initialize submodules after cloning:

```sh
git submodule update --init --recursive
```

---

## Build / Runtime Notes

- `EEPROM_ENABLE=1` — uses the external 24C16-style EEPROM on `PA11` / `PA12`
- `EEPROM_ENABLE=0` — uses HC32's internal flash
- `ODOMETER_ENABLE=1` must be paired with `EEPROM_ENABLE=1`

