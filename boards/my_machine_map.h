/*
  my_machine_map.h - board-local HC32F460 pin map template

  Part of grblHAL
*/

#pragma once

#define BOARD_NAME              "HC32F460 MyMachine"

// Board-local pin map from the current HC32F460 control board.

// Serial console on the CH340-connected pins.
#define SERIAL_PORT_USART       M4_USART2
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

// EEPROM pins for external settings storage.
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

// Limits and probe. Z limit and probe currently share PA7.
#define X_LIMIT_PORT            PortA
#define X_LIMIT_PIN             Pin05
#define Y_LIMIT_PORT            PortA
#define Y_LIMIT_PIN             Pin06
#define Z_LIMIT_PORT            PortA
#define Z_LIMIT_PIN             Pin07

#define PROBE_PORT              PortA
#define PROBE_PIN               Pin07

// Laser PWM on PB1 via TimerA1 channel 7.
#define SPINDLE_PWM_PORT        PortB
#define SPINDLE_PWM_PIN         Pin01
#define SPINDLE_PWM_FUNC        Func_Tima0

// Current assumption: spindle enable on PA1. PA2 may also be usable on this board.
#define SPINDLE_ENABLE_PORT     PortA
#define SPINDLE_ENABLE_PIN      Pin01

#define SPINDLE_HAS_DIRECTION   0

#define SPINDLE_PWM_TIMER       M4_TMRA1
#define SPINDLE_PWM_CLOCK       PWC_FCG2_PERIPH_TIMA1
#define SPINDLE_PWM_CHANNEL     TimeraCh7

// Coolant pins noted for later implementation.
#define COOLANT_FLOOD_PORT      PortA
#define COOLANT_FLOOD_PIN       Pin00
