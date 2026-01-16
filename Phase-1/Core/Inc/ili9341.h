/* ili9341.h */

#ifndef ILI9341_H
#define ILI9341_H

#include "main.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================
   LCD Dimensions
   ============================================ */
#define ILI9341_WIDTH  240
#define ILI9341_HEIGHT 320

/* ============================================
   Color Definitions (RGB565 format)
   ============================================ */
#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF
#define COLOR_RED         0xF800
#define COLOR_GREEN       0x07E0
#define COLOR_BLUE        0x001F
#define COLOR_YELLOW      0xFFE0
#define COLOR_CYAN        0x07FF
#define COLOR_MAGENTA     0xF81F
#define COLOR_GRAY        0x8410
#define COLOR_ORANGE      0xFD20
#define COLOR_PURPLE      0x8010
#define COLOR_PINK        0xFE19

/* ============================================
   ILI9341 Commands
   ============================================ */
#define ILI9341_NOP        0x00
#define ILI9341_SWRESET    0x01
#define ILI9341_RDDID      0x04
#define ILI9341_RDDST      0x09

#define ILI9341_SLPIN      0x10
#define ILI9341_SLPOUT     0x11
#define ILI9341_PTLON      0x12
#define ILI9341_NORON      0x13

#define ILI9341_INVOFF     0x20
#define ILI9341_INVON      0x21
#define ILI9341_DISPOFF    0x28
#define ILI9341_DISPON     0x29
#define ILI9341_CASET      0x2A
#define ILI9341_PASET      0x2B
#define ILI9341_RAMWR      0x2C
#define ILI9341_RAMRD      0x2E

#define ILI9341_PTLAR      0x30
#define ILI9341_MADCTL     0x36
#define ILI9341_PIXFMT     0x3A

/* ============================================
   Orientation
   ============================================ */
typedef enum {
    LCD_ORIENTATION_PORTRAIT = 0,
    LCD_ORIENTATION_LANDSCAPE = 1,
    LCD_ORIENTATION_PORTRAIT_INV = 2,
    LCD_ORIENTATION_LANDSCAPE_INV = 3
} LCD_Orientation_t;

/* ============================================
   Public Functions
   ============================================ */

// Initialization
void ILI9341_Init(void);
void ILI9341_SetOrientation(LCD_Orientation_t orientation);

// Basic drawing
void ILI9341_FillScreen(uint16_t color);
void ILI9341_DrawPixel(uint16_t x, uint16_t y, uint16_t color);
void ILI9341_DrawLine(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint16_t color);
void ILI9341_DrawRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ILI9341_FillRect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color);
void ILI9341_DrawCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);
void ILI9341_FillCircle(uint16_t x0, uint16_t y0, uint16_t r, uint16_t color);

// Text drawing
void ILI9341_DrawChar(uint16_t x, uint16_t y, char c, uint16_t color, uint16_t bg, uint8_t size);
void ILI9341_DrawString(uint16_t x, uint16_t y, const char *str, uint16_t color, uint16_t bg, uint8_t size);

// Color conversion
uint16_t ILI9341_Color565(uint8_t r, uint8_t g, uint8_t b);

#endif /* ILI9341_H */