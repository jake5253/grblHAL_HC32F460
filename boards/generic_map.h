/*
  generic_map.h - provisional generic HC32F460 pin map

  Part of grblHAL
*/

#pragma once

#define BOARD_NAME              "HC32F460 Generic"

// Serial console on the common CH340-connected pins.
#define SERIAL_PORT             1
#define SERIAL1_PORT            2
#define SERIAL_PORT_USART       M4_USART1
#define SERIAL_PORT_TX          PortA
#define SERIAL_PORT_TX_PIN      Pin09
#define SERIAL_PORT_TX_FUNC     Func_Usart1_Tx
#define SERIAL_PORT_RX          PortA
#define SERIAL_PORT_RX_PIN      Pin15
#define SERIAL_PORT_RX_FUNC     Func_Usart1_Rx
#define SERIAL_PORT_RI          INT_USART1_RI
#define SERIAL_PORT_TI          INT_USART1_TI
#define SERIAL_PORT_EI          INT_USART1_EI
#define SERIAL_PORT_TCI         INT_USART1_TCI
#define SERIAL_PORT_CLOCKS      (PWC_FCG1_PERIPH_USART1)
#define SERIAL_PORT_LABEL       "USART1"
#define SERIAL_AUX_PORT_USART   M4_USART2
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

// Step / direction / enable.
#define X_STEP_PORT             PortB
#define X_STEP_PIN              Pin06
#define Y_STEP_PORT             PortB
#define Y_STEP_PIN              Pin07
#define Z_STEP_PORT             PortB
#define Z_STEP_PIN              Pin08

#define X_DIRECTION_PORT        PortB
#define X_DIRECTION_PIN         Pin09
#define Y_DIRECTION_PORT        PortB
#define Y_DIRECTION_PIN         Pin10
#define Z_DIRECTION_PORT        PortB
#define Z_DIRECTION_PIN         Pin11

#define STEPPERS_ENABLE_PORT    PortB
#define STEPPERS_ENABLE_PIN     Pin12

// Limits and probe.
#define X_LIMIT_PORT            PortC
#define X_LIMIT_PIN             Pin13
#define Y_LIMIT_PORT            PortC
#define Y_LIMIT_PIN             Pin14
#define Z_LIMIT_PORT            PortC
#define Z_LIMIT_PIN             Pin15

#define PROBE_PORT              PortD
#define PROBE_PIN               Pin02

// Spindle / laser.
#define SPINDLE_PWM_PORT        PortA
#define SPINDLE_PWM_PIN         Pin00
#define SPINDLE_PWM_FUNC        Func_Tima0

#define SPINDLE_DIRECTION_PORT  PortA
#define SPINDLE_DIRECTION_PIN   Pin01
#define SPINDLE_HAS_DIRECTION   1

#define SPINDLE_ENABLE_PORT     PortA
#define SPINDLE_ENABLE_PIN      Pin02

// TimerA2 channels on PA0/PA1/PA2.
#define SPINDLE_PWM_TIMER       M4_TMRA2
#define SPINDLE_PWM_CLOCK       PWC_FCG2_PERIPH_TIMA2
#define SPINDLE_PWM_CHANNEL     TimeraCh1
