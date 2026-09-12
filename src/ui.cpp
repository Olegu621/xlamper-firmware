// ==================================================================
//  ui.cpp — анимационный UI-фреймворк (см. ui.h).
//  Все анимации неблокирующие: каждый кадр опрашивает pollEvent().
// ==================================================================
#include "ui.h"
#include "config.h"
#include "display.h"
#include "input.h"
#include "sound.h"

// ---------- easing ----------
static inline float easeOutCubic(float t) { float u = 1.0f - t; return 1.0f - u * u * u; }

// Снимок «старого» экрана для SLIDE.
// Раскладка U8g2 full-buffer: тайлы 8×8 пикселей; каждый тайл — 8 байт
// (байт = строка тайла, бит = колонка, LSB слева). Итого 1024 Б на 128x64.
static uint8_t snapBuf[128 * 64 / 8];
static int snapTileW = 16;   // тайлов в ширину (обновляется при снимке)

// Пиксель из СНИМКА (не из живого буфера!)
static bool snapPx(int x, int y) {
  if (x < 0 || x >= W || y < 0 || y >= H) return false;
  int addr = ((y / 8) * snapTileW + (x / 8)) * 8 + (y % 8);
  return (snapBuf[addr] & (1 << (x % 8))) != 0;
}

// ---------- переходы ----------
bool uiTransition(LaunchStyle style, void (*drawer)(void*), void* ctx) {
  const int FRAMES = 10;
  const uint32_t FRAME_MS = 16;

  // Снимок текущего экрана ДО смены: полный кадр одним memcpy.
  {
    snapTileW = d.getBufferTileWidth();
    size_t total = (size_t)snapTileW * 8 * (H / 8);
    memcpy(snapBuf, d.getBufferPtr(), total);
  }

  for (int f = 0; f <= FRAMES; f++) {
    uint32_t t0 = millis();
    float t = easeOutCubic((float)f / FRAMES);

    drawer(ctx);                      // новый экран в буфер

    switch (style) {
      case LaunchStyle::SLIDE: {
        // Композит: старый экран «уезжает» влево, справа открывается новый.
        // Копируем пиксель снимка ЛЮБОГО цвета (1 и 0), иначе на полосе
        // останется «грязь» от нового кадра (v0.12 найдено самокритикой).
        int shift = (int)(t * W);
        if (shift < W) {
          for (int x = shift; x < W; x++)
            for (int y = 0; y < H; y++)
              d.drawPixel(x, y, snapPx(x - shift, y) ? 1 : 0);
        }
        break;
      }
      case LaunchStyle::WIPE: {
        int wpx = (int)(t * W);
        if (wpx < W) {
          // «шторка» уходит вправо, открывая новый экран
          for (int x = wpx; x < W; x += 4)
            for (int y = 0; y < H; y += 4)
              d.drawPixel(x + (y / 4 % 2) * 2, y, 1);
        }
        break;
      }
      case LaunchStyle::ZOOM: {
        int bw = (int)((1.0f - t) * 64);   // инверсная рамка сжимается
        if (bw > 0) {
          d.fillRect(0, 0, W, bw, 1);
          d.fillRect(0, H - bw, W, bw, 1);
          d.fillRect(0, 0, bw, H, 1);
          d.fillRect(W - bw, 0, bw, H, 1);
        }
        break;
      }
      case LaunchStyle::FADE: {
        // dissolve: дизеринг-шум, плотность падает с t
        int dens = (int)((1.0f - t) * 100);
        for (int y = 0; y < H; y += 2)
          for (int x = 0; x < W; x += 2)
            if (((x * 7 + y * 13 + f * 29) % 100) < dens)
              d.drawPixel(x, y, 1);
        break;
      }
    }

    d.display();
    if (pollEvent() == EV_EXIT) return false;   // анимация отменяема
    uint32_t dt = millis() - t0;
    if (dt < FRAME_MS) delay(FRAME_MS - dt);
  }
  return true;
}

// ---------- уведомление (toast) ----------
void uiToast(const char* line1, const char* line2, uint16_t ms) {
  uint32_t t0 = millis();
  int w = 100, h = 34, x = (W - w) / 2, y = (H - h) / 2;
  while (millis() - t0 < ms) {
    pollEvent();
    d.fillRect(x, y, w, h, 1);
    d.setTextColor(0); d.setTextSize(1);
    int tw1 = d.strWidth(line1);
    d.drawStr(x + (w - tw1) / 2, y + 12, line1);
    if (line2 && line2[0]) {
      int tw2 = d.strWidth(line2);
      d.drawStr(x + (w - tw2) / 2, y + 26, line2);
    }
    d.display();
    delay(16);
  }
}

// ---------- спиннер ----------
bool uiSpinner(const char* title, bool (*work)()) {
  uint32_t t0 = millis();
  while (true) {
    if (work && work()) return true;
    if (pollEvent() == EV_EXIT) return false;
    d.clearDisplay();
    headerBar(title);
    int ph = (millis() / 120) % 4;
    for (int i = 0; i < 4; i++) {
      int r = (i == ph) ? 3 : 2;
      d.drawCircle(58 + i * 8, 34, r, 1);
    }
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(4, 50); d.print("hold=cancel");
    d.display();
    delay(16);
    if (millis() - t0 > 120000) return false;   // страховка 2 мин
  }
}

// ---------- прогресс ----------
void uiProgress(int x, int y, int w, int h, int pct) {
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  d.drawRect(x, y, w, h, 1);
  int iw = (w - 2) * pct / 100;
  if (iw > 0) d.fillRect(x + 1, y + 1, iw, h - 2, 1);
}
