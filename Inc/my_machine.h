/*
  my_machine.h - configuration for HC32F460 ARM processors

  Part of grblHAL
*/

#pragma once

// Default local board target.
#if !defined(BOARD_MY_MACHINE) && !defined(BOARD_VOXELAB_AQUILA_V102)
#define BOARD_VOXELAB_AQUILA_V102
#endif

#ifndef N_AXIS
#define N_AXIS                  3
#endif
#define COMPATIBILITY_LEVEL     0

#ifndef BUILD_INFO_BASE
#define BUILD_INFO_BASE         "FFP0173_Aquila_Main_Board_V1.0.2"
#endif

#ifndef BUILD_INFO
#define BUILD_INFO              BUILD_INFO_BASE
#endif

#ifndef USE_USART
#define USE_USART              2
#endif

#ifndef MPG_ENABLE
#define MPG_ENABLE             0
#endif

#ifndef KEYPAD_ENABLE
#define KEYPAD_ENABLE          0
#endif

#ifndef SPINDLE_SELECT_ENABLE
#define SPINDLE_SELECT_ENABLE  1
#endif

#if MPG_ENABLE == 1
#error "MPG_ENABLE=1 is not supported on HC32F460 yet. Use MPG_ENABLE=2 for UART MPG mode."
#endif

#if KEYPAD_ENABLE == 1
#error "KEYPAD_ENABLE=1 is not supported on HC32F460 yet. I2C keypad support is not implemented."
#endif

#if MPG_ENABLE > 2
#error "MPG_ENABLE must be 0 or 2 on HC32F460."
#endif

#if KEYPAD_ENABLE > 2
#error "KEYPAD_ENABLE must be 0 or 2 on HC32F460."
#endif

#if MPG_ENABLE == 2 || KEYPAD_ENABLE == 2
#ifndef AUX_UART_STREAM
#define AUX_UART_STREAM        1
#endif
#endif

#if MPG_ENABLE == 2
#ifndef MPG_STREAM
#define MPG_STREAM             AUX_UART_STREAM
#endif
#endif

#if KEYPAD_ENABLE == 2
#ifndef KEYPAD_STREAM
#define KEYPAD_STREAM          AUX_UART_STREAM
#endif
#endif

#if MPG_ENABLE == 2 && KEYPAD_ENABLE == 2 && MPG_STREAM != KEYPAD_STREAM
#error "HC32F460 has a single auxiliary UART stream. MPG_STREAM and KEYPAD_STREAM must match."
#endif

#ifndef EEPROM_ENABLE
#define EEPROM_ENABLE          1
#endif

#if N_AXIS > 3 && !defined(BOARD_VOXELAB_AQUILA_V102)
#error "N_AXIS > 3 is currently only implemented for BOARD_VOXELAB_AQUILA_V102."
#endif

// Board-default machine profile for the stock Aquila kinematics.
// These values become active after reflashing and resetting settings with $RST=*.
#ifdef BOARD_VOXELAB_AQUILA_V102
#define DEFAULT_X_STEPS_PER_MM  800.0f
#define DEFAULT_Y_STEPS_PER_MM  800.0f
#define DEFAULT_Z_STEPS_PER_MM  800.0f

#define DEFAULT_X_MAX_RATE      1200.0f
#define DEFAULT_Y_MAX_RATE      1200.0f
#define DEFAULT_Z_MAX_RATE      800.0f

#define DEFAULT_X_ACCELERATION  300.0f
#define DEFAULT_Y_ACCELERATION  300.0f
#define DEFAULT_Z_ACCELERATION  100.0f

#define DEFAULT_X_MAX_TRAVEL    300.0f
#define DEFAULT_Y_MAX_TRAVEL    200.0f
#define DEFAULT_Z_MAX_TRAVEL    45.0f

#define DEFAULT_SPINDLE_PWM_FREQ 40000

// Optional direct-MCU control inputs on the display header.
// Uncomment or provide via build flags when external conditioning is present.
//#define CONTROL_ENABLE          (CONTROL_HALT|CONTROL_FEED_HOLD|CONTROL_CYCLE_START)

// Common useful build-time overrides for this board:
// -D USE_USART=1
//     Route the main serial console to USART1 on PC0/PC1 instead of USART2 on PA9/PA15.
// -D USE_USART=2
//     Use the default console on USART2 on PA9/PA15.
// -D MPG_ENABLE=2
//     Enable UART MPG mode on the non-primary USART using real-time command switchover.
// -D KEYPAD_ENABLE=2
//     Enable experimental UART keypad mode on the non-primary USART.
//     This is integrated and build-tested, but not yet validated on HC32 hardware.
// -D PROBE_ENABLE=0
//     Disable the dedicated probe input and build without probe support.
// -D CONTROL_ENABLE=(CONTROL_HALT|CONTROL_FEED_HOLD|CONTROL_CYCLE_START)
//     Enable halt/feed-hold/cycle-start on PB12/PB13/PB14.
// -D ESTOP_ENABLE=1
//     Make the halt input behave as a physical e-stop instead of a reset input.
// -D EEPROM_ENABLE=0
//     Use internal flash-backed NVS instead of the external EEPROM on PA11/PA12.
// -D N_SPINDLE=1
//     Build without the secondary spindle / laser registration.
// -D N_SPINDLE=2 -D SPINDLE1_ENABLE=SPINDLE_PWM1_NODIR
//     Enable the secondary PWM spindle / laser on PB1.
// -D SPINDLE_SELECT_ENABLE=0
//     Disable the local runtime spindle-selection addon even when multiple spindles are built.
// -D FANS_ENABLE=1
//     Enable the upstream fans plugin submodule. Fan mapping is handled through aux port settings.
// -D ODOMETER_ENABLE=1
//     Enable the upstream odometer plugin submodule. Use with EEPROM_ENABLE=1; flash-backed NVS is not suitable.
// -D N_AXIS=4
//     Repurpose the Aquila E-stepper socket as an optional A axis on PB4/PB3.
//     This axis has no dedicated limit/home input and should be tuned/reset separately after flashing.
#endif

// Default probe input is enabled by the core when not overridden.
// Keep coolant enabled for baseline support.
