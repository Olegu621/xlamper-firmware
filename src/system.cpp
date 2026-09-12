// ==================================================================
//  system.cpp — ABOUT и SETTINGS (портированы с v0.11, фиксы:
//   * ABOUT честно показывает XLAMPER_VERSION (была зашита v0.11);
//   * factory reset чистит ВСЕ namespaces (см. settings.cpp);
//   * мёртвый appCube удалён — куб остался пасхалкой в ABOUT.
// ==================================================================
#include "system.h"
#include "config.h"
#include "settings.h"
#include "display.h"
#include "input.h"
#include "sound.h"

// ---------- 3D-куб (пасхалка ABOUT) ----------
static void miniCube(int mx, int my, uint32_t t) {
  float ax = t * 0.0011f, ay = t * 0.0007f;
  float cA = cos(ax), sA = sin(ax), cB = cos(ay), sB = sin(ay);
  int8_t vx[8], vy[8];
  const int8_t P[8][3] = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
  for (int i = 0; i < 8; i++) {
    float x = P[i][0], y = P[i][1], z = P[i][2];
    float ry = y * cA - z * sA, rz = y * sA + z * cA;
    float rx = x * cB + rz * sB;
    float per = 4.0f / (4.0f + (-x * sB + rz * cB));
    vx[i] = mx + rx * 5 * per;
    vy[i] = my + ry * 5 * per;
  }
  const uint8_t E[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
  for (int i = 0; i < 12; i++) d.drawLine(vx[E[i][0]], vy[E[i][0]], vx[E[i][1]], vy[E[i][1]]);
}

void appAbout() {
  int page = 0;
  static bool actH = false;
  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) { beep(400, 100); return; }
    if (e == EV_OK || e == EV_RIGHT) { page = (page + 1) % 7; beep(1200, 25); actH = false; }
    if (e == EV_LEFT)  { page = (page + 6) % 7; beep(1000, 25); actH = false; }

    d.clearDisplay();
    if (page == 0) {
      headerBar("ABOUT");
      d.setTextColor(1); d.setTextSize(1);
      d.setCursor(2, 16); d.print("C3 XLAMPER " XLAMPER_VERSION);
      d.setCursor(2, 28); d.print("ESP32-C3 RISC-V 160MHz");
      char s[26];
      snprintf(s, sizeof s, "RAM free: %luK", (unsigned long)(ESP.getFreeHeap() / 1024));
      d.setCursor(2, 40); d.print(s);
    } else if (page == 1) {
      headerBar("WIRING");
      d.setTextColor(1); d.setTextSize(1);
      d.setCursor(2, 16); d.print("OLED SDA=IO8 SCL=IO9");
      d.setCursor(2, 28); d.print("STICK X=IO1 Y=IO3");
      d.setCursor(2, 40); d.print("CLICK=IO10 SOUND=IO5");
    } else if (page == 2) {
      headerBar("STICK TEST");
      int x, y, cx, cy, lox, hix, loy, hiy;
      stickDiag(x, y, cx, cy, lox, hix, loy, hiy);
      d.setTextColor(1); d.setTextSize(1);
      d.setCursor(2, 16); d.format("X=%4d Y=%4d", x, y);
      d.setCursor(2, 28); d.format("c %d/%d  %d|%d", cx, cy, lox, hix);
      int px = map(x, lox, hix, 66, 126), py = map(y, loy, hiy, 42, 62);
      d.drawRect(64, 40, 64, 24, 1);
      d.fillRect(px - 2, py - 2, 5, 5, 1);
      d.drawFastHLine(px - 8, py, 5, 1); d.drawFastHLine(px + 4, py, 5, 1);
      d.drawFastVLine(px, py - 8, 5, 1); d.drawFastVLine(px, py + 4, 5, 1);
    } else if (page == 3) {
      headerBar("STICK ROTATION");
      d.setTextColor(1); d.setTextSize(1);
      d.setCursor(2, 16); d.print("stick angle:");
      d.setTextSize(3);
      d.setCursor(52, 28); d.format("%d", stickRot);
      d.setTextSize(1);
      d.setCursor(2, 52); d.print("click = rotate 90");
      if (e == EV_OK && !actH) {
        actH = true;
        stickRot = (stickRot + 90) % 360;
        saveRotation(stickRot, dispRot);
        beep(900 + stickRot, 60);
      }
    } else if (page == 4) {
      headerBar("DISPLAY ROT");
      d.setTextColor(1); d.setTextSize(1);
      d.setCursor(2, 16); d.print("screen orientation:");
      d.setTextSize(3);
      d.setCursor(52, 28); d.format("R%d", dispRot);
      d.setTextSize(1);
      d.setCursor(2, 52); d.print("click = rotate 90");
      if (e == EV_OK && !actH) {
        actH = true;
        // фикс v0.11: dispRot не обновлялся — поворот не переживал ребут
        dispRot = (dispRot + 1) % 4;
        d.setRotation(dispRot);
        saveRotation(stickRot, dispRot);
        beep(900 + dispRot * 100, 60);
      }
    } else if (page == 5) {
      static uint32_t t0 = 0;
      if (!t0) t0 = millis();
      d.clearDisplay();
      headerBar("EASTER EGG");
      miniCube(64, 36, millis() - t0);
      d.setTextColor(1); d.setTextSize(1);
      d.setCursor(4, 54); d.print("the good old cube");
    } else {
      headerBar("CREDITS");
      d.setTextColor(1); d.setTextSize(1);
      d.setCursor(2, 16); d.print("menu base: oled-MenuPro");
      d.setCursor(2, 28); d.print("(c) vipulx, github");
      d.setCursor(2, 40); d.print("build: pi agent :)");
      d.setCursor(2, 52); d.print("made for admin");
    }
    d.display();
  }
}

void appSettings() {
  int sel = 0;
  const int N_ITEMS = 4;
  const char* items[N_ITEMS] = { "Reset stick angle", "Reset snake record", "Forget WiFi network", "FACTORY RESET (wipe)" };
  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) { beep(400, 100); return; }
    if (e == EV_UP)   { sel = (sel + N_ITEMS - 1) % N_ITEMS; beep(900, 15); }
    if (e == EV_DOWN) { sel = (sel + 1) % N_ITEMS; beep(900, 15); }

    if (e == EV_OK) {
      if (sel == 0) {
        stickRot = 90;
        saveRotation(stickRot, dispRot);
        beep(900 + stickRot, 60);
        d.clearDisplay(); headerBar("SETTINGS");
        d.setTextColor(1); d.setTextSize(1);
        d.setCursor(4, 26); d.print("Stick angle -> 90");
        d.display(); delay(900);
      } else if (sel == 1) {
        snakeHiSave(0);
        beep(1200, 60);
        d.clearDisplay(); headerBar("SETTINGS");
        d.setTextColor(1); d.setTextSize(1);
        d.setCursor(4, 26); d.print("Snake record cleared");
        d.display(); delay(900);
      } else if (sel == 2) {
        wifiForget();
        beep(1200, 60);
        d.clearDisplay(); headerBar("SETTINGS");
        d.setTextColor(1); d.setTextSize(1);
        d.setCursor(4, 26); d.print("WiFi network forgotten");
        d.display(); delay(900);
      } else {
        bool sure = false;
        bool yesSel = true;
        while (true) {
          Ev e2 = pollEvent();
          if (e2 == EV_EXIT) { sure = false; break; }
          if (e2 == EV_LEFT || e2 == EV_RIGHT) { yesSel = !yesSel; beep(900, 15); }
          if (e2 == EV_OK) { sure = yesSel; break; }
          d.clearDisplay(); headerBar("FACTORY RESET");
          d.setTextColor(1); d.setTextSize(1);
          d.setCursor(4, 16); d.print("Erase ALL settings?");
          d.setCursor(4, 26); d.print("(rot, wifi, records,");
          d.setCursor(4, 34); d.print(" plugin scores)");
          if (yesSel) {
            d.fillRect(20, 40, 36, 14, 1); d.setTextColor(0);
            d.setTextSize(1); d.setCursor(27, 44); d.print("NO");
            d.drawRect(70, 40, 36, 14, 1); d.setTextColor(1);
            d.setCursor(77, 44); d.print("YES");
          } else {
            d.drawRect(20, 40, 36, 14, 1); d.setTextColor(1);
            d.setTextSize(1); d.setCursor(27, 44); d.print("NO");
            d.fillRect(70, 40, 36, 14, 1); d.setTextColor(0);
            d.setCursor(77, 44); d.print("YES");
          }
          d.display();
        }
        if (sure) {
          factoryReset();
          beepWait(1000, 120); beepWait(1500, 120); beepWait(2000, 200);
          d.clearDisplay(); headerBar("FACTORY RESET");
          d.setTextColor(1); d.setTextSize(1);
          d.setCursor(4, 24); d.print("All settings erased.");
          d.setCursor(4, 34); d.print("stick=90 screen=R0");
          d.setCursor(4, 44); d.print("Rebooting...");
          d.display(); delay(1200);
          ESP.restart();
        }
      }
    }

    d.clearDisplay();
    headerBar("SETTINGS");
    for (int i = 0; i < N_ITEMS; i++) {
      int y = 16 + i * 13;
      if (i == sel) { d.fillRect(0, y - 1, W, 12, 1); d.setTextColor(0); }
      else d.setTextColor(1);
      d.setTextSize(1); d.setCursor(4, y); d.print(items[i]);
    }
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(4, 55); d.print("hold=exit");
    d.display();
  }
}
