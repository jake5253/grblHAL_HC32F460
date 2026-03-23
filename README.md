# HC32F460 grblHAL Driver

This repository contains a working HC32F460 grblHAL driver focused on the basic CNC/laser bring-up path:

- serial console
- EEPROM-backed settings persistence
- 3-axis step/dir motion
- spindle / laser enable and PWM
- homing / limit / probe inputs

It is not a finished polished upstream-quality port yet, but it has moved past "scaffold" status and is usable for board bring-up and functional testing.

Repo layout follows the normal grblHAL driver pattern:

- driver sources live in `Src/` and `Inc/`
- board maps live in `boards/`
- the grblHAL core lives in the `grbl/` submodule
- required HC32 SDK sources are vendored locally in `SDK/`
- PlatformIO board metadata lives in `platformio_boards/`
- helper scripts live in `scripts/`

## Current State

Confirmed working on the current `BOARD_VOXELAB_AQUILA_V102` map:

- cold boot and normal startup banner
- serial RX/TX on `USART2` by default, with optional `USE_USART=1` switching for the display header
- settings persistence in external EEPROM on `PA11` / `PA12`
- `$RST=*` and normal settings reload
- planner allocation without the earlier runtime reduction warning
- 3-axis motion with HC32 TimerA-backed step timing
- dual selectable PWM spindles:
  - primary spindle on `PA1`
  - secondary laser-capable spindle on `PB1` with enable on `PB0`
- EXINT-backed hard-limit handling on X / Y / Z

Still rough / incomplete:

- step generation is a practical bring-up implementation, not a final high-performance backend
- control inputs on the display header are optional and need real hardware validation
- no board-specific polish beyond the current machine map

## Current Board Map

The default board definition is [`voxelab_aquila_v1_0_2_map.h`](boards/voxelab_aquila_v1_0_2_map.h).

The older [`my_machine_map.h`](boards/my_machine_map.h) is still kept as a local template / scratch board map.

Pins currently in use:

- Serial TX / RX, default build: `PA9` / `PA15` (`USART2`)
- Serial TX / RX, alternate build flag: `PC0` / `PC1` (`USART1`, `USE_USART=1`)
- EEPROM SDA: `PA11`
- EEPROM SCL: `PA12`
- X limit: `PA5`
- Y limit: `PA6`
- Z limit: `PA7`
- Probe: `PA4`
- Shared stepper enable: `PC3`
- X step / dir: `PC2` / `PB9`
- Y step / dir: `PB8` / `PB7`
- Z step / dir: `PB6` / `PB5`
- Primary spindle PWM: `PA1` (`TB_HEAD`)
- Secondary spindle enable: `PB0`
- Secondary spindle / laser PWM: `PB1`
- Flood coolant: `PA0` (`FAN_PIN_HEADER`)

Notes:

- this map assumes no spindle direction output
- grblHAL is built with `N_SPINDLE=2`
- the default startup spindle remains the primary `PA1` spindle
- `PA7` is a dedicated Z limit and `PA4` is a dedicated probe input
- board output aliases are `FAN_PIN_HEADER` (`PA0`), `TB_HEAD` (`PA1`), and `TB_BOARD` (`PA2`)
- X and Y limit wiring are expected to be normally closed, probe normally open
- hard limits use EXINT-backed interrupts for X / Y / Z
- optional direct-MCU control inputs can be enabled on `PB12` / `PB13` / `PB14`

Board-default motion settings baked into the current build:

- X/Y/Z steps per mm: `1600`
- X/Y max rate: `1200`
- Z max rate: `800`
- X/Y acceleration: `300`
- Z acceleration: `100`
- X/Y/Z max travel: `300 / 200 / 45`

These are compile-time defaults. After changing them in code, reflash and run:

```sh
$RST=*
```

## Build

Primary build path:

```sh
pio run
```

Or from outside the driver directory:

```sh
/home/j/.pyenv/shims/pio run -d /path/to/HC32F460
```

Current default PlatformIO environment:

- `hc32f460_voxelab_aquila_v102`

Useful build-time overrides can be supplied directly in `build_flags` rather than through extra environments. Common examples:

```ini
build_flags =
  ${env.build_flags}
  -DUSE_USART=1
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

PlatformIO outputs:

- `.pio/build/hc32f460_voxelab_aquila_v102/firmware.elf`
- `.pio/build/hc32f460_voxelab_aquila_v102/firmware.bin`

For convenience, the PlatformIO post-build step also copies the final artifacts to:

- `build/firmware.elf`
- `build/firmware.bin`
- `build/firmware.map`

Fallback shell build:

```sh
./scripts/build-hc32f460.sh
```

## Flash / Boot Assumptions

The firmware is linked for the HC32 loader environment already in use on this board:

- application start address: `0x0000C000`
- bootloader relocates `VTOR` to `0x0000C000`
- the bootloader-managed clock handoff is used as-is by the firmware

Preferred direct flash path:

```sh
# get pyocd
pip install pyocd

# fetch the HC32F460 device pack
pyocd pack install hc32f460

# flash firmware
pyocd load build/firmware.bin -t hc32f460 -a 0xC000
```

PlatformIO upload is wired to the same `pyocd` flow, so this also works if `pyocd` is installed and configured:

```sh
pio run -t upload
```

Probe used during bring-up:

- Raspberry Pi PicoProbe running `debugprobe_on_pico.uf2`
- `GP2 -> SWCLK`
- `GP3 -> SWDIO`
- `GND -> GND`

Recommended Linux udev rule for the PicoProbe CMSIS-DAP USB device:

```sh
echo 'ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000c", MODE="660", GROUP="plugdev", TAG+="uaccess"' | sudo tee /etc/udev/rules.d/99-pico-debug.rules
sudo udevadm control --reload-rules && sudo udevadm trigger
```

OpenOCD is still useful for live debug / SWD inspection. The config used during bring-up is:

- [`scripts/openocd-hc32f460.cfg`](scripts/openocd-hc32f460.cfg)

## Build / Runtime Notes

- settings storage uses external EEPROM, not internal flash emulation
- the current laser/spindle PWM implementation uses `TMRA1 CH7` on `PB1`
- step timing uses `TMRA6`
- hard limits use HC32 EXINT channels rather than a polling-only path
- probe input is `PA4`, dedicated Z limit is `PA7`
- optional reset / feed-hold / cycle-start inputs can be mapped to `PB12` / `PB13` / `PB14` with `CONTROL_ENABLE`
- PlatformIO uses a local custom board definition at `platformio_boards/genericHC32F460.json`
- PlatformIO pulls in the grbl core and the required HC32 SDK sources through `scripts/platformio_pre.py`
- this repo is self-contained for building once the `grbl/` submodule is initialized

## Repo Setup

Clone with submodules, or initialize the core submodule after cloning:

```sh
git submodule update --init --recursive
```

The `grbl/` directory is a normal grblHAL core submodule, following the same pattern used by other grblHAL drivers.

## Recommended Next Work

Highest-value next steps:

1. Validate both serial build variants on hardware: default `USART2` on `PA9` / `PA15` and `USE_USART1` on `PC0` / `PC1`.
2. Validate probe behavior on `PA4` and dedicated Z-limit behavior on `PA7`, including hard-limit trips during motion.
3. Validate optional `PB12` / `PB13` / `PB14` control inputs with external pull-ups / filtering / protection.
4. Validate laser mode behavior with `$32=1` during real motion.
5. Improve step timing throughput and overall motion performance.
