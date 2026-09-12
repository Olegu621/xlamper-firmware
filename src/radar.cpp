// ==================================================================
//  radar.cpp — WIFI RADAR: радар с точками-сетями, роутером и
//  хлампером; постоянное обновление (свип + повторный скан).
//
//  Как это работает:
//   * WiFi.scanNetworks(async) — неблокирующий рескан каждые ~12 с;
//   * каждый AP = точка: угол = hash(BSSID) (стабилен между сканами),
//     радиус = мощность (RSSI ближе -> ближе к центру);
//   * XLAMPER — точка в центре; «роутер» (подключённая или
//     сохранённая сеть) — обведён рамкой с антенной;
//   * луч свипа вращается, точки вспыхивают при проходе луча.
//
//  Самокритика v1 (поправлено):
//   * след луча рисовался цветом 0 и СТИРАЛ кольца радара ->
//     теперь хвост — точки цветом 1 без erase;
//   * выделение роутера сравнивало с WiFi.SSID() (пусто без
//     подключения) -> теперь сверяемся и с сохранённым SSID.
// ==================================================================
#include "radar.h"
#include "config.h"
#include "settings.h"
#include "display.h"
#include "input.h"
#include "sound.h"
#include "net.h"
#include <WiFi.h>

// стабильный угол по BSSID (одинаковый между сканами)
static float bssidAngle(const uint8_t* bssid) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < 6; i++) { h ^= bssid[i]; h *= 16777619u; }
  return (float)((h % 3600) / 10.0f);
}

void appRadar() {
  const int CX = 64, CY = 38, RMAX = 26;
  bool scanning = false;
  uint32_t lastScan = 0;
  uint32_t lastFrame = 0;

  // «Роутер» = подключённая ИЛИ сохранённая сеть
  char savedSsid[33], savedPass[65];
  bool hasSaved = wifiLoad(savedSsid, sizeof savedSsid, savedPass, sizeof savedPass);

  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  WiFi.scanNetworks(true);          // первый асинхронный скан
  scanning = true;
  lastScan = millis();
  beep(1200, 40);

  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) { beep(400, 100); break; }
    if (e == EV_OK) {                // ручной рескан
      WiFi.scanDelete();
      WiFi.scanNetworks(true);
      scanning = true;
      lastScan = millis();
      beep(1200, 40);
    }

    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
      scanning = true;
    } else {
      if (scanning) { beep(1600, 30); scanning = false; }
      if (n >= 0 && millis() - lastScan > 12000) {   // авто-рескан 12с
        WiFi.scanDelete();
        WiFi.scanNetworks(true);
        scanning = true;
        lastScan = millis();
      }
    }

    if (millis() - lastFrame >= 33) {   // ~30 fps
      lastFrame = millis();

      // ---- кадр ----
      d.clearDisplay();
      headerBar(scanning ? "RADAR *scan*" : "WIFI RADAR");

      // кольца радара
      d.drawCircle(CX, CY, RMAX, 1);
      d.drawCircle(CX, CY, RMAX * 2 / 3, 1);
      d.drawCircle(CX, CY, RMAX / 3, 1);
      d.drawFastVLine(CX, CY - RMAX, RMAX * 2, 1);
      d.drawFastHLine(CX - RMAX, CY, RMAX * 2, 1);

      // свип: остриё сплошной, хвост — редкие точки (только цвет 1!)
      float sweep = (millis() % 2400) / 2400.0f * 2.0f * PI;
      for (int r = 3; r < RMAX; r += 2)
        d.drawPixel(CX + r * cos(sweep), CY + r * sin(sweep), 1);
      for (int k = 1; k <= 4; k++) {
        float a = sweep - k * 0.09f;
        if (a < 0) a += 2.0f * PI;
        for (int r = 4; r < RMAX; r += 4)
          d.drawPixel(CX + r * cos(a), CY + r * sin(a), 1);
      }

      // XLAMPER в центре
      d.fillCircle(CX, CY, 3, 1);
      d.drawPixel(CX - 1, CY - 1, 0);   // глаза
      d.drawPixel(CX + 1, CY - 1, 0);

      // точки-сети
      String connSsid = WiFi.SSID();     // непусто, если подключены
      if (n > 0) {
        int shown = 0;
        for (int i = 0; i < n && shown < 12; i++) {
          String ssid = WiFi.SSID(i);
          if (!ssid.length()) continue;
          uint8_t* bssid = WiFi.BSSID(i);
          int rssi = WiFi.RSSI(i);
          if (rssi == 0) continue;
          int rr = map(rssi, -90, -30, RMAX - 2, RMAX / 4);
          if (rr < RMAX / 4) rr = RMAX / 4;
          float ang = bssidAngle(bssid) * PI / 180.0f;
          int px = CX + rr * cos(ang);
          int py = CY + rr * sin(ang);
          // вспышка при проходе луча
          float da = fabsf(ang - sweep);
          if (da > PI) da = 2.0f * PI - da;
          bool hit = da < 0.35f;
          if (hit) d.fillCircle(px, py, 2, 1);
          else     d.drawCircle(px, py, 1, 1);
          // «роутер»: подключённая или сохранённая сеть — рамка+антенна
          bool isRouter = (ssid == connSsid) || (hasSaved && ssid == savedSsid);
          if (isRouter) {
            d.drawRect(px - 3, py - 4, 7, 7, 1);
            d.drawFastVLine(px, py - 6, 2, 1);
          }
          shown++;
        }
      }

      // статус-строка
      d.setTextColor(1); d.setTextSize(1);
      char st[24];
      if (n > 0) snprintf(st, sizeof st, "%d AP", n);
      else       snprintf(st, sizeof st, "no AP");
      d.setCursor(4, 55); d.print(st);
      d.setCursor(46, 55); d.print("click=rescan");
      d.display();
    }
    delay(1);
  }

  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
}
