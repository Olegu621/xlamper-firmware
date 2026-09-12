// ==================================================================
//  cloud.cpp — v0.13: ВСЯ облачная логика оболочки.
//  Boot: WiFi -> каталог -> NTP. Запуск приложения:
//  download -> xlaRun -> delete (ничего не хранится).
// ==================================================================
#include "cloud.h"
#include "config.h"
#include "settings.h"
#include "display.h"
#include "input.h"
#include "sound.h"
#include "net.h"        // netOnline, wifiConnectAnimated, ntpTime/ntpGotAt
#include "xla_vm.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <time.h>

// ---------- каталог (кэш в RAM) ----------
struct CloudApp { char file[16]; char title[16]; char cat[10]; };
static CloudApp catalog[16];
static int nCatalog = 0;
static bool catalogOk = false;
static uint32_t lastSync = 0;

bool cloudCatalogOk() { return catalogOk; }
int cloudCount() { return nCatalog; }
const char* cloudTitle(int i) { return (i >= 0 && i < nCatalog) ? catalog[i].title : ""; }
const char* cloudFile(int i) { return (i >= 0 && i < nCatalog) ? catalog[i].file : ""; }
const char* cloudCat(int i) { return (i >= 0 && i < nCatalog) ? catalog[i].cat : ""; }

// ---------- экран статуса v0.13 (анимированный) ----------
// line1 = заголовок; line2/3 = подстроки; анимация: орбита-спиннер + пульс шапки.
static uint32_t statusT0 = 0;

static void statusScreen(const char* line1, const char* line2, const char* line3) {
  if (statusT0 == 0) statusT0 = millis();
  uint32_t t = millis() - statusT0;

  d.clearDisplay();
  headerBar("CLOUD");
  d.setTextColor(1); d.setTextSize(1);

  // заголовок-карточка
  if (line1) {
    d.setTextSize(1);
    int tw = d.strWidth(line1);
    d.setCursor((W - tw) / 2, 18);
    d.print(line1);
    int x1 = (W - tw) / 2 - 6;
    d.drawRect(x1, 16, tw + 12, 15, 1);
    d.drawCircle(x1 - 2, 23, 1, 1);
    d.drawCircle(x1 + tw + 8, 23, 1, 1);
  }

  // орбитальный спиннер — пока работа идёт (нет line3/финального экрана)
  if (!line3) {
    int cxm = 100, cym = 46, r = 7;
    d.drawCircle(cxm, cym, r, 1);
    float ang = t * 0.012f;
    int px = cxm + (int)(r * cosf(ang)), py = cym + (int)(r * sinf(ang));
    d.fillCircle(px, py, 2, 1);
    // хвост кометы
    for (int k = 1; k <= 3; k++) {
      float ta = ang - k * 0.45f;
      int tx = cxm + (int)(r * cosf(ta)), ty = cym + (int)(r * sinf(ta));
      d.drawPixel(tx, ty, 1);
    }
  }

  if (line2) { d.setCursor(4, 30); d.print(line2); }
  if (line3) { d.setCursor(4, 42); d.print(line3); }
  d.display();
}

// ---------- загрузка манифеста ----------
bool cloudFetchCatalog(bool showScreen) {
  if (!netOnline()) {
    if (showScreen) statusScreen("offline: no wifi", "apps list kept", nullptr);
    return catalogOk;   // старый каталог остаётся
  }
  WiFiClientSecure cl;
  cl.setInsecure();
  HTTPClient http;
  if (showScreen) statusScreen("syncing catalog...", nullptr, nullptr);
  if (!(http.begin(cl, XLA_MANIFEST) && http.GET() == 200)) {
    http.end();
    if (showScreen) statusScreen("catalog unreachable", "kept previous", nullptr);
    return catalogOk;
  }
  String body = http.getString();
  http.end();
  int n = 0, from = 0;
  while (n < 16) {
    int nl = body.indexOf('\n', from);
    String line = body.substring(from, nl < 0 ? (int)body.length() : nl);
    line.trim();
    if (line.length()) {
      int b1 = line.indexOf('|');
      int b2 = line.indexOf('|', b1 + 1);
      if (b1 > 0 && b2 > b1) {
        snprintf(catalog[n].file, sizeof catalog[n].file, "%s", line.substring(0, b1).c_str());
        snprintf(catalog[n].title, sizeof catalog[n].title, "%s", line.substring(b1 + 1, b2).c_str());
        snprintf(catalog[n].cat, sizeof catalog[n].cat, "%s", line.substring(b2 + 1).c_str());
        n++;
      }
    }
    if (nl < 0) break;
    from = nl + 1;
  }
  if (n > 0) {
    nCatalog = n;
    catalogOk = true;
    lastSync = millis();
    if (showScreen) {
      char b[24];
      snprintf(b, sizeof b, "%d apps", n);
      statusScreen("catalog synced", b, nullptr);
    }
  } else if (showScreen) {
    statusScreen("catalog empty", nullptr, nullptr);
  }
  Serial.printf("[cloud] catalog: %d apps\n", nCatalog);
  return catalogOk;
}

// ---------- NTP ----------
bool cloudSyncTime(bool showScreen) {
  if (!netOnline()) return false;
  if (showScreen) statusScreen("syncing time...", nullptr, nullptr);
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");   // запуск NTP
  uint32_t t0 = millis();
  while (millis() - t0 < 8000) {
    if (time(nullptr) > 100000) {
      ntpTime = time(nullptr);
      ntpGotAt = millis();
      if (showScreen) {
        struct tm* ti = localtime(&ntpTime);
        char b[24];
        snprintf(b, sizeof b, "%02d:%02d %02d.%02d", ti->tm_hour, ti->tm_min, ti->tm_mday, ti->tm_mon + 1);
        statusScreen("time synced", b, nullptr);
      }
      return true;
    }
    if (pollEvent() == EV_EXIT) break;
    delay(40);
  }
  return false;
}

// ---------- WiFi по сохранённым кредам ----------
bool cloudEnsureWifi(bool showScreen) {
  if (netOnline()) return true;
  char ssid[33], pass[65];
  if (!wifiLoad(ssid, sizeof ssid, pass, sizeof pass)) {
    if (showScreen) statusScreen("no saved wifi", "use NET app once", nullptr);
    return false;
  }
  extern bool wifiConnectAnimated(const char*, const char*, uint32_t);
  if (showScreen) {
    char b[40];
    snprintf(b, sizeof b, "wifi: %s", ssid);
    statusScreen(b, "connecting...", nullptr);
  }
  return wifiConnectAnimated(ssid, pass, 15000);
}

// ---------- download -> run -> delete ----------
bool cloudRunApp(int idx) {
  if (idx < 0 || idx >= nCatalog) return false;

  // 1) сеть
  char l1[48];
  snprintf(l1, sizeof l1, "%s", catalog[idx].title);
  if (!cloudEnsureWifi(false)) {
    statusScreen(l1, "wifi failed", nullptr);
    beepWait(250, 300); delay(900);
    return false;
  }

  // 2) download в /t.xla (один файл — потом удаляем)
  char url[128];
  snprintf(url, sizeof url, "%s%s.xla", XLA_APPBASE, catalog[idx].file);
  statusScreen(l1, "downloading...", nullptr);
  Serial.printf("[cloud] GET %s\n", url);
  WiFiClientSecure cl;
  cl.setInsecure();
  HTTPClient http;
  if (!http.begin(cl, url)) { statusScreen(l1, "url fail", nullptr); return false; }
  int code = http.GET();
  if (code != 200) {
    http.end();
    Serial.printf("[cloud] HTTP %d\n", code);
    char b[24]; snprintf(b, sizeof b, "HTTP %d", code);
    statusScreen(l1, b, nullptr);
    beepWait(250, 300); delay(900);
    return false;
  }
  int total = http.getSize();
  if (total <= 0 || total > XLA_MAXCODE + XLA_MAXDATA + XLA_MAXSTR + 64) {
    http.end();
    statusScreen(l1, "bad size", nullptr);
    return false;
  }
  File f = SPIFFS.open("/t.xla", "w");
  if (!f) { http.end(); statusScreen(l1, "fs fail", nullptr); return false; }
  WiFiClient* st = http.getStreamPtr();
  uint8_t buf[512];
  int got = 0;
  uint32_t tLast = millis();
  while (got < total && millis() - tLast < 15000) {
    size_t avail = st ? st->available() : 0;
    if (avail) {
      int nrd = st->readBytes(buf, min((size_t)512, avail));
      if ((int)f.write(buf, nrd) != nrd) break;
      got += nrd; tLast = millis();
      int pct = got * 100 / total;
      d.fillRect(4, 44, 80, 8, 0);
      d.drawRect(4, 44, 80, 8, 1);
      d.fillRect(6, 46, 76 * pct / 100, 4, 1);
      d.setCursor(92, 44); d.format("%d%%", pct);
      d.display();
    } else delay(4);
  }
  f.close();
  http.end();
  Serial.printf("[cloud] downloaded %d/%d\n", got, total);
  if (got != total) {
    SPIFFS.remove("/t.xla");
    statusScreen(l1, "download failed", nullptr);
    beepWait(250, 300); delay(900);
    return false;
  }

  // 3) run
  beep(1500, 40);
  bool ok = xlaRunFile("/t.xla");

  // 4) delete — ничего не храним (бесконечное облако = нулевой кэш)
  SPIFFS.remove("/t.xla");
  Serial.printf("[cloud] ran %s ok=%d, temp removed\n", catalog[idx].file, ok);
  return ok;
}
