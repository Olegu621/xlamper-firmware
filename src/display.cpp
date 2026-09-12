// ==================================================================
//  display.cpp — OLED SSD1306 128x64 через U8g2 (I2C: SDA=IO8 SCL=IO9).
//  Обёртка Disp сохраняет API v0.11 (d.print/d.drawRect/...), чтобы
//  портированные игры не переписывать, но убирает глобальный u8g2
//  из заголовков: наружу торчит только extern Disp d.
// ==================================================================
#include "display.h"
#include "config.h"
#include "input.h"   // holdProgress для headerBar
#include <U8g2lib.h>
#include <stdarg.h>

static U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 9, 8);

static const uint8_t* FONT1   = u8g2_font_7x13B_tr;   // bold UI
static const uint8_t* FONT3   = u8g2_font_fub20_tr;   // big rounded
static const uint8_t* FONT_KB = u8g2_font_5x8_tr;    // клавиатура
static const uint8_t* FONT4   = u8g2_font_4x6_tr;     // tiny (2048)

static const u8g2_cb_t* dispRotCodes[4] = { U8G2_R0, U8G2_R1, U8G2_R2, U8G2_R3 };
static uint8_t dispRotState = 0;

void Disp::begin() { u8g2.setBusClock(400000); u8g2.begin(); u8g2.setFont(FONT1); }
void Disp::setRotation(uint8_t r) { if (r > 3) r = 0; dispRotState = r; u8g2.setDisplayRotation(dispRotCodes[r]); }
uint8_t Disp::rotation() const { return dispRotState; }
void Disp::clearDisplay() { u8g2.clearBuffer(); }
void Disp::display()      { u8g2.sendBuffer(); }
void Disp::invertDisplay(bool b) { u8g2.sendF("c", b ? 0xA7 : 0xA6); }
void Disp::setTextColor(int c) { u8g2.setDrawColor(c ? 1 : 0); }
void Disp::setTextSize(int n)  { u8g2.setFont(n == 3 ? FONT3 : (n == 4 ? FONT4 : (n == 5 ? FONT_KB : FONT1))); }
void Disp::setCursor(int x, int y) { cx = x; cy = y; }
void Disp::_put(const char* s) { u8g2.drawUTF8(cx, cy + u8g2.getAscent(), s); cx += u8g2.getStrWidth(s); }
void Disp::_nl() { cy += 13; cx = 0; }
void Disp::print(const char* s)   { _put(s); }
void Disp::print(char c)          { char b[2] = {c, 0}; _put(b); }
void Disp::print(int v)           { char b[12]; snprintf(b, sizeof b, "%d", v); _put(b); }
void Disp::print(unsigned long v) { char b[12]; snprintf(b, sizeof b, "%lu", v); _put(b); }
void Disp::print(const String& s){ _put(s.c_str()); }
void Disp::println(const char* s){ _put(s); _nl(); }
void Disp::format(const char* fmt, ...) {
  char b[96];
  va_list ap; va_start(ap, fmt);
  vsnprintf(b, sizeof b, fmt, ap);
  va_end(ap);
  _put(b);
}
void Disp::drawPixel(int x,int y,int c)                { u8g2.setDrawColor(c?1:0); u8g2.drawPixel(x,y); }
void Disp::drawLine(int x0,int y0,int x1,int y1,int c){ u8g2.setDrawColor(c?1:0); u8g2.drawLine(x0,y0,x1,y1); }
void Disp::drawFastHLine(int x,int y,int w,int c)      { u8g2.setDrawColor(c?1:0); u8g2.drawHLine(x,y,w); }
void Disp::drawFastVLine(int x,int y,int h,int c)      { u8g2.setDrawColor(c?1:0); u8g2.drawVLine(x,y,h); }
void Disp::drawRect(int x,int y,int w,int h,int c)     { u8g2.setDrawColor(c?1:0); u8g2.drawFrame(x,y,w,h); }
void Disp::fillRect(int x,int y,int w,int h,int c)     { u8g2.setDrawColor(c?1:0); u8g2.drawBox(x,y,w,h); }
void Disp::drawCircle(int x,int y,int r,int c)        { u8g2.setDrawColor(c?1:0); u8g2.drawCircle(x,y,r,U8G2_DRAW_ALL); }
void Disp::fillCircle(int x,int y,int r,int c)        { u8g2.setDrawColor(c?1:0); u8g2.drawDisc(x,y,r,U8G2_DRAW_ALL); }
void Disp::fillEllipse(int x,int y,int rx,int ry,int c){ u8g2.setDrawColor(c?1:0); u8g2.drawFilledEllipse(x,y,rx,ry); }
// новые примитивы v0.12 (иконки слотов/спиннера/монеты):
void Disp::drawEllipse(int x,int y,int rx,int ry,int c){ u8g2.setDrawColor(c?1:0); u8g2.drawEllipse(x,y,rx,ry,U8G2_DRAW_ALL); }
void Disp::drawTriangle(int x0,int y0,int x1,int y1,int x2,int y2,int c){ u8g2.setDrawColor(c?1:0); u8g2.drawTriangle(x0,y0,x1,y1,x2,y2); }
void Disp::fillTriangle(int x0,int y0,int x1,int y1,int x2,int y2,int c){ u8g2.setDrawColor(c?1:0); u8g2.drawTriangle(x0,y0,x1,y1,x2,y2); }
int  Disp::strWidth(const char* s)                    { return u8g2.getStrWidth(s); }
void Disp::drawStr(int x, int y, const char* s)       { u8g2.drawUTF8(x, y + u8g2.getAscent(), s); }
void Disp::setClip(int x0, int y0, int x1, int y1)    { u8g2.setClipWindow(x0, y0, x1, y1); }
void Disp::clearClip()                                { u8g2.setMaxClipWindow(); }
void Disp::setFontSmall()                             { u8g2.setFont(FONT_KB); }
void Disp::setFontTiny()                              { u8g2.setFont(FONT4); }
int  Disp::getAscent()                                { return u8g2.getAscent(); }
const void* Disp::getBufferPtr() const                 { return u8g2.getBufferPtr(); }
int  Disp::getBufferTileWidth() const                 { return u8g2.getBufferTileWidth(); }

// ---------- общие UI-элементы ----------
void headerBar(const char* s) {
  d.fillRect(0, 0, W, 13, 1);
  d.setTextColor(0); d.setTextSize(1);
  d.setCursor(2, 1); d.print(s);
  int hp = holdProgress();
  if (hp > 0) {
    int bw = 30 * hp / 100;
    d.drawRect(92, 3, 34, 7, 0);
    d.fillRect(94 + (30 - bw), 4, bw, 5, 0);
  }
}
void footerBar(const char* s) {
  d.drawFastHLine(0, 51, W, 1);
  d.setTextColor(1); d.setTextSize(1);
  d.setCursor(2, 53); d.print(s);
}
void flashInvert() { d.invertDisplay(true); delay(70); d.invertDisplay(false); }

Disp d;
