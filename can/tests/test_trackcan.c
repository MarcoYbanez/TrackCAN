/*******************************************************
 * @file test_trackcan.c
 * @brief Host-side tests for the TrackCAN core and the DBC-generated pack/unpack code.
 *
 * Expected frame bytes are worked out by hand from can/trackcan.dbc (little-endian, scale
 * and offset applied), NOT produced by the code under test. A pack/unpack round trip alone
 * would pass even if both directions had the same byte-order bug.
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include <stdio.h>
#include <string.h>

#include "dash.h"
#include "trackcan_core.h"

/*******************************************************
 * Macros
 *******************************************************/
#define CHECK(cond)                                                                                \
    do {                                                                                           \
        l_checks++;                                                                                \
        if (!(cond)) {                                                                             \
            l_failures++;                                                                          \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                 \
        }                                                                                          \
    } while (0)

#define CHECK_BYTES(actual, expected) CHECK(memcmp((actual), (expected), sizeof(expected)) == 0)

#define FAKE_QUEUE_LEN 64

/*******************************************************
 * Static Variables
 *******************************************************/
static int l_checks;
static int l_failures;

static uint32_t l_now;
static tcan_frame_t l_rx_queue[FAKE_QUEUE_LEN];
static int l_rx_head;
static int l_rx_tail;
static int l_send_result;
static tcan_frame_t l_last_sent;
static int l_sent_count;
static tcan_frame_t l_sent[8];

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static int fake_send(const tcan_frame_t *frame)
{
    if (l_send_result < 0) {
        return l_send_result;
    }
    l_last_sent = *frame;
    if (l_sent_count < 8) {
        l_sent[l_sent_count] = *frame;
    }
    l_sent_count++;
    return 0;
}

static int fake_receive(tcan_frame_t *frame)
{
    if (l_rx_head == l_rx_tail) {
        return 0;
    }
    *frame = l_rx_queue[l_rx_head];
    l_rx_head = (l_rx_head + 1) % FAKE_QUEUE_LEN;
    return 1;
}

static uint32_t fake_now_ms(void)
{
    return l_now;
}

static const tcan_port_t l_port = {fake_send, fake_receive, fake_now_ms};

static void fake_reset(void)
{
    l_now = 0;
    l_rx_head = 0;
    l_rx_tail = 0;
    l_send_result = 0;
    l_sent_count = 0;
    memset(&l_last_sent, 0, sizeof(l_last_sent));
}

static void fake_inject(uint32_t id, uint8_t dlc, const uint8_t *data)
{
    tcan_frame_t *f = &l_rx_queue[l_rx_tail];

    memset(f, 0, sizeof(*f));
    f->id = id;
    f->dlc = dlc;
    memcpy(f->data, data, dlc);
    l_rx_tail = (l_rx_tail + 1) % FAKE_QUEUE_LEN;
}

static void test_message_table(void)
{
    const tcan_msg_info_t *m;
    tcan_msg_t msg;

    /* Literal values from the agreed design, independent of the generated macros. */
    static const struct {
        tcan_msg_t msg;
        uint32_t id;
        uint8_t dlc;
        uint16_t cycle;
        tcan_node_t sender;
    } expect[] = {
        {TCAN_MSG_DASH_ENGINE, 0x100, 4, 20, TCAN_NODE_DASH},
        {TCAN_MSG_DATALOGGER_ANALOG, 0x200, 6, 100, TCAN_NODE_DATALOGGER},
        {TCAN_MSG_DATALOGGER_LIGHTS, 0x201, 2, 100, TCAN_NODE_DATALOGGER},
        {TCAN_MSG_ETHANOL_DATA, 0x300, 4, 100, TCAN_NODE_ETHANOL},
        {TCAN_MSG_HEARTBEAT_DASH, 0x701, 2, 1000, TCAN_NODE_DASH},
        {TCAN_MSG_HEARTBEAT_DATALOGGER, 0x702, 2, 1000, TCAN_NODE_DATALOGGER},
        {TCAN_MSG_HEARTBEAT_ETHANOL, 0x703, 2, 1000, TCAN_NODE_ETHANOL},
        {TCAN_MSG_DATALOGGER_STATUS, 0x202, 1, 200, TCAN_NODE_DATALOGGER},
        {TCAN_MSG_LOGGER_CONTROL, 0x400, 1, 100, TCAN_NODE_CONTROLLER},
        {TCAN_MSG_DATALOGGER_OILPRESSURE, 0x203, 2, 20, TCAN_NODE_DATALOGGER},
    };

    for (unsigned i = 0; i < sizeof(expect) / sizeof(expect[0]); i++) {
        m = tcan_msg_info(expect[i].msg);
        CHECK(m != NULL);
        CHECK(m->id == expect[i].id);
        CHECK(m->dlc == expect[i].dlc);
        CHECK(m->cycle_ms == expect[i].cycle);
        CHECK(m->sender == expect[i].sender);
        CHECK(tcan_msg_from_id(expect[i].id, &msg) && msg == expect[i].msg);
    }
    CHECK(tcan_msg_info(TCAN_MSG_COUNT) == NULL);
    CHECK(!tcan_msg_from_id(0x7FF, &msg));
}

static void test_pack_known_bytes(void)
{
    uint8_t buf[8];

    /* Dash_Engine: rpm 3500 = 0x0DAC, speed 123.45 km/h = 12345 = 0x3039, little-endian. */
    {
        struct trackcan_dash_engine_t s = {.rpm = 3500, .speed = 12345};
        static const uint8_t want[] = {0xAC, 0x0D, 0x39, 0x30};

        memset(buf, 0xEE, sizeof(buf));
        CHECK(trackcan_dash_engine_pack(buf, &s, sizeof(buf)) == 4);
        CHECK_BYTES(buf, want);
    }

    /* DataLogger_Analog (slow, 6 bytes since TCAN-28): -40.0 degC = -400 = 0xFE70;
     * 95.0 degC = 950 = 0x03B6; 75.5 % = 755 = 0x02F3. Oil pressure is no longer in it. */
    {
        struct trackcan_data_logger_analog_t s = {.oil_temp = -400, .coolant_temp = 950, .fuel_level = 755};
        static const uint8_t want[] = {0x70, 0xFE, 0xB6, 0x03, 0xF3, 0x02};

        memset(buf, 0xEE, sizeof(buf));
        CHECK(trackcan_data_logger_analog_pack(buf, &s, sizeof(buf)) == 6);
        CHECK_BYTES(buf, want);
        CHECK(buf[6] == 0xEE && buf[7] == 0xEE); /* the unused bytes are not touched */
    }

    /* DataLogger_OilPressure (fast, 2 bytes): 4.50 bar = 450 = 0x01C2, little-endian C2 01. */
    {
        struct trackcan_data_logger_oil_pressure_t s = {.oil_pressure = 450};
        static const uint8_t want[] = {0xC2, 0x01};

        memset(buf, 0xEE, sizeof(buf));
        CHECK(trackcan_data_logger_oil_pressure_pack(buf, &s, sizeof(buf)) == 2);
        CHECK_BYTES(buf, want);
        CHECK(buf[2] == 0xEE);
    }

    /* DataLogger_Lights: byte 0 bit 0 check engine ... bit 7 high beam, byte 1 bit 0 charge
     * warning. Left turn (bit 2) + high beam (bit 7) = 0x84; charge warning = 0x01 in byte 1. */
    {
        struct trackcan_data_logger_lights_t s;
        static const uint8_t want[] = {0x84, 0x01};

        memset(&s, 0, sizeof(s));
        s.left_turn = 1;
        s.high_beam = 1;
        s.charge_warning = 1;
        memset(buf, 0xEE, sizeof(buf));
        CHECK(trackcan_data_logger_lights_pack(buf, &s, sizeof(buf)) == 2);
        CHECK_BYTES(buf, want);
    }

    /* Ethanol_Data: 85 % = 0x55; -12.3 degC = -123 = 0xFF85. Status byte (byte 3): State is
     * bits 0-2, Critical bit 3, OutputIsDefault bit 4, SignalValid bit 5, DacWarning bit 6,
     * ResetRecent bit 7. State Holding (3) + SignalValid = 0x03 | 0x20 = 0x23. */
    {
        struct trackcan_ethanol_data_t s;
        static const uint8_t want[] = {0x55, 0x85, 0xFF, 0x23};

        memset(&s, 0, sizeof(s));
        s.ethanol_content = 85;
        s.fuel_temp = -123;
        s.state = 3;
        s.signal_valid = 1;
        memset(buf, 0xEE, sizeof(buf));
        CHECK(trackcan_ethanol_data_pack(buf, &s, sizeof(buf)) == 4);
        CHECK_BYTES(buf, want);
    }

    /* Critical alert: E85 default (0x55), temperature invalid (0x8000 -> 00 80), State
     * Critical (4) | Critical 0x08 | OutputIsDefault 0x10 | ResetRecent 0x80 = 0x9C. */
    {
        struct trackcan_ethanol_data_t s;
        static const uint8_t want[] = {0x55, 0x00, 0x80, 0x9C};

        memset(&s, 0, sizeof(s));
        s.ethanol_content = 85;
        s.fuel_temp = INT16_MIN;
        s.state = 4;
        s.critical = 1;
        s.output_is_default = 1;
        s.reset_recent = 1;
        memset(buf, 0xEE, sizeof(buf));
        CHECK(trackcan_ethanol_data_pack(buf, &s, sizeof(buf)) == 4);
        CHECK_BYTES(buf, want);
    }

    /* Heartbeat: status 0, counter 0x2A. */
    {
        struct trackcan_heartbeat_ethanol_t s = {.status = 0, .counter = 0x2A};
        static const uint8_t want[] = {0x00, 0x2A};

        memset(buf, 0xEE, sizeof(buf));
        CHECK(trackcan_heartbeat_ethanol_pack(buf, &s, sizeof(buf)) == 2);
        CHECK_BYTES(buf, want);
    }
}

static void test_unpack_known_bytes(void)
{
    /* Decode hand-written bytes (the reverse direction, also independent of pack). */
    static const uint8_t analog[] = {0x70, 0xFE, 0xB6, 0x03, 0xF3, 0x02};
    static const uint8_t oil[] = {0xC2, 0x01};
    static const uint8_t lights[] = {0x01, 0x01};
    struct trackcan_data_logger_analog_t a;
    struct trackcan_data_logger_oil_pressure_t o;
    struct trackcan_data_logger_lights_t l;

    CHECK(trackcan_data_logger_oil_pressure_unpack(&o, oil, sizeof(oil)) == 0);
    CHECK(o.oil_pressure == 450);
    CHECK(trackcan_data_logger_oil_pressure_unpack(&o, oil, 1) != 0); /* too short */

    CHECK(trackcan_data_logger_analog_unpack(&a, analog, sizeof(analog)) == 0);
    CHECK(a.oil_temp == -400);
    CHECK(a.coolant_temp == 950);
    CHECK(a.fuel_level == 755);

    CHECK(trackcan_data_logger_lights_unpack(&l, lights, sizeof(lights)) == 0);
    CHECK(l.check_engine == 1);
    CHECK(l.high_beam == 0);
    CHECK(l.charge_warning == 1);

    /* Too-short buffers are rejected rather than read out of bounds. */
    CHECK(trackcan_data_logger_analog_unpack(&a, analog, 5) != 0);
}

static void test_logger_messages(void)
{
    /* DataLogger_Status (0x202): LogMode in bits 0-1, CardState in bits 2-3. Logger_Control (0x400): LogButton in bit 0. Bytes by hand. */
    struct trackcan_data_logger_status_t st = {0};
    struct trackcan_logger_control_t ctl = {0};
    uint8_t b[8];
    tcan_t can;
    uint8_t payload[8] = {0};

    st.log_mode = 1; /* high resolution */
    st.card_state = 3; /* logging */
    memset(b, 0xAA, sizeof(b));
    CHECK(trackcan_data_logger_status_pack(b, &st, 1) == 1);
    CHECK(b[0] == 0x0D); /* 1 | (3 << 2) */
    st.log_mode = 0;
    st.card_state = 1; /* no card */
    CHECK(trackcan_data_logger_status_pack(b, &st, 1) == 1);
    CHECK(b[0] == 0x04);
    st.log_mode = 0;
    st.card_state = 0;
    CHECK(trackcan_data_logger_status_pack(b, &st, 1) == 1 && b[0] == 0x00);

    ctl.log_button = 1;
    CHECK(trackcan_logger_control_pack(b, &ctl, 1) == 1 && b[0] == 0x01);
    ctl.log_button = 0;
    CHECK(trackcan_logger_control_pack(b, &ctl, 1) == 1 && b[0] == 0x00);

    /* Decoding hand-written bytes (the reverse direction). Bits that are not the signal are ignored. */
    {
        static const uint8_t status[] = {0x0D};
        static const uint8_t status_noise[] = {0xF2}; /* LogMode bits 10b = 2 would be out of range: only bit 0..1 read; CardState = 0 */
        static const uint8_t pressed[] = {0x01};
        static const uint8_t released_noise[] = {0xFE}; /* every bit set except bit 0 */

        CHECK(trackcan_data_logger_status_unpack(&st, status, sizeof(status)) == 0);
        CHECK(st.log_mode == 1 && st.card_state == 3);
        CHECK(trackcan_data_logger_status_unpack(&st, status_noise, sizeof(status_noise)) == 0);
        CHECK(st.log_mode == 2 && st.card_state == 0);
        CHECK(trackcan_logger_control_unpack(&ctl, pressed, sizeof(pressed)) == 0 && ctl.log_button == 1);
        CHECK(trackcan_logger_control_unpack(&ctl, released_noise, sizeof(released_noise)) == 0 && ctl.log_button == 0);
        CHECK(trackcan_logger_control_unpack(&ctl, pressed, 0) != 0); /* too short is rejected */
    }

    /* Who may send what: only the DataLogger sends its status, only a Controller sends the button. */
    fake_reset();
    CHECK(tcan_init(&can, &l_port, TCAN_NODE_DATALOGGER));
    CHECK(tcan_tx(&can, TCAN_MSG_LOGGER_CONTROL, payload) == -2);
    CHECK(tcan_tx(&can, TCAN_MSG_DATALOGGER_STATUS, payload) == 0);
    CHECK(l_last_sent.id == 0x202 && l_last_sent.dlc == 1);
    fake_reset();
    CHECK(tcan_init(&can, &l_port, TCAN_NODE_CONTROLLER));
    CHECK(tcan_tx(&can, TCAN_MSG_DATALOGGER_STATUS, payload) == -2);
    CHECK(tcan_tx(&can, TCAN_MSG_LOGGER_CONTROL, payload) == 0);
    CHECK(l_last_sent.id == 0x400 && l_last_sent.dlc == 1);

    /* The new IDs reach the right message when received. */
    {
        tcan_msg_t m;

        CHECK(tcan_msg_from_id(0x400, &m) && m == TCAN_MSG_LOGGER_CONTROL);
        CHECK(tcan_msg_from_id(0x202, &m) && m == TCAN_MSG_DATALOGGER_STATUS);
    }
}

static void test_staleness(void)
{
    /* 20 ms cycle: stale only when MORE than 60 ms have passed. */
    CHECK(!tcan_is_stale(60, 0, 20));
    CHECK(tcan_is_stale(61, 0, 20));
    CHECK(!tcan_is_stale(1000, 1000, 20));

    /* Counter wrap-around: last just before 2^32, now just after. Elapsed is 32 ms. */
    CHECK(!tcan_is_stale(0x00000010u, 0xFFFFFFF0u, 20));
    CHECK(tcan_is_stale(0x00000030u, 0xFFFFFFF0u, 20)); /* 64 ms elapsed */
}

static void test_rx_tracking(void)
{
    tcan_t can;
    uint8_t out[TCAN_MAX_DLC];
    static const uint8_t eng[] = {0xAC, 0x0D, 0x39, 0x30};
    static const uint8_t short_eng[] = {0xAC, 0x0D};

    fake_reset();
    CHECK(tcan_init(&can, &l_port, TCAN_NODE_DATALOGGER));
    CHECK(tcan_rx_get(&can, TCAN_MSG_DASH_ENGINE, out) == TCAN_RX_NEVER);

    l_now = 1000;
    fake_inject(0x100, 4, eng);
    fake_inject(0x555, 4, eng);       /* unknown ID: ignored */
    fake_inject(0x100, 2, short_eng); /* known ID, wrong length: counted and dropped */
    tcan_poll(&can);

    CHECK(can.rx_frames == 1);
    CHECK(can.rx_bad_frames == 1);
    CHECK(tcan_rx_get(&can, TCAN_MSG_DASH_ENGINE, out) == TCAN_RX_FRESH);
    CHECK(out[0] == 0xAC && out[1] == 0x0D && out[2] == 0x39 && out[3] == 0x30);

    l_now = 1060;
    CHECK(tcan_rx_get(&can, TCAN_MSG_DASH_ENGINE, NULL) == TCAN_RX_FRESH);
    l_now = 1061;
    CHECK(tcan_rx_get(&can, TCAN_MSG_DASH_ENGINE, NULL) == TCAN_RX_STALE);

    /* A fresh frame clears the stale state. */
    fake_inject(0x100, 4, eng);
    tcan_poll(&can);
    CHECK(tcan_rx_get(&can, TCAN_MSG_DASH_ENGINE, NULL) == TCAN_RX_FRESH);

    /* The poll loop is bounded, so a flooded bus cannot starve the caller. */
    for (int i = 0; i < FAKE_QUEUE_LEN - 1; i++) {
        fake_inject(0x100, 4, eng);
    }
    can.rx_frames = 0;
    tcan_poll(&can);
    CHECK(can.rx_frames == TCAN_POLL_MAX_FRAMES);
}

static void test_tx_schedule(void)
{
    tcan_t can;
    uint8_t payload[8] = {0};

    fake_reset();
    CHECK(tcan_init(&can, &l_port, TCAN_NODE_DASH));

    /* Dash owns Dash_Engine (20 ms), not DataLogger_Analog. */
    CHECK(!tcan_tx_due(&can, TCAN_MSG_DATALOGGER_ANALOG));
    CHECK(tcan_tx(&can, TCAN_MSG_DATALOGGER_ANALOG, payload) == -2);

    l_now = 100;
    CHECK(tcan_tx_due(&can, TCAN_MSG_DASH_ENGINE)); /* first call is always due */
    l_now = 119;
    CHECK(!tcan_tx_due(&can, TCAN_MSG_DASH_ENGINE));
    l_now = 120;
    CHECK(tcan_tx_due(&can, TCAN_MSG_DASH_ENGINE));
    l_now = 125; /* late by 5 ms: phase kept, next slot is 140 */
    CHECK(!tcan_tx_due(&can, TCAN_MSG_DASH_ENGINE));
    l_now = 140;
    CHECK(tcan_tx_due(&can, TCAN_MSG_DASH_ENGINE));
    l_now = 500; /* very late: resync to now, no burst of catch-up frames */
    CHECK(tcan_tx_due(&can, TCAN_MSG_DASH_ENGINE));
    CHECK(!tcan_tx_due(&can, TCAN_MSG_DASH_ENGINE));
    l_now = 520;
    CHECK(tcan_tx_due(&can, TCAN_MSG_DASH_ENGINE));

    /* The frame on the wire uses the table's ID and length. */
    payload[0] = 0xAC;
    payload[1] = 0x0D;
    payload[2] = 0x39;
    payload[3] = 0x30;
    CHECK(tcan_tx(&can, TCAN_MSG_DASH_ENGINE, payload) == 0);
    CHECK(l_sent_count == 1);
    CHECK(l_last_sent.id == 0x100 && l_last_sent.dlc == 4);
    CHECK(l_last_sent.data[0] == 0xAC && l_last_sent.data[3] == 0x30);

    /* A failing controller is counted and never blocks or crashes. */
    l_send_result = -1;
    CHECK(tcan_tx(&can, TCAN_MSG_DASH_ENGINE, payload) == -3);
    CHECK(can.tx_dropped == 1);

    CHECK(tcan_tx(NULL, TCAN_MSG_DASH_ENGINE, payload) == -1);
    CHECK(tcan_tx(&can, TCAN_MSG_COUNT, payload) == -1);
}

static void test_init_rejects_bad_port(void)
{
    tcan_t can;
    tcan_port_t incomplete = {fake_send, NULL, fake_now_ms};

    CHECK(!tcan_init(&can, &incomplete, TCAN_NODE_DASH));
    CHECK(!tcan_init(NULL, &l_port, TCAN_NODE_DASH));
    CHECK(!tcan_init(&can, NULL, TCAN_NODE_DASH));
}

static void test_unit_conversion(void)
{
    /* bar x100 to PSI x10 (1 bar = 14.5038 psi). */
    CHECK(tcan_bar_x100_to_psi_x10(0) == 0);
    CHECK(tcan_bar_x100_to_psi_x10(450) == 653);     /* 4.50 bar = 65.27 psi */
    CHECK(tcan_bar_x100_to_psi_x10(1000) == 1450);   /* 10.00 bar = 145.04 psi */
    CHECK(tcan_bar_x100_to_psi_x10(65535) == 95051); /* exercises the 64-bit intermediate */

    /* degC x10 to degF x10. */
    CHECK(tcan_degc_x10_to_degf_x10(0) == 320);
    CHECK(tcan_degc_x10_to_degf_x10(1000) == 2120); /* 100.0 C = 212.0 F */
    CHECK(tcan_degc_x10_to_degf_x10(950) == 2030);  /* 95.0 C = 203.0 F */
    CHECK(tcan_degc_x10_to_degf_x10(-400) == -400); /* -40 C = -40 F */
    CHECK(tcan_degc_x10_to_degf_x10(-1) == 318);    /* -0.1 C = 31.82 F */
}

static void test_dash_display_and_publish(void)
{
    struct dash_display d;
    /* DataLogger_Analog (slow) and DataLogger_OilPressure (fast), hand-computed: -40.0 C, 95.0 C, 75.5 % and 4.50 bar. */
    static const uint8_t analog[] = {0x70, 0xFE, 0xB6, 0x03, 0xF3, 0x02};
    static const uint8_t oil[] = {0xC2, 0x01};
    static const uint8_t lights[] = {0x84, 0x01};
    static const uint8_t ethanol[] = {0x55, 0x85, 0xFF, 0x23}; /* 85 %, -12.3 C, Holding */

    fake_reset();
    CHECK(dash_can_init(&l_port));
    dash_can_get_display(&d);
    CHECK(d.analog_state == TCAN_RX_NEVER);
    CHECK(d.oil_pressure_state == TCAN_RX_NEVER);

    l_now = 5000;
    fake_inject(0x200, 6, analog);
    fake_inject(0x203, 2, oil);
    fake_inject(0x201, 2, lights);
    fake_inject(0x300, 4, ethanol);
    dash_can_step();
    dash_can_get_display(&d);

    CHECK(d.analog_state == TCAN_RX_FRESH);
    CHECK(d.oil_pressure_state == TCAN_RX_FRESH);
    CHECK(d.oil_pressure_psi_x10 == 653);   /* 4.50 bar = 65.3 psi */
    CHECK(d.oil_temp_degf_x10 == -400);     /* -40.0 C = -40.0 F */
    CHECK(d.coolant_temp_degf_x10 == 2030); /* 95.0 C = 203.0 F */
    CHECK(d.fuel_level_pct_x10 == 755);
    CHECK(d.lights_state == TCAN_RX_FRESH && d.lights == 0x184); /* byte 1 bit 0 is bit 8 */
    CHECK(d.ethanol_state == TCAN_RX_FRESH && d.ethanol_pct == 85);
    CHECK(d.fuel_temp_degf_x10 == 99); /* -12.3 C = 9.86 F = 98.6 tenths, rounds to 99 */
    CHECK(d.fuel_temp_valid);
    CHECK(d.ethanol_node_state == 3 && !d.ethanol_output_default && !d.ethanol_alert);

    /* Each analog message goes stale after 3 of its own cycles: the oil pressure (20 ms cycle) after 60 ms, the slow message (100 ms) after
     * 300 ms. They are independent: the oil pressure going quiet must not blank the temperatures, and the other way round. */
    l_now = 5060;
    dash_can_get_display(&d);
    CHECK(d.oil_pressure_state == TCAN_RX_FRESH && d.analog_state == TCAN_RX_FRESH);
    l_now = 5061;
    dash_can_get_display(&d);
    CHECK(d.oil_pressure_state == TCAN_RX_STALE); /* 61 ms without the 20 ms message */
    CHECK(d.analog_state == TCAN_RX_FRESH);       /* the slow message is only 61 ms old */
    l_now = 5300;
    dash_can_get_display(&d);
    CHECK(d.analog_state == TCAN_RX_FRESH);
    l_now = 5301;
    dash_can_get_display(&d);
    CHECK(d.analog_state == TCAN_RX_STALE);

    /* A fresh oil pressure message keeps oil_pressure_state fresh while the slow message is stale. */
    fake_inject(0x203, 2, oil);
    dash_can_step();
    dash_can_get_display(&d);
    CHECK(d.oil_pressure_state == TCAN_RX_FRESH && d.analog_state == TCAN_RX_STALE);

    /* The Dash sends Dash_Engine: rpm 3500, speed 123.45 km/h -> AC 0D 39 30. */
    dash_can_set_engine(3500, 12345);
    l_sent_count = 0;
    l_now = 6000;
    dash_can_step();
    CHECK(l_sent_count == 2); /* engine message, then the heartbeat; both due on first call */
    CHECK(l_sent[0].id == 0x100 && l_sent[0].dlc == 4);
    CHECK(l_sent[0].data[0] == 0xAC && l_sent[0].data[1] == 0x0D);
    CHECK(l_sent[0].data[2] == 0x39 && l_sent[0].data[3] == 0x30);
    CHECK(l_sent[1].id == 0x701 && l_sent[1].dlc == 2);
    /* status OK; counter 1 because heartbeat 0 already went out on the step at t=5000 */
    CHECK(l_sent[1].data[0] == 0x00 && l_sent[1].data[1] == 0x01);

    /* 20 ms later only the engine message is due again (heartbeat is 1000 ms). */
    l_sent_count = 0;
    l_now = 6020;
    dash_can_step();
    CHECK(l_sent_count == 1 && l_sent[0].id == 0x100);
}

static void test_dash_ethanol_alerts(void)
{
    struct dash_display d;
    static const uint8_t critical[] = {0x55, 0x00, 0x80,
                                       0x9C};             /* Critical, default, temp invalid */
    static const uint8_t ok[] = {0x55, 0x85, 0xFF, 0x22}; /* State Ok (2) + SignalValid */
    static const uint8_t hb_fault[] = {0x03, 0x07};       /* fault code 3, counter 7 */

    /* Nothing from the Ethanol Node: no alert for the first 2 s, then a critical alert. */
    fake_reset();
    CHECK(dash_can_init(&l_port));
    l_now = 2000;
    dash_can_get_display(&d);
    CHECK(d.ethanol_state == TCAN_RX_NEVER && !d.ethanol_alert);
    l_now = 2001;
    dash_can_get_display(&d);
    CHECK(d.ethanol_alert);

    /* A healthy Node: no alert. */
    fake_reset();
    CHECK(dash_can_init(&l_port));
    l_now = 100;
    fake_inject(0x300, 4, ok);
    dash_can_step();
    dash_can_get_display(&d);
    CHECK(!d.ethanol_alert && d.ethanol_node_state == 2);

    /* The Node goes quiet (stale after 3 cycles of 100 ms): alert. */
    l_now = 400;
    dash_can_get_display(&d);
    CHECK(!d.ethanol_alert);
    l_now = 401;
    dash_can_get_display(&d);
    CHECK(d.ethanol_state == TCAN_RX_STALE && d.ethanol_alert);

    /* The Node reports Critical: alert, E85 default and invalid temperature are shown as such. */
    fake_reset();
    CHECK(dash_can_init(&l_port));
    l_now = 100;
    fake_inject(0x300, 4, critical);
    fake_inject(0x703, 2, hb_fault);
    dash_can_step();
    dash_can_get_display(&d);
    CHECK(d.ethanol_alert);
    CHECK(d.ethanol_node_state == 4 && d.ethanol_output_default);
    CHECK(!d.fuel_temp_valid); /* the 0x8000 marker is never turned into a fake degF */
    CHECK(d.ethanol_fault_code == 3);
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int main(void)
{
    test_message_table();
    test_pack_known_bytes();
    test_unpack_known_bytes();
    test_logger_messages();
    test_staleness();
    test_rx_tracking();
    test_tx_schedule();
    test_init_rejects_bad_port();
    test_unit_conversion();
    test_dash_display_and_publish();
    test_dash_ethanol_alerts();

    printf("%d checks, %d failures\n", l_checks, l_failures);
    return l_failures == 0 ? 0 : 1;
}
