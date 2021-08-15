#define MY_USE_FB 0

#if MY_USE_FB
#define USE_FRAME_BUFFER
#endif

#include <string.h>
#include <stdio.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <ST7735_t3.h> // Hardware-specific library
#include <ST7789_t3.h> // Hardware-specific library

#include "Sharp.h"
#include "noteset.h"
#include "LCD.h"
#include "Utilities.h"

#include "st7735_t3_font_Arial.h"
#include "st7735_t3_font_ArialBold.h"
#include "st7735_t3_font_ArialBoldItalic.h"

// LCD defines

#define TFT_MISO  12
#define TFT_MOSI  11  //a12
#define TFT_SCK   13  //a13
#define TFT_DC    22 
#define TFT_CS    21  
#define TFT_RST   23
#define TFT_LITE  -1

#if MY_USE_FB
#ifdef USE_FRAME_BUFFER
#define UpdateScreen _ptft->updateScreenAsync 
#else
#define UpdateScreen _ptft->updateScreen
#endif
#endif

ST7789_t3 tft = ST7789_t3(TFT_CS, TFT_DC, TFT_RST);

#define LCD_WIDTH 240
#define LCD_HEIGHT 135
#define ROW_HEIGHT 27

void dotextatrow(int r, const char *text, uint16_t fg, uint16_t bg, const ILI9341_t3_font_t font, bool col2, bool half);

void setupLCD(void) 
{  
  tft.init(LCD_HEIGHT, LCD_WIDTH);

  pinMode(TFT_LITE, OUTPUT);
  digitalWrite(TFT_LITE, HIGH);

  tft.fillScreen(ST7735_BLACK);
  tft.invertDisplay(true);

  tft.setRotation(3);
#if MY_USE_FB
  if (!tft.useFrameBuffer(true))
    Serial.println("Framebuffer failed");
  else
    Serial.println("Framebuffer success");
#endif
  tft.setTextWrap(false);
//  tft.setFont(Arial_14_Bold_Italic);
//  tft.setTextColor(LCD_WHITE);
  tft.fillRect(0, 0, LCD_WIDTH, LCD_HEIGHT, LCD_WHITE);

  dotextatrow(3, "Tempo (bpm)", LCD_WHITE, LCD_BLACK, Arial_13_Bold, false, false);
  
#if MY_USE_FB
  tft.updateScreen();
#endif  
}

void dotextatrow(int r, const char *text, uint16_t fg, uint16_t bg, const ILI9341_t3_font_t font, bool col2, bool half) 
{
//  Serial.println(font.cap_height);
//  Serial.println(font.bits_height);
  int16_t x = col2 ? LCD_WIDTH / 2 : 0;
  int16_t y = r * ROW_HEIGHT + 5;
//  int16_t x1, y1;
//  uint16_t w, h;
//  
//  tft.getTextBounds(text, x, y, &x1, &y1, &w, &h);
  tft.setFont(font);
  if (r == 4)
    tft.fillRect(x + 2, y - 1, (half ? 0.5 : 1) * LCD_WIDTH - 4, ROW_HEIGHT - 8, bg);
  else
    tft.fillRect(x + 2, y - 1, (half ? 0.5 : 1) * LCD_WIDTH - 4, ROW_HEIGHT, bg);
  tft.setCursor(x + 3, y + ((r < 4 || font.cap_height == 12) ? 5 : 1));
  tft.setTextColor(fg);
  tft.print(text);
#if MY_USE_FB
  tft.updateScreen();
#endif  
}

void smallertextatrow(int r, const char *text, uint16_t fg, uint16_t bg)
{
  dotextatrow(r, text, fg, bg, Arial_12_Bold, false, false);
}

void textatrow(int r, const char *text, uint16_t fg, uint16_t bg) 
{
  dotextatrow(r, text, fg, bg, Arial_14_Bold, false, false);
}

void lineatrow(int r, uint16_t c) 
{
  int y = r * ROW_HEIGHT + 2;
  tft.drawLine(0, y, LCD_WIDTH, y, c);
}

static char tempstr[50] = {0};
static noteset *updateNotes = NULL;

void needUpdateNoteDisplay(noteset *notes)
{
  updateNotes = notes;
}

void doUpdateNoteDisplay()
{
  if (!updateNotes)
    return;
  tempstr[0] = '\0';
  char *pstr = tempstr;
  int n = 0;
  for (int i = 0; i < updateNotes->count; ++i)
  {
    if (i > 6)
    {
      sprintf(pstr, "...");
      break;
    }
    char *notestr = getnotestring(updateNotes->notes[i].pitch, false);
    n = sprintf(pstr, "%s ", notestr);
    pstr += n;
  }
  smallertextatrow(4, tempstr, LCD_RED, LCD_WHITE);
  updateNotes = NULL;
}

const char *arpmodeindicator[] = { "Up", "Down", "Up/Down", "In", "Out", "In/Out", "Random", "Shuffle" };
const char *altmodeindicator[] = { "", "[Repeat]", "[Repeat-2]", "[Repeat-3]", "[Octave]", "[Ratchet-F]", "[Ratchet-L]", "[Walk]" };
const char* chordindicator = "[Chord]";

const char *arpmodeshort[] = { "Up", "Down", "UpDn", "In", "Out", "InOut", "Rand", "Shuf" };
const char *altmodeshort[] = { "", "[Rep]", "[Rep2]", "[Rep3]", "[Oct]", "[RatF]", "[RatL]", "[Walk]" };
const char* chordshort = "[Chd]";

const char *devicemodestr[] = { "Arp low", "Arp high", "Dual arp" };

void showMode(int arp, int curArp, devicestate *state)
{
  char str[27] = {0};
  arpstate a = state->arp[arp];
  
  int col = state->common.active ? LCD_GREEN : LCD_CYAN;
  sprintf(str, "%s %s", arpmodeshort[(int)a.mode], 
      a.chords ? chordshort : altmodeshort[(int)a.alt]);
  dotextatrow(0, str, col, curArp == arp ? LCD_BLACK : LCD_LTGREY, Arial_13_Bold, arp == 1, true);

  if (a.timebase > 1)
    sprintf(str, "T'base: %d  ", a.timebase);
  else
    sprintf(str, "T'base: -  ");
  dotextatrow(1, str, col, curArp == arp ? LCD_BLACK : LCD_LTGREY, Arial_13_Bold, arp == 1, true);
}

void showTempo(int tempo, float tempo_factor, bool swing)
{
  char str[27] = {0};
  long bpm = round(60000.0 / tempo);
  if (tempo_factor < 1)
    sprintf(str, "%3ld [1/%d] %s", bpm, (int)(0.5 + 1.0 / tempo_factor), swing ? "(S)" : "");
  else if (tempo_factor > 1)
    sprintf(str, "%3ld [%d] %s", bpm, (int)tempo_factor, swing ? "(S)" : "");
  else
    sprintf(str, "%3ld %s", bpm, swing ? "(S)" : "");
  dotextatrow(3, str, LCD_WHITE, LCD_BLACK, Arial_13_Bold, true, true);
}

void showControl(const char *text, bool invert)
{
  uint16_t fg = invert ? LCD_WHITE : LCD_BLUE;
  uint16_t bg = invert ? LCD_BLUE : LCD_WHITE;

  int16_t x = 0;
  int16_t y = 2 * ROW_HEIGHT + 5;
  int16_t x1, y1;
  uint16_t w, h;
  
  tft.setFont(Arial_14_Bold);
  tft.fillRect(x + 2, y - 1, LCD_WIDTH - 4, ROW_HEIGHT, bg);
  tft.setCursor(x + 3, y + 5);
  tft.setTextColor(fg);
  tft.print(text);
  
  tft.getTextBounds(devicemodestr[curmode], x, y, &x1, &y1, &w, &h);
  tft.setCursor(LCD_WIDTH - 4 - w, y + 5);
  tft.setTextColor(LCD_MAGENTA);
  tft.print(devicemodestr[curmode]);
  
#if MY_USE_FB
  tft.updateScreen();
#endif  
}
