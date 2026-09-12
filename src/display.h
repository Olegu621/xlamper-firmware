#pragma once
// ==================================================================
//  display.h — OLED + общие UI-элементы.
// ==================================================================
#include <Arduino.h>

class Disp {
public:
  int cx = 0, cy = 0;
  void begin();
  void setRotation(uint8_t r);
  uint8_t rotation() const;
  void clearDisplay();
  void display();
  void invertDisplay(bool b);
  void setTextColor(int c);
  void setTextSize(int n);
  void setCursor(int x, int y);
  void print(const char* s);
  void print(char c);
  void print(int v);
  void print(unsigned long v);
  void print(const String& s);
  void println(const char* s);
  void format(const char* fmt, ...);
  void drawPixel(int x, int y, int c = 1);
  void drawLine(int x0, int y0, int x1, int y1, int c = 1);
  void drawFastHLine(int x, int y, int w, int c = 1);
  void drawFastVLine(int x, int y, int h, int c = 1);
  void drawRect(int x, int y, int w, int h, int c = 1);
  void fillRect(int x, int y, int w, int h, int c = 1);
  void drawCircle(int x, int y, int r, int c = 1);
  void fillCircle(int x, int y, int r, int c = 1);
  void drawEllipse(int x, int y, int rx, int ry, int c = 1);
  void fillEllipse(int x, int y, int rx, int ry, int c = 1);
  void drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, int c = 1);
  void fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, int c = 1);
  int  strWidth(const char* s);
  void drawStr(int x, int y, const char* s);
  void setClip(int x0, int y0, int x1, int y1);
  void clearClip();
  void setFontSmall();
  void setFontTiny();
  int  getAscent();
  // Доступ к фреймбуферу U8g2 (для анимаций в ui.cpp):
  // layout — tiles по 8 пикселей в ширину, getBufferTileWidth()*8 байт/строку.
  const void* getBufferPtr() const;
  int getBufferTileWidth() const;
private:
  void _put(const char* s);
  void _nl();
};

extern Disp d;

void headerBar(const char* s);
void footerBar(const char* s);
void flashInvert();
