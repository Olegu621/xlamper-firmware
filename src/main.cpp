// ==================================================================
//  main.cpp — точка входа C3 XLAMPER v0.13-cloud.
//
//  Архитектура: прошивка = тонкая оболочка.
//   * boot: железо -> калибровка -> WiFi -> каталог -> NTP -> часы
//   * часы -> меню (облачный каталог + SYSTEM) -> приложение:
//     download -> xlaRun -> delete (SPIFFS всегда пуст)
//   * RESYNC (в SYSTEM) — обновить каталог+время вручную.
// ==================================================================
#include "config.h"
#include "settings.h"
#include "display.h"
#include "input.h"
#include "sound.h"
#include "apps.h"
#include "menu.h"
#include "cloud.h"
#include "clock.h"
#include <SPIFFS.h>
#include <WiFi.h>

// ---------- boot-анимация ----------
static void drawPerimeter(int n) {
  int drawn = 0;
  for (int x = 0; x < W && drawn < n; x++, drawn++) d.drawPixel(x, 0, 1);
  for (int y = 1; y < H && drawn < n; y++, drawn++) d.drawPixel(W - 1, y, 1);
  for (int x = W - 2; x >= 0 && drawn < n; x--, drawn++) d.drawPixel(x, H - 1, 1);
  for (int y = H - 2; y > 0 && drawn < n; y--, drawn++) d.drawPixel(0, y, 1);
}

static void bootAnimation() {
  int per = 2 * (W + H) - 4;
  uint32_t t0 = millis();
  while (millis() - t0 < 400) {
    d.clearDisplay();
    drawPerimeter((millis() - t0) * per / 400);
    d.display();
    delay(10);
  }
  beep(880, 40);

  t0 = millis();
  while (millis() - t0 < 320) {
    d.clearDisplay();
    drawPerimeter(per);
    int x = -30 + (millis() - t0) * 80 / 320;
    d.setTextColor(1); d.setTextSize(3);
    d.setCursor(x, 10); d.print("C3");
    d.display();
    delay(10);
  }
  beep(1100, 40);

  d.clearDisplay(); drawPerimeter(per);
  d.setTextColor(1); d.setTextSize(3); d.setCursor(40, 8); d.print("C3");
  d.setTextSize(1);
  const char* word = "XLAMPER";
  for (int i = 0; i < 7; i++) {
    d.setCursor(40 + i * 7, 36);
    d.print(word[i]);
    d.display();
    beep(900 + i * 90, 25);
    delay(55);
  }
  for (int x = 40; x <= 88; x += 3) { d.drawFastHLine(40, 50, x - 40 + 1, 1); d.display(); delay(8); }
  beep(1320, 120);
  delay(200);
  d.invertDisplay(true); delay(60); d.invertDisplay(false);
}

// ---------- boot: сеть -> каталог -> время ----------
static void bootCloudSync() {
  d.clearDisplay();
  headerBar("BOOT SYNC");
  d.setTextColor(1); d.setTextSize(1);
  d.setCursor(4, 18); d.print("wifi...");
  d.display();
  if (!cloudEnsureWifi(false)) {
    Serial.println("[boot] no wifi — offline mode");
    d.setCursor(4, 30); d.print("offline");
    d.display(); delay(800);
    return;
  }
  d.setCursor(4, 30); d.print("catalog...");
  d.display();
  cloudFetchCatalog(false);
  d.setCursor(4, 42); d.print("time...");
  d.display();
  bool tok = cloudSyncTime(false);
  char b[24];
  if (tok) {
    extern time_t ntpTime; extern uint32_t ntpGotAt;
    time_t tt = ntpTime + (millis() - ntpGotAt) / 1000;
    struct tm* ti = localtime(&tt);
    snprintf(b, sizeof b, "%02d:%02d  %d apps", ti->tm_hour, ti->tm_min, cloudCount());
  } else {
    snprintf(b, sizeof b, "no ntp, %d apps", cloudCount());
  }
  d.setCursor(4, 54); d.print(b);
  d.display();
  Serial.printf("[boot] sync done: %d apps, ntp=%d\n", cloudCount(), (int)tok);
  delay(600);
  // сеть больше не нужна — экономим питание до первого запуска приложения
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
}

void setup() {
  Serial.begin(115200);
  Serial.printf("\nC3 XLAMPER " XLAMPER_VERSION " cloud\n");
  pinMode(PIN_SW, INPUT_PULLUP);

  d.begin();
  soundBegin();
  settingsLoad();
  d.setRotation(dispRot);
  Serial.printf("stickRot=%d dispRot=%d\n", stickRot, dispRot);

  bootAnimation();

  // калибровка стика
  d.clearDisplay();
  d.setTextColor(1); d.setTextSize(1);
  d.setCursor(4, 10); d.print("STICK CALIBRATION");
  d.setCursor(4, 28); d.print("leave stick alone...");
  d.display();
  delay(300);
  stickCalibrate(false);
  d.setCursor(4, 42); d.print("now draw circles!");
  d.display();
  beep(1200, 60);
  uint32_t t1 = millis();
  while (millis() - t1 < 3000) {
    int x = readX(), y = readY();
    stickStretch(x, y);
    d.drawRect(56, 54, 16, 8, 1);
    d.fillRect(58, 56, (millis() - t1) * 12 / 3000, 4, 1);
    d.display();
    delay(15);
  }
  stickCalibrateGuard();
  beep(1600, 80);

  SPIFFS.begin(true);   // /t.xla — временный файл (удаляется после запуска)

  // ---- душа v0.13: boot-синхронизация ----
  bootCloudSync();
}

void loop() {
  Ev e = pollEvent();
  bgSongTick();
  static uint32_t lastClock = 0xFFFFFFFF;

  // ---- часы (главный экран) ----
  if (e == EV_NONE) {
    uint32_t clk = millis() / 60;
    if (clk != lastClock) { lastClock = clk; drawClock(); }
    return;
  }

  beep(1400, 30);
  flashInvert();

  while (true) {
    MenuPick pick;
    if (!menuRun(&pick)) break;            // таймаут -> часы

    if (pick.type == PICK_NONE) {
      // RESYNC: каталог + время
      cloudEnsureWifi(true);
      cloudFetchCatalog(true);
      cloudSyncTime(true);
      WiFi.disconnect(); WiFi.mode(WIFI_OFF);
      menuReset();
      continue;
    }

    if (pick.type == PICK_CLOUD) {
      // download -> run -> delete (бесконечное облако, нулевой кэш)
      cloudRunApp(pick.idx);
    } else if (pick.type == PICK_LOCAL) {
      const AppDef* app = appGet(pick.idx);
      if (app && app->fn) app->fn();
    }
    menuReset();
    beep(700, 20);
  }
  lastClock = 0xFFFFFFFF;
}
