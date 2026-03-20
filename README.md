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
- serial RX/TX on `USART2`
- settings persistence in external EEPROM on `PA11` / `PA12`
- `$RST=*` and normal settings reload
- planner allocation without the earlier runtime reduction warning
- 3-axis motion with HC32 TimerA-backed step timing
- spindle / laser enable
- PWM output on `PB1` with sane monotonic `S` scaling

Still rough / incomplete:

- step generation is a practical bring-up implementation, not a final high-performance backend
- limit and probe handling are still simple GPIO polling paths
- no board-specific polish beyond the current machine map

## Current Board Map

The default board definition is [`voxelab_aquila_v1_0_2_map.h`](boards/voxelab_aquila_v1_0_2_map.h).

The older [`my_machine_map.h`](boards/my_machine_map.h) is still kept as a local template / scratch board map.

Pins currently in use:

- Serial TX: `PA9`
- Serial RX: `PA15`
- EEPROM SDA: `PA11`
- EEPROM SCL: `PA12`
- X limit: `PA5`
- Y limit: `PA6`
- Z limit: `PA7`
- Probe: `PA7`
- Shared stepper enable: `PC3`
- X step / dir: `PC2` / `PB9`
- Y step / dir: `PB8` / `PB7`
- Z step / dir: `PB6` / `PB5`
- Spindle enable: `PA1`
- Laser / spindle PWM: `PB1`
- Flood coolant: `PA0`

Notes:

- this map assumes no spindle direction output
- `PA7` is shared between Z limit and probe in the current board definition
- X and Y limit wiring are expected to be normally closed, probe normally open

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

1. Validate laser mode behavior with `$32=1` during real motion.
2. Tighten limit and probe behavior for the intended NC / NO wiring.
3. Improve step timing throughput and overall motion performance.
4. Decide whether to keep the fallback shell build once the PlatformIO flow has been used routinely on hardware.
