/*
 * ============================================================================
 *  hardware.c — Hardware Abstraction Layer Implementation
 * ============================================================================
 *
 *  WHAT IS THIS FILE?
 *  ------------------
 *  This file contains the IMPLEMENTATIONS of the functions declared in
 *  hardware.h. Currently it returns simulated/demo values for testing.
 *
 *  To connect real hardware, replace the function bodies with actual
 *  hardware communication code (I2C for battery, UART for cellular).
 *
 *
 *  C CONCEPTS DEMONSTRATED:
 *  ------------------------
 *  1. Static variables - private data within a file
 *  2. Struct initialization - creating structs with values
 *  3. Return by value - returning entire structs (not pointers)
 *  4. Stub functions - placeholder implementations
 *
 *
 *  FILE ORGANIZATION:
 *  ------------------
 *  1. Includes - headers we need
 *  2. Static data - simulated hardware state
 *  3. Function implementations - the actual code
 *
 *
 *  ============================================================================
 *  REAL HARDWARE IMPLEMENTATION EXAMPLES
 *  ============================================================================
 *
 *  EXAMPLE 1: I2C BATTERY READING (Linux)
 *  --------------------------------------
 *  #include <linux/i2c-dev.h>
 *  #include <sys/ioctl.h>
 *  #include <fcntl.h>
 *  #include <unistd.h>
 *
 *  static int i2c_fd = -1;
 *
 *  void hardware_init(void) {
 *      // Open I2C bus
 *      i2c_fd = open("/dev/i2c-1", O_RDWR);
 *      if (i2c_fd < 0) {
 *          perror("Failed to open I2C bus");
 *          return;
 *      }
 *
 *      // Set slave address (MAX17048 fuel gauge = 0x36)
 *      if (ioctl(i2c_fd, I2C_SLAVE, 0x36) < 0) {
 *          perror("Failed to set I2C address");
 *          close(i2c_fd);
 *          i2c_fd = -1;
 *      }
 *  }
 *
 *  battery_status_t hardware_get_battery(void) {
 *      battery_status_t status = {0, false};
 *
 *      if (i2c_fd < 0) return status;  // Hardware not available
 *
 *      // Read SOC (State of Charge) register at 0x04
 *      uint8_t reg = 0x04;
 *      write(i2c_fd, &reg, 1);
 *      uint8_t data[2];
 *      read(i2c_fd, data, 2);
 *
 *      // SOC is in the high byte, as a percentage
 *      status.percent = data[0];
 *      if (status.percent > 100) status.percent = 100;
 *
 *      // Read charging status from GPIO (example)
 *      // status.charging = (gpio_read(CHARGING_PIN) == HIGH);
 *
 *      return status;
 *  }
 *
 *
 *  EXAMPLE 2: UART CELLULAR MODEM (Linux)
 *  --------------------------------------
 *  #include <termios.h>
 *  #include <fcntl.h>
 *  #include <unistd.h>
 *  #include <string.h>
 *
 *  static int uart_fd = -1;
 *
 *  void hardware_init(void) {
 *      // Open serial port
 *      uart_fd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY);
 *      if (uart_fd < 0) {
 *          perror("Failed to open UART");
 *          return;
 *      }
 *
 *      // Configure serial port
 *      struct termios tty;
 *      tcgetattr(uart_fd, &tty);
 *      cfsetispeed(&tty, B115200);
 *      cfsetospeed(&tty, B115200);
 *      tty.c_cflag |= (CLOCAL | CREAD);
 *      tty.c_cflag &= ~PARENB;  // No parity
 *      tty.c_cflag &= ~CSTOPB;  // 1 stop bit
 *      tty.c_cflag &= ~CSIZE;
 *      tty.c_cflag |= CS8;      // 8 data bits
 *      tcsetattr(uart_fd, TCSANOW, &tty);
 *  }
 *
 *  cellular_status_t hardware_get_cellular(void) {
 *      cellular_status_t status = {0, false, ""};
 *
 *      if (uart_fd < 0) return status;
 *
 *      // Send AT+CSQ command for signal quality
 *      write(uart_fd, "AT+CSQ\r", 7);
 *      usleep(100000);  // Wait 100ms for response
 *
 *      char response[64];
 *      int n = read(uart_fd, response, sizeof(response) - 1);
 *      response[n] = '\0';
 *
 *      // Parse response like "+CSQ: 18,0"
 *      int csq = 99;
 *      sscanf(response, "+CSQ: %d", &csq);
 *
 *      // Convert CSQ to bars
 *      if (csq == 99) {
 *          status.signal_bars = 0;
 *          status.connected = false;
 *      } else {
 *          status.connected = true;
 *          if (csq >= 20) status.signal_bars = 4;
 *          else if (csq >= 15) status.signal_bars = 3;
 *          else if (csq >= 10) status.signal_bars = 2;
 *          else status.signal_bars = 1;
 *      }
 *
 *      // Get carrier name with AT+COPS?
 *      // ... similar pattern ...
 *
 *      return status;
 *  }
 *
 * ============================================================================
 */

/* ═══════════════════════════════════════════════════════════════════════════
 *  INCLUDES
 * ═══════════════════════════════════════════════════════════════════════════
 */

/*
 * "hardware.h" - Our Header
 *
 * This includes our function declarations and struct definitions.
 * Including our own header ensures our definitions match our declarations.
 *
 * C CONCEPT: INCLUDE YOUR OWN HEADER
 * ----------------------------------
 * A .c file should always #include its own .h file. This lets the
 * compiler verify that the implementations match the declarations.
 * If they don't match, you get a compile error (which is good!).
 */
#include "platform/hardware.h"

/*
 * <string.h> - String Functions
 *
 * We use strcpy() to copy the carrier name into our struct.
 * In a real implementation, you'd use strncpy() for safety.
 */
#include <string.h>


/* ═══════════════════════════════════════════════════════════════════════════
 *  SIMULATED HARDWARE STATE
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  C CONCEPT: STATIC VARIABLES
 *  ---------------------------
 *  'static' at file scope means "private to this file."
 *  Other .c files cannot access these variables.
 *
 *  These variables hold simulated hardware state. You can modify them
 *  here to test different scenarios (low battery, no signal, etc.).
 *
 *  In a real implementation, these would be replaced with actual
 *  hardware readings from I2C/UART/GPIO.
 */

/*
 * sim_battery — Simulated Battery State
 *
 * MODIFY THESE VALUES TO TEST DIFFERENT SCENARIOS:
 *   .percent = 15   → Test low battery warning (red icon)
 *   .percent = 35   → Test medium battery (yellow icon)
 *   .percent = 100  → Test full battery
 *   .charging = true → Test charging indicator
 *
 * C CONCEPT: STRUCT INITIALIZER
 * -----------------------------
 * { .field = value } is called a "designated initializer."
 * It sets specific fields by name. Unmentioned fields get 0/false/NULL.
 *
 * This is clearer than positional initialization:
 *   { 75, false }  // What does 75 mean? What's false for?
 *   { .percent = 75, .charging = false }  // Self-documenting!
 */
static battery_status_t sim_battery = {
    .percent  = 75,     /* Battery at 75% */
    .charging = false   /* Not connected to charger */
};

/*
 * sim_cellular — Simulated Cellular State
 *
 * MODIFY THESE VALUES TO TEST DIFFERENT SCENARIOS:
 *   .signal_bars = 1  → Test weak signal (1 green bar, 3 grey)
 *   .signal_bars = 0  → Test no signal
 *   .connected = false → Test "No Signal" text
 *   .carrier = "AT&T" → Test different carrier name
 */
static cellular_status_t sim_cellular = {
    .signal_bars = 3,        /* Good signal (3 out of 4 bars) */
    .connected   = true,     /* Connected to network */
    .carrier     = "BH Mobile"  /* Carrier name */
};


/* ═══════════════════════════════════════════════════════════════════════════
 *  FUNCTION IMPLEMENTATIONS
 * ═══════════════════════════════════════════════════════════════════════════
 */

/*
 * hardware_init() — Initialize Hardware
 *
 * CURRENT: Does nothing (simulated values don't need setup).
 *
 * REAL HARDWARE: Would open I2C/UART devices, configure GPIO, etc.
 * See the examples at the top of this file.
 */
void hardware_init(void) {
    /*
     * STUB IMPLEMENTATION - nothing to initialize for simulated values.
     *
     * REAL HARDWARE EXAMPLE:
     *
     *   // Open I2C bus for battery gauge
     *   i2c_fd = open("/dev/i2c-1", O_RDWR);
     *   if (i2c_fd < 0) {
     *       fprintf(stderr, "Warning: Cannot open I2C bus\n");
     *   }
     *
     *   // Open UART for cellular modem
     *   uart_fd = open("/dev/ttyUSB0", O_RDWR | O_NOCTTY);
     *   if (uart_fd < 0) {
     *       fprintf(stderr, "Warning: Cannot open UART\n");
     *   }
     *
     *   // Send initialization commands to modem
     *   write(uart_fd, "AT\r", 3);  // Wake up
     *   write(uart_fd, "ATE0\r", 5); // Disable echo
     */
}

/*
 * hardware_cleanup() — Release Hardware Resources
 *
 * CURRENT: Does nothing (simulated values don't need cleanup).
 *
 * REAL HARDWARE: Would close file descriptors, release GPIO, etc.
 */
void hardware_cleanup(void) {
    /*
     * STUB IMPLEMENTATION - nothing to clean up for simulated values.
     *
     * REAL HARDWARE EXAMPLE:
     *
     *   if (i2c_fd >= 0) {
     *       close(i2c_fd);
     *       i2c_fd = -1;
     *   }
     *
     *   if (uart_fd >= 0) {
     *       close(uart_fd);
     *       uart_fd = -1;
     *   }
     */
}

/*
 * hardware_get_battery() — Get Battery Status
 *
 * CURRENT: Returns the simulated battery state defined above.
 *
 * REAL HARDWARE: Would read from I2C fuel gauge.
 *
 * C CONCEPT: RETURN BY VALUE
 * --------------------------
 * This function returns the entire struct BY VALUE, meaning the struct
 * is COPIED when returned. This is fine for small structs like ours.
 *
 * For large structs, you might pass a pointer instead:
 *   void hardware_get_battery(battery_status_t *out) {
 *       out->percent = ...;
 *       out->charging = ...;
 *   }
 *
 * But for small structs, return by value is simpler and clearer.
 */
battery_status_t hardware_get_battery(void) {
    /*
     * SIMULATED: Just return our static simulated state.
     *
     * REAL HARDWARE: Would read from I2C and return actual values.
     * See the detailed example at the top of this file.
     */
    return sim_battery;
}

/*
 * hardware_get_cellular() — Get Cellular Status
 *
 * CURRENT: Returns the simulated cellular state defined above.
 *
 * REAL HARDWARE: Would query modem via AT commands over UART.
 */
cellular_status_t hardware_get_cellular(void) {
    /*
     * SIMULATED: Just return our static simulated state.
     *
     * REAL HARDWARE: Would communicate with modem and return actual values.
     * See the detailed example at the top of this file.
     */
    return sim_cellular;
}
