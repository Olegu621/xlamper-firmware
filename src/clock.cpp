// ==================================================================
//  clock.cpp — часы v0.13: главный экран.
//  * крупные 7-сегментные цифры (рисованные, высота 28)
//  * кольцо прогресса секунд вокруг центра
//  * дата + день недели, индикатор NTP
//  * бегущая строка статуса (v0.11 сохранена, стиль чище)
// ==================================================================
#include "clock.h"
#include "config.h"
#include "display.h"
#include "net.h"
#include "cloud.h"
#include <time.h>

static const char* WD[] = { "SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT" };

// ---------- 7-сегментная цифра высотой h в (x,y) ----------
// сегменты: A верх, B верх-право, C низ-право, D низ, E низ-лево, F верх-лево, G середина
static const uint8_t SEG[10] = {
  0b1111110,  // 0: ABCDEF
  0b0110000,  // 1: BC
  0b1101101,  // 2: ABGED
  0b1111001,  // 3: ABGCD
  0b0110011,  // 4: FGBC
  0b1011011,  // 5: AFGCD
  0b1011111,  // 6: AFGEDC
  0b1110000,  // 7: ABC
  0b1111111,  // 8
  0b1111011,  // 9: ABCDFG
};
#define SEG_A 0x40
#define SEG_B 0x20
#define SEG_C 0x10
#define SEG_D 0x08
#define SEG_E 0x04
#define SEG_F 0x02
#define SEG_G 0x01

static void drawDigit(int x, int y, int h, int digit) {
  uint8_t s = SEG[digit];
  int t = h / 7;          // толщина сегмента
  if (t < 2) t = 2;
  int w = h / 2;          // ширина цифры
  if (s & SEG_A) d.fillTriangle(x + t, y, x + w - t, y, x + w, y + t, 1);           // A, со скошенными углами
  if (s & SEG_D) d.fillTriangle(x + t, y + h, x + w - t, y + h, x + w, y + h - t, 1); // D
  if (s & SEG_B) d.fillTriangle(x + w, y + t, x + w, y + h / 2 - t, x + w - t, y + h / 2, 1); // B
  if (s & SEG_C) d.fillTriangle(x + w, y + h / 2 + t, x + w, y + h - t, x + w - t, y + h / 2, 1); // C
  if (s & SEG_E) d.fillTriangle(x, y + h / 2 + t, x + t, y + h / 2, x, y + h - t, 1); // E
  if (s & SEG_F) d.fillTriangle(x, y + t, x + t, y + h / 2, x, y + h / 2 - t, 1);    // F
  if (s & SEG_G) d.fillTriangle(x + t, y + h / 2, x + w - t, y + h / 2, x + w / 2, y + h / 2 + t / 2, 1); // G
}

void drawClock() {
  struct tm tmi = {};
  if (ntpTime) {
    time_t tt = ntpTime + (millis() - ntpGotAt) / 1000;
    tmi = *localtime(&tt);
  } else {
    uint32_t t = millis() / 1000;
    tmi.tm_hour = (t / 3600) % 24; tmi.tm_min = (t / 60) % 60; tmi.tm_sec = t % 60;
  }

  d.clearDisplay();
  d.setTextColor(1);

  // ---------- время: крупные рисованные цифры ----------
  const int DH = 26;          // высота цифры
  const int DW = 13;          // шаг между цифрами
  int x0 = 10, y0 = 14;
  drawDigit(x0,        y0, DH, (tmi.tm_hour / 10) % 10);
  drawDigit(x0 + DW,   y0, DH, tmi.tm_hour % 10);
  // мигающее двоеточие
  if ((millis() / 500) % 2) {
    d.fillCircle(x0 + 2 * DW + 4, y0 + 8, 2, 1);
    d.fillCircle(x0 + 2 * DW + 4, y0 + 18, 2, 1);
  }
  drawDigit(x0 + 3 * DW, y0, DH, tmi.tm_min / 10);
  drawDigit(x0 + 4 * DW, y0, DH, tmi.tm_min % 10);

  // ---------- кольцо секунд (дуга 0..59) ----------
  int cxm = 104, cym = 27, rr = 16;
  d.drawCircle(cxm, cym, rr, 1);
  // секундная стрелка
  float ang = (tmi.tm_sec * 6 - 90) * 3.14159f / 180.0f;
  d.drawLine(cxm, cym, cxm + (int)(rr * cosf(ang)), cym + (int)(rr * sinf(ang)), 1);
  d.fillCircle(cxm, cym, 2, 1);
  // секундные тики: 12 отметок
  for (int i = 0; i < 12; i++) {
    float ta = i * 30 * 3.14159f / 180.0f;
    int tx1 = cxm + (rr - 3) * cosf(ta), ty1 = cym + (rr - 3) * sinf(ta);
    int tx2 = cxm + (rr - 1) * cosf(ta), ty2 = cym + (rr - 1) * sinf(ta);
    d.drawLine(tx1, ty1, tx2, ty2, 1);
  }

  // ---------- шапка ----------
  d.setTextSize(1);   // 7x13B
  const char* tt = ntpTime ? "C3 XLAMPER" : "NO NTP";
  d.setCursor(2, 2);
  d.print(tt);
  // индикатор облака
  if (cloudCatalogOk()) {
    d.drawCircle(122, 6, 3, 1);
    d.fillCircle(122, 6, 1, 1);
  } else {
    d.drawCircle(122, 6, 3, 1);
  }
  // полоска версии
  d.setCursor(60, 2);
  d.format("%d apps", cloudCount());

  // ---------- дата ----------
  d.setTextSize(1);
  d.setCursor(10, 46);
  if (ntpTime) {
    d.format("%s %02d.%02d.%d", WD[tmi.tm_wday], tmi.tm_mday, tmi.tm_mon + 1, tmi.tm_year + 1900);
  } else {
    d.print("press OK");
  }

  // ---------- бегущая строка ----------
  char tick[96];
  if (ntpTime)
    snprintf(tick, sizeof tick, "%02d:%02d:%02d  %02d.%02d.%d  C3 XLAMPER  ESP32-C3 160MHz  ",
             tmi.tm_hour, tmi.tm_min, tmi.tm_sec, tmi.tm_mday, tmi.tm_mon + 1, tmi.tm_year + 1900);
  else
    snprintf(tick, sizeof tick, "C3 XLAMPER " XLAMPER_VERSION "  ESP32-C3 RISC-V  OLED 128x64  STICK  CLOUD GAMES  ");
  int tw = d.strWidth(tick);
  uint32_t span = (uint32_t)(tw + W);
  int x = W - (int)((millis() / 50) % span) * 2;
  d.drawFastHLine(0, 53, W, 1);
  d.setClip(0, 54, 127, 64);
  d.drawStr(x, 61, tick);
  if (x + tw < W) d.drawStr(x + tw, 61, tick);
  d.clearClip();
  d.display();
}
