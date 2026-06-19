/*
 * ============================================================================
 *  hardware.h — Hardware Abstraction Layer (HAL) Header
 * ============================================================================
 *
 *  WHAT IS THIS FILE?
 *  ------------------
 *  This header defines the interface between the UI and hardware sensors.
 *  It declares functions and data types for reading battery level and
 *  cellular signal strength.
 *
 *  Currently uses simulated values for development/testing. The implementation
 *  in hardware.c can be swapped out for real hardware drivers later.
 *
 *
 *  WHY USE A HARDWARE ABSTRACTION LAYER?
 *  -------------------------------------
 *  1. SEPARATION OF CONCERNS: UI code doesn't need to know HOW to read
 *     hardware - it just calls hardware_get_battery() and gets data.
 *
 *  2. TESTABILITY: We can test the UI with fake/simulated values without
 *     needing actual hardware connected.
 *
 *  3. PORTABILITY: To port to different hardware, only hardware.c needs
 *     to change. The UI code stays exactly the same.
 *
 *  4. DEVELOPMENT SPEED: UI developers can work without hardware ready.
 *
 *
 *  C CONCEPTS YOU'LL LEARN:
 *  ------------------------
 *  1. Header guards (#ifndef/#define/#endif) - preventing double inclusion
 *  2. typedef struct - creating custom data types
 *  3. Function declarations - telling compiler what functions exist
 *  4. Include dependencies - what headers we need
 *
 *
 *  REAL HARDWARE INTEGRATION GUIDE:
 *  ================================
 *
 *  BATTERY MONITORING (I2C Fuel Gauge):
 *  ------------------------------------
 *  Common ICs: MAX17048, BQ27441, LC709203F
 *
 *  Wiring (I2C):
 *    - SDA (data) → GPIO pin (e.g., GPIO 2 on Raspberry Pi)
 *    - SCL (clock) → GPIO pin (e.g., GPIO 3 on Raspberry Pi)
 *    - VCC → 3.3V
 *    - GND → Ground
 *
 *  Linux I2C Example:
 *    #include <linux/i2c-dev.h>
 *    int fd = open("/dev/i2c-1", O_RDWR);
 *    ioctl(fd, I2C_SLAVE, 0x36);  // MAX17048 address
 *    // Read register 0x04 for State of Charge (SOC)
 *
 *
 *  CELLULAR MODEM (UART AT Commands):
 *  ----------------------------------
 *  Common modules: SIM800, SIM7600, Quectel EC25
 *
 *  Wiring (UART):
 *    - TX → RX on your board
 *    - RX → TX on your board
 *    - GND → Ground
 *    - VCC → Check module specs (often 4V for SIM800)
 *
 *  AT Commands:
 *    - AT+CSQ       → Signal quality (returns 0-31, 99=unknown)
 *    - AT+CREG?     → Network registration status
 *    - AT+COPS?     → Current operator name
 *
 *  Signal Quality to Bars Conversion:
 *    CSQ 0-9   → 1 bar  (marginal)
 *    CSQ 10-14 → 2 bars (OK)
 *    CSQ 15-19 → 3 bars (good)
 *    CSQ 20-31 → 4 bars (excellent)
 *    CSQ 99    → No signal
 *
 * ============================================================================
 */

/*
 * C CONCEPT: HEADER GUARDS
 * ------------------------
 * Headers can be #included multiple times (e.g., main.c includes ui.h,
 * ui.h includes notcurses.h, main.c also includes notcurses.h directly).
 *
 * Without guards, this causes "redefinition" errors - the compiler sees
 * the same struct/function declared twice.
 *
 * The pattern:
 *   #ifndef UNIQUE_NAME    // If UNIQUE_NAME is NOT defined...
 *   #define UNIQUE_NAME    // ...define it now
 *   // ... header contents ...
 *   #endif                 // End of the #ifndef block
 *
 * Second time the header is included, UNIQUE_NAME is already defined,
 * so everything between #ifndef and #endif is skipped.
 *
 * NAMING CONVENTION: Use the filename in ALL_CAPS with underscores.
 */
#ifndef BLACKHAND_HARDWARE_H
#define BLACKHAND_HARDWARE_H

/*
 * <stdbool.h> - Boolean Type
 *
 * We need this for the 'bool' type used in our structs.
 *   - bool charging;   // true or false
 *   - bool connected;  // true or false
 */
#include <stdbool.h>


/* ═══════════════════════════════════════════════════════════════════════════
 *  DATA STRUCTURES
 * ═══════════════════════════════════════════════════════════════════════════
 */

/*
 * battery_status_t — Battery Information
 *
 * Contains all the information about the battery that the UI needs.
 *
 * FIELDS:
 *   percent  - Current charge level, 0-100
 *              0 = empty, 100 = fully charged
 *
 *   charging - true if connected to power and charging
 *              false if running on battery
 *
 * USAGE EXAMPLE:
 *   battery_status_t batt = hardware_get_battery();
 *   if (batt.percent < 20) {
 *       show_low_battery_warning();
 *   }
 *   if (batt.charging) {
 *       show_charging_icon();
 *   }
 *
 * EXTENDING THIS STRUCT:
 *   You might want to add more fields for a real device:
 *     - int voltage_mv;        // Battery voltage in millivolts
 *     - int current_ma;        // Charging/discharging current
 *     - int temperature_c;     // Battery temperature
 *     - int time_to_empty;     // Estimated minutes remaining
 *     - int time_to_full;      // Estimated minutes to full charge
 */
typedef struct {
    int  percent;      /* 0-100, battery charge level */
    bool charging;     /* true if connected to charger */
} battery_status_t;

/*
 * cellular_status_t — Cellular/Mobile Network Information
 *
 * Contains information about cellular connectivity.
 *
 * FIELDS:
 *   signal_bars - Signal strength indicator, 0-4
 *                 0 = no signal, 4 = excellent
 *
 *   connected   - true if registered to a network
 *                 false if searching or no service
 *
 *   carrier     - Network name (e.g., "T-Mobile", "AT&T")
 *                 Empty string if not connected
 *
 * USAGE EXAMPLE:
 *   cellular_status_t cell = hardware_get_cellular();
 *   if (cell.connected) {
 *       draw_signal_bars(cell.signal_bars);
 *       draw_carrier_name(cell.carrier);
 *   } else {
 *       draw_no_service();
 *   }
 *
 * C CONCEPT: FIXED-SIZE ARRAYS IN STRUCTS
 * ----------------------------------------
 * char carrier[16] is an array of 16 characters.
 *
 * Why not use char *carrier (a pointer)?
 *   - With a pointer, we'd need to allocate memory separately
 *   - With a fixed array, the memory is part of the struct
 *   - Simpler memory management (no malloc/free needed)
 *
 * Tradeoff: Limited to 15 characters + null terminator.
 * If carrier names could be longer, increase the size or use a pointer.
 */
typedef struct {
    int  signal_bars;  /* 0-4, signal strength indicator */
    bool connected;    /* true if registered to network */
    char carrier[16];  /* carrier name, e.g., "T-Mobile" */
} cellular_status_t;


/* ═══════════════════════════════════════════════════════════════════════════
 *  FUNCTION DECLARATIONS
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  C CONCEPT: DECLARATIONS VS DEFINITIONS
 *  ---------------------------------------
 *  A DECLARATION tells the compiler "this function exists" - its name,
 *  parameters, and return type. No function body.
 *
 *  A DEFINITION provides the actual code (function body).
 *
 *  Headers contain DECLARATIONS.
 *  .c files contain DEFINITIONS.
 *
 *  This separation allows:
 *    - Multiple .c files to use the same functions
 *    - Changing implementation without recompiling users
 *    - Faster compilation (only recompile what changed)
 */

/*
 * hardware_get_battery() — Get Current Battery Status
 *
 * WHAT IT DOES:
 * Returns a battery_status_t struct with current battery information.
 *
 * CURRENT IMPLEMENTATION (in hardware.c):
 * Returns simulated values for testing (75% battery, not charging).
 *
 * REAL HARDWARE IMPLEMENTATION:
 * Would read from I2C fuel gauge IC:
 *   1. Open I2C device: open("/dev/i2c-1", O_RDWR)
 *   2. Set slave address: ioctl(fd, I2C_SLAVE, 0x36)
 *   3. Read SOC register: i2c_smbus_read_word_data(fd, 0x04)
 *   4. Read status register or GPIO for charging status
 *   5. Return the values in a battery_status_t
 *
 * RETURNS:
 *   battery_status_t struct with current values
 *
 * CALLED BY:
 *   draw_status_bar() in main.c, once per frame
 */
battery_status_t hardware_get_battery(void);

/*
 * hardware_get_cellular() — Get Current Cellular Status
 *
 * WHAT IT DOES:
 * Returns a cellular_status_t struct with current network information.
 *
 * CURRENT IMPLEMENTATION (in hardware.c):
 * Returns simulated values (3 bars, connected, "BH Mobile").
 *
 * REAL HARDWARE IMPLEMENTATION:
 * Would communicate with cellular modem via UART:
 *   1. Open serial port: open("/dev/ttyUSB0", O_RDWR)
 *   2. Configure baud rate with termios
 *   3. Send "AT+CSQ\r" and parse response for signal quality
 *   4. Send "AT+CREG?\r" and check registration status
 *   5. Send "AT+COPS?\r" and parse for carrier name
 *   6. Return the values in a cellular_status_t
 *
 * RETURNS:
 *   cellular_status_t struct with current values
 *
 * CALLED BY:
 *   draw_status_bar() in main.c, once per frame
 */
cellular_status_t hardware_get_cellular(void);

/*
 * hardware_init() — Initialize Hardware Interfaces
 *
 * WHAT IT DOES:
 * Sets up communication channels with hardware devices.
 * Called once at program startup.
 *
 * CURRENT IMPLEMENTATION:
 * Does nothing (stub for simulated values).
 *
 * REAL HARDWARE IMPLEMENTATION:
 *   - Open I2C device for battery gauge
 *   - Open UART device for cellular modem
 *   - Configure GPIO pins for charging status
 *   - Initialize modem with AT commands
 *   - Store file descriptors in static variables for later use
 *
 * CALLED BY:
 *   main() at startup, before the event loop
 *
 * ERROR HANDLING:
 *   In a real implementation, this might return an error code
 *   or set a global error flag if hardware isn't available.
 */
void hardware_init(void);

/*
 * hardware_cleanup() — Release Hardware Resources
 *
 * WHAT IT DOES:
 * Closes communication channels and frees any allocated resources.
 * Called once at program shutdown.
 *
 * CURRENT IMPLEMENTATION:
 * Does nothing (stub for simulated values).
 *
 * REAL HARDWARE IMPLEMENTATION:
 *   - Close I2C file descriptor
 *   - Close UART file descriptor
 *   - Release any GPIO pins
 *
 * CALLED BY:
 *   main() at shutdown, after the event loop ends
 *
 * C CONCEPT: CLEANUP ORDER
 * ------------------------
 * Resources should be cleaned up in REVERSE order of creation.
 * If hardware_init() opens I2C then UART, hardware_cleanup()
 * should close UART then I2C.
 */
void hardware_cleanup(void);

#endif /* BLACKHAND_HARDWARE_H */
