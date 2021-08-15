#pragma once

#include "notetracker.h"

#define LCD_BLACK      0x0000
#define LCD_WHITE      0xFFFF
#define LCD_RED        0xF800
#define LCD_GREEN      0x07E0
#define LCD_BLUE       0x001F
#define LCD_CYAN       0x07FF
#define LCD_MAGENTA    0xF81F     // 11111 000000 11111
#define LCD_YELLOW     0xFFE0     // 11111 111111 00000
#define LCD_ORANGE     0xFC00
#define LCD_PINK       0xF81F
#define LCD_DKGREEN    0x03E0     // 00000 011111 00000
#define LCD_GREY       0xC618     // 11000 110000 11000
#define LCD_LTGREY     0x4208     // 01000 010000 01000

//#define ILI9341_BLACK       0x0000      /*   0,   0,   0 */
//#define ILI9341_NAVY        0x000F      /*   0,   0, 128 */
//#define ILI9341_DARKGREEN   0x03E0      /*   0, 128,   0 */
//#define ILI9341_DARKCYAN    0x03EF      /*   0, 128, 128 */
//#define ILI9341_MAROON      0x7800      /* 128,   0,   0 */
//#define ILI9341_PURPLE      0x780F      /* 128,   0, 128 */
//#define ILI9341_OLIVE       0x7BE0      /* 128, 128,   0 */
//#define ILI9341_LIGHTGREY   0xC618      /* 192, 192, 192 */
//#define ILI9341_DARKGREY    0x7BEF      /* 128, 128, 128 */
//#define ILI9341_BLUE        0x001F      /*   0,   0, 255 */
//#define ILI9341_GREEN       0x07E0      /*   0, 255,   0 */
//#define ILI9341_CYAN        0x07FF      /*   0, 255, 255 */
//#define ILI9341_RED         0xF800      /* 255,   0,   0 */
//#define ILI9341_MAGENTA     0xF81F      /* 255,   0, 255 */
//#define ILI9341_YELLOW      0xFFE0      /* 255, 255,   0 */
//#define ILI9341_WHITE       0xFFFF      /* 255, 255, 255 */
//#define ILI9341_ORANGE      0xFD20      /* 255, 165,   0 */
//#define ILI9341_GREENYELLOW 0xAFE5      /* 173, 255,  47 */
//#define ILI9341_PINK        0xF81F

void setupLCD(void);

void showMode(int arp, int curArp, devicestate *state);
void showTempo(int tempo, float tempo_factor, bool swing);
void showControl(const char *text, bool invert);

void smallertextatrow(int r, const char *text, uint16_t fg, uint16_t bg);
void textatrow(int r, const char *text, uint16_t fg, uint16_t bg);
void text2atrow(int r, const char *text, uint16_t fg, uint16_t bg);
void textathalfrow(int r, bool lower, const char *text, uint16_t fg, uint16_t bg, int shiftdn, int shiftin, bool monospace);
void lineatrow(int r, uint16_t c);

void needUpdateNoteDisplay(noteset *notes);
void doUpdateNoteDisplay();
