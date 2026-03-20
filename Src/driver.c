/*
  driver.c - HC32F460 grblHAL driver

  Part of grblHAL
*/

#include <math.h>

#include "main.h"
#include "flash.h"
#include "serial.h"

#include "grbl/hal.h"
#include "grbl/motor_pins.h"
#include "grbl/spindle_control.h"

static bool io_init_done = false;
static volatile uint32_t systicks = 0;
static uint8_t probe_invert_mask = 0;
static spindle_pwm_t spindle_pwm = { .offset = -1 };
static volatile axes_signals_t pending_step_outbits = {0};
static bool stepper_timer_ready = false;
static bool spindle_pwm_ready = false;
static bool spindle_pwm_output_enabled = false;
static uint16_t step_pulse_ticks = 1u;
static uint16_t step_delay_ticks = 0u;

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
static bool spindle_pwm_init (void);
static void spindle_pwm_output_enable (bool enable);
static void spindle_set_pwm_value (uint_fast16_t pwm_value);

static void set_step_outputs (axes_signals_t step_outbits)
{
    step_outbits.mask ^= settings.steppers.step_invert.mask;

    hc32_gpio_write(X_STEP_PORT, X_STEP_PIN, step_outbits.x);
    hc32_gpio_write(Y_STEP_PORT, Y_STEP_PIN, step_outbits.y);
    hc32_gpio_write(Z_STEP_PORT, Z_STEP_PIN, step_outbits.z);
}

static void set_dir_outputs (axes_signals_t dir_outbits)
{
    dir_outbits.mask ^= settings.steppers.dir_invert.mask;

    hc32_gpio_write(X_DIRECTION_PORT, X_DIRECTION_PIN, dir_outbits.x);
    hc32_gpio_write(Y_DIRECTION_PORT, Y_DIRECTION_PIN, dir_outbits.y);
    hc32_gpio_write(Z_DIRECTION_PORT, Z_DIRECTION_PIN, dir_outbits.z);
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

static void limitsEnable (bool on, axes_signals_t homing_cycle)
{
    (void)on;
    (void)homing_cycle;
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
    return (control_signals_t){0};
}

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

static void spindle_off (spindle_ptrs_t *spindle)
{
    (void)spindle;
    hc32_gpio_write(SPINDLE_ENABLE_PORT, SPINDLE_ENABLE_PIN, settings.pwm_spindle.invert.on);

    if(spindle_pwm.flags.always_on)
        spindle_set_pwm_value(spindle_pwm.off_value);
    else
        spindle_pwm_output_enable(false);
}

static void spindle_on (spindle_ptrs_t *spindle)
{
    (void)spindle;
    hc32_gpio_write(SPINDLE_ENABLE_PORT, SPINDLE_ENABLE_PIN, !settings.pwm_spindle.invert.on);
}

static void spindle_dir (bool ccw)
{
#if SPINDLE_HAS_DIRECTION
    hc32_gpio_write(SPINDLE_DIRECTION_PORT, SPINDLE_DIRECTION_PIN, ccw ^ settings.pwm_spindle.invert.ccw);
#else
    (void)ccw;
#endif
}

static void spindle_set_speed (spindle_ptrs_t *spindle, uint_fast16_t pwm_value)
{
    (void)spindle;

    if(pwm_value == spindle_pwm.off_value && !spindle_pwm.flags.always_on)
        spindle_pwm_output_enable(false);
    else {
        spindle_pwm_output_enable(true);
        spindle_set_pwm_value(pwm_value);
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

    spindle_dir(state.ccw);
    spindle_set_speed(spindle, spindleGetPWM(spindle, rpm));
    spindle_on(spindle);
}

static spindle_state_t spindleGetState (spindle_ptrs_t *spindle)
{
    (void)spindle;

    spindle_state_t state = {0};
    state.on = hc32_gpio_read(SPINDLE_ENABLE_PORT, SPINDLE_ENABLE_PIN);
#if SPINDLE_HAS_DIRECTION
    state.ccw = hc32_gpio_read(SPINDLE_DIRECTION_PORT, SPINDLE_DIRECTION_PIN);
#endif
    state.value ^= settings.pwm_spindle.invert.mask;

    return state;
}

static bool spindleConfig (spindle_ptrs_t *spindle)
{
    if(!spindle_precompute_pwm_values(spindle, &spindle_pwm, &settings.pwm_spindle, SPINDLE_PWM_CLOCK_HZ))
        return false;

    return spindle_pwm_init();
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

static void spindle_set_pwm_value (uint_fast16_t pwm_value)
{
    if(!spindle_pwm_ready)
        return;

    if(spindle_pwm.period == 0u)
        return;

    uint_fast16_t compare = pwm_value;

    if(compare >= spindle_pwm.period)
        compare = spindle_pwm.period - 1u;

    TIMERA_SetCompareValue(SPINDLE_PWM_TIMER, SPINDLE_PWM_CHANNEL, (uint16_t)compare);
}

static void spindle_pwm_output_enable (bool enable)
{
    if(!spindle_pwm_ready)
        return;

    if(enable) {
        if(!spindle_pwm_output_enabled) {
            PORT_SetFunc(SPINDLE_PWM_PORT, SPINDLE_PWM_PIN, SPINDLE_PWM_FUNC, Disable);
            TIMERA_CompareCmd(SPINDLE_PWM_TIMER, SPINDLE_PWM_CHANNEL, Enable);
            TIMERA_SetCurrCount(SPINDLE_PWM_TIMER, 0u);
            TIMERA_Cmd(SPINDLE_PWM_TIMER, Disable);
            TIMERA_Cmd(SPINDLE_PWM_TIMER, Enable);
            spindle_pwm_output_enabled = true;
        }
    } else if(spindle_pwm_output_enabled) {
        TIMERA_CompareCmd(SPINDLE_PWM_TIMER, SPINDLE_PWM_CHANNEL, Disable);
        hc32_gpio_config_output(SPINDLE_PWM_PORT, SPINDLE_PWM_PIN);
        hc32_gpio_write(SPINDLE_PWM_PORT, SPINDLE_PWM_PIN, settings.pwm_spindle.invert.pwm);
        spindle_pwm_output_enabled = false;
    }
}

static bool spindle_pwm_init (void)
{
    uint16_t off_value;

    stc_timera_base_init_t base_cfg = {
        .enClkDiv = SPINDLE_PWM_DIVIDER,
        .enCntMode = TimeraCountModeSawtoothWave,
        .enCntDir = TimeraCountDirUp,
        .enSyncStartupEn = Disable,
        .u16PeriodVal = spindle_pwm.period > 1u ? spindle_pwm.period - 1u : 1u
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

    if(spindle_pwm.period < 2u || spindle_pwm.period > 0x10000u)
        return false;

    off_value = spindle_pwm.off_value >= spindle_pwm.period ? (uint16_t)(spindle_pwm.period - 1u) : (uint16_t)spindle_pwm.off_value;
    compare_cfg.u16CompareVal = off_value;
    compare_cfg.u16CompareCacheVal = off_value;

    PWC_Fcg2PeriphClockCmd(SPINDLE_PWM_CLOCK, Enable);
    PORT_SetFunc(SPINDLE_PWM_PORT, SPINDLE_PWM_PIN, SPINDLE_PWM_FUNC, Disable);

    spindle_pwm_ready = false;
    TIMERA_DeInit(SPINDLE_PWM_TIMER);
    TIMERA_BaseInit(SPINDLE_PWM_TIMER, &base_cfg);
    TIMERA_CompareInit(SPINDLE_PWM_TIMER, SPINDLE_PWM_CHANNEL, &compare_cfg);
    TIMERA_CompareCmd(SPINDLE_PWM_TIMER, SPINDLE_PWM_CHANNEL, Enable);
    TIMERA_HwStartupConfig(SPINDLE_PWM_TIMER, &hw_cfg);
    TIMERA_SetCurrCount(SPINDLE_PWM_TIMER, 0u);
    spindle_pwm_ready = true;
    spindle_pwm_output_enabled = false;
    TIMERA_Cmd(SPINDLE_PWM_TIMER, Enable);
    spindle_pwm_output_enable(spindle_pwm.flags.always_on);
    if(spindle_pwm.flags.always_on)
        spindle_set_pwm_value(spindle_pwm.off_value);
    else {
        hc32_gpio_config_output(SPINDLE_PWM_PORT, SPINDLE_PWM_PIN);
        hc32_gpio_write(SPINDLE_PWM_PORT, SPINDLE_PWM_PIN, settings.pwm_spindle.invert.pwm);
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

    if(!spindle_pwm_ready || changed.spindle) {
        spindle_ptrs_t *spindle = spindle_get(0);

        if(spindle)
            spindleConfig(spindle);
    }
}

static bool driver_setup (settings_t *settings)
{
    hc32_gpio_config_output(X_STEP_PORT, X_STEP_PIN);
    hc32_gpio_config_output(Y_STEP_PORT, Y_STEP_PIN);
    hc32_gpio_config_output(Z_STEP_PORT, Z_STEP_PIN);

    hc32_gpio_config_output(X_DIRECTION_PORT, X_DIRECTION_PIN);
    hc32_gpio_config_output(Y_DIRECTION_PORT, Y_DIRECTION_PIN);
    hc32_gpio_config_output(Z_DIRECTION_PORT, Z_DIRECTION_PIN);
    hc32_gpio_config_output(STEPPERS_ENABLE_PORT, STEPPERS_ENABLE_PIN);

    hc32_gpio_config_input(X_LIMIT_PORT, X_LIMIT_PIN, true);
    hc32_gpio_config_input(Y_LIMIT_PORT, Y_LIMIT_PIN, true);
    hc32_gpio_config_input(Z_LIMIT_PORT, Z_LIMIT_PIN, true);
    hc32_gpio_config_input(PROBE_PORT, PROBE_PIN, true);

    hc32_gpio_config_output(SPINDLE_ENABLE_PORT, SPINDLE_ENABLE_PIN);
#if SPINDLE_HAS_DIRECTION
    hc32_gpio_config_output(SPINDLE_DIRECTION_PORT, SPINDLE_DIRECTION_PIN);
#endif

    io_init_done = settings->version.id == SETTINGS_VERSION;
    settings_changed(settings, (settings_changed_flags_t){0});

    stepperGoIdle(true);
    spindle_off(NULL);

    return io_init_done;
}

static void register_spindle (void)
{
    static const spindle_ptrs_t spindle = {
        .type = SpindleType_PWM,
        .ref_id = SPINDLE_PWM0,
        .config = spindleConfig,
        .set_state = spindleSetState,
        .get_state = spindleGetState,
        .get_pwm = spindleGetPWM,
        .update_pwm = spindle_set_speed,
        .cap = {
            .gpio_controlled = On,
            .variable = On,
            .laser = On,
            .pwm_invert = On,
            .direction = SPINDLE_HAS_DIRECTION ? On : Off
        }
    };

    spindle_register(&spindle, "PWM");
}

bool driver_init (void)
{
    SysTick_Config(SystemCoreClock / 1000u);

    NVIC_SetPriority(SysTick_IRQn, (1u << __NVIC_PRIO_BITS) - 1u);

    hal.info = "HC32F460";
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
    hal.probe.get_state = probeGetState;
    hal.probe.configure = probeConfigure;
    hal.coolant.set_state = coolantSetState;
    hal.coolant.get_state = coolantGetState;

    hal.nvs.type = NVS_EEPROM;
    hal.nvs.size_max = 2048u;
    hal.nvs.get_byte = nvsGetByte;
    hal.nvs.put_byte = nvsPutByte;
    hal.nvs.memcpy_from_nvs = nvsRead;
    hal.nvs.memcpy_to_nvs = nvsWrite;
    hal.nvs.memcpy_from_flash = NULL;
    hal.nvs.memcpy_to_flash = NULL;

    const io_stream_t *serial = serialInit(115200);
    if(serial == NULL)
        return false;

    memcpy(&hal.stream, serial, sizeof(io_stream_t));
    serialEnableRxInterrupt();

    hal.driver_cap.pwm_spindle = On;
    hal.driver_cap.probe = On;
    hal.driver_cap.control_pull_up = On;
    hal.driver_cap.limits_pull_up = On;
    hal.driver_cap.probe_pull_up = On;
    hal.driver_cap.step_pulse_delay = On;

    hal.signals_cap.reset = On;
    hal.signals_cap.feed_hold = On;
    hal.signals_cap.cycle_start = On;

    hal.limits_cap.min.x = On;
    hal.limits_cap.min.y = On;
    hal.limits_cap.min.z = On;
    hal.home_cap.a.x = On;
    hal.home_cap.a.y = On;
    hal.home_cap.a.z = On;

    register_spindle();

    return hal.version == HAL_VERSION;
}

void SysTick_Handler (void)
{
    systicks++;
}
