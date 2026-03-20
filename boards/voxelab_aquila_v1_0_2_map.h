/*
  voxelab_aquila_v1_0_2_map.h - Voxelab Aquila main board v1.0.2 pin map

  Board ID: FFP0173_Aquila_Main_Board_V1.0.2

  Part of grblHAL
*/

#pragma once

#define BOARD_NAME              "Voxelab Aquila V1.0.2 (HC32F460)"

// Main serial console through the onboard CH340 / micro USB port.
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

// The stock board exposes the probe signal on the Z endstop connector.
#define PROBE_PORT              PortA
#define PROBE_PIN               Pin07

// Laser PWM on the BLTouch "OUT" pin.
#define SPINDLE_PWM_PORT        PortB
#define SPINDLE_PWM_PIN         Pin01
#define SPINDLE_PWM_FUNC        Func_Tima0

// Spindle enable on the NOZZLE connector.
#define SPINDLE_ENABLE_PORT     PortA
#define SPINDLE_ENABLE_PIN      Pin01

#define SPINDLE_HAS_DIRECTION   0

#define SPINDLE_PWM_TIMER       M4_TMRA1
#define SPINDLE_PWM_CLOCK       PWC_FCG2_PERIPH_TIMA1
#define SPINDLE_PWM_CHANNEL     TimeraCh7

// Stock fan output FET.
#define COOLANT_FLOOD_PORT      PortA
#define COOLANT_FLOOD_PIN       Pin00

/*
  Expansion / future-use notes for this board:

  Easily accessible external-use pins:
  - BLTouch IN: PB0
  - Filament sensor: PA4 (10k pull-up on board)
  - LCD connector UART: PC0 / PC1 (USART4-capable general serial pins)
  - Fan output: PA0
  - Bed heater FET: PA2

  Other notable board-connected signals:
  - Status LED: PA3
  - E0 stepper: PB4 step, PB3 dir
  - SDIO: PC8-PC11, PC12, PD2, detect on PA10
  - Thermistors: PC5 hotend, PC4 bed
  - LCD buttons / beeper: PB15, PB12, PB13, PB14
*/

