/* ili9341.c */

#include "ili9341.h"
#include "spi.h"
#include <stdlib.h>
/* ============================================
   Private Definitions
   ============================================ */

// GPIO Pins (from STM32F429I-Discovery schematic)
#define LCD_CS_PIN       GPIO_PIN_2
#define LCD_CS_PORT      GPIOC

#define LCD_DC_PIN       GPIO_PIN_13
#define LCD_DC_PORT      GPIOD

// Current orientation
static LCD_Orientation_t current_orientation = LCD_ORIENTATION_PORTRAIT;
static uint16_t lcd_width = ILI9341_WIDTH;
static uint16_t lcd_height = ILI9341_HEIGHT;

/* ============================================
   Private Function Prototypes
   ============================================ */
static void ILI9341_WriteCommand(uint8_t cmd);
static void ILI9341_WriteData(uint8_t data);
static void ILI9341_WriteData16(uint16_t data);
static void ILI9341_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
static void ILI9341_CS_Low(void);
static void ILI9341_CS_High(void);
static void ILI9341_DC_Low(void);
static void ILI9341_DC_High(void);

/* ============================================
   Low-Level Control Functions
   ============================================ */

/**
 * @brief Select LCD (Chip Select LOW)
 */
static void ILI9341_CS_Low(void)
{
    HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_RESET);
}

/**
 * @brief Deselect LCD (Chip Select HIGH)
 */
static void ILI9341_CS_High(void)
{
    HAL_GPIO_WritePin(LCD_CS_PORT, LCD_CS_PIN, GPIO_PIN_SET);
}

/**
 * @brief Set DC pin LOW (Command mode)
 */
static void ILI9341_DC_Low(void)
{
    HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_RESET);
}

/**
 * @brief Set DC pin HIGH (Data mode)
 */
static void ILI9341_DC_High(void)
{
    HAL_GPIO_WritePin(LCD_DC_PORT, LCD_DC_PIN, GPIO_PIN_SET);
}

/**
 * @brief Write command to LCD
 * @param cmd: Command byte
 */
static void ILI9341_WriteCommand(uint8_t cmd)
{
    ILI9341_DC_Low();   // Command mode
    ILI9341_CS_Low();   // Select LCD
    
    HAL_SPI_Transmit(&hspi5, &cmd, 1, HAL_MAX_DELAY);
    
    ILI9341_CS_High();  // Deselect LCD
}

/**
 * @brief Write single data byte to LCD
 * @param data: Data byte
 */
static void ILI9341_WriteData(uint8_t data)
{
    ILI9341_DC_High();  // Data mode
    ILI9341_CS_Low();   // Select LCD
    
    HAL_SPI_Transmit(&hspi5, &data, 1, HAL_MAX_DELAY);
    
    ILI9341_CS_High();  // Deselect LCD
}

/**
 * @brief Write 16-bit data to LCD
 * @param data: 16-bit data (color)
 */
static void ILI9341_WriteData16(uint16_t data)
{
    uint8_t buffer[2];
    buffer[0] = data >> 8;    // High byte
    buffer[1] = data & 0xFF;  // Low byte
    
    ILI9341_DC_High();  // Data mode
    ILI9341_CS_Low();   // Select LCD
    
    HAL_SPI_Transmit(&hspi5, buffer, 2, HAL_MAX_DELAY);
    
    ILI9341_CS_High();  // Deselect LCD
}

/* ============================================
   Drawing Functions
   ============================================ */

/**
 * @brief Convert RGB888 to RGB565
 * @param r, g, b: RGB values (0-255)
 * @return RGB565 color value
 */
uint16_t ILI9341_Color565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

/**
 * @brief Fill entire screen with color
 * @param color: RGB565 color
 */
void ILI9341_FillScreen(uint16_t color)
{
    ILI9341_FillRect(0, 0, lcd_width, lcd_height, color);
}

/**
 * @brief Draw single pixel
 * @param x, y: Coordinates
 * @param color: RGB565 color
 */
void ILI9341_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
    // Boundary check
    if (x >= lcd_width || y >= lcd_height) return;
    
    ILI9341_SetWindow(x, y, x, y);
    ILI9341_WriteData16(color);
}

/**
 * @brief Fill rectangle
 * @param x, y: Top-left corner
 * @param w, h: Width and height
 * @param color: RGB565 color
 */
void ILI9341_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    // Boundary check
    if (x >= lcd_width || y >= lcd_height) return;
    if (x + w > lcd_width) w = lcd_width - x;
    if (y + h > lcd_height) h = lcd_height - y;
    
    // Set window to rectangle
    ILI9341_SetWindow(x, y, x + w - 1, y + h - 1);
    
    // Prepare color bytes
    uint8_t buffer[2];
    buffer[0] = color >> 8;
    buffer[1] = color & 0xFF;
    
    // Fill rectangle
    ILI9341_DC_High();  // Data mode
    ILI9341_CS_Low();   // Select LCD
    
    uint32_t pixel_count = w * h;
    for (uint32_t i = 0; i < pixel_count; i++) {
        HAL_SPI_Transmit(&hspi5, buffer, 2, HAL_MAX_DELAY);
    }
    
    ILI9341_CS_High();  // Deselect LCD
}

/**
 * @brief Draw rectangle outline
 * @param x, y: Top-left corner
 * @param w, h: Width and height
 * @param color: RGB565 color
 */
void ILI9341_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    ILI9341_DrawLine(x, y, x + w - 1, y, color);           // Top
    ILI9341_DrawLine(x, y, x, y + h - 1, color);           // Left
    ILI9341_DrawLine(x + w - 1, y, x + w - 1, y + h - 1, color); // Right
    ILI9341_DrawLine(x, y + h - 1, x + w - 1, y + h - 1, color); // Bottom
}

/**
 * @brief Draw line (Bresenham's algorithm)
 * @param x0, y0: Start point
 * @param x1, y1: End point
 * @param color: RGB565 color
 */
void ILI9341_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color)
{
    int16_t dx = abs(x1 - x0);
    int16_t dy = abs(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;
    
    while (1) {
        ILI9341_DrawPixel(x0, y0, color);
        
        if (x0 == x1 && y0 == y1) break;
        
        int16_t e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/**
 * @brief Draw circle outline
 * @param x0, y0: Center
 * @param r: Radius
 * @param color: RGB565 color
 */
void ILI9341_DrawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
    int16_t x = r;
    int16_t y = 0;
    int16_t err = 0;
    
    while (x >= y) {
        ILI9341_DrawPixel(x0 + x, y0 + y, color);
        ILI9341_DrawPixel(x0 + y, y0 + x, color);
        ILI9341_DrawPixel(x0 - y, y0 + x, color);
        ILI9341_DrawPixel(x0 - x, y0 + y, color);
        ILI9341_DrawPixel(x0 - x, y0 - y, color);
        ILI9341_DrawPixel(x0 - y, y0 - x, color);
        ILI9341_DrawPixel(x0 + y, y0 - x, color);
        ILI9341_DrawPixel(x0 + x, y0 - y, color);
        
        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }
}

/**
 * @brief Draw filled circle
 * @param x0, y0: Center
 * @param r: Radius
 * @param color: RGB565 color
 */
void ILI9341_FillCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color)
{
    int16_t x = r;
    int16_t y = 0;
    int16_t err = 0;
    
    while (x >= y) {
        ILI9341_DrawLine(x0 - x, y0 + y, x0 + x, y0 + y, color);
        ILI9341_DrawLine(x0 - y, y0 + x, x0 + y, y0 + x, color);
        ILI9341_DrawLine(x0 - x, y0 - y, x0 + x, y0 - y, color);
        ILI9341_DrawLine(x0 - y, y0 - x, x0 + y, y0 - x, color);
        
        if (err <= 0) {
            y += 1;
            err += 2 * y + 1;
        }
        if (err > 0) {
            x -= 1;
            err -= 2 * x + 1;
        }
    }
}

/* ili9341.c - continued */

// Simple 5x7 font (ASCII 32-126)
static const uint8_t font5x7[][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, // Space
    {0x00, 0x00, 0x5F, 0x00, 0x00}, // !
    {0x00, 0x07, 0x00, 0x07, 0x00}, // "
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, // #
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, // $
    {0x23, 0x13, 0x08, 0x64, 0x62}, // %
    {0x36, 0x49, 0x55, 0x22, 0x50}, // &
    {0x00, 0x05, 0x03, 0x00, 0x00}, // '
    {0x00, 0x1C, 0x22, 0x41, 0x00}, // (
    {0x00, 0x41, 0x22, 0x1C, 0x00}, // )
    {0x08, 0x2A, 0x1C, 0x2A, 0x08}, // *
    {0x08, 0x08, 0x3E, 0x08, 0x08}, // +
    {0x00, 0x50, 0x30, 0x00, 0x00}, // ,
    {0x08, 0x08, 0x08, 0x08, 0x08}, // -
    {0x00, 0x60, 0x60, 0x00, 0x00}, // .
    {0x20, 0x10, 0x08, 0x04, 0x02}, // /
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, // 0
    {0x00, 0x42, 0x7F, 0x40, 0x00}, // 1
    {0x42, 0x61, 0x51, 0x49, 0x46}, // 2
    {0x21, 0x41, 0x45, 0x4B, 0x31}, // 3
    {0x18, 0x14, 0x12, 0x7F, 0x10}, // 4
    {0x27, 0x45, 0x45, 0x45, 0x39}, // 5
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, // 6
    {0x01, 0x71, 0x09, 0x05, 0x03}, // 7
    {0x36, 0x49, 0x49, 0x49, 0x36}, // 8
    {0x06, 0x49, 0x49, 0x29, 0x1E}, // 9
    // ... (add more characters as needed)
    // For brevity, showing only 0-9, add A-Z, a-z, symbols as needed
};

/**
 * @brief Draw single character
 * @param x, y: Top-left position
 * @param c: Character to draw
 * @param color: Text color
 * @param bg: Background color
 * @param size: Font size multiplier (1, 2, 3, etc.)
 */
void ILI9341_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size)
{
    if (c < 32 || c > 126) return;  // Only printable ASCII
    
    uint8_t char_index = c - 32;  // Font starts at space (ASCII 32)
    
    for (uint8_t col = 0; col < 5; col++) {
        uint8_t line = font5x7[char_index][col];
        
        for (uint8_t row = 0; row < 8; row++) {
            if (line & 0x01) {
                // Draw foreground pixel(s)
                if (size == 1) {
                    ILI9341_DrawPixel(x + col, y + row, color);
                } else {
                    ILI9341_FillRect(x + col * size, y + row * size, size, size, color);
                }
            } else if (bg != color) {
                // Draw background pixel(s)
                if (size == 1) {
                    ILI9341_DrawPixel(x + col, y + row, bg);
                } else {
                    ILI9341_FillRect(x + col * size, y + row * size, size, size, bg);
                }
            }
            line >>= 1;
        }
    }
}

/**
 * @brief Draw string
 * @param x, y: Top-left position
 * @param str: String to draw
 * @param color: Text color
 * @param bg: Background color
 * @param size: Font size multiplier
 */
void ILI9341_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg, uint8_t size)
{
    uint16_t cursor_x = x;
    uint16_t cursor_y = y;
    
    while (*str) {
        if (*str == '\n') {
            // New line
            cursor_y += 8 * size;
            cursor_x = x;
        } else if (*str == '\r') {
            // Carriage return
            cursor_x = x;
        } else {
            // Draw character
            ILI9341_DrawChar(cursor_x, cursor_y, *str, color, bg, size);
            cursor_x += 6 * size;  // 5 pixels + 1 space
            
            // Wrap if needed
            if (cursor_x + 6 * size > lcd_width) {
                cursor_y += 8 * size;
                cursor_x = x;
            }
        }
        str++;
    }
}

/**
 * @brief Set screen orientation
 * @param orientation: Orientation mode
 */
void ILI9341_SetOrientation(LCD_Orientation_t orientation)
{
    uint8_t madctl_value;
    
    current_orientation = orientation;
    
    switch (orientation) {
        case LCD_ORIENTATION_PORTRAIT:
            madctl_value = 0x48;
            lcd_width = ILI9341_WIDTH;
            lcd_height = ILI9341_HEIGHT;
            break;
        case LCD_ORIENTATION_LANDSCAPE:
            madctl_value = 0x28;
            lcd_width = ILI9341_HEIGHT;
            lcd_height = ILI9341_WIDTH;
            break;
        case LCD_ORIENTATION_PORTRAIT_INV:
            madctl_value = 0x88;
            lcd_width = ILI9341_WIDTH;
            lcd_height = ILI9341_HEIGHT;
            break;
        case LCD_ORIENTATION_LANDSCAPE_INV:
            madctl_value = 0xE8;
            lcd_width = ILI9341_HEIGHT;
            lcd_height = ILI9341_WIDTH;
            break;
        default:
            madctl_value = 0x48;
            lcd_width = ILI9341_WIDTH;
            lcd_height = ILI9341_HEIGHT;
            break;
    }
    
    ILI9341_WriteCommand(ILI9341_MADCTL);
    ILI9341_WriteData(madctl_value);
}

/**
 * @brief Set drawing window (address window)
 * @param x0, y0: Top-left corner
 * @param x1, y1: Bottom-right corner
 */
static void ILI9341_SetWindow(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    // Column address set
    ILI9341_WriteCommand(ILI9341_CASET);
    ILI9341_WriteData(x0 >> 8);
    ILI9341_WriteData(x0 & 0xFF);
    ILI9341_WriteData(x1 >> 8);
    ILI9341_WriteData(x1 & 0xFF);

    // Page address set
    ILI9341_WriteCommand(ILI9341_PASET);
    ILI9341_WriteData(y0 >> 8);
    ILI9341_WriteData(y0 & 0xFF);
    ILI9341_WriteData(y1 >> 8);
    ILI9341_WriteData(y1 & 0xFF);

    // Write to RAM
    ILI9341_WriteCommand(ILI9341_RAMWR);
}

/**
 * @brief Initialize ILI9341 LCD controller
 */
void ILI9341_Init(void)
{
    // Hardware reset (if reset pin connected, add code here)
    HAL_Delay(10);

    // Software reset
    ILI9341_WriteCommand(ILI9341_SWRESET);
    HAL_Delay(120);

    // Display off
    ILI9341_WriteCommand(ILI9341_DISPOFF);

    // Power control A
    ILI9341_WriteCommand(0xCB);
    ILI9341_WriteData(0x39);
    ILI9341_WriteData(0x2C);
    ILI9341_WriteData(0x00);
    ILI9341_WriteData(0x34);
    ILI9341_WriteData(0x02);

    // Power control B
    ILI9341_WriteCommand(0xCF);
    ILI9341_WriteData(0x00);
    ILI9341_WriteData(0xC1);
    ILI9341_WriteData(0x30);

    // Driver timing control A
    ILI9341_WriteCommand(0xE8);
    ILI9341_WriteData(0x85);
    ILI9341_WriteData(0x00);
    ILI9341_WriteData(0x78);

    // Driver timing control B
    ILI9341_WriteCommand(0xEA);
    ILI9341_WriteData(0x00);
    ILI9341_WriteData(0x00);

    // Power on sequence control
    ILI9341_WriteCommand(0xED);
    ILI9341_WriteData(0x64);
    ILI9341_WriteData(0x03);
    ILI9341_WriteData(0x12);
    ILI9341_WriteData(0x81);

    // Pump ratio control
    ILI9341_WriteCommand(0xF7);
    ILI9341_WriteData(0x20);

    // Power control 1
    ILI9341_WriteCommand(0xC0);
    ILI9341_WriteData(0x23);  // VRH[5:0]

    // Power control 2
    ILI9341_WriteCommand(0xC1);
    ILI9341_WriteData(0x10);  // SAP[2:0];BT[3:0]

    // VCOM control 1
    ILI9341_WriteCommand(0xC5);
    ILI9341_WriteData(0x3E);
    ILI9341_WriteData(0x28);

    // VCOM control 2
    ILI9341_WriteCommand(0xC7);
    ILI9341_WriteData(0x86);

    // Pixel format
    ILI9341_WriteCommand(ILI9341_PIXFMT);
    ILI9341_WriteData(0x55);  // 16-bit/pixel

    // Frame rate control
    ILI9341_WriteCommand(0xB1);
    ILI9341_WriteData(0x00);
    ILI9341_WriteData(0x18);

    // Display function control
    ILI9341_WriteCommand(0xB6);
    ILI9341_WriteData(0x08);
    ILI9341_WriteData(0x82);
    ILI9341_WriteData(0x27);

    // Enable 3 gamma control
    ILI9341_WriteCommand(0xF2);
    ILI9341_WriteData(0x00);

    // Gamma curve selected
    ILI9341_WriteCommand(0x26);
    ILI9341_WriteData(0x01);

    // Positive gamma correction
    ILI9341_WriteCommand(0xE0);
    ILI9341_WriteData(0x0F);
    ILI9341_WriteData(0x31);
    ILI9341_WriteData(0x2B);
    ILI9341_WriteData(0x0C);
    ILI9341_WriteData(0x0E);
    ILI9341_WriteData(0x08);
    ILI9341_WriteData(0x4E);
    ILI9341_WriteData(0xF1);
    ILI9341_WriteData(0x37);
    ILI9341_WriteData(0x07);
    ILI9341_WriteData(0x10);
    ILI9341_WriteData(0x03);
    ILI9341_WriteData(0x0E);
    ILI9341_WriteData(0x09);
    ILI9341_WriteData(0x00);

    // Negative gamma correction
    ILI9341_WriteCommand(0xE1);
    ILI9341_WriteData(0x00);
    ILI9341_WriteData(0x0E);
    ILI9341_WriteData(0x14);
    ILI9341_WriteData(0x03);
    ILI9341_WriteData(0x11);
    ILI9341_WriteData(0x07);
    ILI9341_WriteData(0x31);
    ILI9341_WriteData(0xC1);
    ILI9341_WriteData(0x48);
    ILI9341_WriteData(0x08);
    ILI9341_WriteData(0x0F);
    ILI9341_WriteData(0x0C);
    ILI9341_WriteData(0x31);
    ILI9341_WriteData(0x36);
    ILI9341_WriteData(0x0F);

    // Sleep out
    ILI9341_WriteCommand(ILI9341_SLPOUT);
    HAL_Delay(120);

    // Set default orientation
    ILI9341_SetOrientation(LCD_ORIENTATION_PORTRAIT);

    // Display on
    ILI9341_WriteCommand(ILI9341_DISPON);
    HAL_Delay(10);
}