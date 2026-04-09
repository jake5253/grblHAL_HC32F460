/*
  driver.h - HC32F460 grblHAL driver definitions

  Part of grblHAL
*/

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef OVERRIDE_MY_MACHINE
#include "my_machine.h"
#endif

#include "hc32_ddl.h"
#include "hc32f46x_adc.h"
#include "hc32f46x_clk.h"
#include "hc32f46x_efm.h"
#include "hc32f46x_exint_nmi_swi.h"
#include "hc32f46x_gpio.h"
#include "hc32f46x_interrupts.h"
#include "hc32f46x_pwc.h"
#include "hc32f46x_timera.h"
#include "hc32f46x_usart.h"

#ifdef BIT_SET
#undef BIT_SET
#endif

#include "grbl/driver_opts.h"

#if defined(BOARD_VOXELAB_AQUILA_V102)
#include "boards/voxelab_aquila_v1_0_2_map.h"
#elif defined(BOARD_MY_MACHINE)
#include "boards/my_machine_map.h"
#else
#include "boards/generic_map.h"
#endif

#include "grbl/driver_opts2.h"

#define STEPPER_IRQ             Int010_IRQn
#define STEPPER_PULSE_IRQ       Int011_IRQn
#define LIMIT_X_IRQ             Int012_IRQn
#define LIMIT_Y_IRQ             Int013_IRQn
#define LIMIT_Z_IRQ             Int014_IRQn
#define CONTROL_IRQ             Int015_IRQn

#define STEPPER_TIMER           M4_TMRA6
#define STEPPER_TIMER_CLOCK     PWC_FCG2_PERIPH_TIMA6
#define STEPPER_TIMER_INT       INT_TMRA6_OVF
#define STEPPER_TIMER_CMP_INT   INT_TMRA6_CMP
#define STEPPER_TIMER_DIVIDER   TimeraPclkDiv64
#define STEPPER_TIMER_DIV       64u
#define STEPPER_TIMER_CLOCK_HZ  (SystemCoreClock / STEPPER_TIMER_DIV)
#define STEPPER_DELAY_CHANNEL   TimeraCh1
#define STEPPER_PULSE_CHANNEL   TimeraCh2

#define SPINDLE_PWM_DIVIDER     TimeraPclkDiv8
#define SPINDLE_PWM_DIV         8u
#define SPINDLE_PWM_CLOCK_HZ    (SystemCoreClock / SPINDLE_PWM_DIV)

#ifndef SPINDLE0_HAS_ENABLE
#define SPINDLE0_HAS_ENABLE     0
#endif

#ifndef SPINDLE0_HAS_DIRECTION
#define SPINDLE0_HAS_DIRECTION  0
#endif

#ifndef SPINDLE1_HAS_ENABLE
#define SPINDLE1_HAS_ENABLE     0
#endif

#ifndef SPINDLE1_HAS_DIRECTION
#define SPINDLE1_HAS_DIRECTION  0
#endif

#define STEP_PULSE_LENGTH_US    5u

#define HC32_FLASH_BASE         0x00000000u
#define HC32_FLASH_APP_START    0x0000C000u
#define HC32_FLASH_NVS_BASE     0x0007C000u
#define HC32_FLASH_NVS_SIZE     0x00002000u

static inline uint32_t pinmask_to_pinno (uint16_t pinmask)
{
    uint32_t pin = 0;

    while(pin < 16u && ((pinmask >> pin) & 1u) == 0u)
        pin++;

    return pin;
}

static inline void hc32_gpio_write (en_port_t port, uint16_t pin, bool on)
{
    if(on)
        PORT_SetBits(port, pin);
    else
        PORT_ResetBits(port, pin);
}

static inline bool hc32_gpio_read (en_port_t port, uint16_t pin)
{
    return PORT_GetBit(port, pin) == Set;
}

bool flash_nvs_is_valid (void);

typedef struct {
    bool erase_ok;
    bool erase_blank;
    bool program_ok;
    uint32_t fpmtsw_before_erase;
    uint32_t fpmtew_before_erase;
    uint32_t status_after_erase;
    uint32_t status_after_program;
    uint32_t expected[4];
    uint32_t readback[4];
} flash_selftest_info_t;

bool flash_selftest_run (flash_selftest_info_t *info);
bool flash_selftest_erase_only (flash_selftest_info_t *info);
bool flash_selftest_program_only (flash_selftest_info_t *info);
void flash_selftest_dump (uint32_t *words, uint32_t count);
void flash_selftest_breakpoint (void);

static inline void hc32_gpio_config_output (en_port_t port, uint16_t pin)
{
    stc_port_init_t cfg = {0};

    cfg.enPinMode = Pin_Mode_Out;
    cfg.enPinDrv = Pin_Drv_H;
    cfg.enPinOType = Pin_OType_Cmos;
    cfg.enPullUp = Disable;
    cfg.enExInt = Disable;
    cfg.enPinSubFunc = Disable;

    PORT_SetFunc(port, pin, Func_Gpio, Disable);
    PORT_Init(port, pin, &cfg);
}

static inline void hc32_gpio_config_input (en_port_t port, uint16_t pin, bool pullup)
{
    stc_port_init_t cfg = {0};

    cfg.enPinMode = Pin_Mode_In;
    cfg.enPinDrv = Pin_Drv_L;
    cfg.enPinOType = Pin_OType_Cmos;
    cfg.enPullUp = pullup ? Enable : Disable;
    cfg.enExInt = Disable;
    cfg.enPinSubFunc = Disable;

    PORT_SetFunc(port, pin, Func_Gpio, Disable);
    PORT_Init(port, pin, &cfg);
}

static inline void hc32_gpio_config_input_exint (en_port_t port, uint16_t pin, bool pullup)
{
    stc_port_init_t cfg = {0};

    cfg.enPinMode = Pin_Mode_In;
    cfg.enPinDrv = Pin_Drv_L;
    cfg.enPinOType = Pin_OType_Cmos;
    cfg.enPullUp = pullup ? Enable : Disable;
    cfg.enExInt = Enable;
    cfg.enPinSubFunc = Disable;

    PORT_SetFunc(port, pin, Func_Gpio, Disable);
    PORT_Init(port, pin, &cfg);
}

typedef struct {
    en_int_src_t source;
    IRQn_Type irq;
    func_ptr_t handler;
    uint32_t priority;
} hc32_irq_registration_t;

bool hc32_irq_register (hc32_irq_registration_t registration);
void hc32_ioports_init (void);
