/*******************************************************
 * @file ethanol.c
 * @brief Ethanol Node (Adafruit RP2040 CAN Bus Feather, Pico SDK + FreeRTOS): TrackCAN port.
 *
 * The Feather's CAN controller is an MCP25625 (an MCP2515 with a built-in transceiver) on
 * SPI1. Pin and oscillator values come from Adafruit's guide and Zephyr's
 * boards/adafruit/feather_canbus_rp2040 devicetree (16 MHz oscillator, CS GPIO19).
 *******************************************************/

/*******************************************************
 * Includes
 *******************************************************/
#include "ethanol.h"

#include <stdint.h>
#include <string.h>

#include "FreeRTOS.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include "pico/stdlib.h"
#include "task.h"

/*******************************************************
 * Macros
 *******************************************************/
/* Feather wiring */
#define CAN_SPI spi1
#define CAN_PIN_SCK 14
#define CAN_PIN_MOSI 15
#define CAN_PIN_MISO 8
#define CAN_PIN_CS 19
#define CAN_PIN_RESET 18   /* active low */
#define CAN_PIN_STANDBY 16 /* high = transceiver standby, low = normal. Unverified polarity. */
#define CAN_SPI_HZ 1000000u

/* Task */
#define CAN_TASK_STACK_WORDS 512
#define CAN_TASK_PRIORITY 1 /* below the DAC and reader tasks, see main.c */
#define CAN_TASK_PERIOD_MS 5
#define CAN_INIT_RETRY_MS 1000

/*
 * Bit timing for 500 kbit/s with the 16 MHz oscillator (worked out by hand):
 *   TQ = 2 * (BRP + 1) / Fosc = 2 * 1 / 16 MHz = 125 ns, so one bit = 2 us = 16 TQ.
 *   Sync 1 + Prop 6 + PS1 7 = 14 TQ before the sample point -> 87.5 %. PS2 = 2 TQ.
 *   CNF1: SJW = 1 TQ (00), BRP = 0                      -> 0x00
 *   CNF2: BTLMODE = 1, SAM = 0, PHSEG1 = 6 (110), PRSEG = 5 (101) -> 1011 0101 = 0xB5
 *   CNF3: SOF = 0, WAKFIL = 0, PHSEG2 = 1 (PS2 = 2 TQ)  -> 0x01
 */
#define CNF1_500K_16MHZ 0x00
#define CNF2_500K_16MHZ 0xB5
#define CNF3_500K_16MHZ 0x01

/* MCP2515 SPI instructions */
#define MCP_RESET 0xC0
#define MCP_READ 0x03
#define MCP_WRITE 0x02
#define MCP_READ_STATUS 0xA0
#define MCP_BIT_MODIFY 0x05
#define MCP_LOAD_TX 0x40 /* | 0, 2, 4 for TXB0/1/2 starting at SIDH */
#define MCP_RTS 0x80     /* | 1, 2, 4 for TXB0/1/2 */
#define MCP_READ_RX 0x90 /* | 0, 4 for RXB0/1 starting at SIDH */

/* MCP2515 registers */
#define REG_CANSTAT 0x0E
#define REG_CANCTRL 0x0F
#define REG_CNF3 0x28
#define REG_CNF2 0x29
#define REG_CNF1 0x2A
#define REG_CANINTE 0x2B
#define REG_RXB0CTRL 0x60
#define REG_RXB1CTRL 0x70

#define MODE_MASK 0xE0
#define MODE_NORMAL 0x00
#define MODE_CONFIG 0x80
#define CANCTRL_ABAT 0x10

/* READ STATUS bits */
#define ST_RX0IF 0x01
#define ST_RX1IF 0x02
#define ST_TXREQ0 0x04
#define ST_TXREQ1 0x10
#define ST_TXREQ2 0x40

/*******************************************************
 * Static Variables
 *******************************************************/
static tcan_t l_can;
static uint8_t l_heartbeat_counter;
static bool l_ready;
static bool l_abort_pending;

/* Latest status from the safety logic. Copied under a critical section; never read while it is
 * being written. Initial values describe the boot state. */
static ethanol_can_status_t l_status = {
    .content_pct = 85, .fuel_temp_x10 = INT16_MIN, .output_is_default = true};

/*******************************************************
 * Static Function Prototypes
 *******************************************************/
static int port_send(const tcan_frame_t *frame);
static int port_receive(tcan_frame_t *frame);
static uint32_t port_now_ms(void);

static const tcan_port_t l_port = {
    .send = port_send,
    .receive = port_receive,
    .now_ms = port_now_ms,
};

/*******************************************************
 * Static Function Definitions
 *******************************************************/
static void cs_select(void)
{
    gpio_put(CAN_PIN_CS, 0);
}

static void cs_deselect(void)
{
    gpio_put(CAN_PIN_CS, 1);
}

static void mcp_reset_instruction(void)
{
    uint8_t cmd = MCP_RESET;

    cs_select();
    spi_write_blocking(CAN_SPI, &cmd, 1);
    cs_deselect();
}

static uint8_t mcp_read_reg(uint8_t reg)
{
    uint8_t tx[3] = {MCP_READ, reg, 0};
    uint8_t rx[3] = {0};

    cs_select();
    spi_write_read_blocking(CAN_SPI, tx, rx, sizeof(tx));
    cs_deselect();
    return rx[2];
}

static void mcp_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t tx[3] = {MCP_WRITE, reg, value};

    cs_select();
    spi_write_blocking(CAN_SPI, tx, sizeof(tx));
    cs_deselect();
}

static void mcp_bit_modify(uint8_t reg, uint8_t mask, uint8_t value)
{
    uint8_t tx[4] = {MCP_BIT_MODIFY, reg, mask, value};

    cs_select();
    spi_write_blocking(CAN_SPI, tx, sizeof(tx));
    cs_deselect();
}

static uint8_t mcp_read_status(void)
{
    uint8_t tx[2] = {MCP_READ_STATUS, 0};
    uint8_t rx[2] = {0};

    cs_select();
    spi_write_read_blocking(CAN_SPI, tx, rx, sizeof(tx));
    cs_deselect();
    return rx[1];
}

/* Returns 0 on success. A missing chip reads 0xFF or 0x00, which fails the mode check. */
static int mcp_init(void)
{
    mcp_reset_instruction();
    vTaskDelay(pdMS_TO_TICKS(10)); /* oscillator start-up, lets other tasks run meanwhile */

    if ((mcp_read_reg(REG_CANSTAT) & MODE_MASK) != MODE_CONFIG) {
        return -1;
    }

    mcp_write_reg(REG_CNF1, CNF1_500K_16MHZ);
    mcp_write_reg(REG_CNF2, CNF2_500K_16MHZ);
    mcp_write_reg(REG_CNF3, CNF3_500K_16MHZ);
    if (mcp_read_reg(REG_CNF2) != CNF2_500K_16MHZ) {
        return -2; /* SPI wiring or chip problem */
    }

    mcp_write_reg(REG_CANINTE, 0x00);  /* polled, no interrupts */
    mcp_write_reg(REG_RXB0CTRL, 0x64); /* accept any frame, roll over into RXB1 */
    mcp_write_reg(REG_RXB1CTRL, 0x60); /* accept any frame */

    mcp_write_reg(REG_CANCTRL, MODE_NORMAL);
    busy_wait_us(1000);
    if ((mcp_read_reg(REG_CANSTAT) & MODE_MASK) != MODE_NORMAL) {
        return -3;
    }
    return 0;
}

static void hw_init(void)
{
    gpio_init(CAN_PIN_CS);
    gpio_set_dir(CAN_PIN_CS, GPIO_OUT);
    cs_deselect();

    gpio_init(CAN_PIN_RESET);
    gpio_set_dir(CAN_PIN_RESET, GPIO_OUT);
    gpio_put(CAN_PIN_RESET, 1);

    gpio_init(CAN_PIN_STANDBY);
    gpio_set_dir(CAN_PIN_STANDBY, GPIO_OUT);
    gpio_put(CAN_PIN_STANDBY, 0);

    spi_init(CAN_SPI, CAN_SPI_HZ);
    spi_set_format(CAN_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(CAN_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(CAN_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(CAN_PIN_MISO, GPIO_FUNC_SPI);
}

static int port_send(const tcan_frame_t *frame)
{
    uint8_t status;
    uint8_t buf;
    uint8_t cmd[6 + TCAN_MAX_DLC];
    size_t len;

    if (!l_ready) {
        return -1;
    }

    status = mcp_read_status();
    if ((status & ST_TXREQ0) == 0) {
        buf = 0;
    } else if ((status & ST_TXREQ1) == 0) {
        buf = 1;
    } else if ((status & ST_TXREQ2) == 0) {
        buf = 2;
    } else {
        /*
         * All three buffers are waiting, which means nobody is acknowledging (bus off or
         * disconnected). Abort them so stale frames are not sent in a burst when the bus
         * comes back, and drop this one.
         */
        mcp_bit_modify(REG_CANCTRL, CANCTRL_ABAT, CANCTRL_ABAT);
        l_abort_pending = true;
        return -1;
    }

    if (l_abort_pending) {
        mcp_bit_modify(REG_CANCTRL, CANCTRL_ABAT, 0);
        l_abort_pending = false;
    }

    cmd[0] = MCP_LOAD_TX | (uint8_t)(buf * 2u);
    cmd[1] = (uint8_t)(frame->id >> 3);
    cmd[2] = (uint8_t)((frame->id & 0x07u) << 5); /* standard frame, EXIDE = 0 */
    cmd[3] = 0;
    cmd[4] = 0;
    cmd[5] = frame->dlc & 0x0F;
    for (unsigned i = 0; i < frame->dlc; i++) {
        cmd[6 + i] = frame->data[i];
    }
    len = 6u + frame->dlc;

    cs_select();
    spi_write_blocking(CAN_SPI, cmd, len);
    cs_deselect();

    cmd[0] = MCP_RTS | (uint8_t)(1u << buf);
    cs_select();
    spi_write_blocking(CAN_SPI, cmd, 1);
    cs_deselect();
    return 0;
}

static int port_receive(tcan_frame_t *frame)
{
    uint8_t status;
    uint8_t tx[14] = {0};
    uint8_t rx[14] = {0};
    uint8_t dlc;

    if (!l_ready) {
        return 0;
    }

    status = mcp_read_status();
    if (status & ST_RX0IF) {
        tx[0] = MCP_READ_RX | 0x00; /* RXB0 from SIDH, clears RX0IF on CS rising edge */
    } else if (status & ST_RX1IF) {
        tx[0] = MCP_READ_RX | 0x04;
    } else {
        return 0;
    }

    cs_select();
    spi_write_read_blocking(CAN_SPI, tx, rx, sizeof(tx));
    cs_deselect();

    /* rx[1]=SIDH rx[2]=SIDL rx[3]=EID8 rx[4]=EID0 rx[5]=DLC rx[6..13]=data */
    if (rx[2] & 0x08) {
        return 0; /* extended frame: not used by TrackCAN */
    }
    dlc = rx[5] & 0x0F;
    if ((rx[5] & 0x40) || (rx[2] & 0x10) || dlc > TCAN_MAX_DLC) {
        return 0; /* remote frame or bad length */
    }

    frame->id = ((uint32_t)rx[1] << 3) | (rx[2] >> 5);
    frame->dlc = dlc;
    for (unsigned i = 0; i < TCAN_MAX_DLC; i++) {
        frame->data[i] = (i < dlc) ? rx[6 + i] : 0;
    }
    return 1;
}

static uint32_t port_now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

static void publish(void)
{
    uint8_t buf[TCAN_MAX_DLC];

    if (tcan_tx_due(&l_can, TCAN_MSG_ETHANOL_DATA)) {
        ethanol_can_status_t st;
        struct trackcan_ethanol_data_t m;

        taskENTER_CRITICAL();
        st = l_status;
        taskEXIT_CRITICAL();

        memset(&m, 0, sizeof(m));
        m.ethanol_content = st.content_pct;
        m.fuel_temp = st.fuel_temp_x10;
        m.state = st.state;
        m.critical = st.critical ? 1 : 0;
        m.output_is_default = st.output_is_default ? 1 : 0;
        m.signal_valid = st.signal_valid ? 1 : 0;
        m.dac_warning = st.dac_warning ? 1 : 0;
        m.reset_recent = st.reset_recent ? 1 : 0;

        if (trackcan_ethanol_data_pack(buf, &m, sizeof(buf)) > 0) {
            (void)tcan_tx(&l_can, TCAN_MSG_ETHANOL_DATA, buf);
        }
    }

    if (tcan_tx_due(&l_can, TCAN_MSG_HEARTBEAT_ETHANOL)) {
        ethanol_can_status_t st;
        struct trackcan_heartbeat_ethanol_t hb;

        taskENTER_CRITICAL();
        st = l_status;
        taskEXIT_CRITICAL();

        /* Status carries the active fault code, so the dash and logger see every change. */
        hb.status = st.fault_code;
        hb.counter = l_heartbeat_counter++;
        if (trackcan_heartbeat_ethanol_pack(buf, &hb, sizeof(buf)) > 0) {
            (void)tcan_tx(&l_can, TCAN_MSG_HEARTBEAT_ETHANOL, buf);
        }
    }
}

static void can_task(void *params)
{
    (void)params;

    hw_init();
    (void)tcan_init(&l_can, &l_port, TCAN_NODE_ETHANOL);

    /* Retry forever. Nothing here can block the DAC or reader tasks. */
    while (mcp_init() != 0) {
        vTaskDelay(pdMS_TO_TICKS(CAN_INIT_RETRY_MS));
    }
    l_ready = true;

    while (1) {
        tcan_poll(&l_can);
        publish();
        vTaskDelay(pdMS_TO_TICKS(CAN_TASK_PERIOD_MS));
    }
}

/*******************************************************
 * Public Function Definitions
 *******************************************************/
int ethanol_can_start(void)
{
    return xTaskCreate(can_task, "trackcan", CAN_TASK_STACK_WORDS, NULL, CAN_TASK_PRIORITY, NULL) ==
                   pdPASS
               ? 0
               : -1;
}

void ethanol_can_set_status(const ethanol_can_status_t *status)
{
    taskENTER_CRITICAL();
    l_status = *status;
    taskEXIT_CRITICAL();
}
