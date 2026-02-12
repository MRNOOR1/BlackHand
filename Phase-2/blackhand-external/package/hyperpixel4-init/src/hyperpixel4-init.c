/*
 * hyperpixel4-init.c — HyperPixel 4.0 display panel initializer
 *
 * 1) Muxes DPI GPIO pins to ALT2 (18-bit cpadhi mode)
 * 2) Sends the LCD controller init sequence over SPI-GPIO (bit-banged)
 * 3) Turns on the backlight
 *
 * GPIO pins (BCM numbering):
 *   DPI:  0-9, 12-17, 20-25 → ALT2  (pixel clock, sync, 18-bit color)
 *   SPI:  CLK=27, MOSI=26, CS=18     (panel init bit-bang)
 *   BL:   19                          (backlight)
 *   Free: 10,11 (I2C for touch)
 *
 * On kernel 6.x the sysfs GPIO base is often 512 (not 0), so this
 * program auto-detects the base by scanning /sys/class/gpio/gpiochipN
 * for the "pinctrl-bcm2711" label.
 *
 * Must be run as root.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <dirent.h>
#include <sys/mman.h>
#include <stdint.h>

/* BCM pin numbers for SPI bit-bang */
#define BCM_CLK   27
#define BCM_MOSI  26
#define BCM_CS    18
#define BCM_BL    19

#define GPIO_PATH  "/sys/class/gpio"

/* ================================================================
 * Direct register access for GPIO alt-function muxing.
 * sysfs can only do input/output, not ALT2. We need /dev/mem.
 * ================================================================ */

/* BCM2711 GPIO register base (directly from the datasheet) */
#define BCM2711_GPIO_BASE  0xFE200000
#define GPIO_BLOCK_SIZE    0x100

/* GPFSEL registers: 10 GPIOs per register, 3 bits each */
#define GPFSEL0  0x00
#define GPFSEL1  0x04
#define GPFSEL2  0x08

/* Alt function codes (3-bit field in GPFSEL) */
#define FSEL_INPUT   0
#define FSEL_OUTPUT  1
#define FSEL_ALT0    4
#define FSEL_ALT1    5
#define FSEL_ALT2    6
#define FSEL_ALT3    7
#define FSEL_ALT4    3
#define FSEL_ALT5    2

static volatile uint32_t *gpio_regs = NULL;

static int gpio_mmap_init(void) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("/dev/mem");
        fprintf(stderr, "ERROR: need root for /dev/mem access (GPIO muxing)\n");
        return -1;
    }
    gpio_regs = (volatile uint32_t *)mmap(NULL, GPIO_BLOCK_SIZE,
                                          PROT_READ | PROT_WRITE,
                                          MAP_SHARED, fd,
                                          BCM2711_GPIO_BASE);
    close(fd);
    if (gpio_regs == MAP_FAILED) {
        perror("mmap GPIO");
        gpio_regs = NULL;
        return -1;
    }
    return 0;
}

static void gpio_set_alt(int bcm_pin, int alt_func) {
    if (!gpio_regs) return;
    int reg = bcm_pin / 10;           /* GPFSEL register index */
    int shift = (bcm_pin % 10) * 3;   /* bit position within register */
    uint32_t val = gpio_regs[reg];
    val &= ~(7 << shift);             /* clear 3-bit field */
    val |= (alt_func << shift);       /* set new function */
    gpio_regs[reg] = val;
}

/*
 * Configure all DPI pins to ALT2 for 18-bit cpadhi mode.
 * This is equivalent to the device-tree pinctrl "dpi_18bit_cpadhi_gpio0".
 *
 * Pins used:  0-9, 12-17, 20-25  (22 pins total)
 * Pins free:  10,11 (touch I2C), 18 (SPI CS), 19 (backlight)
 */
static void setup_dpi_gpio(void) {
    int dpi_pins[] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9,       /* PCLK, DE, VSYNC, HSYNC, Blue[5:0] */
        12, 13, 14, 15, 16, 17,               /* Green[5:0] */
        20, 21, 22, 23, 24, 25                /* Red[5:0] */
    };
    int n = sizeof(dpi_pins) / sizeof(dpi_pins[0]);

    printf("  muxing %d GPIO pins to ALT2 (DPI 18-bit cpadhi)...\n", n);
    for (int i = 0; i < n; i++) {
        gpio_set_alt(dpi_pins[i], FSEL_ALT2);
    }
}

/* ================================================================
 * sysfs GPIO for SPI bit-bang and backlight
 * ================================================================ */

static int gpio_base_offset = -1;
static int PIN_CLK, PIN_MOSI, PIN_CS, PIN_BL;

#define MAX_PIN 1024
static int gpio_value_fd[MAX_PIN];

static int detect_gpio_base(void) {
    DIR *d = opendir(GPIO_PATH);
    if (!d) { perror(GPIO_PATH); return -1; }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (strncmp(ent->d_name, "gpiochip", 8) != 0)
            continue;

        char label_path[256];
        snprintf(label_path, sizeof(label_path),
                 GPIO_PATH "/%s/label", ent->d_name);

        int fd = open(label_path, O_RDONLY);
        if (fd < 0) continue;

        char label[128] = {0};
        int n = read(fd, label, sizeof(label) - 1);
        close(fd);
        if (n <= 0) continue;
        if (label[n - 1] == '\n') label[n - 1] = '\0';

        if (strstr(label, "pinctrl-bcm2711") ||
            strstr(label, "pinctrl-bcm2835")) {
            int base = atoi(ent->d_name + 8);
            printf("  found %s (label=%s) -> base=%d\n",
                   ent->d_name, label, base);
            closedir(d);
            return base;
        }
    }
    closedir(d);

    if (access(GPIO_PATH "/gpiochip0", F_OK) == 0)
        return 0;

    return -1;
}

static int gpio_export(int sysfs_pin) {
    char buf[64];
    int fd = open(GPIO_PATH "/export", O_WRONLY);
    if (fd < 0) { perror("export"); return -1; }
    int n = snprintf(buf, sizeof(buf), "%d", sysfs_pin);
    if (write(fd, buf, n) < 0) {
        /* EBUSY = already exported, OK */
        close(fd);
        return 0;
    }
    close(fd);
    usleep(100000);
    return 0;
}

static void gpio_unexport(int sysfs_pin) {
    char buf[64];
    int fd = open(GPIO_PATH "/unexport", O_WRONLY);
    if (fd < 0) return;
    int n = snprintf(buf, sizeof(buf), "%d", sysfs_pin);
    write(fd, buf, n);
    close(fd);
}

static int gpio_direction(int sysfs_pin, const char *dir) {
    char path[128];
    snprintf(path, sizeof(path), GPIO_PATH "/gpio%d/direction", sysfs_pin);

    int fd = -1;
    for (int retry = 0; retry < 10; retry++) {
        fd = open(path, O_WRONLY);
        if (fd >= 0) break;
        usleep(50000);
    }
    if (fd < 0) { perror(path); return -1; }
    write(fd, dir, strlen(dir));
    close(fd);
    return 0;
}

static int gpio_open_value(int sysfs_pin) {
    char path[128];
    snprintf(path, sizeof(path), GPIO_PATH "/gpio%d/value", sysfs_pin);

    int fd = -1;
    for (int retry = 0; retry < 10; retry++) {
        fd = open(path, O_WRONLY);
        if (fd >= 0) break;
        usleep(50000);
    }
    if (fd < 0) { perror(path); return -1; }
    if (sysfs_pin < MAX_PIN)
        gpio_value_fd[sysfs_pin] = fd;
    return fd;
}

static inline void gpio_set(int sysfs_pin, int value) {
    const char c = value ? '1' : '0';
    if (sysfs_pin < MAX_PIN && gpio_value_fd[sysfs_pin] > 0) {
        lseek(gpio_value_fd[sysfs_pin], 0, SEEK_SET);
        write(gpio_value_fd[sysfs_pin], &c, 1);
    }
}

static void delay_us(int us) {
    struct timespec ts;
    ts.tv_sec = 0;
    ts.tv_nsec = us * 1000L;
    nanosleep(&ts, NULL);
}

/* ================================================================
 * SPI bit-bang protocol
 * ================================================================ */

static void clock_tick(void) {
    delay_us(10);
    gpio_set(PIN_CLK, 0);
    delay_us(10);
    gpio_set(PIN_CLK, 1);
    delay_us(10);
}

static void write_bit(int v) {
    gpio_set(PIN_MOSI, v ? 1 : 0);
    clock_tick();
}

static void write_byte(unsigned char v) {
    for (int x = 0; x < 8; x++) {
        write_bit(v & (0x80 >> x));
    }
}

static void write_register(unsigned char reg, const unsigned char *data, int len) {
    gpio_set(PIN_CS, 0);
    write_bit(0);  /* register select */
    write_byte(reg);
    for (int i = 0; i < len; i++) {
        write_bit(1);  /* register write */
        write_byte(data[i]);
    }
    gpio_set(PIN_CS, 1);
}

static void write_reg1(unsigned char reg, unsigned char val) {
    write_register(reg, &val, 1);
}

static void select_page(unsigned char number) {
    unsigned char data[] = {0xFF, 0x98, 0x06, 0x04, number};
    write_register(0xFF, data, 5);
}

/* ================================================================
 * LCD init sequence (ILI9806-like controller)
 * ================================================================ */

static void setup_lcd(void) {
    /* Page 1 — power and gamma settings */
    select_page(1);
    write_reg1(0x08, 0x10);
    write_reg1(0x21, 0x0D);
    write_reg1(0x30, 0x02);
    write_reg1(0x31, 0x00);
    write_reg1(0x40, 0x10);
    write_reg1(0x41, 0x55);
    write_reg1(0x42, 0x02);
    write_reg1(0x43, 0x84);
    write_reg1(0x44, 0x84);
    write_reg1(0x50, 0x78);
    write_reg1(0x51, 0x78);
    write_reg1(0x52, 0x00);
    write_reg1(0x53, 0x77);
    write_reg1(0x57, 0x60);
    write_reg1(0x60, 0x07);
    write_reg1(0x61, 0x00);
    write_reg1(0x62, 0x08);
    write_reg1(0x63, 0x00);

    /* Positive gamma */
    write_reg1(0xA0, 0x00);
    write_reg1(0xA1, 0x07);
    write_reg1(0xA2, 0x0C);
    write_reg1(0xA3, 0x0B);
    write_reg1(0xA4, 0x03);
    write_reg1(0xA5, 0x07);
    write_reg1(0xA6, 0x06);
    write_reg1(0xA7, 0x04);
    write_reg1(0xA8, 0x08);
    write_reg1(0xA9, 0x0C);
    write_reg1(0xAA, 0x13);
    write_reg1(0xAB, 0x06);
    write_reg1(0xAC, 0x0D);
    write_reg1(0xAD, 0x19);
    write_reg1(0xAE, 0x10);
    write_reg1(0xAF, 0x00);

    /* Negative gamma */
    write_reg1(0xC0, 0x00);
    write_reg1(0xC1, 0x07);
    write_reg1(0xC2, 0x0C);
    write_reg1(0xC3, 0x0B);
    write_reg1(0xC4, 0x03);
    write_reg1(0xC5, 0x07);
    write_reg1(0xC6, 0x07);
    write_reg1(0xC7, 0x04);
    write_reg1(0xC8, 0x08);
    write_reg1(0xC9, 0x0C);
    write_reg1(0xCA, 0x13);
    write_reg1(0xCB, 0x06);
    write_reg1(0xCC, 0x0D);
    write_reg1(0xCD, 0x18);
    write_reg1(0xCE, 0x10);
    write_reg1(0xCF, 0x00);

    /* Page 6 — GIP (Gate-In-Panel) settings */
    select_page(6);
    write_reg1(0x00, 0x20);
    write_reg1(0x01, 0x0A);
    write_reg1(0x02, 0x00);
    write_reg1(0x03, 0x00);
    write_reg1(0x04, 0x01);
    write_reg1(0x05, 0x01);
    write_reg1(0x06, 0x98);
    write_reg1(0x07, 0x06);
    write_reg1(0x08, 0x01);
    write_reg1(0x09, 0x80);
    write_reg1(0x0A, 0x00);
    write_reg1(0x0B, 0x00);
    write_reg1(0x0C, 0x01);
    write_reg1(0x0D, 0x01);
    write_reg1(0x0E, 0x00);
    write_reg1(0x0F, 0x00);
    write_reg1(0x10, 0xF0);
    write_reg1(0x11, 0xF4);
    write_reg1(0x12, 0x01);
    write_reg1(0x13, 0x00);
    write_reg1(0x14, 0x00);
    write_reg1(0x15, 0xC0);
    write_reg1(0x16, 0x08);
    write_reg1(0x17, 0x00);
    write_reg1(0x18, 0x00);
    write_reg1(0x19, 0x00);
    write_reg1(0x1A, 0x00);
    write_reg1(0x1B, 0x00);
    write_reg1(0x1C, 0x00);
    write_reg1(0x1D, 0x00);
    write_reg1(0x20, 0x01);
    write_reg1(0x21, 0x23);
    write_reg1(0x22, 0x45);
    write_reg1(0x23, 0x67);
    write_reg1(0x24, 0x01);
    write_reg1(0x25, 0x23);
    write_reg1(0x26, 0x45);
    write_reg1(0x27, 0x67);
    write_reg1(0x30, 0x11);
    write_reg1(0x31, 0x11);
    write_reg1(0x32, 0x00);
    write_reg1(0x33, 0xEE);
    write_reg1(0x34, 0xFF);
    write_reg1(0x35, 0xBB);
    write_reg1(0x36, 0xAA);
    write_reg1(0x37, 0xDD);
    write_reg1(0x38, 0xCC);
    write_reg1(0x39, 0x66);
    write_reg1(0x3A, 0x77);
    write_reg1(0x3B, 0x22);
    write_reg1(0x3C, 0x22);
    write_reg1(0x3D, 0x22);
    write_reg1(0x3E, 0x22);
    write_reg1(0x3F, 0x22);
    write_reg1(0x40, 0x22);
    write_reg1(0x52, 0x10);
    write_reg1(0x53, 0x10);
    write_reg1(0x54, 0x13);

    /* Page 7 — voltage regulator settings */
    select_page(7);
    write_reg1(0x18, 0x1D);
    write_reg1(0x17, 0x22);
    write_reg1(0x02, 0x77);
    write_reg1(0x26, 0xB2);
    write_reg1(0xE1, 0x79);

    /* Page 0 — display on */
    select_page(0);
    write_reg1(0x3A, 0x60);  /* 18-bit pixel format */
    write_reg1(0x35, 0x00);  /* tearing effect on */
    write_reg1(0x11, 0x00);  /* sleep out */
    usleep(200000);
    write_reg1(0x29, 0x00);  /* display on */
    usleep(200000);
}

/* ================================================================
 * Backlight
 * ================================================================ */

static void setup_backlight(void) {
    gpio_export(PIN_BL);
    gpio_direction(PIN_BL, "out");
    gpio_open_value(PIN_BL);
    gpio_set(PIN_BL, 1);
    /* Close FD but do NOT unexport — pin must stay high */
    if (PIN_BL < MAX_PIN && gpio_value_fd[PIN_BL] > 0)
        close(gpio_value_fd[PIN_BL]);
}

/* ================================================================
 * Main
 * ================================================================ */

int main(void) {
    memset(gpio_value_fd, 0, sizeof(gpio_value_fd));

    printf("HyperPixel 4.0 init\n");

    /* Step 1: Mux DPI GPIO pins to ALT2 via /dev/mem */
    printf("Step 1: DPI GPIO muxing\n");
    if (gpio_mmap_init() < 0) {
        fprintf(stderr, "FATAL: cannot mmap GPIO registers.\n");
        return 1;
    }
    setup_dpi_gpio();
    printf("  done\n");

    /* Step 2: Detect GPIO sysfs base for SPI bit-bang */
    printf("Step 2: detecting GPIO base\n");
    gpio_base_offset = detect_gpio_base();
    if (gpio_base_offset < 0) {
        fprintf(stderr, "FATAL: cannot detect GPIO base offset.\n");
        return 1;
    }

    PIN_CLK  = gpio_base_offset + BCM_CLK;
    PIN_MOSI = gpio_base_offset + BCM_MOSI;
    PIN_CS   = gpio_base_offset + BCM_CS;
    PIN_BL   = gpio_base_offset + BCM_BL;

    printf("  base=%d  CLK=%d  MOSI=%d  CS=%d  BL=%d\n",
           gpio_base_offset, PIN_CLK, PIN_MOSI, PIN_CS, PIN_BL);

    /* Step 3: SPI bit-bang LCD init sequence */
    printf("Step 3: SPI panel init\n");
    int spi_pins[] = {PIN_CLK, PIN_MOSI, PIN_CS};
    for (int i = 0; i < 3; i++) {
        if (gpio_export(spi_pins[i]) < 0 ||
            gpio_direction(spi_pins[i], "out") < 0 ||
            gpio_open_value(spi_pins[i]) < 0) {
            fprintf(stderr, "FATAL: GPIO %d setup failed\n", spi_pins[i]);
            return 1;
        }
    }

    gpio_set(PIN_CS, 1);
    gpio_set(PIN_CLK, 1);

    setup_lcd();
    printf("  done\n");

    /* Step 4: Backlight on */
    printf("Step 4: backlight\n");
    setup_backlight();
    printf("  done\n");

    /* Cleanup SPI pins (no longer needed) */
    for (int i = 0; i < 3; i++) {
        if (spi_pins[i] < MAX_PIN && gpio_value_fd[spi_pins[i]] > 0)
            close(gpio_value_fd[spi_pins[i]]);
        gpio_unexport(spi_pins[i]);
    }
    /* Do NOT unexport PIN_BL — must stay exported and high */

    /* Unmap GPIO registers */
    if (gpio_regs)
        munmap((void *)gpio_regs, GPIO_BLOCK_SIZE);

    printf("HyperPixel 4.0 init: complete!\n");
    return 0;
}
