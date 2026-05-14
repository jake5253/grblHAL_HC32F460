# HC32F460 Driver Add-ons

Local add-ons for the HC32F460 grblHAL driver. These are driver-owned integrations that behave like plugins but are managed within the driver repository.

## Current Add-ons

### [Relays](relays/)
Provides support for controlling multiple relays via user M-codes (M160/M161). Typically used for auxiliary hardware like vacuum pumps or lighting.
- **Enable**: Set `-D RELAYS_ENABLE=1` in `platformio.ini`.

### [Spindle Select](spindle_select/)
A local runtime spindle-selection addon derived from the STM32F4xx reference. It allows switching between multiple spindles/lasers (e.g., M3 vs M3.1) during operation.
- **Enable**: Set `-D SPINDLE_SELECT_ENABLE=1` in `platformio.ini` (enabled by default in `my_machine.h`).
- **Dependency**: Requires `N_SPINDLE > 1`.

## Build System Integration

These add-ons are modularized and included in the build via PlatformIO's `lib_deps` mechanism:

```ini
lib_deps =
  ...
  addons/relays
  addons/spindle_select
```

Each add-on directory is treated as a separate library module, ensuring clean separation of concerns and matching the grblHAL reference architecture.
