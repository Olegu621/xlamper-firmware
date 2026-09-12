// ==================================================================
//  input.cpp — стик + кнопка: калибровка, события, 8-way направление.
//  Чинит баг v0.11: рекурсия pollEvent() при диагоналях не исправлена
//  тут (её не было), но устранена слабая зона: readX/readY читали ADC
//  по два раза на такт — теперь один раз на вызов pollEvent.
// ==================================================================
#include "input.h"
#include "config.h"
#include "settings.h"
#include "sound.h"

// ---------- калибровка ----------
static int cX = 2048, cY = 2048;
static int loX = 1148, hiX = 2948, loY = 1148, hiY = 2948;

static inline int rawX() { return analogRead(PIN_VRX); }
static inline int rawY() { return analogRead(PIN_VRY); }

int readX() {
  switch (stickRot) {
    case 0:   return rawX();
    case 90:  return rawY();
    case 180: return 4095 - rawX();
    default:  return 4095 - rawY();
  }
}
int readY() {
  switch (stickRot) {
    case 0:   return rawY();
    case 90:  return 4095 - rawX();
    case 180: return 4095 - rawY();
    default:  return rawX();
  }
}

// ---------- события ----------
#define HOLD_MS 600
static bool swHeld = false, exitFired = false;
static uint32_t swDownAt = 0;
static int8_t lastDir = 0;
static uint32_t repAt = 0, firstAt = 0;

int holdProgress() {
  if (!swHeld || exitFired) return 0;
  uint32_t dt = millis() - swDownAt;
  return dt >= HOLD_MS ? 100 : dt * 100 / HOLD_MS;
}

static int8_t axX() {
  int v = readX();
  int tL = cX - (cX - loX) / 2, tR = cX + (hiX - cX) / 2;
  if (tL >= cX - 80) tL = cX - 80;
  if (tR <= cX + 80) tR = cX + 80;
  return v > tR ? 1 : (v < tL ? -1 : 0);
}
static int8_t axY() {
  int v = readY();
  int tL = cY - (cY - loY) / 2, tR = cY + (hiY - cY) / 2;
  if (tL >= cY - 80) tL = cY - 80;
  if (tR <= cY + 80) tR = cY + 80;
  return v > tR ? 1 : (v < tL ? -1 : 0);
}

// 8-way для XLA VM: -1 покой, 0 вверх, 2 вправо, 4 вниз, 6 влево, диагонали между
int8_t stick8() {
  int8_t dx = axX(), dy = axY();
  if (dx == 0 && dy == 0) return -1;
  if (dy == -1) return (dx == 0) ? 0 : ((dx == 1) ? 1 : 7);
  if (dy == 0)  return (dx == 1) ? 2 : 6;
  return (dx == 0) ? 4 : ((dx == 1) ? 3 : 5);
}

Ev pollEvent() {
  beepTick();
  uint32_t now = millis();
  bool sw = !digitalRead(PIN_SW);
  if (sw && !swHeld) { swHeld = true; swDownAt = now; exitFired = false; }
  if (sw && swHeld && !exitFired && now - swDownAt >= HOLD_MS) {
    exitFired = true; beep(500, 150); return EV_EXIT;
  }
  if (!sw && swHeld) {
    if (!exitFired && now - swDownAt > 35) { swHeld = false; swDownAt = 0; return EV_OK; }
    swHeld = false; swDownAt = 0;
  }
  int8_t dx = axX(), dy = axY();
  int8_t dir = 0;
  if      (dy == -1) dir = EV_UP;
  else if (dy ==  1) dir = EV_DOWN;
  else if (dx == -1) dir = EV_LEFT;
  else if (dx ==  1) dir = EV_RIGHT;
  if (dir) {
    if (dir != lastDir) { lastDir = dir; firstAt = now; repAt = now; return (Ev)dir; }
    if (now - firstAt > 340 && now - repAt > 110) { repAt = now; return (Ev)dir; }
  } else lastDir = 0;
  return EV_NONE;
}

// ---------- калибровка при старте (двухфазная, как в v0.11) ----------
// stickCalibrate(false): фаза 1 — центр (покой), сброс размаха.
void stickCalibrate(bool interactive) {
  (void)interactive;   // размах теперь отдельно — stickStretch()/stickCalibrateGuard()
  int sx = 0, sy = 0, n = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 700) { sx += readX(); sy += readY(); n++; delay(3); }
  cX = n ? sx / n : 2048;
  cY = n ? sy / n : 2048;
  loX = hiX = cX; loY = hiY = cY;
}

// stickStretch(x,y): фаза 2 — инкрементально расширять границы размаха.
void stickStretch(int x, int y) {
  if (x < loX) loX = x;
  if (x > hiX) hiX = x;
  if (y < loY) loY = y;
  if (y > hiY) hiY = y;
}

// stickCalibrateGuard(): минимальные границы для дрожащего/отсутствующего стика.
void stickCalibrateGuard() {
  if (hiX - loX < 300) { loX = (cX > 900) ? cX - 900 : 0; hiX = (cX < 3195) ? cX + 900 : 4095; }
  if (hiY - loY < 300) { loY = (cY > 900) ? cY - 900 : 0; hiY = (cY < 3195) ? cY + 900 : 4095; }
  Serial.printf("[stick] calib c=%d/%d x[%d..%d] y[%d..%d]\n", cX, cY, loX, hiX, loY, hiY);
}

// для ABOUT: диагностика стика
void stickDiag(int& x, int& y, int& cxOut, int& cyOut, int& lox, int& hix, int& loy, int& hiy) {
  x = readX(); y = readY();
  cxOut = cX; cyOut = cY; lox = loX; hix = hiX; loy = loY; hiy = hiY;
}
