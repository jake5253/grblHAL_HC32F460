/*
  driver.c - HC32F460 grblHAL driver

  Part of grblHAL
*/

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "main.h"
#include "flash.h"
#include "sdcard_port.h"
#include "serial.h"

#include "grbl/hal.h"
#include "grbl/crc.h"
#include "grbl/motor_pins.h"
#include "grbl/nvs_buffer.h"
#include "grbl/probe.h"
#include "grbl/settings.h"
#include "grbl/spindle_control.h"
#include "grbl/system.h"

static bool io_init_done = false;
static volatile uint32_t systicks = 0;
static uint8_t probe_invert_mask = 0;
static volatile axes_signals_t pending_step_outbits = {0};
static bool stepper_timer_ready = false;
static bool limits_irq_ready = false;
static uint16_t step_pulse_ticks = 1u;
static uint16_t step_delay_ticks = 0u;
static spindle_id_t spindle0_id = -1;
static spindle_id_t spindle1_id = -1;
static spindle1_pwm_settings_t *spindle1_settings = NULL;
#if !EEPROM_ENABLE
static bool flash_nvs_bootstrap_pending = false;
#endif

typedef struct {
    spindle_pwm_t pwm;
    spindle_pwm_settings_t *settings;
    M4_TMRA_TypeDef *timer;
    uint32_t clock;
    en_timera_channel_t channel;
    en_port_t pwm_port;
    uint16_t pwm_pin;
    en_port_func_t pwm_func;
    bool pwm_ready;
    bool output_enabled;
    bool has_enable;
    en_port_t enable_port;
    uint16_t enable_pin;
    bool has_direction;
    en_port_t direction_port;
    uint16_t direction_pin;
} hc32_spindle_t;

typedef struct {
    en_port_t port;
    uint16_t pin;
    IRQn_Type irq;
    en_exti_ch_t channel;
    en_int_src_t source;
} hc32_limit_irq_t;

static hc32_spindle_t spindle0 = {
    .pwm = { .offset = -1 },
    .timer = SPINDLE0_PWM_TIMER,
    .clock = SPINDLE0_PWM_CLOCK,
    .channel = SPINDLE0_PWM_CHANNEL,
    .pwm_port = SPINDLE0_PWM_PORT,
    .pwm_pin = SPINDLE0_PWM_PIN,
    .pwm_func = SPINDLE0_PWM_FUNC,
    .has_enable = SPINDLE0_HAS_ENABLE,
#if SPINDLE0_HAS_ENABLE
    .enable_port = SPINDLE0_ENABLE_PORT,
    .enable_pin = SPINDLE0_ENABLE_PIN,
#endif
    .has_direction = SPINDLE0_HAS_DIRECTION,
};

static hc32_spindle_t spindle1 = {
    .pwm = { .offset = -1 },
    .timer = SPINDLE1_PWM_TIMER,
    .clock = SPINDLE1_PWM_CLOCK,
    .channel = SPINDLE1_PWM_CHANNEL,
    .pwm_port = SPINDLE1_PWM_PORT,
    .pwm_pin = SPINDLE1_PWM_PIN,
    .pwm_func = SPINDLE1_PWM_FUNC,
    .has_enable = SPINDLE1_HAS_ENABLE,
#if SPINDLE1_HAS_ENABLE
    .enable_port = SPINDLE1_ENABLE_PORT,
    .enable_pin = SPINDLE1_ENABLE_PIN,
#endif
    .has_direction = SPINDLE1_HAS_DIRECTION,
};

static void driver_delay_ms (uint32_t ms, delay_callback_ptr callback);
static void settings_changed (settings_t *settings, settings_changed_flags_t changed);
static bool driver_setup (settings_t *settings);
static uint16_t timer_ticks_from_us (float us);
static uint16_t stepper_timer_period (uint32_t cycles_per_tick);
static void stepper_timer_start (uint16_t period);
static void stepper_timer_stop (void);
static uint16_t stepper_schedule_compare (en_timera_channel_t channel, uint16_t ticks);
static void stepper_pulse_arm (axes_signals_t step_outbits);
static bool stepper_timer_init (void);
static bool limits_irq_init (void);
static limit_signals_t limitsGetState (void);
static void enumeratePins (bool low_level, pin_info_ptr callback, void *data);
static bool spindle_pwm_init (hc32_spindle_t *spindle);
static void spindle_pwm_output_enable (hc32_spindle_t *spindle, bool enable);
static void spindle_set_pwm_value (hc32_spindle_t *spindle, uint_fast16_t pwm_value);
static hc32_spindle_t *get_spindle_data (spindle_ptrs_t *spindle);
static void spindle_settings_changed (spindle1_pwm_settings_t *settings);

#if SDCARD_ENABLE && defined(SD_DETECT_PIN)
static float sd_detect_pin_value (xbar_t *pin);
#endif

static en_exti_ch_t exti_channel_from_pin (uint16_t pin)
{
    return (en_exti_ch_t)pinmask_to_pinno(pin);
}

static en_int_src_t exti_source_from_channel (en_exti_ch_t channel)
{
    return (en_int_src_t)((uint32_t)INT_PORT_EIRQ0 + (uint32_t)channel);
}

static const char *port_name (en_port_t port)
{
    switch(port) {
        case PortA: return "PA";
        case PortB: return "PB";
        case PortC: return "PC";
        case PortD: return "PD";
        case PortE: return "PE";
        case PortH: return "PH";
        default: return "";
    }
}

static void emit_pin (pin_info_ptr callback, void *data, uint8_t id, pin_function_t function, pin_group_t group, en_port_t port, uint16_t pinmask, const char *description)
{
    xbar_t pin = {
        .id = id,
        .function = function,
        .group = group,
        .port = (void *)port_name(port),
        .description = description,
        .pin = pinmask_to_pinno(pinmask)
    };

    callback(&pin, data);
}

#if SDCARD_ENABLE && defined(SD_DETECT_PIN)
static float sd_detect_pin_value (xbar_t *pin)
{
    (void)pin;

    return hc32_sdcard_is_inserted() ? 0.0f : 1.0f;
}
#endif

static void enumeratePins (bool low_level, pin_info_ptr callback, void *data)
{
    (void)low_level;

    uint8_t id = 0;

    emit_pin(callback, data, id++, Output_TX, PinGroup_UART, SERIAL_PORT_TX, SERIAL_PORT_TX_PIN, SERIAL_PORT_LABEL);
    emit_pin(callback, data, id++, Input_RX, PinGroup_UART, SERIAL_PORT_RX, SERIAL_PORT_RX_PIN, SERIAL_PORT_LABEL);

#if MPG_ENABLE == 2 || KEYPAD_ENABLE == 2
    emit_pin(callback, data, id++, Output_TX, PinGroup_UART + AUX_UART_STREAM, SERIAL_AUX_PORT_TX, SERIAL_AUX_PORT_TX_PIN, SERIAL_AUX_PORT_LABEL);
    emit_pin(callback, data, id++, Input_RX, PinGroup_UART + AUX_UART_STREAM, SERIAL_AUX_PORT_RX, SERIAL_AUX_PORT_RX_PIN, SERIAL_AUX_PORT_LABEL);
#endif

#if EEPROM_ENABLE
    emit_pin(callback, data, id++, Bidirectional_I2CSDA, PinGroup_I2C, EEPROM_SDA_PORT, EEPROM_SDA_PIN, "EEPROM SDA");
    emit_pin(callback, data, id++, Output_I2CSCK, PinGroup_I2C, EEPROM_SCL_PORT, EEPROM_SCL_PIN, "EEPROM SCL");
#endif

    emit_pin(callback, data, id++, Input_LimitX, PinGroup_Limit, X_LIMIT_PORT, X_LIMIT_PIN, "X limit");
    emit_pin(callback, data, id++, Input_LimitY, PinGroup_Limit, Y_LIMIT_PORT, Y_LIMIT_PIN, "Y limit");
    emit_pin(callback, data, id++, Input_LimitZ, PinGroup_Limit, Z_LIMIT_PORT, Z_LIMIT_PIN, "Z limit");

#if PROBE_ENABLE
    emit_pin(callback, data, id++, Input_Probe, PinGroup_Probe, PROBE_PORT, PROBE_PIN, "Probe");
#endif

    emit_pin(callback, data, id++, Output_StepX, PinGroup_StepperStep, X_STEP_PORT, X_STEP_PIN, "X step");
    emit_pin(callback, data, id++, Output_StepY, PinGroup_StepperStep, Y_STEP_PORT, Y_STEP_PIN, "Y step");
    emit_pin(callback, data, id++, Output_StepZ, PinGroup_StepperStep, Z_STEP_PORT, Z_STEP_PIN, "Z step");
#ifdef A_AXIS
    emit_pin(callback, data, id++, Output_StepA, PinGroup_StepperStep, A_STEP_PORT, A_STEP_PIN, "A step");
#endif

    emit_pin(callback, data, id++, Output_DirX, PinGroup_StepperDir, X_DIRECTION_PORT, X_DIRECTION_PIN, "X dir");
    emit_pin(callback, data, id++, Output_DirY, PinGroup_StepperDir, Y_DIRECTION_PORT, Y_DIRECTION_PIN, "Y dir");
    emit_pin(callback, data, id++, Output_DirZ, PinGroup_StepperDir, Z_DIRECTION_PORT, Z_DIRECTION_PIN, "Z dir");
#ifdef A_AXIS
    emit_pin(callback, data, id++, Output_DirA, PinGroup_StepperDir, A_DIRECTION_PORT, A_DIRECTION_PIN, "A dir");
#endif

    emit_pin(callback, data, id++, Output_StepperEnable, PinGroup_StepperEnable, STEPPERS_ENABLE_PORT, STEPPERS_ENABLE_PIN, "Stepper enable");

    emit_pin(callback, data, id++, Output_SpindlePWM, PinGroup_SpindlePWM, SPINDLE0_PWM_PORT, SPINDLE0_PWM_PIN, "Spindle 1 PWM / TB_HEAD");
#if SPINDLE1_HAS_ENABLE
    emit_pin(callback, data, id++, Output_Spindle1On, PinGroup_SpindleControl, SPINDLE1_ENABLE_PORT, SPINDLE1_ENABLE_PIN, "Spindle 2 enable");
#endif
    emit_pin(callback, data, id++, Output_Spindle1PWM, PinGroup_SpindlePWM, SPINDLE1_PWM_PORT, SPINDLE1_PWM_PIN, "Spindle 2 PWM");

#ifdef COOLANT_FLOOD_PIN
    emit_pin(callback, data, id++, Output_CoolantFlood, PinGroup_Coolant, COOLANT_FLOOD_PORT, COOLANT_FLOOD_PIN, "Flood / FAN_PIN_HEADER");
#endif
#ifdef COOLANT_MIST_PIN
    emit_pin(callback, data, id++, Output_CoolantMist, PinGroup_Coolant, COOLANT_MIST_PORT, COOLANT_MIST_PIN, "Mist");
#endif

#if defined(RESET_PIN) && !ESTOP_ENABLE
    emit_pin(callback, data, id++, Input_Reset, PinGroup_Control, RESET_PORT, RESET_PIN, "Reset");
#endif
#if defined(RESET_PIN) && ESTOP_ENABLE
    emit_pin(callback, data, id++, Input_EStop, PinGroup_Control, RESET_PORT, RESET_PIN, "E-stop");
#endif
#ifdef FEED_HOLD_PIN
    emit_pin(callback, data, id++, Input_FeedHold, PinGroup_Control, FEED_HOLD_PORT, FEED_HOLD_PIN, "Feed hold");
#endif
#ifdef CYCLE_START_PIN
    emit_pin(callback, data, id++, Input_CycleStart, PinGroup_Control, CYCLE_START_PORT, CYCLE_START_PIN, "Cycle start");
#endif

#if SDCARD_ENABLE && defined(SD_DETECT_PIN)
    xbar_t sd_detect = {
        .id = id++,
        .function = Input_SdCardDetect,
        .group = PinGroup_SdCard,
        .port = (void *)port_name(SD_DETECT_PORT),
        .description = "SD card detect",
        .pin = pinmask_to_pinno(SD_DETECT_PIN),
        .get_value = sd_detect_pin_value
    };

    callback(&sd_detect, data);
#endif
}

static void limit_irq_handler (void)
{
    static const hc32_limit_irq_t limits[] = {
        { X_LIMIT_PORT, X_LIMIT_PIN, LIMIT_X_IRQ, 0, 0 },
        { Y_LIMIT_PORT, Y_LIMIT_PIN, LIMIT_Y_IRQ, 0, 0 },
        { Z_LIMIT_PORT, Z_LIMIT_PIN, LIMIT_Z_IRQ, 0, 0 }
    };
    bool triggered = false;

    for(uint_fast8_t i = 0; i < sizeof(limits) / sizeof(limits[0]); i++) {
        en_exti_ch_t channel = exti_channel_from_pin(limits[i].pin);

        if(EXINT_IrqFlgGet(channel) == Set) {
            EXINT_IrqFlgClr(channel);
            triggered = true;
        }
    }

    if(!triggered || hal.limits.interrupt_callback == NULL)
        return;

    limit_signals_t state = limitsGetState();

#if PROBE_ENABLE && PROBE_PORT == X_LIMIT_PORT && PROBE_PIN == X_LIMIT_PIN
    if(sys.probing_state == Probing_Active)
        state.min.x = Off;
#endif
#if PROBE_ENABLE && PROBE_PORT == Y_LIMIT_PORT && PROBE_PIN == Y_LIMIT_PIN
    if(sys.probing_state == Probing_Active)
        state.min.y = Off;
#endif
#if PROBE_ENABLE && PROBE_PORT == Z_LIMIT_PORT && PROBE_PIN == Z_LIMIT_PIN
    if(sys.probing_state == Probing_Active)
        state.min.z = Off;
#endif

    if(state.min.mask)
        hal.limits.interrupt_callback(state);
}

static void set_step_outputs (axes_signals_t step_outbits)
{
    step_outbits.mask ^= settings.steppers.step_invert.mask;

    hc32_gpio_write(X_STEP_PORT, X_STEP_PIN, step_outbits.x);
    hc32_gpio_write(Y_STEP_PORT, Y_STEP_PIN, step_outbits.y);
    hc32_gpio_write(Z_STEP_PORT, Z_STEP_PIN, step_outbits.z);
#ifdef A_AXIS
    hc32_gpio_write(A_STEP_PORT, A_STEP_PIN, step_outbits.a);
#endif
}

static void set_dir_outputs (axes_signals_t dir_outbits)
{
    dir_outbits.mask ^= settings.steppers.dir_invert.mask;

    hc32_gpio_write(X_DIRECTION_PORT, X_DIRECTION_PIN, dir_outbits.x);
    hc32_gpio_write(Y_DIRECTION_PORT, Y_DIRECTION_PIN, dir_outbits.y);
    hc32_gpio_write(Z_DIRECTION_PORT, Z_DIRECTION_PIN, dir_outbits.z);
#ifdef A_AXIS
    hc32_gpio_write(A_DIRECTION_PORT, A_DIRECTION_PIN, dir_outbits.a);
#endif
}

static void stepperEnable (axes_signals_t enable, bool hold)
{
    (void)hold;

    enable.mask ^= settings.steppers.enable_invert.mask;
    hc32_gpio_write(STEPPERS_ENABLE_PORT, STEPPERS_ENABLE_PIN, enable.x);
}

static void stepperWakeUp (void)
{
    stepperEnable((axes_signals_t){AXES_BITMASK}, false);
    stepper_timer_start(stepper_timer_period(STEPPER_TIMER_CLOCK_HZ / 500u));
}

static void stepperGoIdle (bool clear_signals)
{
    stepper_timer_stop();
    pending_step_outbits.mask = 0;

    if(clear_signals) {
        set_step_outputs((axes_signals_t){0});
        set_dir_outputs((axes_signals_t){0});
    }
}

static void stepperCyclesPerTick (uint32_t cycles_per_tick)
{
    if(stepper_timer_ready)
        TIMERA_SetPeriodValue(STEPPER_TIMER, stepper_timer_period(cycles_per_tick));
}

static void stepperPulseStart (stepper_t *stepper)
{
    if(stepper->dir_changed.bits)
        set_dir_outputs(stepper->dir_out);

    if(stepper->step_out.bits)
        stepper_pulse_arm(stepper->step_out);
}

static void stepperPulseStartDelayed (stepper_t *stepper)
{
    if(stepper->dir_changed.bits)
        set_dir_outputs(stepper->dir_out);

    if(stepper->step_out.bits) {
        pending_step_outbits = stepper->step_out;
        stepper_schedule_compare(STEPPER_DELAY_CHANNEL, step_delay_ticks);
    }
}

static void configure_control_inputs (settings_t *settings)
{
#ifdef RESET_PIN
    hc32_gpio_config_input(RESET_PORT, RESET_PIN, !(settings->control_disable_pullup.mask & SIGNALS_RESET_BIT));
#endif
#ifdef FEED_HOLD_PIN
    hc32_gpio_config_input(FEED_HOLD_PORT, FEED_HOLD_PIN, !(settings->control_disable_pullup.mask & SIGNALS_FEEDHOLD_BIT));
#endif
#ifdef CYCLE_START_PIN
    hc32_gpio_config_input(CYCLE_START_PORT, CYCLE_START_PIN, !(settings->control_disable_pullup.mask & SIGNALS_CYCLESTART_BIT));
#endif
}

static void limitsEnable (bool on, axes_signals_t homing_cycle)
{
    if(!limits_irq_ready)
        return;

    IRQn_Type irqs[] = { LIMIT_X_IRQ, LIMIT_Y_IRQ, LIMIT_Z_IRQ };
    axes_signals_t masks[] = {
        { .x = On },
        { .y = On },
        { .z = On }
    };

    for(uint_fast8_t i = 0; i < sizeof(irqs) / sizeof(irqs[0]); i++) {
        NVIC_ClearPendingIRQ(irqs[i]);

        if(on && !(homing_cycle.mask & masks[i].mask))
            NVIC_EnableIRQ(irqs[i]);
        else
            NVIC_DisableIRQ(irqs[i]);
    }
}

static limit_signals_t limitsGetState (void)
{
    limit_signals_t signals = {0};

    signals.min.x = hc32_gpio_read(X_LIMIT_PORT, X_LIMIT_PIN);
    signals.min.y = hc32_gpio_read(Y_LIMIT_PORT, Y_LIMIT_PIN);
    signals.min.z = hc32_gpio_read(Z_LIMIT_PORT, Z_LIMIT_PIN);
    signals.min.mask ^= settings.limits.invert.mask;

    return signals;
}

static home_signals_t homeGetState (void)
{
    home_signals_t home = {0};

    home.a = limitsGetState().min;

    return home;
}

static control_signals_t systemGetState (void)
{
    control_signals_t signals = { settings.control_invert.mask };

#if defined(RESET_PIN) && !ESTOP_ENABLE
    signals.reset = hc32_gpio_read(RESET_PORT, RESET_PIN);
#endif
#if defined(RESET_PIN) && ESTOP_ENABLE
    signals.e_stop = hc32_gpio_read(RESET_PORT, RESET_PIN);
#endif
#ifdef FEED_HOLD_PIN
    signals.feed_hold = hc32_gpio_read(FEED_HOLD_PORT, FEED_HOLD_PIN);
#endif
#ifdef CYCLE_START_PIN
    signals.cycle_start = hc32_gpio_read(CYCLE_START_PORT, CYCLE_START_PIN);
#endif

    if(settings.control_invert.mask)
        signals.value ^= settings.control_invert.mask;

    return signals;
}

#if PROBE_ENABLE
static void probeConfigure (bool is_probe_away, bool probing)
{
    (void)probing;

    probe_invert_mask = settings.probe.invert_probe_pin ? 0u : 1u;

    if(is_probe_away)
        probe_invert_mask ^= 1u;
}

static probe_state_t probeGetState (void)
{
    probe_state_t state = {
        .connected = On,
        .probe_id = Probe_Default
    };

    bool triggered = hc32_gpio_read(PROBE_PORT, PROBE_PIN);
    state.triggered = triggered ^ probe_invert_mask;

    return state;
}
#endif

static void spindle_off (spindle_ptrs_t *spindle)
{
    hc32_spindle_t *ctx = get_spindle_data(spindle);

    if(ctx->has_enable)
        hc32_gpio_write(ctx->enable_port, ctx->enable_pin, ctx->settings->invert.on);

    if(ctx->pwm.flags.always_on)
        spindle_set_pwm_value(ctx, ctx->pwm.off_value);
    else
        spindle_pwm_output_enable(ctx, false);
}

static void spindle_on (spindle_ptrs_t *spindle)
{
    hc32_spindle_t *ctx = get_spindle_data(spindle);

    if(ctx->has_enable)
        hc32_gpio_write(ctx->enable_port, ctx->enable_pin, !ctx->settings->invert.on);
}

static void spindle_dir (spindle_ptrs_t *spindle, bool ccw)
{
    hc32_spindle_t *ctx = get_spindle_data(spindle);

    if(ctx->has_direction)
        hc32_gpio_write(ctx->direction_port, ctx->direction_pin, ccw ^ ctx->settings->invert.ccw);
    else
        (void)ccw;
}

static void spindle_set_speed (spindle_ptrs_t *spindle, uint_fast16_t pwm_value)
{
    hc32_spindle_t *ctx = get_spindle_data(spindle);

    if(pwm_value == ctx->pwm.off_value && !ctx->pwm.flags.always_on)
        spindle_pwm_output_enable(ctx, false);
    else {
        spindle_pwm_output_enable(ctx, true);
        spindle_set_pwm_value(ctx, pwm_value);
    }
}

static uint_fast16_t spindleGetPWM (spindle_ptrs_t *spindle, float rpm)
{
    return spindle->context.pwm->compute_value(spindle->context.pwm, rpm, false);
}

static void spindleSetState (spindle_ptrs_t *spindle, spindle_state_t state, float rpm)
{
    if(!state.on) {
        spindle_off(spindle);
        return;
    }

    spindle_dir(spindle, state.ccw);
    spindle_set_speed(spindle, spindleGetPWM(spindle, rpm));
    spindle_on(spindle);
}

static spindle_state_t spindleGetState (spindle_ptrs_t *spindle)
{
    hc32_spindle_t *ctx = get_spindle_data(spindle);

    spindle_state_t state = {0};
    state.on = ctx->has_enable
        ? hc32_gpio_read(ctx->enable_port, ctx->enable_pin)
        : ctx->output_enabled;

    if(ctx->has_direction)
        state.ccw = hc32_gpio_read(ctx->direction_port, ctx->direction_pin);

    state.value ^= ctx->settings->invert.mask;

    return state;
}

static bool spindleConfig (spindle_ptrs_t *spindle)
{
    hc32_spindle_t *ctx = get_spindle_data(spindle);

    if(!ctx->settings)
        return false;

    if(!spindle_precompute_pwm_values(spindle, &ctx->pwm, ctx->settings, SPINDLE_PWM_CLOCK_HZ))
        return false;

    spindle_update_caps(spindle, &ctx->pwm);

    return spindle_pwm_init(ctx);
}

static void coolantSetState (coolant_state_t mode)
{
    (void)mode;
}

static coolant_state_t coolantGetState (void)
{
    return (coolant_state_t){0};
}

static void bitsSetAtomic (volatile uint_fast16_t *ptr, uint_fast16_t bits)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *ptr |= bits;
    if(!primask)
        __enable_irq();
}

static uint_fast16_t bitsClearAtomic (volatile uint_fast16_t *ptr, uint_fast16_t bits)
{
    uint32_t primask = __get_PRIMASK();
    uint_fast16_t previous;

    __disable_irq();
    previous = *ptr;
    *ptr &= ~bits;
    if(!primask)
        __enable_irq();

    return previous;
}

static uint_fast16_t valueSetAtomic (volatile uint_fast16_t *ptr, uint_fast16_t value)
{
    uint32_t primask = __get_PRIMASK();
    uint_fast16_t previous;

    __disable_irq();
    previous = *ptr;
    *ptr = value;
    if(!primask)
        __enable_irq();

    return previous;
}

static uint32_t get_elapsed_ticks (void)
{
    return systicks;
}

static uint64_t get_micros (void)
{
    uint32_t ticks = systicks;
    uint32_t load = SysTick->LOAD + 1u;
    uint32_t val = SysTick->VAL;
    uint64_t micros = (uint64_t)ticks * 1000u;
    micros += ((load - val) * 1000u) / load;

    return micros;
}

static uint16_t timer_ticks_from_us (float us)
{
    if(us <= 0.0f)
        return 1u;

    uint32_t ticks = (uint32_t)((us * (float)STEPPER_TIMER_CLOCK_HZ) / 1000000.0f);

    if(ticks == 0u)
        ticks = 1u;

    if(ticks > 0xFFFEu)
        ticks = 0xFFFEu;

    return (uint16_t)ticks;
}

static uint16_t stepper_timer_period (uint32_t cycles_per_tick)
{
    uint32_t period = cycles_per_tick;

    if(period == 0u)
        period = 1u;

    if(period > 0x10000u)
        period = 0x10000u;

    return (uint16_t)(period - 1u);
}

static void stepper_timer_start (uint16_t period)
{
    if(!stepper_timer_ready)
        return;

    TIMERA_Cmd(STEPPER_TIMER, Disable);
    TIMERA_SetCurrCount(STEPPER_TIMER, 0u);
    TIMERA_SetPeriodValue(STEPPER_TIMER, period);
    TIMERA_ClearFlag(STEPPER_TIMER, TimeraFlagOverflow);
    TIMERA_Cmd(STEPPER_TIMER, Enable);
}

static void stepper_timer_stop (void)
{
    if(!stepper_timer_ready)
        return;

    TIMERA_IrqCmd(STEPPER_TIMER, TimeraIrqCaptureOrCompareCh1, Disable);
    TIMERA_IrqCmd(STEPPER_TIMER, TimeraIrqCaptureOrCompareCh2, Disable);
    TIMERA_ClearFlag(STEPPER_TIMER, TimeraFlagCaptureOrCompareCh1);
    TIMERA_ClearFlag(STEPPER_TIMER, TimeraFlagCaptureOrCompareCh2);
    TIMERA_Cmd(STEPPER_TIMER, Disable);
    TIMERA_ClearFlag(STEPPER_TIMER, TimeraFlagOverflow);
}

static uint16_t stepper_schedule_compare (en_timera_channel_t channel, uint16_t ticks)
{
    uint16_t count = TIMERA_GetCurrCount(STEPPER_TIMER);
    uint16_t period = TIMERA_GetPeriodValue(STEPPER_TIMER);
    uint32_t compare = (uint32_t)count + (uint32_t)(ticks == 0u ? 1u : ticks);

    if(compare >= period)
        compare = period > 1u ? period - 1u : 0u;

    switch(channel) {
        case TimeraCh1:
            TIMERA_ClearFlag(STEPPER_TIMER, TimeraFlagCaptureOrCompareCh1);
            TIMERA_IrqCmd(STEPPER_TIMER, TimeraIrqCaptureOrCompareCh1, Enable);
            break;
        case TimeraCh2:
            TIMERA_ClearFlag(STEPPER_TIMER, TimeraFlagCaptureOrCompareCh2);
            TIMERA_IrqCmd(STEPPER_TIMER, TimeraIrqCaptureOrCompareCh2, Enable);
            break;
        default:
            break;
    }

    TIMERA_SetCompareValue(STEPPER_TIMER, channel, (uint16_t)compare);

    return (uint16_t)compare;
}

static void stepper_pulse_arm (axes_signals_t step_outbits)
{
    pending_step_outbits.mask = 0;
    set_step_outputs(step_outbits);
    stepper_schedule_compare(STEPPER_PULSE_CHANNEL, step_pulse_ticks);
}

static void stepper_driver_isr (void)
{
    TIMERA_ClearFlag(STEPPER_TIMER, TimeraFlagOverflow);

    if(hal.stepper.interrupt_callback)
        hal.stepper.interrupt_callback();
}

static void stepper_pulse_isr (void)
{
    if(TIMERA_GetFlag(STEPPER_TIMER, TimeraFlagCaptureOrCompareCh1) == Set) {
        TIMERA_ClearFlag(STEPPER_TIMER, TimeraFlagCaptureOrCompareCh1);
        TIMERA_IrqCmd(STEPPER_TIMER, TimeraIrqCaptureOrCompareCh1, Disable);

        if(pending_step_outbits.bits)
            stepper_pulse_arm(pending_step_outbits);
    }

    if(TIMERA_GetFlag(STEPPER_TIMER, TimeraFlagCaptureOrCompareCh2) == Set) {
        TIMERA_ClearFlag(STEPPER_TIMER, TimeraFlagCaptureOrCompareCh2);
        TIMERA_IrqCmd(STEPPER_TIMER, TimeraIrqCaptureOrCompareCh2, Disable);
        set_step_outputs((axes_signals_t){0});
    }
}

static bool limits_irq_init (void)
{
    const hc32_limit_irq_t limits[] = {
        {
            .port = X_LIMIT_PORT,
            .pin = X_LIMIT_PIN,
            .irq = LIMIT_X_IRQ,
            .channel = exti_channel_from_pin(X_LIMIT_PIN),
            .source = exti_source_from_channel(exti_channel_from_pin(X_LIMIT_PIN))
        },
        {
            .port = Y_LIMIT_PORT,
            .pin = Y_LIMIT_PIN,
            .irq = LIMIT_Y_IRQ,
            .channel = exti_channel_from_pin(Y_LIMIT_PIN),
            .source = exti_source_from_channel(exti_channel_from_pin(Y_LIMIT_PIN))
        },
        {
            .port = Z_LIMIT_PORT,
            .pin = Z_LIMIT_PIN,
            .irq = LIMIT_Z_IRQ,
            .channel = exti_channel_from_pin(Z_LIMIT_PIN),
            .source = exti_source_from_channel(exti_channel_from_pin(Z_LIMIT_PIN))
        }
    };
    stc_exint_config_t exint_cfg = {
        .enFilterEn = Enable,
        .enFltClk = Pclk3Div32,
        .enExtiLvl = ExIntBothEdge
    };

    limits_irq_ready = true;

    for(uint_fast8_t i = 0; i < sizeof(limits) / sizeof(limits[0]); i++) {
        hc32_irq_registration_t irq = {
            .source = limits[i].source,
            .irq = limits[i].irq,
            .handler = limit_irq_handler,
            .priority = DDL_IRQ_PRIORITY_02
        };

        exint_cfg.enExitCh = limits[i].channel;

        limits_irq_ready = EXINT_Init(&exint_cfg) == Ok &&
                           hc32_irq_register(irq) &&
                           limits_irq_ready;

        EXINT_IrqFlgClr(limits[i].channel);
        NVIC_DisableIRQ(limits[i].irq);
    }

    return limits_irq_ready;
}

static bool stepper_timer_init (void)
{
    static const hc32_irq_registration_t stepper_irq = {
        .source = STEPPER_TIMER_INT,
        .irq = STEPPER_IRQ,
        .handler = stepper_driver_isr,
        .priority = DDL_IRQ_PRIORITY_03
    };
    static const hc32_irq_registration_t pulse_irq = {
        .source = STEPPER_TIMER_CMP_INT,
        .irq = STEPPER_PULSE_IRQ,
        .handler = stepper_pulse_isr,
        .priority = DDL_IRQ_PRIORITY_02
    };

    stc_timera_base_init_t timer_cfg = {
        .enClkDiv = STEPPER_TIMER_DIVIDER,
        .enCntMode = TimeraCountModeSawtoothWave,
        .enCntDir = TimeraCountDirUp,
        .enSyncStartupEn = Disable,
        .u16PeriodVal = stepper_timer_period(STEPPER_TIMER_CLOCK_HZ / 500u)
    };
    stc_timera_compare_init_t compare_cfg = {
        .u16CompareVal = 1u,
        .enStartCountOutput = TimeraCountStartOutputKeep,
        .enStopCountOutput = TimeraCountStopOutputKeep,
        .enCompareMatchOutput = TimeraCompareMatchOutputKeep,
        .enPeriodMatchOutput = TimeraPeriodMatchOutputKeep,
        .enSpecifyOutput = TimeraSpecifyOutputInvalid,
        .enCacheEn = Disable,
        .enTriangularTroughTransEn = Disable,
        .enTriangularCrestTransEn = Disable,
        .u16CompareCacheVal = 1u
    };

    PWC_Fcg2PeriphClockCmd(STEPPER_TIMER_CLOCK, Enable);
    TIMERA_DeInit(STEPPER_TIMER);
    TIMERA_BaseInit(STEPPER_TIMER, &timer_cfg);
    TIMERA_CompareInit(STEPPER_TIMER, STEPPER_DELAY_CHANNEL, &compare_cfg);
    TIMERA_CompareInit(STEPPER_TIMER, STEPPER_PULSE_CHANNEL, &compare_cfg);
    TIMERA_CompareCmd(STEPPER_TIMER, STEPPER_DELAY_CHANNEL, Enable);
    TIMERA_CompareCmd(STEPPER_TIMER, STEPPER_PULSE_CHANNEL, Enable);
    TIMERA_IrqCmd(STEPPER_TIMER, TimeraIrqOverflow, Enable);
    TIMERA_ClearFlag(STEPPER_TIMER, TimeraFlagOverflow);
    TIMERA_ClearFlag(STEPPER_TIMER, TimeraFlagCaptureOrCompareCh1);
    TIMERA_ClearFlag(STEPPER_TIMER, TimeraFlagCaptureOrCompareCh2);
    stepper_timer_ready = hc32_irq_register(stepper_irq) && hc32_irq_register(pulse_irq);
    stepper_timer_stop();

    return stepper_timer_ready;
}

static hc32_spindle_t *get_spindle_data (spindle_ptrs_t *spindle)
{
    return spindle && spindle->context.pwm == &spindle1.pwm ? &spindle1 : &spindle0;
}

static void spindle_set_pwm_value (hc32_spindle_t *spindle, uint_fast16_t pwm_value)
{
    if(!spindle->pwm_ready || spindle->pwm.period == 0u)
        return;

    uint_fast16_t compare = pwm_value;

    if(compare >= spindle->pwm.period)
        compare = spindle->pwm.period - 1u;

    TIMERA_SetCompareValue(spindle->timer, spindle->channel, (uint16_t)compare);
}

static void spindle_pwm_output_enable (hc32_spindle_t *spindle, bool enable)
{
    if(!spindle->pwm_ready)
        return;

    if(enable) {
        if(!spindle->output_enabled) {
            PORT_SetFunc(spindle->pwm_port, spindle->pwm_pin, spindle->pwm_func, Disable);
            TIMERA_CompareCmd(spindle->timer, spindle->channel, Enable);
            TIMERA_SetCurrCount(spindle->timer, 0u);
            TIMERA_Cmd(spindle->timer, Disable);
            TIMERA_Cmd(spindle->timer, Enable);
            spindle->output_enabled = true;
        }
    } else if(spindle->output_enabled) {
        TIMERA_CompareCmd(spindle->timer, spindle->channel, Disable);
        hc32_gpio_config_output(spindle->pwm_port, spindle->pwm_pin);
        hc32_gpio_write(spindle->pwm_port, spindle->pwm_pin, spindle->settings->invert.pwm);
        spindle->output_enabled = false;
    }
}

static bool spindle_pwm_init (hc32_spindle_t *spindle)
{
    uint16_t off_value;

    stc_timera_base_init_t base_cfg = {
        .enClkDiv = SPINDLE_PWM_DIVIDER,
        .enCntMode = TimeraCountModeSawtoothWave,
        .enCntDir = TimeraCountDirUp,
        .enSyncStartupEn = Disable,
        .u16PeriodVal = spindle->pwm.period > 1u ? spindle->pwm.period - 1u : 1u
    };
    stc_timera_compare_init_t compare_cfg = {
        .u16CompareVal = 0u,
        .enStartCountOutput = TimeraCountStartOutputHigh,
        .enStopCountOutput = TimeraCountStopOutputHigh,
        .enCompareMatchOutput = TimeraCompareMatchOutputLow,
        .enPeriodMatchOutput = TimeraPeriodMatchOutputHigh,
        .enSpecifyOutput = TimeraSpecifyOutputInvalid,
        .enCacheEn = Disable,
        .enTriangularTroughTransEn = Disable,
        .enTriangularCrestTransEn = Disable,
        .u16CompareCacheVal = 0u
    };
    stc_timera_hw_startup_cofig_t hw_cfg = {0};

    if(spindle->pwm.period < 2u || spindle->pwm.period > 0x10000u)
        return false;

    off_value = spindle->pwm.off_value >= spindle->pwm.period ? (uint16_t)(spindle->pwm.period - 1u) : (uint16_t)spindle->pwm.off_value;
    compare_cfg.u16CompareVal = off_value;
    compare_cfg.u16CompareCacheVal = off_value;

    PWC_Fcg2PeriphClockCmd(spindle->clock, Enable);
    PORT_SetFunc(spindle->pwm_port, spindle->pwm_pin, spindle->pwm_func, Disable);

    spindle->pwm_ready = false;
    TIMERA_DeInit(spindle->timer);
    TIMERA_BaseInit(spindle->timer, &base_cfg);
    TIMERA_CompareInit(spindle->timer, spindle->channel, &compare_cfg);
    TIMERA_CompareCmd(spindle->timer, spindle->channel, Enable);
    TIMERA_HwStartupConfig(spindle->timer, &hw_cfg);
    TIMERA_SetCurrCount(spindle->timer, 0u);
    spindle->pwm_ready = true;
    spindle->output_enabled = false;
    TIMERA_Cmd(spindle->timer, Enable);
    spindle_pwm_output_enable(spindle, spindle->pwm.flags.always_on);
    if(spindle->pwm.flags.always_on)
        spindle_set_pwm_value(spindle, spindle->pwm.off_value);
    else {
        hc32_gpio_config_output(spindle->pwm_port, spindle->pwm_pin);
        hc32_gpio_write(spindle->pwm_port, spindle->pwm_pin, spindle->settings->invert.pwm);
    }

    return true;
}

static void driver_delay_ms (uint32_t ms, delay_callback_ptr callback)
{
    uint32_t start = systicks;

    while((systicks - start) < ms) {
    }

    if(callback)
        callback();
}

static void sync_build_info (void)
{
    stored_line_t current = {0};
    stored_line_t build_info = {0};

    strncpy(build_info, BUILD_INFO, sizeof(build_info) - 1);

    if(hal.nvs.type == NVS_None)
        return;

    if(hal.nvs.type == NVS_Flash && hal.nvs.memcpy_from_flash && hal.nvs.memcpy_to_flash) {
        uint8_t *image = malloc(HC32_FLASH_NVS_SIZE);

        if(image == NULL)
            return;

        if(!hal.nvs.memcpy_from_flash(image) || image[0] != SETTINGS_VERSION) {
            free(image);
            return;
        }

        memcpy(current, &image[NVS_ADDR_BUILD_INFO], sizeof(stored_line_t));

        if(strncmp(current, build_info, sizeof(stored_line_t)) != 0) {
            uint16_t checksum = calc_checksum((uint8_t *)build_info, sizeof(stored_line_t));

            memcpy(&image[NVS_ADDR_BUILD_INFO], build_info, sizeof(stored_line_t));
            image[NVS_ADDR_BUILD_INFO + sizeof(stored_line_t)] = checksum & 0xFFu;
#if NVS_CRC_BYTES > 1
            image[NVS_ADDR_BUILD_INFO + sizeof(stored_line_t) + 1u] = checksum >> 8;
#endif
            (void)hal.nvs.memcpy_to_flash(image);
        }

        free(image);

        return;
    }

    if(hal.nvs.memcpy_from_nvs == NULL || hal.nvs.memcpy_to_nvs == NULL)
        return;

    if(hal.nvs.memcpy_from_nvs((uint8_t *)current, NVS_ADDR_BUILD_INFO, sizeof(stored_line_t), true) != NVS_TransferResult_OK ||
       strncmp(current, build_info, sizeof(stored_line_t)) != 0)
        settings_write_build_info(build_info);
}

static void settings_changed (settings_t *settings, settings_changed_flags_t changed)
{
    if(!io_init_done)
        return;

    stepperEnable(settings->steppers.energize, false);
    step_pulse_ticks = timer_ticks_from_us(settings->steppers.pulse_microseconds);
    step_delay_ticks = timer_ticks_from_us(settings->steppers.pulse_delay_microseconds);

    if(settings->steppers.pulse_delay_microseconds > 0.0f)
        hal.stepper.pulse_start = stepperPulseStartDelayed;
    else
        hal.stepper.pulse_start = stepperPulseStart;

    configure_control_inputs(settings);

    if(changed.spindle) {
        spindle_ptrs_t *spindle;

        if(spindle0_id != -1 && (spindle = spindle_get_hal(spindle0_id, SpindleHAL_Configured)))
            spindleConfig(spindle);

        if(spindle1_id != -1 && (spindle = spindle_get_hal(spindle1_id, SpindleHAL_Configured)))
            spindleConfig(spindle);
    }
}

static bool driver_setup (settings_t *settings)
{
#if !EEPROM_ENABLE
    if(flash_nvs_bootstrap_pending && hal.nvs.type == NVS_Emulated && hal.nvs.memcpy_from_nvs) {
        nvs_io_t *physical = nvs_buffer_get_physical();

        physical->type = NVS_Flash;
        physical->size = hal.nvs.size;
        physical->size_max = HC32_FLASH_NVS_SIZE;
        physical->get_byte = nvsGetByte;
        physical->put_byte = nvsPutByte;
        physical->memcpy_from_nvs = NULL;
        physical->memcpy_to_nvs = NULL;
        physical->memcpy_from_flash = memcpy_from_flash;
        physical->memcpy_to_flash = memcpy_to_flash;
        flash_nvs_bootstrap_pending = false;
    }
#endif

    hc32_gpio_config_output(X_STEP_PORT, X_STEP_PIN);
    hc32_gpio_config_output(Y_STEP_PORT, Y_STEP_PIN);
    hc32_gpio_config_output(Z_STEP_PORT, Z_STEP_PIN);
#ifdef A_AXIS
    hc32_gpio_config_output(A_STEP_PORT, A_STEP_PIN);
#endif

    hc32_gpio_config_output(X_DIRECTION_PORT, X_DIRECTION_PIN);
    hc32_gpio_config_output(Y_DIRECTION_PORT, Y_DIRECTION_PIN);
    hc32_gpio_config_output(Z_DIRECTION_PORT, Z_DIRECTION_PIN);
#ifdef A_AXIS
    hc32_gpio_config_output(A_DIRECTION_PORT, A_DIRECTION_PIN);
#endif
    hc32_gpio_config_output(STEPPERS_ENABLE_PORT, STEPPERS_ENABLE_PIN);

    hc32_gpio_config_input_exint(X_LIMIT_PORT, X_LIMIT_PIN, true);
    hc32_gpio_config_input_exint(Y_LIMIT_PORT, Y_LIMIT_PIN, true);
    hc32_gpio_config_input_exint(Z_LIMIT_PORT, Z_LIMIT_PIN, true);
#if PROBE_ENABLE
    hc32_gpio_config_input(PROBE_PORT, PROBE_PIN, true);
#endif
#if SDCARD_ENABLE && defined(SD_DETECT_PIN)
    hc32_gpio_config_input(SD_DETECT_PORT, SD_DETECT_PIN, true);
#endif
    configure_control_inputs(settings);

    hc32_gpio_config_output(SPINDLE0_PWM_PORT, SPINDLE0_PWM_PIN);
#if SPINDLE0_HAS_ENABLE
    hc32_gpio_config_output(SPINDLE0_ENABLE_PORT, SPINDLE0_ENABLE_PIN);
#endif
#if SPINDLE1_HAS_ENABLE
    hc32_gpio_config_output(SPINDLE1_ENABLE_PORT, SPINDLE1_ENABLE_PIN);
#endif
    hc32_gpio_config_output(SPINDLE1_PWM_PORT, SPINDLE1_PWM_PIN);

    limits_irq_init();

#if SDCARD_ENABLE && SDCARD_SDIO
    sdcard_events_t *card = sdcard_init();

    card->on_mount = hc32_sdcard_mount;
    card->on_unmount = hc32_sdcard_unmount;

    (void)hc32_sdcard_mount(NULL);
#elif SDCARD_ENABLE
    sdcard_init();
#endif

    io_init_done = settings->version.id == SETTINGS_VERSION;
    settings_changed(settings, (settings_changed_flags_t){0});

    stepperGoIdle(true);
    spindle_off(spindle_get_hal(spindle0_id, SpindleHAL_Configured));
    spindle_off(spindle_get_hal(spindle1_id, SpindleHAL_Configured));

    return io_init_done;
}

static void spindle_settings_changed (spindle1_pwm_settings_t *settings)
{
    spindle_ptrs_t *spindle;

    spindle1.settings = &settings->cfg;

    if(io_init_done && spindle1_id != -1 && (spindle = spindle_get_hal(spindle1_id, SpindleHAL_Configured)))
        spindleConfig(spindle);
}

static void register_spindles (void)
{
    static const spindle_ptrs_t pa1_spindle = {
        .type = SpindleType_PWM,
        .ref_id = SPINDLE_PWM0_NODIR,
        .config = spindleConfig,
        .set_state = spindleSetState,
        .get_state = spindleGetState,
        .get_pwm = spindleGetPWM,
        .update_pwm = spindle_set_speed,
        .context = { .pwm = &spindle0.pwm },
        .cap = {
            .gpio_controlled = On,
            .variable = On,
            .laser = Off,
            .pwm_invert = On
        }
    };
    static const spindle_ptrs_t pb1_spindle = {
        .type = SpindleType_PWM,
        .ref_id = SPINDLE_PWM1_NODIR,
        .config = spindleConfig,
        .set_state = spindleSetState,
        .get_state = spindleGetState,
        .get_pwm = spindleGetPWM,
        .update_pwm = spindle_set_speed,
        .context = { .pwm = &spindle1.pwm },
        .cap = {
            .gpio_controlled = On,
            .variable = On,
            .laser = On,
            .pwm_invert = On,
            .rpm_range_locked = On
        }
    };

    spindle0.settings = &settings.pwm_spindle;
    spindle0_id = spindle_register(&pa1_spindle, "PA1 PWM spindle");

    if((spindle1_settings = spindle1_settings_add(false))) {
        spindle1.settings = &spindle1_settings->cfg;
        spindle1_id = spindle_register(&pb1_spindle, "PB1 PWM laser");
        if(spindle1_id != -1)
            spindle1_settings_register(pb1_spindle.cap, spindle_settings_changed);
    }
}

bool driver_init (void)
{
    SysTick_Config(SystemCoreClock / 1000u);

    NVIC_SetPriority(SysTick_IRQn, (1u << __NVIC_PRIO_BITS) - 1u);

#if EEPROM_ENABLE
    hal.info = "HC32F460";
#else
    hal.info = "HC32F460";
    hal.driver_options = NULL;
#endif
    hal.driver_version = "260319";
    hal.driver_url = GRBL_URL "/HC32F460";
    hal.board = BOARD_NAME;
    hal.driver_setup = driver_setup;
    hal.f_mcu = SystemCoreClock / 1000000u;
    hal.f_step_timer = STEPPER_TIMER_CLOCK_HZ;
    hal.rx_buffer_size = RX_BUFFER_SIZE;
    hal.delay_ms = driver_delay_ms;
    hal.settings_changed = settings_changed;
    hal.step_us_min = STEP_PULSE_LENGTH_US;
    hal.enumerate_pins = enumeratePins;

    hal.irq_enable = __enable_irq;
    hal.irq_disable = __disable_irq;
    hal.set_bits_atomic = bitsSetAtomic;
    hal.clear_bits_atomic = bitsClearAtomic;
    hal.set_value_atomic = valueSetAtomic;
    hal.get_elapsed_ticks = get_elapsed_ticks;
    hal.get_micros = get_micros;

    hal.stepper.wake_up = stepperWakeUp;
    hal.stepper.go_idle = stepperGoIdle;
    hal.stepper.enable = stepperEnable;
    hal.stepper.cycles_per_tick = stepperCyclesPerTick;
    hal.stepper.pulse_start = stepperPulseStart;

    if(!stepper_timer_init())
        return false;

    hal.limits.enable = limitsEnable;
    hal.limits.get_state = limitsGetState;
    hal.homing.get_state = homeGetState;
    hal.control.get_state = systemGetState;
#if PROBE_ENABLE
    hal.probe.get_state = probeGetState;
    hal.probe.configure = probeConfigure;
#endif
    hal.coolant.set_state = coolantSetState;
    hal.coolant.get_state = coolantGetState;

#if EEPROM_ENABLE
    hal.nvs.type = NVS_EEPROM;
    hal.nvs.size_max = 2048u;
    hal.nvs.get_byte = nvsGetByte;
    hal.nvs.put_byte = nvsPutByte;
    hal.nvs.memcpy_from_nvs = nvsRead;
    hal.nvs.memcpy_to_nvs = nvsWrite;
    hal.nvs.memcpy_from_flash = NULL;
    hal.nvs.memcpy_to_flash = NULL;
#else
    if(flash_nvs_is_valid()) {
        hal.nvs.type = NVS_Flash;
        hal.nvs.size_max = HC32_FLASH_NVS_SIZE;
        hal.nvs.get_byte = nvsGetByte;
        hal.nvs.put_byte = nvsPutByte;
        hal.nvs.memcpy_from_nvs = NULL;
        hal.nvs.memcpy_to_nvs = NULL;
        hal.nvs.memcpy_from_flash = memcpy_from_flash;
        hal.nvs.memcpy_to_flash = memcpy_to_flash;
    } else {
        flash_nvs_bootstrap_pending = true;
        hal.nvs.type = NVS_None;
        hal.nvs.size_max = HC32_FLASH_NVS_SIZE;
        hal.nvs.get_byte = NULL;
        hal.nvs.put_byte = NULL;
        hal.nvs.memcpy_from_nvs = NULL;
        hal.nvs.memcpy_to_nvs = NULL;
        hal.nvs.memcpy_from_flash = NULL;
        hal.nvs.memcpy_to_flash = NULL;
    }
#endif

    const io_stream_t *serial = serialInit(115200);
    if(serial == NULL)
        return false;

    memcpy(&hal.stream, serial, sizeof(io_stream_t));
    serialEnableRxInterrupt();
    serialRegisterStreams();
    hc32_ioports_init();

    hal.driver_cap.pwm_spindle = On;
#if PROBE_ENABLE
    hal.driver_cap.probe = On;
#endif
    hal.driver_cap.control_pull_up = On;
    hal.driver_cap.limits_pull_up = On;
#if PROBE_ENABLE
    hal.driver_cap.probe_pull_up = On;
#endif
    hal.driver_cap.step_pulse_delay = On;

#if defined(RESET_PIN) && !ESTOP_ENABLE
    hal.signals_cap.reset = On;
#endif
#if defined(RESET_PIN) && ESTOP_ENABLE
    hal.signals_cap.e_stop = On;
#endif
#ifdef FEED_HOLD_PIN
    hal.signals_cap.feed_hold = On;
#endif
#ifdef CYCLE_START_PIN
    hal.signals_cap.cycle_start = On;
#endif

    hal.limits_cap.min.x = On;
    hal.limits_cap.min.y = On;
    hal.limits_cap.min.z = On;
    hal.home_cap.a.x = On;
    hal.home_cap.a.y = On;
    hal.home_cap.a.z = On;

    register_spindles();

#include "grbl/plugins_init.h"

#if MPG_ENABLE == 2
    if(!hal.driver_cap.mpg_mode)
        hal.driver_cap.mpg_mode = stream_mpg_register(stream_open_instance(MPG_STREAM, 115200, NULL, NULL), false, stream_mpg_check_enable);
#endif

    sync_build_info();

    return hal.version == HAL_VERSION;
}

void SysTick_Handler (void)
{
    systicks++;
}
