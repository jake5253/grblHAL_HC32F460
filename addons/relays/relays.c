/*

  relays.c - addon for handling relays linked to spindles

*/

#include "driver.h"

#if RELAYS_ENABLE

#if RELAYS_ENABLE > 3
#undef RELAYS_ENABLE
#define RELAYS_ENABLE 3
#warning Max number of allowed relays is 3!
#endif

#include <string.h>
#include <math.h>

#include "grbl/hal.h"
#include "grbl/protocol.h"
#include "grbl/nvs_buffer.h"
#include "grbl/spindle_control.h"

typedef struct {
    uint8_t port[3];
    int32_t spindle_link[3];
    float off_delay[3];
} relay_settings_t;

static const char *relay_names[] = {
    "Relay 0",
    "Relay 1",
    "Relay 2"
};

static uint32_t n_relays = 0, relays_on = 0, relays_linked = 0;
static user_mcode_ptrs_t user_mcode;
static relay_settings_t relay_setting, relays;
static io_port_cfg_t d_out;
static nvs_address_t nvs_address;

static on_spindle_select_ptr on_spindle_select;
static on_report_options_ptr on_report_options;
static on_program_completed_ptr on_program_completed;
static spindle_set_state_ptr on_spindle_set_state = NULL;
static driver_reset_ptr driver_reset;

bool relay_get_state (uint8_t relay);
void relay_set_state (uint8_t relay, bool on);

static user_mcode_type_t userMCodeCheck (user_mcode_t mcode)
{
    return mcode == 160 || mcode == 161
            ? UserMCode_Normal
            : (user_mcode.check ? user_mcode.check(mcode) : UserMCode_Unsupported);
}

static status_code_t userMCodeValidate (parser_block_t *gc_block)
{
    status_code_t state = Status_OK;

    switch(gc_block->user_mcode) {

        case 160:    // M160
        case 161:    // M161
            if(gc_block->words.p) {
                if(!isintf(gc_block->values.p) || gc_block->values.p < 0.0f || gc_block->values.p >= RELAYS_ENABLE || relays.port[(uint_fast8_t)gc_block->values.p] == IOPORT_UNASSIGNED)
                    state = Status_GcodeValueOutOfRange;
                gc_block->words.p = Off;
            } else if (relays.port[0] == IOPORT_UNASSIGNED) {
                state = Status_GcodeValueOutOfRange;
            }
            break;

        default:
            state = Status_Unhandled;
            break;
    }

    return state == Status_Unhandled && user_mcode.validate ? user_mcode.validate(gc_block) : state;
}

static void relay0_off (void *data) { relay_set_state(0, false); }
#if RELAYS_ENABLE > 1
static void relay1_off (void *data) { relay_set_state(1, false); }
#endif
#if RELAYS_ENABLE > 2
static void relay2_off (void *data) { relay_set_state(2, false); }
#endif

typedef void (*task_fn_t)(void *data);

static const task_fn_t relay_off_tasks[] = {
    relay0_off
#if RELAYS_ENABLE > 1
    , relay1_off
#endif
#if RELAYS_ENABLE > 2
    , relay2_off
#endif
};

static void userMCodeExecute (uint_fast16_t state, parser_block_t *gc_block)
{
    bool handled = true;
    uint_fast8_t relay = gc_block->words.p ? (uint8_t)gc_block->values.p : 0;

    if (state != STATE_CHECK_MODE)
      switch(gc_block->user_mcode) {

        case 160:
            relay_set_state(relay, true);
            break;

        case 161:
            task_delete(relay_off_tasks[relay], NULL);
            relay_set_state(relay, false);
            break;

        default:
            handled = false;
            break;
    }

    if(!handled && user_mcode.execute)
        user_mcode.execute(state, gc_block);
}

static void driverReset (void)
{
    if(driver_reset)
        driver_reset();

    uint32_t idx = RELAYS_ENABLE;
    do {
        relay_set_state(--idx, false);
    } while(idx);
}

static void onSpindleSetState (spindle_ptrs_t *spindle, spindle_state_t state, float rpm)
{
    uint_fast8_t idx = RELAYS_ENABLE;
    do {
        idx--;
        if(spindle != NULL && relay_setting.spindle_link[idx] != -1 && spindle->id == relay_setting.spindle_link[idx]) {

            if(!state.on && bit_isfalse(relays_linked, bit(idx)))
                continue;

            if(state.on && !relay_get_state(idx))
                bit_true(relays_linked, bit(idx));

            if(!state.on && relay_setting.off_delay[idx] > 0.0f)
                task_add_delayed(relay_off_tasks[idx], NULL, (uint32_t)(relay_setting.off_delay[idx] * 60.0f * 1000.0f));
            else
                relay_set_state(idx, state.on);
        }
    } while(idx);

    if(on_spindle_set_state)
        on_spindle_set_state(spindle, state, rpm);
}

static bool onSpindleSelect (spindle_ptrs_t *spindle)
{
    if(spindle->set_state != onSpindleSetState) {
        on_spindle_set_state = spindle->set_state;
        spindle->set_state = onSpindleSetState;
    }
    return on_spindle_select == NULL || on_spindle_select(spindle);
}

static void onProgramCompleted (program_flow_t program_flow, bool check_mode)
{
    uint_fast8_t idx = RELAYS_ENABLE;
    do {
        idx--;
        if(relays.port[idx] != IOPORT_UNASSIGNED && relay_setting.off_delay[idx] > 0.0f)
            task_add_delayed(relay_off_tasks[idx], NULL, (uint32_t)(relay_setting.off_delay[idx] * 60.0f * 1000.0f));
        else
            relay_set_state(idx, false);
    } while(idx);

    if(on_program_completed)
        on_program_completed(program_flow, check_mode);
}

bool relay_get_state (uint8_t relay)
{
    return relays.port[relay] != IOPORT_UNASSIGNED && !!(relays_on & (1 << relay));
}

void relay_set_state (uint8_t relay, bool on)
{
    if(relays.port[relay] != IOPORT_UNASSIGNED) {

        if(on)
            bit_true(relays_on, bit(relay));
        else {
            bit_false(relays_on, bit(relay));
            bit_false(relays_linked, bit(relay));
        }

        task_delete(relay_off_tasks[relay], NULL);
        ioport_digital_out(relays.port[relay], on);
    }
}

static void relays_setup (void)
{
    memcpy(&user_mcode, &grbl.user_mcode, sizeof(user_mcode_ptrs_t));

    grbl.user_mcode.check = userMCodeCheck;
    grbl.user_mcode.validate = userMCodeValidate;
    grbl.user_mcode.execute = userMCodeExecute;

    driver_reset = hal.driver_reset;
    hal.driver_reset = driverReset;

    on_program_completed = grbl.on_program_completed;
    grbl.on_program_completed = onProgramCompleted;
}

static bool is_setting_available (const setting_detail_t *setting, uint_fast16_t offset)
{
    return d_out.n_ports >= ((setting->id - Setting_UserDefined_0) / 3);
}

static status_code_t set_float (setting_id_t setting, float value)
{
    return d_out.set_value(&d_out, &relay_setting.port[(setting - Setting_UserDefined_0) / 3], (pin_cap_t){}, value);
}

static float get_float (setting_id_t setting)
{
    return d_out.get_value(&d_out, relay_setting.port[(setting - Setting_UserDefined_0) / 3]);
}

static status_code_t set_spindle_link (setting_id_t setting, float value)
{
    uint8_t relay = (setting - Setting_UserDefined_0) / 3;
    if (value < -1.0f || value > 7.0f || (value != -1.0f && value >= N_SPINDLE))
        return Status_GcodeValueOutOfRange;
    relay_setting.spindle_link[relay] = (int32_t)value;
    return Status_OK;
}

static float get_spindle_link (setting_id_t setting)
{
    uint8_t relay = (setting - Setting_UserDefined_0) / 3;
    return (float)relay_setting.spindle_link[relay];
}

static const setting_detail_t relay_settings[] = {
    { Setting_UserDefined_0, Group_AuxPorts, "Relay 0 port", NULL, Format_Decimal, "-#0", "-1", d_out.port_maxs, Setting_NonCoreFn, set_float, get_float, is_setting_available, { .reboot_required = On } },
    { Setting_UserDefined_1, Group_Spindle, "Relay 0 spindle link", NULL, Format_Decimal, "-#0", "-1", "7", Setting_NonCoreFn, set_spindle_link, get_spindle_link, NULL },
    { Setting_UserDefined_2, Group_Spindle, "Relay 0 off delay", "minutes", Format_Decimal, "#0.0", "0.0", "30.0", Setting_NonCore, &relay_setting.off_delay[0], NULL, NULL },
#if RELAYS_ENABLE > 1
    { Setting_UserDefined_3, Group_AuxPorts, "Relay 1 port", NULL, Format_Decimal, "-#0", "-1", d_out.port_maxs, Setting_NonCoreFn, set_float, get_float, is_setting_available, { .reboot_required = On } },
    { Setting_UserDefined_4, Group_Spindle, "Relay 1 spindle link", NULL, Format_Decimal, "-#0", "-1", "7", Setting_NonCoreFn, set_spindle_link, get_spindle_link, NULL },
    { Setting_UserDefined_5, Group_Spindle, "Relay 1 off delay", "minutes", Format_Decimal, "#0.0", "0.0", "30.0", Setting_NonCore, &relay_setting.off_delay[1], NULL, NULL },
#endif
#if RELAYS_ENABLE > 2
    { Setting_UserDefined_6, Group_AuxPorts, "Relay 2 port", NULL, Format_Decimal, "-#0", "-1", d_out.port_maxs, Setting_NonCoreFn, set_float, get_float, is_setting_available, { .reboot_required = On } },
    { Setting_UserDefined_7, Group_Spindle, "Relay 2 spindle link", NULL, Format_Decimal, "-#0", "-1", "7", Setting_NonCoreFn, set_spindle_link, get_spindle_link, NULL },
    { Setting_UserDefined_8, Group_Spindle, "Relay 2 off delay", "minutes", Format_Decimal, "#0.0", "0.0", "30.0", Setting_NonCore, &relay_setting.off_delay[2], NULL, NULL },
#endif
};

static const setting_descr_t relay_settings_descr[] = {
    { Setting_UserDefined_0, "Aux output port number to use for relay 0 control. Set to -1 to disable." },
    { Setting_UserDefined_1, "ID of the spindle to link relay 0 to. Set to -1 to disable linking." },
    { Setting_UserDefined_2, "Delay before turning relay 0 off after spindle/program end." },
#if RELAYS_ENABLE > 1
    { Setting_UserDefined_3, "Aux output port number to use for relay 1 control. Set to -1 to disable." },
    { Setting_UserDefined_4, "ID of the spindle to link relay 1 to. Set to -1 to disable linking." },
    { Setting_UserDefined_5, "Delay before turning relay 1 off after spindle/program end." },
#endif
#if RELAYS_ENABLE > 2
    { Setting_UserDefined_6, "Aux output port number to use for relay 2 control. Set to -1 to disable." },
    { Setting_UserDefined_7, "ID of the spindle to link relay 2 to. Set to -1 to disable linking." },
    { Setting_UserDefined_8, "Delay before turning relay 2 off after spindle/program end." },
#endif
};

// Write settings to non volatile storage (NVS).
static void relay_settings_save (void)
{
    hal.nvs.memcpy_to_nvs(nvs_address, (uint8_t *)&relay_setting, sizeof(relay_settings_t), true);
}

// Restore default settings and write to non volatile storage (NVS).
static void relay_settings_restore (void)
{
    uint32_t idx = RELAYS_ENABLE;

    do {
        idx--;
        relay_setting.port[idx] = IOPORT_UNASSIGNED;
        relay_setting.spindle_link[idx] = -1;
        relay_setting.off_delay[idx] = 0.0f;
    } while(idx);

    hal.nvs.memcpy_to_nvs(nvs_address, (uint8_t *)&relay_setting, sizeof(relay_settings_t), true);
}

static void relay_settings_load (void)
{
    uint_fast8_t failed = 0;
    uint_fast8_t idx = RELAYS_ENABLE;

    if(hal.nvs.memcpy_from_nvs((uint8_t *)&relay_setting, nvs_address, sizeof(relay_settings_t), true) != NVS_TransferResult_OK)
        relay_settings_restore();

    do {
        idx--;
        if((relays.port[idx] = relay_setting.port[idx]) != IOPORT_UNASSIGNED && d_out.claim(&d_out, &relays.port[idx], relay_names[idx], (pin_cap_t){}))
            n_relays++;
        else {
            failed++;
            relays.port[idx] = IOPORT_UNASSIGNED;
        }
    } while(idx);

    if(n_relays)
        relays_setup();

    if(failed)
        task_run_on_startup(report_warning, "Relays addon: configured port number(s) not available");
}

static void onReportOptions (bool newopt)
{
    if (on_report_options)
        on_report_options(newopt);

    if(!newopt) {
        report_plugin("Relays", "1.0.0");
        hal.stream.write("[RELAYS:");
        hal.stream.write(uitoa(n_relays));
        hal.stream.write("]" ASCII_EOL);
    }
}

void relays_init (void)
{
    static setting_details_t setting_details = {
        .settings = relay_settings,
        .n_settings = sizeof(relay_settings) / sizeof(setting_detail_t),
        .descriptions = relay_settings_descr,
        .n_descriptions = sizeof(relay_settings_descr) / sizeof(setting_descr_t),
        .save = relay_settings_save,
        .load = relay_settings_load,
        .restore = relay_settings_restore
    };

    if(ioports_cfg(&d_out, Port_Digital, Port_Output)->n_ports && (nvs_address = nvs_alloc(sizeof(relay_settings_t)))) {

        settings_register(&setting_details);

        on_report_options = grbl.on_report_options;
        grbl.on_report_options = onReportOptions;

        on_spindle_select = grbl.on_spindle_select;
        grbl.on_spindle_select = onSpindleSelect;

    } else
        task_run_on_startup(report_warning, "Relays addon failed to initialize!");
}

#endif // RELAYS_ENABLE