/*
  generic_map.h - provisional generic HC32F460 pin map / template

  Part of grblHAL
*/

#pragma once

#define BOARD_NAME              "HC32F460 Generic"

// Board-local pin map template for HC32F460 control boards.

// Serial console defaults to USART2 on PA9/PA15 (common CH340 path).
// Set USE_USART=1 in build flags to swap to USART1 on PC0/PC1.
#if USE_USART == 1
#define SERIAL_PORT             1
#define SERIAL1_PORT            2
#define SERIAL_PORT_USART       usart(1)
#define SERIAL_PORT_TX          PortC
#define SERIAL_PORT_TX_PIN      Pin00
#define SERIAL_PORT_TX_FUNC     Func_Usart1_Tx
#define SERIAL_PORT_RX          PortC
#define SERIAL_PORT_RX_PIN      Pin01
#define SERIAL_PORT_RX_FUNC     Func_Usart1_Rx
#define SERIAL_PORT_RI          INT_USART1_RI
#define SERIAL_PORT_TI          INT_USART1_TI
#define SERIAL_PORT_EI          INT_USART1_EI
#define SERIAL_PORT_TCI         INT_USART1_TCI
#define SERIAL_PORT_CLOCKS      (PWC_FCG1_PERIPH_USART1)
#define SERIAL_PORT_LABEL       "USART1"
#define SERIAL_AUX_PORT_USART   usart(2)
#define SERIAL_AUX_PORT_TX      PortA
#define SERIAL_AUX_PORT_TX_PIN  Pin09
#define SERIAL_AUX_PORT_TX_FUNC Func_Usart2_Tx
#define SERIAL_AUX_PORT_RX      PortA
#define SERIAL_AUX_PORT_RX_PIN  Pin15
#define SERIAL_AUX_PORT_RX_FUNC Func_Usart2_Rx
#define SERIAL_AUX_PORT_RI      INT_USART2_RI
#define SERIAL_AUX_PORT_TI      INT_USART2_TI
#define SERIAL_AUX_PORT_EI      INT_USART2_EI
#define SERIAL_AUX_PORT_TCI     INT_USART2_TCI
#define SERIAL_AUX_PORT_CLOCKS  (PWC_FCG1_PERIPH_USART2)
#define SERIAL_AUX_PORT_LABEL   "USART2"
#else
#define SERIAL_PORT             2
#define SERIAL1_PORT            1
#define SERIAL_PORT_USART       usart(2)
#define SERIAL_PORT_TX          PortA
#define SERIAL_PORT_TX_PIN      Pin09
#define SERIAL_PORT_TX_FUNC     Func_Usart2_Tx
#define SERIAL_PORT_RX          PortA
#define SERIAL_PORT_RX_PIN      Pin15
#define SERIAL_PORT_RX_FUNC     Func_Usart2_Rx
#define SERIAL_PORT_RI          INT_USART2_RI
#define SERIAL_PORT_TI          INT_USART2_TI
#define SERIAL_PORT_EI          INT_USART2_EI
#define SERIAL_PORT_TCI         INT_USART2_TCI
#define SERIAL_PORT_CLOCKS      (PWC_FCG1_PERIPH_USART2)
#define SERIAL_PORT_LABEL       "USART2"
#define SERIAL_AUX_PORT_USART   usart(1)
#define SERIAL_AUX_PORT_TX      PortC
#define SERIAL_AUX_PORT_TX_PIN  Pin00
#define SERIAL_AUX_PORT_TX_FUNC Func_Usart1_Tx
#define SERIAL_AUX_PORT_RX      PortC
#define SERIAL_AUX_PORT_RX_PIN  Pin01
#define SERIAL_AUX_PORT_RX_FUNC Func_Usart1_Rx
#define SERIAL_AUX_PORT_RI      INT_USART1_RI
#define SERIAL_AUX_PORT_TI      INT_USART1_TI
#define SERIAL_AUX_PORT_EI      INT_USART1_EI
#define SERIAL_AUX_PORT_TCI     INT_USART1_TCI
#define SERIAL_AUX_PORT_CLOCKS  (PWC_FCG1_PERIPH_USART1)
#define SERIAL_AUX_PORT_LABEL   "USART1"
#endif

// EEPROM pins for external settings storage (e.g. 24C16 on I2C-0).
#define EEPROM_SDA_PORT         PortA
#define EEPROM_SDA_PIN          Pin11
#define EEPROM_SCL_PORT         PortA
#define EEPROM_SCL_PIN          Pin12

// Step / direction / enable.
#define X_STEP_PORT             PortC
#define X_STEP_PIN              Pin02
#define Y_STEP_PORT             PortB
#define Y_STEP_PIN              Pin08
#define Z_STEP_PORT             PortB
#define Z_STEP_PIN              Pin06

#define X_DIRECTION_PORT        PortB
#define X_DIRECTION_PIN         Pin09
#define Y_DIRECTION_PORT        PortB
#define Y_DIRECTION_PIN         Pin07
#define Z_DIRECTION_PORT        PortB
#define Z_DIRECTION_PIN         Pin05

#define STEPPERS_ENABLE_PORT    PortC
#define STEPPERS_ENABLE_PIN     Pin03

// Limits and probe.
#define X_LIMIT_PORT            PortA
#define X_LIMIT_PIN             Pin05
#define Y_LIMIT_PORT            PortA
#define Y_LIMIT_PIN             Pin06
#define Z_LIMIT_PORT            PortA
#define Z_LIMIT_PIN             Pin07

#define PROBE_PORT              PortA
#define PROBE_PIN               Pin04

// Spindle / Laser PWM.
#define SPINDLE_PWM_PORT        PortB
#define SPINDLE_PWM_PIN         Pin01
#define SPINDLE_PWM_FUNC        Func_Tima0
#define SPINDLE_PWM_TIMER       timer(1)
#define SPINDLE_PWM_CLOCK       PWC_FCG2_PERIPH_TIMA1
#define SPINDLE_PWM_CHANNEL     TimeraCh7

#define SPINDLE_ENABLE_PORT     PortA
#define SPINDLE_ENABLE_PIN      Pin01
#define SPINDLE_HAS_DIRECTION   0

// Coolant / Auxiliary outputs.
#define COOLANT_FLOOD_PORT      PortA
#define COOLANT_FLOOD_PIN       Pin00
