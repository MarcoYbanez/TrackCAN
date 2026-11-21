/*******************************************************
 * @file dash.c
 * @brief Dash Node (STM32H7): TrackCAN logic. Hardware-independent.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "dash.h"

#include <string.h>

/*******************************************************
 * Static Variables
 *******************************************************/
static tcan_t l_can;
static uint8_t l_heartbeat_counter;
static uint16_t l_rpm;
static uint16_t l_speed_x100;
static uint32_t l_init_ms;

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static void publish(void)
{
    uint8_t buf[TCAN_MAX_DLC];

    if (tcan_tx_due(&l_can, TCAN_MSG_DASH_ENGINE)) {
        struct trackcan_dash_engine_t m = {.rpm = l_rpm, .speed = l_speed_x100};

        if (trackcan_dash_engine_pack(buf, &m, sizeof(buf)) > 0) {
            (void)tcan_tx(&l_can, TCAN_MSG_DASH_ENGINE, buf);
        }
    }

    if (tcan_tx_due(&l_can, TCAN_MSG_HEARTBEAT_DASH)) {
        struct trackcan_heartbeat_dash_t hb = {.status = 0, .counter = l_heartbeat_counter++};

        if (trackcan_heartbeat_dash_pack(buf, &hb, sizeof(buf)) > 0) {
            (void)tcan_tx(&l_can, TCAN_MSG_HEARTBEAT_DASH, buf);
        }
    }
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
bool dash_can_init(const tcan_port_t *port)
{
    l_heartbeat_counter = 0;
    l_rpm = 0;
    l_speed_x100 = 0;
    if (!tcan_init(&l_can, port, TCAN_NODE_DASH)) {
        return false;
    }
    l_init_ms = port->now_ms();
    return true;
}

void dash_can_step(void)
{
    tcan_poll(&l_can);
    publish();
}

void dash_can_set_engine(uint16_t rpm, uint16_t speed_kmh_x100)
{
    l_rpm = rpm;
    l_speed_x100 = speed_kmh_x100;
}

void dash_can_get_display(struct dash_display *out)
{
    uint8_t buf[TCAN_MAX_DLC];

    memset(out, 0, sizeof(*out));

    out->analog_state = tcan_rx_get(&l_can, TCAN_MSG_DATALOGGER_ANALOG, buf);
    if (out->analog_state != TCAN_RX_NEVER) {
        struct trackcan_data_logger_analog_t a;

        if (trackcan_data_logger_analog_unpack(&a, buf, sizeof(buf)) == 0) {
            out->oil_pressure_psi_x10 = tcan_bar_x100_to_psi_x10(a.oil_pressure);
            out->oil_temp_degf_x10 = tcan_degc_x10_to_degf_x10(a.oil_temp);
            out->coolant_temp_degf_x10 = tcan_degc_x10_to_degf_x10(a.coolant_temp);
            out->fuel_level_pct_x10 = a.fuel_level;
        }
    }

    out->lights_state = tcan_rx_get(&l_can, TCAN_MSG_DATALOGGER_LIGHTS, buf);
    if (out->lights_state != TCAN_RX_NEVER) {
        out->lights = buf[0];
    }

    out->ethanol_state = tcan_rx_get(&l_can, TCAN_MSG_ETHANOL_DATA, buf);
    if (out->ethanol_state != TCAN_RX_NEVER) {
        struct trackcan_ethanol_data_t e;

        if (trackcan_ethanol_data_unpack(&e, buf, TRACKCAN_ETHANOL_DATA_LENGTH) == 0) {
            out->ethanol_pct = e.ethanol_content;
            out->fuel_temp_valid = (e.fuel_temp != INT16_MIN);
            if (out->fuel_temp_valid) {
                out->fuel_temp_degf_x10 = tcan_degc_x10_to_degf_x10(e.fuel_temp);
            }
            out->ethanol_node_state = e.state;
            out->ethanol_output_default = (e.output_is_default != 0);
            out->ethanol_alert = (e.critical != 0);
        }
    }

    /* The Ethanol Node going quiet is itself critical: the ECU would be fed a frozen value. */
    if (out->ethanol_state == TCAN_RX_STALE) {
        out->ethanol_alert = true;
    } else if (out->ethanol_state == TCAN_RX_NEVER &&
               (uint32_t)(l_can.port->now_ms() - l_init_ms) > DASH_ETHANOL_BOOT_ALERT_MS) {
        out->ethanol_alert = true;
    }

    if (tcan_rx_get(&l_can, TCAN_MSG_HEARTBEAT_ETHANOL, buf) != TCAN_RX_NEVER) {
        out->ethanol_fault_code = buf[0];
    }
}
