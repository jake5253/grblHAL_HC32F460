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
#define SERIAL_PORT             1
#define SERIAL1_PORT            2
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
#elif USE_USART == 2
#define SERIAL_PORT             2
#define SERIAL1_PORT            1
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
#define SERIAL_PORT_LABEL       "USART2"
#define SERIAL_AUX_PORT_USART   M4_USART1
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

#if N_AXIS > 3
#define M3_AVAILABLE            1
#define M3_STEP_PORT            PortB
#define M3_STEP_PIN             Pin04
#define M3_DIRECTION_PORT       PortB
#define M3_DIRECTION_PIN        Pin03
#endif

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
// Use PB1 as the sole laser control signal; main laser power is handled externally.
#define SPINDLE1_PWM_PORT       PortB
#define SPINDLE1_PWM_PIN        Pin01
#define SPINDLE1_PWM_FUNC       Func_Tima0
#define SPINDLE1_PWM_TIMER      M4_TMRA1
#define SPINDLE1_PWM_CLOCK      PWC_FCG2_PERIPH_TIMA1
#define SPINDLE1_PWM_CHANNEL    TimeraCh7
#define SPINDLE1_HAS_ENABLE     0
#define SPINDLE1_HAS_DIRECTION  0

// PA0 is left available for auxiliary output / fan plugin use instead of being
// hard-assigned as coolant flood on this board.

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

#if SDCARD_ENABLE
#define SDCARD_SDIO             1
#define SDIOC_UNIT              M4_SDIOC1
#define SDIOC_CLOCK             PWC_FCG1_PERIPH_SDIOC1
#define SDIOC_CK_PORT           PortC
#define SDIOC_CK_PIN            Pin12
#define SDIOC_CMD_PORT          PortD
#define SDIOC_CMD_PIN           Pin02
#define SDIOC_D0_PORT           PortC
#define SDIOC_D0_PIN            Pin08
#define SDIOC_D1_PORT           PortC
#define SDIOC_D1_PIN            Pin09
#define SDIOC_D2_PORT           PortC
#define SDIOC_D2_PIN            Pin10
#define SDIOC_D3_PORT           PortC
#define SDIOC_D3_PIN            Pin11
#define SD_DETECT_PORT          PortA
#define SD_DETECT_PIN           Pin10
#define SDIOC_DETECT_VIA_GPIO   1
#endif

/*
  Aquila V1.0.1 / V1.0.2 board notes:

  Known major ICs:
  - HC32F460KCTA MCU
  - 24C16 I2C EEPROM on PA11/PA12
  - CH340G USB/UART bridge on PA9/PA15
  - Four MS35775 stepper drivers (TMC2208-class clones)

  Motion hardware:
  - X: PC2 step, PB9 dir
  - Y: PB8 step, PB7 dir
  - Z: PB6 step, PB5 dir
  - E0: PB4 step, PB3 dir
  - Shared stepper enable: PC3
  - Driver strap resistors indicate fixed 1/16 microstepping

  High-current outputs:
  - PA0 switches both the part-cooling and motherboard fans in parallel
  - PA1 switches the hotend heater MOSFET
  - PA2 switches the heated-bed MOSFET

  Sensor / IO headers:
  - Filament runout: PA4
  - Endstops: PA5/PA6/PA7 for X/Y/Z
  - Thermistors: PC4 bed, PC5 hotend
  - BLTouch: PB0 servo, PB1 probe input
  - SDIO: PC8-PC11 data, PC12 clock, PD2 cmd, PA10 detect

  Accessible expansion candidates:
  - BLTouch servo pin PB0 if probing servo output is not needed
  - LCD connector pins PC0/PC1 and PB12-PB15
  - Fan output PA0 and heater output PA2 for non-thermally-managed repurposing

  Misc:
  - Status LED: PA3, active low
  - Crystal: 8 MHz on PH0/PH1
  - SWD: PA13/PA14
*/
