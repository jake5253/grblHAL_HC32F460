/*
  my_machine.h - HC32F460 grblHAL driver configuration
*/

#ifndef MY_MACHINE_H
#define MY_MACHINE_H

// NOTE: Only one board may be enabled!
// If none is enabled pin mappings from generic_map.h will be used.
//#define BOARD_VOXELAB_AQUILA_V102

#ifndef N_AXIS
#define N_AXIS                  3
#endif

#define COMPATIBILITY_LEVEL     0

#ifndef BUILD_INFO
#define BUILD_INFO              "HC32F460 grblHAL"
#endif

#ifndef USE_USART
#define USE_USART               2
#endif

#ifndef MPG_ENABLE
#define MPG_ENABLE              0
#endif

#ifndef KEYPAD_ENABLE
#define KEYPAD_ENABLE           0
#endif

#ifndef SPINDLE_SELECT_ENABLE
#define SPINDLE_SELECT_ENABLE   1
#endif

#if MPG_ENABLE == 2 || KEYPAD_ENABLE == 2
#ifndef AUX_UART_STREAM
#define AUX_UART_STREAM         1
#endif
#endif

#if MPG_ENABLE == 2
#ifndef MPG_STREAM
#define MPG_STREAM              AUX_UART_STREAM
#endif
#endif

#if KEYPAD_ENABLE == 2
#ifndef KEYPAD_STREAM
#define KEYPAD_STREAM           AUX_UART_STREAM
#endif
#endif

#ifndef EEPROM_ENABLE
#define EEPROM_ENABLE           1
#endif

#endif
