/*
 * sx1276.h — minimal Semtech SX1276 (RFM95W) LoRa driver over spidev.
 *
 * HARDWARE / WIRING (CM4 Lite on the BlackHand carrier board)
 * ----------------------------------------------------------------
 * The radio now has SPI0 to itself. The display moved to SPI4, and SPI0 is
 * restricted to a single chip select (dtoverlay=spi0-1cs in config.txt)
 * because the carrier reuses GPIO7 — normally SPI0 CE1 — as SPI4's clock.
 *
 *   Radio pad   Pi signal    BCM   Notes
 *   VIN         3V3          —     3.3 V ONLY (module is 3.3 V logic)
 *   GND         GND          —
 *   SCK         SPI0_SCLK    11
 *   MOSI        SPI0_MOSI    10
 *   MISO        SPI0_MISO    9
 *   CS          SPI0_CE0     8     /dev/spidev0.0  (display is on SPI4 now)
 *   RST         GPIO         5     active-low reset, driven at init
 *   DIO0        GPIO         13    wired on this board, but still POLLED
 *   DIO1        GPIO         19    wired, unused
 *   DIO2        GPIO         26    wired, unused
 *
 * NOTE: BCM26 was the RESET line on the old Pi 4 wiring and is DIO2 here.
 * Driving it as reset on this board would toggle an interrupt line instead.
 *
 * ERRATA — this carrier does not power the radio. The LoRa rail is missing,
 * so sx1276_init() is expected to read chip version 0x00 and fail. That is a
 * hardware fault, not a software one; the daemon reports "not ready".
 *
 * MODE: LoRa, 915 MHz (AU915) by default, Bw125 Cr4/5 SF7, CRC on, PA_BOOST.
 * Point-to-point broadcast: no addressing, no ACK, no encryption (MVP).
 *
 * This whole module is Linux-only (uses <linux/spi/spidev.h> and
 * <linux/gpio.h>). It cannot be exercised without the real radio; only a
 * board can verify RF behaviour.
 */
#ifndef SX1276_H
#define SX1276_H

#include <stddef.h>
#include <stdint.h>

/* ── Build-time configuration ──────────────────────────────────────────────
 * Frequency: 915 MHz for Australia (AU915). Europe/UK is 868 MHz — changing
 * the line below to 868000000UL is the ONLY change required for EU868.
 */
#define SX1276_FREQ_HZ    915000000UL   /* AU915. EU868 → 868000000UL */
#define SX1276_TX_POWER   13            /* dBm on PA_BOOST (2..20) */
#define SX1276_SPI_DEV    "/dev/spidev0.0"   /* SPI0 CE0; display is on SPI4 */
#define SX1276_RESET_BCM  5             /* RST line, BCM numbering (was 26 = DIO2 here) */

#define SX1276_MAX_PAYLOAD 250          /* bytes of text per packet */

/*
 * sx1276_init — reset the radio and configure it for LoRa RX/TX.
 * Opens SX1276_SPI_DEV, pulses RST, verifies the chip version, and programs
 * the modem. Returns 0 on success, negative on failure (SPI open error,
 * wrong/absent chip version, etc.). On failure the radio is unusable and the
 * caller should report "not ready" rather than send.
 */
int sx1276_init(void);

/*
 * sx1276_send — transmit one packet, blocking until TxDone or timeout.
 * len must be 1..SX1276_MAX_PAYLOAD. Returns 0 on success, negative on
 * failure (not ready, bad length, or TX timeout). Returns the radio to
 * continuous-receive mode afterwards.
 */
int sx1276_send(const uint8_t *data, size_t len);

/*
 * sx1276_recv_poll — check for one received packet (non-blocking).
 * Reads RegIrqFlags; if RxDone is set and CRC is valid, copies up to
 * bufsize bytes into buf and returns the payload length (>0). Returns 0 when
 * nothing has arrived, and negative on a CRC error or read fault. Must be
 * called repeatedly (e.g. from a poll loop) while in RX mode.
 */
int sx1276_recv_poll(uint8_t *buf, size_t bufsize);

/* Signal quality of the most recently received packet, for status reporting. */
int sx1276_last_rssi(void);   /* dBm */
int sx1276_last_snr(void);    /* dB */

/* Non-zero once sx1276_init has succeeded. */
int sx1276_ready(void);

#endif /* SX1276_H */
