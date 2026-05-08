#include "driver.h"

#include <math.h>
#include <string.h>

#include "grbl/ioports.h"
#include "grbl/protocol.h"

typedef struct {
    pin_function_t id;
    pin_cap_t cap;
    pin_mode_t mode;
    en_port_t port;
    uint_fast8_t pin;
    uint16_t pin_mask;
    pin_group_t group;
    const char *description;
    bool value;
} hc32_aux_output_t;

typedef struct {
    pin_function_t id;
    pin_cap_t cap;
    pin_mode_t mode;
    en_port_t port;
    uint_fast8_t pin;
    uint16_t pin_mask;
    pin_group_t group;
    const char *description;
} hc32_aux_input_t;

typedef struct {
    pin_function_t id;
    pin_cap_t cap;
    pin_mode_t mode;
    en_port_t port;
    uint_fast8_t pin;
    uint16_t pin_mask;
    uint8_t adc_index;
    pin_group_t group;
    const char *description;
    float value;
} hc32_aux_analog_input_t;

static io_ports_data_t digital = {0};
static io_ports_data_t analog = {0};
static hc32_aux_input_t aux_inputs[N_AUX_DIN_MAX] = {0};
static hc32_aux_output_t aux_outputs[N_AUX_DOUT_MAX] = {0};
static hc32_aux_analog_input_t aux_analog_inputs[N_AUX_AIN_MAX] = {0};
static uint8_t n_aux_inputs = 0;
static uint8_t n_aux_outputs = 0;
static uint8_t n_aux_analog_inputs = 0;
static bool adc_ready = false;

static const char *aux_port_name (en_port_t port)
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

static bool hc32_adc_init (void)
{
    if(adc_ready)
        return true;

    static uint8_t sample_times[] = { 0x0Bu, 0x0Bu };
    static const stc_adc_init_t init = {
        .enResolution = AdcResolution_12Bit,
        .enDataAlign = AdcDataAlign_Right,
        .enAutoClear = AdcClren_Disable,
        .enScanMode = AdcMode_SAOnce,
        .enRschsel = AdcRschsel_Continue
    };
    static const stc_adc_ch_cfg_t channels = {
        .u32Channel = ADC1_CH14 | ADC1_CH15,
        .u8Sequence = ADC_SEQ_A,
        .pu8SampTime = sample_times
    };
    stc_port_init_t cfg = {0};

    PWC_Fcg3PeriphClockCmd(PWC_FCG3_PERIPH_ADC1, Enable);

    cfg.enPinMode = Pin_Mode_Ana;
    cfg.enPinDrv = Pin_Drv_L;
    cfg.enPinOType = Pin_OType_Cmos;
    cfg.enPullUp = Disable;
    cfg.enExInt = Disable;
    cfg.enPinSubFunc = Disable;

    PORT_SetFunc(PortC, Pin04 | Pin05, Func_Gpio, Disable);
    PORT_Init(PortC, Pin04 | Pin05, &cfg);

    adc_ready = ADC_Init(M4_ADC1, &init) == Ok && ADC_AddAdcChannel(M4_ADC1, &channels) == Ok;

    return adc_ready;
}

static float hc32_adc_read (uint8_t adc_index)
{
    uint16_t values[2];

    if(!adc_ready && !hc32_adc_init())
        return -1.0f;

    if(ADC_PollingSa(M4_ADC1, values, sizeof(values) / sizeof(values[0]), 2u) != Ok)
        return -1.0f;

    switch(adc_index) {
        case ADC_CH_IDX14:
            return (float)values[0];
        case ADC_CH_IDX15:
            return (float)values[1];
        default:
            return -1.0f;
    }
}

static void hc32_aux_config_output (hc32_aux_output_t *output)
{
    stc_port_init_t cfg = {0};

    cfg.enPinMode = Pin_Mode_Out;
    cfg.enPinDrv = Pin_Drv_H;
    cfg.enPinOType = output->mode.open_drain ? Pin_OType_Od : Pin_OType_Cmos;
    cfg.enPullUp = Disable;
    cfg.enExInt = Disable;
    cfg.enPinSubFunc = Disable;

    PORT_SetFunc(output->port, output->pin_mask, Func_Gpio, Disable);
    PORT_Init(output->port, output->pin_mask, &cfg);
    hc32_gpio_write(output->port, output->pin_mask, output->value ^ output->mode.inverted);
}

static void hc32_aux_config_input (hc32_aux_input_t *input)
{
    stc_port_init_t cfg = {0};

    cfg.enPinMode = Pin_Mode_In;
    cfg.enPinDrv = Pin_Drv_L;
    cfg.enPinOType = Pin_OType_Cmos;
    cfg.enPullUp = input->mode.pull_mode == PullMode_Up ? Enable : Disable;
    cfg.enExInt = Disable;
    cfg.enPinSubFunc = Disable;

    PORT_SetFunc(input->port, input->pin_mask, Func_Gpio, Disable);
    PORT_Init(input->port, input->pin_mask, &cfg);
}

static bool digital_out_cfg (xbar_t *pin, xbar_cfg_ptr_t cfg_data, bool persistent)
{
    gpio_out_config_t *config = cfg_data.gpio_out_config;

    if(pin == NULL || config == NULL || pin->id >= n_aux_outputs)
        return false;

    hc32_aux_output_t *output = &aux_outputs[pin->id];

    output->mode.inverted = config->inverted;
    output->mode.open_drain = config->open_drain;
    hc32_aux_config_output(output);

    if(persistent)
        ioport_save_output_settings(pin, config);

    return true;
}

static float digital_out_state (xbar_t *pin)
{
    if(pin == NULL || pin->id >= n_aux_outputs)
        return -1.0f;

    hc32_aux_output_t *output = &aux_outputs[pin->id];

    return (float)(hc32_gpio_read(output->port, output->pin_mask) ^ output->mode.inverted);
}

static bool digital_in_cfg (xbar_t *pin, xbar_cfg_ptr_t cfg_data, bool persistent)
{
    gpio_in_config_t *config = cfg_data.gpio_in_config;

    if(pin == NULL || config == NULL || pin->id >= n_aux_inputs || config->pull_mode == PullMode_UpDown)
        return false;

    hc32_aux_input_t *input = &aux_inputs[pin->id];

    input->mode.inverted = config->inverted;
    input->mode.debounce = config->debounce;
    input->mode.pull_mode = config->pull_mode;
    hc32_aux_config_input(input);

    if(persistent)
        ioport_save_input_settings(pin, config);

    return true;
}

static float digital_in_state (xbar_t *pin)
{
    if(pin == NULL || pin->id >= n_aux_inputs)
        return -1.0f;

    hc32_aux_input_t *input = &aux_inputs[pin->id];

    return (float)(hc32_gpio_read(input->port, input->pin_mask) ^ input->mode.inverted);
}

static float analog_in_state (xbar_t *pin)
{
    if(pin == NULL || pin->id >= n_aux_analog_inputs)
        return -1.0f;

    hc32_aux_analog_input_t *input = &aux_analog_inputs[pin->id];

    return input->value = hc32_adc_read(input->adc_index);
}

static int32_t wait_on_input (uint8_t port, wait_mode_t wait_mode, float timeout)
{
    if(port >= n_aux_inputs)
        return -1;

    hc32_aux_input_t *input = &aux_inputs[port];

    if(wait_mode == WaitMode_Immediate)
        return (int32_t)(hc32_gpio_read(input->port, input->pin_mask) ^ input->mode.inverted);

    uint_fast16_t delay = (uint_fast16_t)ceilf((1000.0f / 50.0f) * timeout) + 1u;
    int32_t value = -1;
    bool wait_for = wait_mode != WaitMode_Low && wait_mode != WaitMode_Fall;

    do {
        bool state = hc32_gpio_read(input->port, input->pin_mask) ^ input->mode.inverted;

        if(wait_mode == WaitMode_Fall || wait_mode == WaitMode_Rise) {
            if(state == wait_for) {
                value = (int32_t)state;
                break;
            }
        } else if(state == wait_for) {
            value = (int32_t)state;
            break;
        }

        if(delay == 0u || sys.abort)
            break;

        protocol_execute_realtime();
        hal.delay_ms(50u, NULL);
    } while(--delay);

    return value;
}

static int32_t analog_wait_on_input (uint8_t port, wait_mode_t wait_mode, float timeout)
{
    (void)wait_mode;
    (void)timeout;

    return port < n_aux_analog_inputs ? (int32_t)hc32_adc_read(aux_analog_inputs[port].adc_index) : -1;
}

static void digital_out (uint8_t port, bool on)
{
    if(port >= n_aux_outputs)
        return;

    aux_outputs[port].value = on;
    hc32_aux_config_output(&aux_outputs[port]);
}

static bool set_function (xbar_t *pin, pin_function_t function)
{
    if(pin == NULL)
        return false;

    if(pin->group == PinGroup_AuxInputAnalog) {
        if(pin->id >= n_aux_analog_inputs)
            return false;
        aux_analog_inputs[pin->id].id = function;
        return true;
    }

    if(pin->group == PinGroup_AuxInput) {
        if(pin->id >= n_aux_inputs)
            return false;
        aux_inputs[pin->id].id = function;
        return true;
    }

    if(pin->group == PinGroup_AuxOutput) {
        if(pin->id >= n_aux_outputs)
            return false;
        aux_outputs[pin->id].id = function;
        return true;
    }

    return false;
}

static xbar_t *get_pin_info (io_port_direction_t dir, uint8_t port)
{
    static xbar_t pin;
    xbar_t *info = NULL;

    memset(&pin, 0, sizeof(pin));
    pin.set_function = set_function;

    if(dir == Port_Input && port < n_aux_inputs) {
        XBAR_SET_DIN_INFO(pin, port, aux_inputs[port], digital_in_cfg, digital_in_state);
        pin.group = PinGroup_AuxInput;
        pin.port = (void *)aux_port_name(aux_inputs[port].port);
        pin.pin = pinmask_to_pinno(aux_inputs[port].pin_mask);
        info = &pin;
    } else if(dir == Port_Output && port < n_aux_outputs) {
        XBAR_SET_DOUT_INFO(pin, port, aux_outputs[port], digital_out_cfg, digital_out_state);
        pin.group = PinGroup_AuxOutput;
        pin.port = (void *)aux_port_name(aux_outputs[port].port);
        pin.pin = pinmask_to_pinno(aux_outputs[port].pin_mask);
        info = &pin;
    }

    return info;
}

static xbar_t *get_analog_pin_info (io_port_direction_t dir, uint8_t port)
{
    static xbar_t pin;
    xbar_t *info = NULL;

    memset(&pin, 0, sizeof(pin));
    pin.set_function = set_function;

    if(dir == Port_Input && port < n_aux_analog_inputs) {
        hc32_aux_analog_input_t *input = &aux_analog_inputs[port];

        pin.id = port;
        pin.function = input->id;
        pin.group = PinGroup_AuxInputAnalog;
        pin.port = (void *)aux_port_name(input->port);
        pin.pin = input->pin;
        pin.description = input->description;
        pin.cap = input->cap;
        pin.mode = input->mode;
        pin.get_value = analog_in_state;
        info = &pin;
    }

    return info;
}

static void set_pin_description (io_port_direction_t dir, uint8_t port, const char *s)
{
    if(dir == Port_Input && port < n_aux_inputs)
        aux_inputs[port].description = s;
    else if(dir == Port_Output && port < n_aux_outputs)
        aux_outputs[port].description = s;
}

static void set_analog_pin_description (io_port_direction_t dir, uint8_t port, const char *s)
{
    if(dir == Port_Input && port < n_aux_analog_inputs)
        aux_analog_inputs[port].description = s;
}

static void add_aux_output (en_port_t port, uint16_t pin_mask, const char *description)
{
    if(n_aux_outputs >= N_AUX_DOUT_MAX)
        return;

    hc32_aux_output_t *output = &aux_outputs[n_aux_outputs];

    output->id = (pin_function_t)(Output_Aux0 + n_aux_outputs);
    output->cap = (pin_cap_t){ .output = On, .claimable = On };
    output->mode = (pin_mode_t){ .output = On };
    output->port = port;
    output->pin = pinmask_to_pinno(pin_mask);
    output->pin_mask = pin_mask;
    output->group = PinGroup_AuxOutput;
    output->description = description;
    output->value = Off;

    n_aux_outputs++;
}

#if !SDCARD_ENABLE
static void add_aux_input (en_port_t port, uint16_t pin_mask, const char *description)
{
    if(n_aux_inputs >= N_AUX_DIN_MAX)
        return;

    hc32_aux_input_t *input = &aux_inputs[n_aux_inputs];

    input->id = (pin_function_t)(Input_Aux0 + n_aux_inputs);
    input->cap = (pin_cap_t){ .input = On, .claimable = On, .invert = On };
    input->mode = (pin_mode_t){ .input = On, .pull_mode = PullMode_Up };
    input->port = port;
    input->pin = pinmask_to_pinno(pin_mask);
    input->pin_mask = pin_mask;
    input->group = PinGroup_AuxInput;
    input->description = description;
    hc32_aux_config_input(input);

    n_aux_inputs++;
}
#endif

static void add_aux_analog_input (en_port_t port, uint16_t pin_mask, uint8_t adc_index, const char *description)
{
    if(n_aux_analog_inputs >= N_AUX_AIN_MAX)
        return;

    hc32_aux_analog_input_t *input = &aux_analog_inputs[n_aux_analog_inputs];

    input->id = (pin_function_t)(Input_Analog_Aux0 + n_aux_analog_inputs);
    input->cap = (pin_cap_t){ .input = On, .analog = On, .resolution = Resolution_12bit, .claimable = On };
    input->mode = (pin_mode_t){ .input = On, .analog = On };
    input->port = port;
    input->pin = pinmask_to_pinno(pin_mask);
    input->pin_mask = pin_mask;
    input->adc_index = adc_index;
    input->group = PinGroup_AuxInputAnalog;
    input->description = description;
    input->value = -1.0f;

    n_aux_analog_inputs++;
}

void hc32_ioports_init (void)
{
    static bool initialized = false;

    if(initialized)
        return;

    n_aux_inputs = 0;
    n_aux_outputs = 0;
    n_aux_analog_inputs = 0;

    add_aux_output(PortA, Pin00, "PA0 / fan header MOSFET");
    add_aux_output(PortA, Pin02, "PA2 / bed heater MOSFET");
    add_aux_output(PortB, Pin00, "PB0 / BLTouch servo");
    add_aux_output(PortC, Pin06, "PC6 / display pin 1");
    add_aux_output(PortB, Pin02, "PB2 / display pin 2");
    add_aux_output(PortB, Pin15, "PB15 / display pin 8");

#if USE_USART == 2 && !(MPG_ENABLE == 2 || KEYPAD_ENABLE == 2)
    add_aux_output(PortC, Pin00, "PC0 / USART1 TX / display pin 3");
    add_aux_output(PortC, Pin01, "PC1 / USART1 RX / display pin 4");
#elif USE_USART == 1 && !(MPG_ENABLE == 2 || KEYPAD_ENABLE == 2)
    add_aux_output(PortA, Pin09, "PA9 / USART2 TX / CH340");
    add_aux_output(PortA, Pin15, "PA15 / USART2 RX / CH340");
#endif

#if !(CONTROL_ENABLE & CONTROL_HALT)
    add_aux_output(PortB, Pin12, "PB12 / display pin 7");
#endif
#if !(CONTROL_ENABLE & CONTROL_FEED_HOLD)
    add_aux_output(PortB, Pin13, "PB13 / display pin 6");
#endif
#if !(CONTROL_ENABLE & CONTROL_CYCLE_START)
    add_aux_output(PortB, Pin14, "PB14 / display pin 5");
#endif

#if !EEPROM_ENABLE
    add_aux_output(PortA, Pin11, "PA11 / EEPROM SDA / USB DM");
    add_aux_output(PortA, Pin12, "PA12 / EEPROM SCL / USB DP");
#endif

#if !SDCARD_ENABLE
    add_aux_input(PortA, Pin10, "PA10 / SD detect");
    add_aux_output(PortC, Pin08, "PC8 / SD D0");
    add_aux_output(PortC, Pin09, "PC9 / SD D1");
    add_aux_output(PortC, Pin10, "PC10 / SD D2");
    add_aux_output(PortC, Pin11, "PC11 / SD D3");
    add_aux_output(PortC, Pin12, "PC12 / SD CLK");
    add_aux_output(PortD, Pin02, "PD2 / SD CMD");
#endif

    add_aux_analog_input(PortC, Pin04, ADC_CH_IDX14, "PC4 / bed thermistor");
    add_aux_analog_input(PortC, Pin05, ADC_CH_IDX15, "PC5 / hotend thermistor");

    digital.in.n_ports = n_aux_inputs;
    digital.out.n_ports = n_aux_outputs;
    analog.in.n_ports = n_aux_analog_inputs;

    io_digital_t ports = {
        .ports = &digital,
        .digital_out = digital_out,
        .get_pin_info = get_pin_info,
        .wait_on_input = wait_on_input,
        .set_pin_description = set_pin_description
    };
    io_analog_t analog_ports = {
        .ports = &analog,
        .get_pin_info = get_analog_pin_info,
        .wait_on_input = analog_wait_on_input,
        .set_pin_description = set_analog_pin_description
    };

    if(n_aux_inputs || n_aux_outputs)
        ioports_add_digital(&ports);
    if(n_aux_analog_inputs)
        ioports_add_analog(&analog_ports);

    initialized = true;
}
