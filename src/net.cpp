// ==================================================================
//  net.cpp — Wi-Fi: подключение с анимацией, скан, клавиатура, NTP.
//  Порт v0.11 с исправлениями:
//   * appNet(): вместо goto-прыжков — явные фазы; рекурсивных
//     рестартов нет (appWifi больше не зовёт сам себя);
//   * tx-лестница мощности SuperMini сохранена (лечит reason 2/3);
//   * PS off как в v0.11.
// ==================================================================
#include "net.h"
#include "config.h"
#include "settings.h"
#include "display.h"
#include "input.h"
#include "sound.h"
#include <WiFi.h>
#include <esp_wifi.h>
#include <time.h>

static const char* NTP1 = "ntp.msn.ru";
static const char* NTP2 = "pool.ntp.org";
time_t ntpTime = 0;
uint32_t ntpGotAt = 0;

static void ntpConfig() { configTime(3 * 3600, 0, NTP1, NTP2); }

// ---------- события Wi-Fi (причина отказа) ----------
static volatile int wifiFailReason = 0;
static bool evRegistered = false;

static void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  switch (event) {
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: wifiFailReason = info.wifi_sta_disconnected.reason; break;
    case ARDUINO_EVENT_WIFI_STA_AUTHMODE_CHANGE:
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:    wifiFailReason = 0; break;
    default: break;
  }
}
const char* wifiFailText(int reason) {
  switch (reason) {
    case 201: return "AUTH FAILED (201)";
    case 202: return "AP NOT FOUND (202)";
    case 203: return "ASSOC FAIL (203)";
    case 204: return "HANDSHAKE TMO (204)";
    case 15:  return "4WAY HANDSHAKE TMO";
    case 2:   return "AUTH EXPIRED";
    default: { static char buf[20]; snprintf(buf, sizeof buf, "REASON %d", reason); return buf; }
  }
}

// ---------- анимация подключения ----------
static void connectingAnim(const char* ssid, uint32_t t) {
  d.clearDisplay();
  headerBar("CONNECTING");
  d.setTextColor(1); d.setTextSize(1);
  char s[24];
  strncpy(s, ssid, 20); s[20] = 0;
  d.setCursor(4, 16); d.print("net:"); d.print(s);
  for (int k = 0; k < 3; k++) {
    bool on = ((t / 160) % 4) > k;
    if (!on) continue;
    for (int a = 215; a <= 325; a += 4) {
      float r = 7 + k * 8, rad = a * PI / 180.0;
      d.drawPixel(64 + r * cos(rad), 44 + r * sin(rad), 1);
    }
  }
  d.fillCircle(64, 42, 2, 1);
  footerBar("hold=cancel");
  d.display();
}

bool wifiConnectAnimated(const char* ssid, const char* pass, uint32_t timeoutMs) {
  Serial.printf("[wifi] connecting to '%s'...\n", ssid);
  wifiFailReason = 0;
  if (!evRegistered) { WiFi.onEvent(onWifiEvent); evRegistered = true; }
  WiFi.mode(WIFI_STA);
  WiFi.setMinSecurity(WIFI_AUTH_WPA_PSK);
  WiFi.disconnect(true);
  delay(80);

  // ДЕФЕКТ SUPERMINI: отражения в антенной цепи ломают handshake при
  // полной мощности. Лечение: лестница TX-мощности (см. v0.11).
  static const wifi_power_t txSteps[] = {
    WIFI_POWER_8_5dBm, WIFI_POWER_5dBm, WIFI_POWER_11dBm, WIFI_POWER_19_5dBm
  };

  for (int stage = 0; stage < 4; stage++) {
    WiFi.setTxPower(txSteps[stage]);
    wifi_config_t conf;
    memset(&conf, 0, sizeof(conf));
    conf.sta.pmf_cfg.capable = false;
    conf.sta.pmf_cfg.required = false;
    esp_wifi_set_config(WIFI_IF_STA, &conf);
    wifiFailReason = 0;
    Serial.printf("[wifi] attempt %d (tx %d, pmf off)\n", stage + 1, (int)txSteps[stage]);
    WiFi.begin(ssid, pass);

    uint32_t t0 = millis();
    uint32_t budget = timeoutMs / 4;
    bool cancelled = false;
    while (WiFi.status() != WL_CONNECTED) {
      connectingAnim(ssid, millis());
      if (millis() - t0 > budget) break;
      if (pollEvent() == EV_EXIT) { cancelled = true; break; }
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("[wifi] CONNECTED ip=%s (tx=%d)\n", WiFi.localIP().toString().c_str(), (int)txSteps[stage]);
      beep(1300, 60); delay(30); beep(1700, 80);
      ntpConfig();
      return true;
    }
    WiFi.disconnect();
    if (cancelled) { Serial.println("[wifi] cancelled by user"); beep(400, 100); return false; }
    Serial.printf("[wifi] attempt %d failed status=%d reason=%d\n", stage + 1, WiFi.status(), wifiFailReason);
    if (stage < 3) { beepWait(200, 150); delay(150); }
  }
  Serial.printf("[wifi] FAIL after %lums reason=%d\n", (unsigned long)timeoutMs, wifiFailReason);
  beepWait(200, 300);
  return false;
}

// ---------- клавиатура ----------
static const char* KB_L[3] = { "abcdefghij", "klmnopqrst", "uvwxyz.-_+" };
static const char* KB_U[3] = { "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ,!?#" };
static const char* KB_D[3] = { "0123456789", "@#$%&*()-=", ":;<>/\\'\"" };
static uint32_t kbLastTypeAt = 0;

static void drawKey(int x, int y, int w, const char* label, bool cur) {
  if (cur) { d.fillRect(x, y, w, 10, 1); d.setTextColor(0); }
  else     { d.drawRect(x, y, w, 10, 1); d.setTextColor(1); }
  d.setFontSmall();
  int lw = d.strWidth(label);
  d.drawStr(x + (w - lw) / 2, y + 1, label);
}

static void drawKeyboard(int row, int col, int page, const char* typed) {
  d.clearDisplay();
  headerBar(page == 0 ? "PASSWORD abc" : page == 1 ? "PASSWORD ABC" : "PASSWORD 123");

  d.setFontSmall();
  d.setTextColor(1);
  d.drawRect(2, 14, 124, 11, 1);
  int tl = strlen(typed);
  int show = tl > 20 ? 20 : tl;
  int xx = 5;
  bool showLast = tl > 0 && (millis() - kbLastTypeAt) < 1500;
  d.setCursor(xx, 16);
  for (int i = 0; i < show; i++) {
    if (i == show - 1 && showLast) d.drawStr(xx + i * 6, 16, &typed[tl - 1]);
    else d.print('*');
  }
  if ((millis() / 400) % 2) d.fillRect(5 + show * 6, 16, 5, 7, 1);
  char cnt[12]; snprintf(cnt, sizeof cnt, "%d", tl);
  int cw = d.strWidth(cnt);
  d.drawStr(122 - cw, 16, cnt);

  const char** rows = page == 0 ? KB_L : page == 1 ? KB_U : KB_D;
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 10; c++) {
      char key[2] = { rows[r][c], 0 };
      drawKey(3 + c * 12, 26 + r * 10, 11, key, row == r && col == c);
    }
  const char* nextLbl = page == 0 ? "ABC" : page == 1 ? "123" : "abc";
  drawKey(3,  56, 22, nextLbl, (row == 3 && col < 2));
  drawKey(27, 56, 40, "SPACE", (row == 3 && col >= 2 && col < 6));
  drawKey(69, 56, 26, "<-",    (row == 3 && col >= 6 && col < 8));
  drawKey(97, 56, 28, "OK",    (row == 3 && col >= 8));
  d.display();
}

void inputPassword(char* out, int maxLen) {
  int row = 0, col = 0, page = 0, len = 0;
  out[0] = 0;
  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) { out[0] = 0; return; }
    if (e == EV_UP   && row > 0) { row--; beep(900, 12); }
    if (e == EV_DOWN && row < 3) { row++; beep(900, 12); }
    if (e == EV_LEFT  && col > 0) { col--; beep(900, 12); }
    if (e == EV_RIGHT && col < 9) { col++; beep(900, 12); }

    drawKeyboard(row, col, page, out);

    if (e == EV_OK) {
      if (row < 3) {
        const char** rows = page == 0 ? KB_L : page == 1 ? KB_U : page == 2 ? KB_D : KB_L;
        if (len < maxLen - 1) { out[len++] = rows[row][col]; out[len] = 0; beep(1100, 20); kbLastTypeAt = millis(); }
        else beep(200, 60);
      } else if (col < 2) { page = (page + 1) % 3; beep(900, 30); }
      else if (col < 6) { if (len < maxLen - 1) { out[len++] = ' '; out[len] = 0; beep(1100, 20); kbLastTypeAt = millis(); } }
      else if (col < 8) { if (len > 0) { len--; out[len] = 0; beep(700, 25); kbLastTypeAt = millis(); } }
      else { beep(1500, 50); return; }
    }
  }
}

// ---------- экраны выбора/ошибки ----------
static void showWrongPassword(const char* ssid) {
  beepWait(300, 250); beepWait(220, 350);
  d.clearDisplay();
  headerBar("ERROR");
  d.setTextColor(1); d.setTextSize(1);
  d.setCursor(4, 18); d.print("WRONG PASSWORD!");
  d.setCursor(4, 32); d.print("net:"); d.print(ssid);
  d.setCursor(4, 44); d.print("removed from db");
  d.display();
  wifiForget();
  delay(1600);
}

static int askRetryForgetExit() {
  int sel = 0;
  const char* items[4] = { "RETRY same password", "RE-ENTER password", "FORGET network", "EXIT to menu" };
  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) return 3;
    if (e == EV_UP   && sel > 0) { sel--; beep(900, 15); }
    if (e == EV_DOWN && sel < 3) { sel++; beep(900, 15); }
    if (e == EV_OK) { beep(1200, 40); return sel; }
    d.clearDisplay();
    headerBar("CONNECT FAILED");
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(4, 16); d.print("Cannot connect.");
    d.setCursor(4, 25); d.print("AP says:");
    d.setCursor(4, 34); d.print(wifiFailReason ? wifiFailText(wifiFailReason) : "TIMEOUT / no reply");
    for (int i = 0; i < 4; i++) {
      int y = 42 + i * 7;
      if (y > 62) break;
      if (i == sel) { d.fillRect(0, y - 1, W, 8, 1); d.setTextColor(0); }
      else d.setTextColor(1);
      d.setFontTiny(); d.setCursor(4, y); d.print(items[i]);
      d.setTextSize(1);
    }
    d.display();
  }
}

// экран списка сетей; true = выбрана (selOut), false = выход
static bool netListScreen(int& selOut, int n) {
  int sel = 0, top = 0;
  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) return false;
    if (e == EV_UP   && sel > 0)     { sel--; if (sel < top) top = sel; beep(900, 15); }
    if (e == EV_DOWN && sel < n - 1) { sel++; if (sel > top + 3) top = sel - 3; beep(900, 15); }
    if (e == EV_OK) { selOut = sel; return true; }

    d.clearDisplay();
    char hb[18]; snprintf(hb, sizeof hb, "NETS: %d", n);
    headerBar(hb);
    for (int i = 0; i < 4 && top + i < n; i++) {
      int idx = top + i, y = 16 + i * 10;
      if (idx == sel) { d.fillRect(0, y - 1, W, 11, 1); d.setTextColor(0); }
      else d.setTextColor(1);
      String ssid = WiFi.SSID(idx); if (ssid.length() > 10) ssid = ssid.substring(0, 10);
      d.setTextSize(1); d.setCursor(2, y); d.print(ssid);
      int rssi = WiFi.RSSI(idx);
      int bars = rssi > -55 ? 4 : rssi > -67 ? 3 : rssi > -78 ? 2 : rssi > -88 ? 1 : 0;
      for (int b = 0; b < 4; b++) {
        int bh = 2 + b * 2, bx = 78 + b * 5, by = y + 9 - bh;
        if (b < bars) d.fillRect(bx, by, 3, bh, idx == sel ? 0 : 1);
        else d.drawRect(bx, y + 7, 3, 2, idx == sel ? 0 : 1);
      }
      d.setCursor(100, y); d.print(WiFi.encryptionType(idx) == WIFI_AUTH_OPEN ? "open" : "lock");
    }
    footerBar("click=select");
    d.display();
  }
}

// ---------- скан (общий для appWifi и appNet) ----------
int wifiScanAsync() {
  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  int n = WIFI_SCAN_RUNNING;
  while (n == WIFI_SCAN_RUNNING) {
    if (pollEvent() == EV_EXIT) { WiFi.scanDelete(); return -1; }
    d.clearDisplay(); headerBar("SCAN");
    int rx = 64, ry = 34, R = 16;
    d.drawCircle(rx, ry, R, 1);
    d.drawCircle(rx, ry, R * 2 / 3, 1);
    d.drawCircle(rx, ry, R / 3, 1);
    float a = (millis() % 1800) * 2.0 * PI / 1800.0;
    for (int r = 0; r <= R; r += 3) d.drawPixel(rx + r * cos(a), ry + r * sin(a), 1);
    d.fillCircle(rx, ry, 2, 1);
    footerBar("scanning...");
    d.display();
    n = WiFi.scanComplete();
    delay(30);
  }
  return n;
}

// ---------- приложения ----------
void appWifi() {
  int n = wifiScanAsync();
  if (n > 0) {
    int sel;
    if (netListScreen(sel, n)) {
      // выбранная сеть -> сохранить как целевую (NET сделает остальное)
      String s = WiFi.SSID(sel);
      char ssid[33]; strncpy(ssid, s.c_str(), 32); ssid[32] = 0;
      char pass[65] = "";
      if (WiFi.encryptionType(sel) != WIFI_AUTH_OPEN) inputPassword(pass, 64);
      if (WiFi.encryptionType(sel) == WIFI_AUTH_OPEN || pass[0]) wifiSave(ssid, pass);
    }
  } else {
    d.clearDisplay(); headerBar("WIFI SCAN");
    d.setTextColor(1); d.setCursor(10, 30); d.print("No networks found");
    d.display(); beepWait(250, 400); delay(900);
  }
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
}

void appNet() {
  char ssid[33], pass[65];
  if (wifiLoad(ssid, sizeof ssid, pass, sizeof pass)) {
    Serial.printf("[net] saved: ssid='%s'\n", ssid);
    while (true) {
      if (wifiConnectAnimated(ssid, pass, 15000)) break;
      int ch = askRetryForgetExit();
      if (ch == 1) {
        inputPassword(pass, 64);
        if (!pass[0]) { WiFi.mode(WIFI_OFF); return; }
        wifiSave(ssid, pass);
      } else if (ch == 2) { showWrongPassword(ssid); WiFi.mode(WIFI_OFF); return; }
      else { WiFi.mode(WIFI_OFF); return; }   // EXIT или RETRY
    }
  } else {
    // нет сохранённых: скан -> выбор -> пароль
    int n = wifiScanAsync();
    if (n <= 0) {
      d.clearDisplay(); headerBar("NET");
      d.setTextColor(1); d.setCursor(10, 30); d.print("No networks found");
      d.display(); beepWait(250, 400); delay(900);
      WiFi.mode(WIFI_OFF);
      return;
    }
    int sel;
    if (!netListScreen(sel, n)) { WiFi.scanDelete(); WiFi.mode(WIFI_OFF); return; }
    String s = WiFi.SSID(sel);
    strncpy(ssid, s.c_str(), 32); ssid[32] = 0;
    pass[0] = 0;
    bool openNet = WiFi.encryptionType(sel) == WIFI_AUTH_OPEN;
    if (!openNet) inputPassword(pass, 64);
    if (!openNet && !pass[0]) { WiFi.scanDelete(); WiFi.mode(WIFI_OFF); return; }
    wifiSave(ssid, pass);
    Serial.printf("[net] new: ssid='%s'\n", ssid);
    while (true) {
      if (wifiConnectAnimated(ssid, pass, 15000)) break;
      int ch = askRetryForgetExit();
      if (ch == 3) { WiFi.mode(WIFI_OFF); return; }
      if (ch == 2) { showWrongPassword(ssid); WiFi.mode(WIFI_OFF); return; }
      if (ch == 1) {
        inputPassword(pass, 64);
        if (!pass[0]) { WiFi.mode(WIFI_OFF); return; }
        wifiSave(ssid, pass);
      }
    }
  }

  // NTP-фаза
  d.clearDisplay(); headerBar("NET: NTP SYNC");
  d.setTextColor(1); d.setTextSize(1);
  d.setCursor(6, 24); d.print("NTP sync...");
  d.display();
  ntpConfig();
  uint32_t t0 = millis();
  bool ok = false;
  while (millis() - t0 < 10000) {
    if (time(nullptr) > 100000) { ok = true; break; }
    if (pollEvent() == EV_EXIT) break;
    d.fillRect(70, 24, 30, 10, 0);
    d.setCursor(70, 24);
    for (int i = 0; i < (millis() / 300) % 4; i++) d.print('.');
    d.display();
    delay(50);
  }
  if (ok) {
    ntpTime = time(nullptr);
    ntpGotAt = millis();
    struct tm* ti = localtime(&ntpTime);
    beepWait(880, 80); beepWait(1320, 120);
    d.clearDisplay(); headerBar("TIME SYNCED");
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(10, 18); d.print("Internet time is set");
    d.setTextSize(3);
    d.setCursor(28, 30); d.format("%02d:%02d", ti->tm_hour, ti->tm_min);
    d.setTextSize(1);
    d.setCursor(30, 52); d.format("%02d.%02d.%d", ti->tm_mday, ti->tm_mon + 1, ti->tm_year + 1900);
    d.display();
    delay(1500);
  } else {
    d.clearDisplay(); headerBar("NET");
    d.setTextColor(1); d.setCursor(6, 26); d.print("NTP failed :(");
    d.setCursor(6, 40); d.print("Check internet");
    d.display(); beepWait(250, 400); delay(1000);
  }
  WiFi.disconnect();
  WiFi.mode(WIFI_OFF);
}

bool netOnline() { return WiFi.status() == WL_CONNECTED; }
