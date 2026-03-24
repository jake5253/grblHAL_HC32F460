/*
  my_machine.h - configuration for HC32F460 ARM processors

  Part of grblHAL
*/

#pragma once

// Default local board target.
#if !defined(BOARD_MY_MACHINE) && !defined(BOARD_VOXELAB_AQUILA_V102)
#define BOARD_VOXELAB_AQUILA_V102
#endif

#define N_AXIS                  3
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

#ifndef EEPROM_ENABLE
#define EEPROM_ENABLE          1
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
// -D PROBE_ENABLE=0
//     Disable the dedicated probe input and build without probe support.
// -D CONTROL_ENABLE=(CONTROL_HALT|CONTROL_FEED_HOLD|CONTROL_CYCLE_START)
//     Enable halt/feed-hold/cycle-start on PB12/PB13/PB14.
// -D ESTOP_ENABLE=1
//     Make the halt input behave as a physical e-stop instead of a reset input.
// -D EEPROM_ENABLE=0
//     Disable the external EEPROM-backed NVS backend.
// -D N_SPINDLE=1
//     Build without the secondary spindle / laser registration.
// -D N_SPINDLE=2 -D SPINDLE1_ENABLE=SPINDLE_PWM1_NODIR
//     Enable the secondary PWM spindle / laser on PB1 with enable on PB0.
#endif

// Default probe input is enabled by the core when not overridden.
// Keep coolant enabled for baseline support.
