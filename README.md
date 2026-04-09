# HC32F460 grblHAL Driver

A grblHAL driver for the HC32F460 SoC, developed and tested on the **Voxelab Aquila X2 / S2** 3D printer controller board (hardware revision **v1.0.2**). Most design decisions — pin assignments, peripheral choices, default motion settings - reflect that specific board.

This is not a finished, feature-complete port. It started as a bring-up driver for the features I personally wanted, and was later extended with additional functionality and plugins support. Features that could not be verified on real hardware are clearly marked as [untested](#untested-features).

> **⚠ This should NOT be used for production equipment and is considered experimental, at best. Use at your own risk.**
>
> Note: AI was used extensively for this porting effort. I have tested as much of it as I can with the hardware I have available, but your results may vary.

---

## Table of Contents

- [HC32F460 grblHAL Driver](#hc32f460-grblhal-driver)
  - [Table of Contents](#table-of-contents)
  - [Status](#status)
  - [Supported Features](#supported-features)
  - [Untested Features](#untested-features)
  - [Build](#build)
  - [Flashing](#flashing)
    - [Via SD Card (recommended)](#via-sd-card-recommended)
    - [Via pyocd](#via-pyocd)
  - [Debugging](#debugging)
  - [Repository Layout](#repository-layout)
  - [Hardware Reference — Voxelab Aquila v1.0.2](#hardware-reference--voxelab-aquila-v102)
    - [Build Flags Reference](#build-flags-reference)
    - [Active Pin Assignments](#active-pin-assignments)
    - [Default Motion Settings](#default-motion-settings)
    - [USB Host Serial](#usb-host-serial)
    - [SD Card Slot](#sd-card-slot)
    - [I²C EEPROM](#ic-eeprom)
    - [Labeled Board Outputs](#labeled-board-outputs)
    - [BLTouch Header](#bltouch-header)
    - [Endstop / Probe Inputs](#endstop--probe-inputs)
    - [Display Header](#display-header)

---

## Status

Confirmed working on `BOARD_VOXELAB_AQUILA_V102`:

- Original bootloader is preserved; firmware can be installed via SD card (grblHAL SD Card support is not required for this to work)
- Serial RX/TX via `USART2` (via CH340 -> microUSB connector on-board)
- Settings persistence via external EEPROM or reserved internal flash
- `$RST=*` and normal settings reload
- 3-axis step/dir motion using HC32 TimerA-backed step timing (4-axis motion is available but I only have a 3-axis machine for testing)
- Dual selectable PWM spindles (primary on `PA1` / **Head** 24V, secondary/laser on `PB1` / **BLTouch OUT** 5V signal)
- EXINT-backed hard limits on X, Y, and Z (using X, Y, Z limit headers, respectively)
- EXINT-backed Z-probe (connected to filament runout header)

---

## Supported Features

| Feature | Notes / Options |
|---|---|
| Serial | USART2 (default); USART1 |
| Settings persistence | External EEPROM; Internal reserved flash |
| Motion | 3 or 4-axis; TimerA-backed step timing |
| Spindles | Primary PWM; Secondary PWM (laser) |
| Limits | EXINT-backed interrupts |
| Probe | Optional; working; `PA4` |
| SD card | Optional; working |

---

## Untested Features

The following features are supported but untested due to lack of hardware:

| Feature | Notes |
|---|---|
| `USART1` serial path | Maps to display header pins `PC0` / `PC1` |
| Control inputs (reset / feed-hold / cycle-start) | Maps to `PB14` / `PB13` / `PB12` on display header |
| Fans plugin | Default maps to `PA0` (2 fan headers on Aquila board) |
| Odometer plugin | Requires external EEPROM; incompatible with internal flash |

---

## Build

To build, just clone this repo with submodules and use PlatformIO

```sh
git clone --recurse-submodules $repo
cd HC32F460
# make any changes you want
```

Then build:


```sh
pio run [-e your_board_environment]
```
Available build flags and their defaults are documented in the [Build Flags Reference](#build-flags-reference). See also: [platformio.ini](platformio.ini).

Default environment: `hc32f460_voxelab_aquila_v102`. Build artifacts are copied to `build/` (`firmware.elf`, `firmware.bin`, `firmware.map`).


---

## Flashing

### Via SD Card (recommended)

The stock bootloader is preserved on the board. Firmware is flashed by dropping a file onto an SD card - no programmer required. `SDCARD_ENABLE` is **not** needed for this method.

1. Format an SD card (16 GB or smaller) as FAT32
2. Create a folder named `firmware` 
3. Build firmware and place `build/firmware.bin` inside it: `[sdcard]/firmware/firmware.bin`
4. Eject SD Card after writing, insert the card into your board and power on - the bootloader flashes automatically
5. A successful flash deletes `firmware.bin` from the card

**If it doesn't flash, try a different card**. This board is picky about what cards it will and wont flash firmware files from.  When flashing, the LED on the Aquila board blinks quickly; you can also see serial output if you're connected to the board via serial

### Via pyocd

Alternatively, if you don't have an SD card or the board doesn't like your SD card, you can connect an ST-Link, PicoProbe, or similar flasher/debugger to the [debug header](#debugging), then:

```sh
pip install pyocd
pyocd pack install hc32f460
pyocd load build/firmware.bin -t hc32f460 -a 0xC000
```

---

## Debugging

Bring-up was done using a Raspberry Pi Pico with PicoProbe firmware (`debugprobe_on_pico.uf2`).

SWD wiring:

| PicoProbe | Target |
|---|---|
| `GP2` | `SWCLK` |
| `GP3` | `SWDIO` |
| `GND` | `GND` |

You'll probably need to add the Linux udev rule for the PicoProbe CMSIS-DAP USB device:

```sh
# make sure you're in the plugdev group
sudo adduser $SUDO_USER plugdev
newgrp plugdev
```
Then add the udev rule:
```sh
echo 'ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", MODE="660", GROUP="plugdev", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/99-picoprobe-debug.rules
sudo udevadm control --reload-rules 
sudo udevadm trigger
# unplug and re-plug the device
```

OpenOCD config: [`scripts/openocd-hc32f460.cfg`](scripts/openocd-hc32f460.cfg)

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

After cloning, initialize submodules:

```sh
git submodule update --init --recursive
```

---

## Hardware Reference — Voxelab Aquila v1.0.2

Pin assignments and hardware details specific to the `BOARD_VOXELAB_AQUILA_V102` board map. The default board definition is [`voxelab_aquila_v1_0_2_map.h`](boards/voxelab_aquila_v1_0_2_map.h). The older [`my_machine_map.h`](boards/my_machine_map.h) is kept as a local scratch template.

### Build Flags Reference

All flags are set via `build_flags` in `platformio.ini`.

| Flag | Values | Default | Notes |
|---|---|---|---|
| `EEPROM_ENABLE` | `0` / `1` | `1` | `1` = external 24C16-style EEPROM on `PA11`/`PA12`; `0` = reserved internal flash |
| `USE_USART` | `1` | — | Switch serial to `USART1` on display header (`PC0`/`PC1`) [untested] |
| `N_AXIS` | `3` / `4` | `3` | Number of motion axes |
| `N_SPINDLE` | `1` / `2` | `2` | `2` enables secondary spindle/laser on `PB1` |
| `SPINDLE1_ENABLE` | `SPINDLE_PWM1_NODIR` | — | Required when `N_SPINDLE=2` |
| `CONTROL_ENABLE` | `(CONTROL_HALT\|CONTROL_FEED_HOLD\|CONTROL_CYCLE_START)` | — | Enables control inputs on display header [untested] |
| `PROBE_ENABLE` | `0` / `1` | `1` | Disable if probe input is unused |
| `ESTOP_ENABLE` | `1` | — | Enable emergency-stop input |
| `SDCARD_ENABLE` | `1` | — | Enable SD card support (not required for bootloader flashing) |
| `FANS_ENABLE` | `1` | — | Fans plugin; `PA0` is the natural Aquila mapping [untested] |
| `ODOMETER_ENABLE` | `1` | — | Odometer plugin; requires `EEPROM_ENABLE=1` |

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

- All pins not required for normal operation are designated as `ioport` pins and are available for dynamic assignment via grblHAL's I/O port interface
- No spindle direction output is used on this map
- `N_SPINDLE=2` — primary spindle (`PA1`) is selected on startup
- X, Y, and Z limits are normally closed; probe is normally open (pull-up enabled, connect to GND to trigger)

### Default Motion Settings

Compile-time defaults baked into the board map. After reflashing with new values, run `$RST=*` to apply them.

| Axis | Steps/mm | Max Rate (mm/min) | Acceleration (mm/s²) | Max Travel (mm) |
|---|---|---|---|---|
| X | 1600 | 1200 | 300 | 300 |
| Y | 1600 | 1200 | 300 | 200 |
| Z | 1600 | 800 | 100 | 45 |

### USB Host Serial

Provided by a `CH340G` USB-to-serial converter bridging USB to UART on the HC32F460.

| Signal | MCU Pin |
|---|---|
| TX | `PA9` |
| RX | `PA15` |

> `PA11`/`PA12` could theoretically be used for native USB, but they are wired to the EEPROM on this board.

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
| **Head** | `PA1` | Primary spindle PWM output; aliased as `TB_HEAD` |
| **Board** | `PA2` | Available output; aliased as `TB_BOARD` |
| *(unmarked)* | `PA0` | Two 2.54 mm 1×2 headers next to the **CPU** LED; natural fan 0 mapping; aliased as `FAN_PIN_HEADER` |
| **TB** | `PC4` | Thermistor input (circuitry intended for thermistors only) |
| **TH** | `PC5` | Thermistor input (circuitry intended for thermistors only) |

### BLTouch Header

Provides +5V, GND, and two signal pins.

| Pin Label | MCU Pin | grblHAL Use |
|---|---|---|
| **IN** | `PB0` | Available |
| **OUT** | `PB1` | Secondary spindle / laser PWM (`TMRA1 CH7`) |

### Endstop / Probe Inputs

| Board Label | MCU Pin | grblHAL Use |
|---|---|---|
| **X** | `PA5` | X hard limit (EXINT, normally closed) |
| **Y** | `PA6` | Y hard limit (EXINT, normally closed) |
| **Z** | `PA7` | Z hard limit (EXINT, normally closed) |
| **SILK** | `PA4` | Probe input (normally open) — filament runout sensor header; provides +5V and GND |

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