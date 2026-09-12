// ==================================================================
//  settings.cpp — всё постоянное хранилище (NVS) в одном модуле.
//  Миграционная совместимость с v0.11: те же пространства и ключи
//  (c3flip: rot/drot/snake_hi/ssid/pass; xla: <TITLE>_<key>).
//  Устраняет баг v0.11: factory reset чистил только c3flip,
//  оставляя рекорды плагинов (xla) и кэш облака (cloud).
// ==================================================================
#include "settings.h"
#include "config.h"

Preferences prefs;

// ---------- c3flip ----------
uint8_t stickRot = 90;   // угол установки стика
uint8_t dispRot  = 0;    // поворот экрана 0..3

static char wifiSSID[33] = "";
static char wifiPASS[65] = "";

void settingsLoad() {
  prefs.begin(NS_USER, true);
  stickRot = prefs.getUChar("rot", 90);
  dispRot  = prefs.getUChar("drot", 0);
  prefs.end();
}

// ---------- Wi-Fi креды ----------
bool wifiLoad(char* ssid, size_t ssidCap, char* pass, size_t passCap) {
  prefs.begin(NS_USER, true);
  String s = prefs.getString("ssid", "");
  String p = prefs.getString("pass", "");
  prefs.end();
  if (s.isEmpty()) return false;
  snprintf(ssid, ssidCap, "%s", s.c_str());
  snprintf(pass, passCap, "%s", p.c_str());
  return true;
}
void wifiSave(const char* ssid, const char* pass) {
  prefs.begin(NS_USER, false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}
void wifiForget() {
  prefs.begin(NS_USER, false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
  wifiSSID[0] = 0; wifiPASS[0] = 0;
  Serial.println("[wifi] credentials forgotten");
}

// ---------- рекорды встроенных игр ----------
uint32_t snakeHiLoad() {
  prefs.begin(NS_USER, true);
  uint32_t hi = prefs.getUInt("snake_hi", 0);
  prefs.end();
  return hi;
}
void snakeHiSave(uint32_t hi) {
  prefs.begin(NS_USER, false);
  prefs.putUInt("snake_hi", hi);
  prefs.end();
}

// ---------- прикладные настройки ----------
void saveRotation(uint8_t stick, uint8_t disp) {
  prefs.begin(NS_USER, false);
  prefs.putUChar("rot", stick);
  prefs.putUChar("drot", disp);
  prefs.end();
}

// ---------- рекорды XLA-плагинов (int16) ----------
int16_t xlaScoreLoad(const char* title, const char* key, int16_t def) {
  char full[XLA_KEY_MAX];
  snprintf(full, sizeof full, "%s_%s", title, key);
  prefs.begin(NS_XLA, true);
  int16_t v = prefs.getShort(full, def);
  prefs.end();
  return v;
}
void xlaScoreSave(const char* title, const char* key, int16_t v) {
  char full[XLA_KEY_MAX];
  snprintf(full, sizeof full, "%s_%s", title, key);
  prefs.begin(NS_XLA, false);
  prefs.putShort(full, v);
  prefs.end();
}

// ---------- сброс ----------
// Factory reset теперь честно сносит ВСЁ пользовательское:
// настройки+wifi (c3flip), рекорды плагинов (xla), кэш облака (cloud).
void factoryReset() {
  prefs.begin(NS_USER, false);  prefs.clear(); prefs.end();
  prefs.begin(NS_XLA, false);   prefs.clear(); prefs.end();
  prefs.begin("cloud", false);  prefs.clear(); prefs.end();
  stickRot = 90;
  dispRot = 0;
}
