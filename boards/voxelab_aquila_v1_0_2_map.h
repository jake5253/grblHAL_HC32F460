/*
  voxelab_aquila_v1_0_2_map.h - Voxelab Aquila main board v1.0.2 pin map

  Board ID: FFP0173_Aquila_Main_Board_V1.0.2

  Part of grblHAL
*/

#pragma once

#define BOARD_NAME              "Voxelab Aquila V1.0.2 (HC32F460)"

// Main serial console defaults to the onboard CH340 / micro USB path on USART2.
// Set USE_USART=1 to use the display header PC0/PC1 pins instead.

#if USE_USART == 1
#define SERIAL_PORT_USART       M4_USART1
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
#elif USE_USART == 2
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
#else
#error "USE_USART must be set to 1 or 2"
#endif

// External 24C16 EEPROM on I2C-0.
#define EEPROM_SDA_PORT         PortA
#define EEPROM_SDA_PIN          Pin11
#define EEPROM_SCL_PORT         PortA
#define EEPROM_SCL_PIN          Pin12

// Shared stepper enable plus X/Y/Z motion pins.
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

// Endstop / probe headers.
#define X_LIMIT_PORT            PortA
#define X_LIMIT_PIN             Pin05
#define Y_LIMIT_PORT            PortA
#define Y_LIMIT_PIN             Pin06
#define Z_LIMIT_PORT            PortA
#define Z_LIMIT_PIN             Pin07

// Use PA7 as a dedicated Z limit. PA4 is a safer probe candidate than the thermistor inputs.
#define PROBE_PORT              PortA
#define PROBE_PIN               Pin04

// Board-silkscreened high-current outputs.
// FAN_PIN_HEADER is the pair of 1x2 pin headers above HEAD, both switched together by PA0.
#define FAN_PIN_HEADER_PORT     PortA
#define FAN_PIN_HEADER_PIN      Pin00
#define FAN_PIN_HEADER_FUNC     Func_Tima0
#define FAN_PIN_HEADER_TIMER    M4_TMRA2
#define FAN_PIN_HEADER_CLOCK    PWC_FCG2_PERIPH_TIMA2
#define FAN_PIN_HEADER_CHANNEL  TimeraCh1

#define TB_HEAD_PORT            PortA
#define TB_HEAD_PIN             Pin01
#define TB_HEAD_FUNC            Func_Tima0
#define TB_HEAD_TIMER           M4_TMRA2
#define TB_HEAD_CLOCK           PWC_FCG2_PERIPH_TIMA2
#define TB_HEAD_CHANNEL         TimeraCh2

#define TB_BOARD_PORT           PortA
#define TB_BOARD_PIN            Pin02
#define TB_BOARD_FUNC           Func_Tima0
#define TB_BOARD_TIMER          M4_TMRA2
#define TB_BOARD_CLOCK          PWC_FCG2_PERIPH_TIMA2
#define TB_BOARD_CHANNEL        TimeraCh3

// Default functional assignment: primary spindle PWM on the HEAD terminal block.
#define SPINDLE0_PWM_PORT       TB_HEAD_PORT
#define SPINDLE0_PWM_PIN        TB_HEAD_PIN
#define SPINDLE0_PWM_FUNC       TB_HEAD_FUNC
#define SPINDLE0_PWM_TIMER      TB_HEAD_TIMER
#define SPINDLE0_PWM_CLOCK      TB_HEAD_CLOCK
#define SPINDLE0_PWM_CHANNEL    TB_HEAD_CHANNEL
#define SPINDLE0_HAS_ENABLE     0
#define SPINDLE0_HAS_DIRECTION  0

// Compatibility aliases for the primary spindle expected by grblHAL's generic option layer.
#define SPINDLE_PWM_PORT        SPINDLE0_PWM_PORT
#define SPINDLE_PWM_PIN         SPINDLE0_PWM_PIN
#define SPINDLE_PWM_FUNC        SPINDLE0_PWM_FUNC
#define SPINDLE_ENABLE_PORT     SPINDLE0_PWM_PORT
#define SPINDLE_ENABLE_PIN      SPINDLE0_PWM_PIN

// Secondary PWM spindle / laser on the BLTouch connector.
#define SPINDLE1_PWM_PORT       PortB
#define SPINDLE1_PWM_PIN        Pin01
#define SPINDLE1_PWM_FUNC       Func_Tima0
#define SPINDLE1_PWM_TIMER      M4_TMRA1
#define SPINDLE1_PWM_CLOCK      PWC_FCG2_PERIPH_TIMA1
#define SPINDLE1_PWM_CHANNEL    TimeraCh7
#define SPINDLE1_ENABLE_PORT    PortB
#define SPINDLE1_ENABLE_PIN     Pin00
#define SPINDLE1_HAS_ENABLE     1
#define SPINDLE1_HAS_DIRECTION  0

// Default functional assignment: flood coolant on the fan pin header pair.
#define COOLANT_FLOOD_PORT      FAN_PIN_HEADER_PORT
#define COOLANT_FLOOD_PIN       FAN_PIN_HEADER_PIN

// Optional direct-MCU control inputs on the display header.
#define CONTROL_PORT            PortB
#if CONTROL_ENABLE & CONTROL_HALT
#define RESET_PIN               Pin12
#endif
#if CONTROL_ENABLE & CONTROL_FEED_HOLD
#define FEED_HOLD_PIN           Pin13
#endif
#if CONTROL_ENABLE & CONTROL_CYCLE_START
#define CYCLE_START_PIN         Pin14
#endif

/*
  Expansion / future-use notes for this board:

  Easily accessible external-use pins:
  - BLTouch IN: PB0 (used here as secondary spindle / laser enable)
  - Filament sensor / probe candidate: PA4 (10k pull-up on board)
  - LCD connector UART / spare GPIO: PC0 / PC1
  - FAN_PIN_HEADER: PA0 (two linked 1x2 headers)
  - TB_BOARD: PA2

  Other notable board-connected signals:
  - Status LED: PA3
  - E0 stepper: PB4 step, PB3 dir
  - SDIO: PC8-PC11, PC12, PD2, detect on PA10
  - Thermistors: PC5 hotend, PC4 bed
  - LCD header GPIOs: PB12, PB13, PB14, PB15
*/
