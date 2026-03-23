#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/build"

mkdir -p "$OUT"

CFLAGS=(
  -mcpu=cortex-m4
  -mthumb
  -std=gnu11
  -O2
  -g3
  -ffunction-sections
  -fdata-sections
  -fno-common
  -Wall
  -Wextra
  -Wno-unused-parameter
  -Wno-missing-field-initializers
  -DHC32F46x
  -DHC32F460
  -D__HC32F460__
  -D__FPU_PRESENT=1
  -include "$ROOT/Inc/my_machine.h"
  -DN_SPINDLE=2
  -DADAPTIVE_MULTI_AXIS_STEP_SMOOTHING=0
  -I"$ROOT"
  -I"$ROOT/Inc"
  -I"$ROOT/grbl"
  -I"$ROOT/SDK/main"
  -I"$ROOT/SDK/main/hdsc32core/common"
  -I"$ROOT/SDK/main/hdsc32core/Include"
  -I"$ROOT/SDK/drivers/library/inc"
)

LDFLAGS=(
  -mcpu=cortex-m4
  -mthumb
  -nostartfiles
  -Wl,--gc-sections
  -Wl,-Map="$OUT/firmware.map"
  -T"$ROOT/ldscripts/hc32f46x_flash.ld"
  -lm
)

CORE_SOURCES=(
  "$ROOT"/grbl/*.c
  "$ROOT"/grbl/kinematics/*.c
)

DRIVER_SOURCES=(
  "$ROOT"/Src/*.c
)

SDK_SOURCES=(
  "$ROOT/SDK/drivers/library/src/hc32f46x_clk.c"
  "$ROOT/SDK/drivers/library/src/hc32f46x_efm.c"
  "$ROOT/SDK/drivers/library/src/hc32f46x_exint_nmi_swi.c"
  "$ROOT/SDK/drivers/library/src/hc32f46x_gpio.c"
  "$ROOT/SDK/drivers/library/src/hc32f46x_pwc.c"
  "$ROOT/SDK/drivers/library/src/hc32f46x_timera.c"
  "$ROOT/SDK/drivers/library/src/hc32f46x_usart.c"
)

OBJECTS=()

for src in "${CORE_SOURCES[@]}" "${DRIVER_SOURCES[@]}" "${SDK_SOURCES[@]}"; do
  obj="$OUT/$(basename "${src%.*}").o"
  OBJECTS+=("$obj")
  arm-none-eabi-gcc "${CFLAGS[@]}" -c "$src" -o "$obj"
done

arm-none-eabi-gcc "${OBJECTS[@]}" "$ROOT/Src/startup_hc32f46x.S" "${LDFLAGS[@]}" -o "$OUT/firmware.elf"
arm-none-eabi-objcopy -O binary "$OUT/firmware.elf" "$OUT/firmware.bin"
arm-none-eabi-size "$OUT/firmware.elf"
