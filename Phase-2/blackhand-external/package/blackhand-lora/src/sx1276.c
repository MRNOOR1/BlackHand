/*
 * sx1276.c — minimal SX1276 (RFM95W) LoRa driver over spidev.
 *
 * Register sequence follows the well-known RadioHead RH_RF95 defaults:
 * SLEEP → LoRa mode, set carrier frequency, Bw125Cr45Sf128 modem config with
 * CRC on, 8-symbol preamble, FIFO base addresses at 0, PA_BOOST TX power.
 * IRQs are polled over SPI (RegIrqFlags) — DIO0 is not wired.
 *
 * See sx1276.h for the wiring table. Linux-only (spidev + gpio char device).
 */
#include "sx1276.h"

#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <dirent.h>
#include <linux/spi/spidev.h>
#include <linux/gpio.h>

/* ── SX1276 LoRa registers ────────────────────────────────────────────── */
#define REG_FIFO                 0x00
#define REG_OP_MODE              0x01
#define REG_FRF_MSB              0x06
#define REG_FRF_MID              0x07
#define REG_FRF_LSB              0x08
#define REG_PA_CONFIG            0x09
#define REG_FIFO_ADDR_PTR        0x0D
#define REG_FIFO_TX_BASE_ADDR    0x0E
#define REG_FIFO_RX_BASE_ADDR    0x0F
#define REG_FIFO_RX_CURRENT_ADDR 0x10
#define REG_IRQ_FLAGS            0x12
#define REG_RX_NB_BYTES          0x13
#define REG_PKT_SNR_VALUE        0x19
#define REG_PKT_RSSI_VALUE       0x1A
#define REG_MODEM_CONFIG1        0x1D
#define REG_MODEM_CONFIG2        0x1E
#define REG_PREAMBLE_MSB         0x20
#define REG_PREAMBLE_LSB         0x21
#define REG_PAYLOAD_LENGTH       0x22
#define REG_MODEM_CONFIG3        0x26
#define REG_VERSION              0x42
#define REG_PA_DAC               0x4D

/* OpMode bits */
#define MODE_LONG_RANGE          0x80  /* LoRa (must set while in sleep) */
#define MODE_SLEEP               0x00
#define MODE_STDBY               0x01
#define MODE_TX                  0x03
#define MODE_RX_CONTINUOUS       0x05

/* IrqFlags bits */
#define IRQ_RX_DONE              0x40
#define IRQ_CRC_ERROR            0x20
#define IRQ_TX_DONE              0x08

/* PA config */
#define PA_SELECT_BOOST          0x80
#define PA_DAC_DISABLE           0x04

#define SX1276_CHIP_VERSION      0x12  /* RegVersion for SX1276/RFM95 */

#define SPI_HZ                   1000000   /* 1 MHz — safe for the SX1276 */

/* ── module state ──────────────────────────────────────────────────────── */
static int  g_fd = -1;
static int  g_ready = 0;
static int  g_last_rssi = 0;
static int  g_last_snr = 0;

/* ── low-level SPI ─────────────────────────────────────────────────────── */

static int spi_xfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    struct spi_ioc_transfer tr;
    memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (unsigned long)tx;
    tr.rx_buf = (unsigned long)rx;
    tr.len = (uint32_t)len;
    tr.speed_hz = SPI_HZ;
    tr.bits_per_word = 8;
    if (ioctl(g_fd, SPI_IOC_MESSAGE(1), &tr) < 0)
        return -1;
    return 0;
}

static uint8_t reg_read(uint8_t reg)
{
    uint8_t tx[2] = { (uint8_t)(reg & 0x7F), 0x00 };
    uint8_t rx[2] = { 0, 0 };
    spi_xfer(tx, rx, 2);
    return rx[1];
}

static void reg_write(uint8_t reg, uint8_t val)
{
    uint8_t tx[2] = { (uint8_t)(reg | 0x80), val };
    uint8_t rx[2];
    spi_xfer(tx, rx, 2);
}

static void burst_write(uint8_t reg, const uint8_t *data, size_t n)
{
    uint8_t tx[1 + SX1276_MAX_PAYLOAD];
    uint8_t rx[1 + SX1276_MAX_PAYLOAD];
    if (n > SX1276_MAX_PAYLOAD)
        n = SX1276_MAX_PAYLOAD;
    tx[0] = (uint8_t)(reg | 0x80);
    memcpy(tx + 1, data, n);
    spi_xfer(tx, rx, n + 1);
}

static void burst_read(uint8_t reg, uint8_t *data, size_t n)
{
    uint8_t tx[1 + SX1276_MAX_PAYLOAD];
    uint8_t rx[1 + SX1276_MAX_PAYLOAD];
    if (n > SX1276_MAX_PAYLOAD)
        n = SX1276_MAX_PAYLOAD;
    memset(tx, 0, n + 1);
    tx[0] = (uint8_t)(reg & 0x7F);
    spi_xfer(tx, rx, n + 1);
    memcpy(data, rx + 1, n);
}

/* ── reset via the GPIO character device ───────────────────────────────────
 * sysfs GPIO numbering is unreliable on the Pi 4 (the pinctrl base is not 0),
 * so we use /dev/gpiochipN + the v1 line-handle ioctl and pick the chip whose
 * label names the bcm2711 pin controller. Reset is best-effort: if it fails
 * the SX1276's power-on reset usually leaves the chip usable, so we log and
 * continue rather than abort. Returns 0 on success, negative otherwise.
 */
static int msleep(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    return nanosleep(&ts, NULL);
}

static int open_pinctrl_chip(void)
{
    for (int i = 0; i < 8; i++) {
        char path[32];
        snprintf(path, sizeof(path), "/dev/gpiochip%d", i);
        int fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0)
            continue;
        struct gpiochip_info info;
        memset(&info, 0, sizeof(info));
        if (ioctl(fd, GPIO_GET_CHIPINFO_IOCTL, &info) == 0 &&
            (strstr(info.label, "bcm2711") || strstr(info.label, "pinctrl"))) {
            return fd;
        }
        close(fd);
    }
    return -1;
}

static void reset_radio(void)
{
    int chip = open_pinctrl_chip();
    if (chip < 0) {
        fprintf(stderr, "lora: no pinctrl gpiochip; skipping hardware reset\n");
        return;
    }

    struct gpiohandle_request req;
    memset(&req, 0, sizeof(req));
    req.lineoffsets[0] = SX1276_RESET_BCM;
    req.lines = 1;
    req.flags = GPIOHANDLE_REQUEST_OUTPUT;
    req.default_values[0] = 1;            /* idle high */
    strncpy(req.consumer_label, "blackhand-lora", sizeof(req.consumer_label) - 1);

    if (ioctl(chip, GPIO_GET_LINEHANDLE_IOCTL, &req) < 0 || req.fd < 0) {
        fprintf(stderr, "lora: gpio line request failed (%s)\n", strerror(errno));
        close(chip);
        return;
    }

    struct gpiohandle_data data;
    data.values[0] = 0; ioctl(req.fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    msleep(1);                            /* >100us low */
    data.values[0] = 1; ioctl(req.fd, GPIOHANDLE_SET_LINE_VALUES_IOCTL, &data);
    msleep(10);                           /* >5ms for chip to be ready */

    close(req.fd);
    close(chip);
}

/* ── configuration ─────────────────────────────────────────────────────── */

static void set_frequency(uint32_t hz)
{
    /* frf = freq * 2^19 / 32MHz (32 MHz TCXO on the RFM95). */
    uint64_t frf = ((uint64_t)hz << 19) / 32000000ULL;
    reg_write(REG_FRF_MSB, (uint8_t)(frf >> 16));
    reg_write(REG_FRF_MID, (uint8_t)(frf >> 8));
    reg_write(REG_FRF_LSB, (uint8_t)(frf));
}

int sx1276_init(void)
{
    g_ready = 0;

    g_fd = open(SX1276_SPI_DEV, O_RDWR);
    if (g_fd < 0) {
        fprintf(stderr, "lora: cannot open %s (%s)\n",
                SX1276_SPI_DEV, strerror(errno));
        return -1;
    }

    uint8_t mode = SPI_MODE_0;
    uint8_t bits = 8;
    uint32_t speed = SPI_HZ;
    ioctl(g_fd, SPI_IOC_WR_MODE, &mode);
    ioctl(g_fd, SPI_IOC_WR_BITS_PER_WORD, &bits);
    ioctl(g_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed);

    reset_radio();

    /* Verify the chip is actually there before trusting any writes. */
    uint8_t version = reg_read(REG_VERSION);
    if (version != SX1276_CHIP_VERSION) {
        fprintf(stderr, "lora: unexpected chip version 0x%02x (want 0x%02x) — "
                "check wiring/CS1\n", version, SX1276_CHIP_VERSION);
        close(g_fd);
        g_fd = -1;
        return -2;
    }

    /* LoRa mode can only be set while in SLEEP. */
    reg_write(REG_OP_MODE, MODE_LONG_RANGE | MODE_SLEEP);
    msleep(10);
    reg_write(REG_OP_MODE, MODE_LONG_RANGE | MODE_STDBY);

    /* FIFO: whole 256-byte buffer for both TX and RX. */
    reg_write(REG_FIFO_TX_BASE_ADDR, 0x00);
    reg_write(REG_FIFO_RX_BASE_ADDR, 0x00);

    set_frequency(SX1276_FREQ_HZ);

    /* Bw125 Cr4/5 SF7, explicit header, CRC on (RadioHead Bw125Cr45Sf128). */
    reg_write(REG_MODEM_CONFIG1, 0x72);
    reg_write(REG_MODEM_CONFIG2, 0x74);
    reg_write(REG_MODEM_CONFIG3, 0x00);

    /* 8-symbol preamble. */
    reg_write(REG_PREAMBLE_MSB, 0x00);
    reg_write(REG_PREAMBLE_LSB, 0x08);

    /* TX power on PA_BOOST (RFM95 routes the antenna through PA_BOOST). */
    reg_write(REG_PA_DAC, PA_DAC_DISABLE);
    int p = SX1276_TX_POWER;
    if (p < 2)  p = 2;
    if (p > 20) p = 20;
    reg_write(REG_PA_CONFIG, PA_SELECT_BOOST | (uint8_t)(p - 5));

    /* Start listening. */
    reg_write(REG_FIFO_ADDR_PTR, 0x00);
    reg_write(REG_IRQ_FLAGS, 0xFF);
    reg_write(REG_OP_MODE, MODE_LONG_RANGE | MODE_RX_CONTINUOUS);

    g_ready = 1;
    return 0;
}

int sx1276_send(const uint8_t *data, size_t len)
{
    if (!g_ready)
        return -1;
    if (len == 0 || len > SX1276_MAX_PAYLOAD)
        return -1;

    reg_write(REG_OP_MODE, MODE_LONG_RANGE | MODE_STDBY);
    reg_write(REG_FIFO_ADDR_PTR, 0x00);
    burst_write(REG_FIFO, data, len);
    reg_write(REG_PAYLOAD_LENGTH, (uint8_t)len);

    reg_write(REG_OP_MODE, MODE_LONG_RANGE | MODE_TX);

    /* Poll TxDone. At SF7/Bw125 a 250-byte packet is well under 1 s; give a
       generous 5 s ceiling so a stuck radio cannot block the thread forever. */
    for (int i = 0; i < 5000; i++) {
        if (reg_read(REG_IRQ_FLAGS) & IRQ_TX_DONE) {
            reg_write(REG_IRQ_FLAGS, 0xFF);
            reg_write(REG_OP_MODE, MODE_LONG_RANGE | MODE_RX_CONTINUOUS);
            return 0;
        }
        msleep(1);
    }

    /* Timeout — recover into RX so future receives still work. */
    reg_write(REG_IRQ_FLAGS, 0xFF);
    reg_write(REG_OP_MODE, MODE_LONG_RANGE | MODE_RX_CONTINUOUS);
    return -2;
}

int sx1276_recv_poll(uint8_t *buf, size_t bufsize)
{
    if (!g_ready)
        return 0;

    uint8_t flags = reg_read(REG_IRQ_FLAGS);
    if (!(flags & IRQ_RX_DONE))
        return 0;

    /* Consume this packet regardless of outcome so RX keeps flowing. */
    if (flags & IRQ_CRC_ERROR) {
        reg_write(REG_IRQ_FLAGS, 0xFF);
        return -1;
    }

    uint8_t len = reg_read(REG_RX_NB_BYTES);
    if (len > bufsize)
        len = (uint8_t)bufsize;

    reg_write(REG_FIFO_ADDR_PTR, reg_read(REG_FIFO_RX_CURRENT_ADDR));
    burst_read(REG_FIFO, buf, len);
    reg_write(REG_IRQ_FLAGS, 0xFF);

    /* SNR is signed, 0.25 dB steps; RSSI needs the LF/HF offset (-157 for HF). */
    g_last_snr  = (int8_t)reg_read(REG_PKT_SNR_VALUE) / 4;
    g_last_rssi = (int)reg_read(REG_PKT_RSSI_VALUE) - 157;

    return len;
}

int sx1276_last_rssi(void) { return g_last_rssi; }
int sx1276_last_snr(void)  { return g_last_snr;  }
int sx1276_ready(void)     { return g_ready;     }
