// ==================================================================
//  C3 XLAMPER v0.11
//  ESP32-C3 Super Mini + SSD1306 128x64 + analog stick + speaker
//
//  OLED: SDA=IO8 SCL=IO9 (0x3C)
//  Stick: VRx=IO1 VRy=IO3 SW=IO10   Speaker: IO5
//
//  Menu sections: < > switch section, v^ navigate items
//    GAMES: SNAKE, TETRIS, 2048, PONG
//    NET:   WIFI SCAN, NET (wifi + NTP)
//    MEDIA: MUSIC
//    SYS:   ABOUT, SETTINGS
// ==================================================================
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <time.h>
#include <Preferences.h>
// XLA VM: подключаем ДО #define W/H (иначе конфликт с mbedtls H)
#include <FS.h>
#include <SPIFFS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

#define PIN_VRX    1
#define PIN_VRY    3
#define PIN_SW     10
#define PIN_BUZZER 5

#define W 128
#define H 64

// ==================================================================
//  DISPLAY
// ==================================================================
uint8_t dispRot = 0;
const u8g2_cb_t* dispRotCodes[4] = { U8G2_R0, U8G2_R1, U8G2_R2, U8G2_R3 };
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, 9, 8);

const uint8_t* FONT1  = u8g2_font_7x13B_tr;    // bold UI
const uint8_t* FONT3  = u8g2_font_fub20_tr;    // big rounded
const uint8_t* FONT_KB = u8g2_font_5x8_tr;    // keyboard
const uint8_t* FONT4  = u8g2_font_4x6_tr;     // tiny (2048 tiles)

class Disp {
public:
  int cx = 0, cy = 0;
  void begin() { u8g2.setBusClock(400000); u8g2.begin(); u8g2.setFont(FONT1); }
  void setRotation(uint8_t r) { if (r > 3) r = 0; dispRot = r; u8g2.setDisplayRotation(dispRotCodes[r]); }
  void clearDisplay() { u8g2.clearBuffer(); }
  void display()      { u8g2.sendBuffer(); }
  void invertDisplay(bool b) { u8g2.sendF("c", b ? 0xA7 : 0xA6); }
  void setTextColor(int c) { u8g2.setDrawColor(c ? 1 : 0); }
  void setTextSize(int n)  { u8g2.setFont(n == 3 ? FONT3 : (n == 4 ? FONT4 : (n == 5 ? FONT_KB : FONT1))); }
  void setCursor(int x, int y) { cx = x; cy = y; }
  void _put(const char* s) { u8g2.drawUTF8(cx, cy + u8g2.getAscent(), s); cx += u8g2.getStrWidth(s); }
  void _nl() { cy += 13; cx = 0; }
  void print(const char* s)   { _put(s); }
  void print(char c)          { char b[2] = {c, 0}; _put(b); }
  void print(int v)           { char b[12]; snprintf(b, 12, "%d", v); _put(b); }
  void print(unsigned long v) { char b[12]; snprintf(b, 12, "%lu", v); _put(b); }
  void print(const String& s){ _put(s.c_str()); }
  void println(const char* s){ _put(s); _nl(); }
  void printf(const char* fmt, ...) {
    char b[96];
    va_list ap; va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    _put(b);
  }
  void drawPixel(int x,int y,int c=1)               { u8g2.setDrawColor(c?1:0); u8g2.drawPixel(x,y); }
  void drawLine(int x0,int y0,int x1,int y1,int c=1){ u8g2.setDrawColor(c?1:0); u8g2.drawLine(x0,y0,x1,y1); }
  void drawFastHLine(int x,int y,int w,int c=1)      { u8g2.setDrawColor(c?1:0); u8g2.drawHLine(x,y,w); }
  void drawFastVLine(int x,int y,int h,int c=1)      { u8g2.setDrawColor(c?1:0); u8g2.drawVLine(x,y,h); }
  void drawRect(int x,int y,int w,int h,int c=1)     { u8g2.setDrawColor(c?1:0); u8g2.drawFrame(x,y,w,h); }
  void fillRect(int x,int y,int w,int h,int c=1)     { u8g2.setDrawColor(c?1:0); u8g2.drawBox(x,y,w,h); }
  void drawCircle(int x,int y,int r,int c=1)        { u8g2.setDrawColor(c?1:0); u8g2.drawCircle(x,y,r,U8G2_DRAW_ALL); }
  void fillCircle(int x,int y,int r,int c=1)        { u8g2.setDrawColor(c?1:0); u8g2.drawDisc(x,y,r,U8G2_DRAW_ALL); }
  void fillEllipse(int x,int y,int rx,int ry,int c=1){ u8g2.setDrawColor(c?1:0); u8g2.drawFilledEllipse(x,y,rx,ry); }
};

Disp d;
Preferences prefs;

// ---------- sound ----------
uint32_t sndUntil = 0;
bool bgPlaying = false;
void beep(int f, int ms) { if (bgPlaying) return; ledcWriteTone(PIN_BUZZER, f); sndUntil = millis() + ms; }
void beepTick() { if (sndUntil && (int32_t)(millis() - sndUntil) >= 0) { ledcWriteTone(PIN_BUZZER, 0); sndUntil = 0; } }
void beepWait(int f, int ms) { ledcWriteTone(PIN_BUZZER, f); delay(ms); ledcWriteTone(PIN_BUZZER, 0); }

// ---------- stick ----------
uint8_t stickRot = 90;
int cx = 2048, cy = 2048;
int loX = 1148, hiX = 2948, loY = 1148, hiY = 2948;

int readX() {
  int a = analogRead(PIN_VRX), b = analogRead(PIN_VRY);
  switch (stickRot) { case 0: return a; case 90: return b; case 180: return 4095 - a; default: return 4095 - b; }
}
int readY() {
  int a = analogRead(PIN_VRX), b = analogRead(PIN_VRY);
  switch (stickRot) { case 0: return b; case 90: return 4095 - a; case 180: return 4095 - b; default: return a; }
}
int8_t axX() {
  int v = readX();
  int tL = cx - (cx - loX) / 2, tR = cx + (hiX - cx) / 2;
  if (tL >= cx - 80) tL = cx - 80;
  if (tR <= cx + 80) tR = cx + 80;
  return v > tR ? 1 : (v < tL ? -1 : 0);
}
int8_t axY() {
  int v = readY();
  int tL = cy - (cy - loY) / 2, tR = cy + (hiY - cy) / 2;
  if (tL >= cy - 80) tL = cy - 80;
  if (tR <= cy + 80) tR = cy + 80;
  return v > tR ? 1 : (v < tL ? -1 : 0);
}

// ---------- events ----------
enum Ev { EV_NONE = 0, EV_UP, EV_DOWN, EV_LEFT, EV_RIGHT, EV_OK, EV_EXIT };
#define HOLD_MS 600
bool swHeld = false, exitFired = false;
uint32_t swDownAt = 0;
int8_t lastDir = 0;
uint32_t repAt = 0, firstAt = 0;

Ev pollEvent();
void drawMenu();
void bgSongTick(); void bgSongStart(int idx); void bgSongStop();
void appSongs(); void appSnake(); void appTetris(); void app2048(); void appPong();
void appWifi(); void appNet(); void appCube(); void appAbout(); void appSettings();
void appStore();
void drawClock();
void headerBar(const char* s);
void showWrongPassword(const char* ssid);

int holdProgress() {
  if (!swHeld || exitFired) return 0;
  uint32_t dt = millis() - swDownAt;
  return dt >= HOLD_MS ? 100 : dt * 100 / HOLD_MS;
}

Ev pollEvent() {
  beepTick();
  uint32_t now = millis();
  bool sw = !digitalRead(PIN_SW);
  if (sw && !swHeld) { swHeld = true; swDownAt = now; exitFired = false; }
  if (sw && swHeld && !exitFired && now - swDownAt >= HOLD_MS) { exitFired = true; beep(500, 150); return EV_EXIT; }
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

// ---------- UI ----------
void headerBar(const char* s) {
  d.fillRect(0, 0, W, 13, 1);
  d.setTextColor(0); d.setTextSize(1);
  d.setCursor(2, 1); d.print(s);
  int hp = holdProgress();
  if (hp > 0) {
    int bw = 30 * hp / 100;
    d.drawRect(92, 3, 34, 7, 0);
    d.fillRect(94 + (30 - bw), 4, bw, 5, 0);
  }
}
void footerBar(const char* s) {
  d.drawFastHLine(0, 51, W, 1);
  d.setTextColor(1); d.setTextSize(1);
  d.setCursor(2, 53); d.print(s);
}
void flashInvert() { d.invertDisplay(true); delay(70); d.invertDisplay(false); }

// ==================================================================
//  SONGS
// ==================================================================
struct Note { uint16_t f; uint8_t t; };

const Note SONG1[] PROGMEM = {
  {659,3},{659,3},{0,1},{659,6},{0,1},{523,4},{587,4},
  {659,3},{659,3},{0,1},{659,6},{0,1},{523,4},{587,4},
  {587,3},{587,3},{0,1},{587,6},{0,1},{494,4},{523,4},
  {587,3},{587,3},{0,1},{587,6},{0,1},{494,4},{523,4},
  {659,3},{659,3},{0,1},{659,6},{0,1},{523,4},{587,4},
  {587,3},{587,3},{0,1},{587,6},{0,1},{494,4},{523,4},
  {659,8},{587,8},{523,8},{494,8},
  {0,0}
};
const Note SONG2[] PROGMEM = {
  {784,4},{659,2},{587,2},{523,4},{587,4},
  {659,2},{587,2},{523,2},{494,2},{523,2},
  {587,4},{523,2},{494,2},{440,4},{494,4},
  {523,2},{494,2},{440,2},{415,2},{440,2},
  {494,4},{440,2},{392,2},{349,4},{392,4},
  {440,8},{392,8},{349,16},
  {784,4},{880,4},{784,4},{659,8},
  {587,4},{659,4},{523,8},
  {440,4},{494,4},{523,4},{587,4},
  {523,8},{0,2},
  {0,0}
};
const Note SONG3[] PROGMEM = {
  {392,2},{392,2},{392,4},{392,2},{392,2},{392,4},
  {392,2},{440,2},{494,4},{494,2},{440,2},
  {494,2},{523,2},{587,4},
  {587,2},{523,2},{494,2},{440,2},{392,4},
  {392,2},{440,2},{494,4},{494,2},{440,2},{392,4},
  {392,2},{440,2},{494,4},{523,2},{494,2},
  {440,2},{392,2},{392,2},{392,2},{440,4},
  {494,2},{523,2},{494,2},{440,2},{392,4},
  {0,0}
};
const Note SONG4[] PROGMEM = {
  {523,2},{494,2},{440,4},{392,2},
  {440,2},{494,4},{523,2},{494,2},
  {440,4},{392,8},
  {494,2},{440,2},{392,2},{349,2},{392,4},
  {440,2},{392,2},{349,2},{330,2},{349,4},
  {392,2},{349,2},{330,2},{294,2},{330,4},
  {349,4},{392,4},{440,8},
  {523,2},{494,2},{440,4},{392,2},
  {440,2},{494,4},{523,2},{494,2},
  {440,4},{392,8},
  {0,0}
};
const Note SONG5[] PROGMEM = {
  {698,2},{587,2},{523,2},{466,2},{392,4},{392,2},{392,2},
  {466,2},{392,2},{349,2},{349,2},{349,2},
  {466,2},{349,2},{311,2},{311,2},{311,2},
  {466,2},{349,2},{698,2},{587,2},{523,2},{466,2},
  {784,4},{698,2},{587,2},{523,2},{466,2},{392,4},
  {831,4},{784,2},{698,2},{659,2},{622,2},
  {659,4},{587,2},{523,2},{466,2},
  {392,8},{0,2},
  {0,0}
};

const char* songNames[] = { "Grasshopper", "Kalinka", "Xmas Tree", "Swings", "Harry P." };
const Note* const songs[] = { SONG1, SONG2, SONG3, SONG4, SONG5 };
const int N_SONGS = 5;

int curSong = 0;
uint32_t songPos = 0, songTUntil = 0;

void bgSongTick() {
  if (!bgPlaying) return;
  if ((int32_t)(millis() - songTUntil) < 0) return;
  Note nt;
  memcpy_P(&nt, &songs[curSong][songPos], sizeof(Note));
  if (nt.f == 0 && nt.t == 0) { songPos = 0; return; }
  songTUntil = millis() + (uint32_t)nt.t * 90;
  songPos++;
  if (nt.f) ledcWriteTone(PIN_BUZZER, nt.f); else ledcWriteTone(PIN_BUZZER, 0);
}
void bgSongStart(int idx) { curSong = idx; songPos = 0; songTUntil = 0; bgPlaying = true; sndUntil = 0; }
void bgSongStop() { bgPlaying = false; ledcWriteTone(PIN_BUZZER, 0); }

void appSongs() {
  int sel = 0;
  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) { bgSongStop(); beep(400, 100); return; }
    if (e == EV_UP)   { sel = (sel + N_SONGS - 1) % N_SONGS; beep(900, 15); }
    if (e == EV_DOWN) { sel = (sel + 1) % N_SONGS; beep(900, 15); }
    if (e == EV_OK) {
      if (bgPlaying && curSong == sel) { bgSongStop(); beep(500, 80); }
      else { bgSongStart(sel); beep(1500, 40); }
    }
    bgSongTick();

    d.clearDisplay();
    d.fillRect(0, 0, W, 13, 1);
    d.setTextColor(0); d.setTextSize(1);
    d.setCursor(2, 1); d.print("MUSIC");
    d.setCursor(100, 1); d.printf("%d/%d", sel + 1, N_SONGS);
    for (int i = 0; i < N_SONGS; i++) {
      int y = 16 + i * 10;
      bool cur = (i == sel);
      if (cur) { d.fillRect(0, y - 1, W, 11, 1); d.setTextColor(0); }
      else d.setTextColor(1);
      d.setTextSize(1); d.setCursor(4, y); d.print(songNames[i]);
      if (bgPlaying && i == curSong) {
        uint8_t ph = (millis() / 160) % 2;
        d.print(ph ? " *" : " +");
      }
    }
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(4, 54); d.print("click=play/stop hold=exit");
    d.display();
  }
}

// ==================================================================
//  WIFI + NTP
// ==================================================================
const char* NTP1 = "ntp.msn.ru";
const char* NTP2 = "pool.ntp.org";
char wifiSSID[33] = "";
char wifiPASS[65] = "";
time_t ntpTime = 0;
uint32_t ntpGotAt = 0;

void ntpConfig() { configTime(3 * 3600, 0, NTP1, NTP2); }

bool loadWifi() {
  prefs.begin("c3flip", true);
  String s = prefs.getString("ssid", "");
  String p = prefs.getString("pass", "");
  prefs.end();
  if (s.length() == 0) return false;
  strncpy(wifiSSID, s.c_str(), 32); wifiSSID[32] = 0;
  strncpy(wifiPASS, p.c_str(), 64); wifiPASS[64] = 0;
  return true;
}
void saveWifi() {
  prefs.begin("c3flip", false);
  prefs.putString("ssid", wifiSSID);
  prefs.putString("pass", wifiPASS);
  prefs.end();
}
void forgetWifi() {
  prefs.begin("c3flip", false);
  prefs.remove("ssid"); prefs.remove("pass");
  prefs.end();
  wifiSSID[0] = 0; wifiPASS[0] = 0;
  Serial.println("wifi forgotten");
}

void showWrongPassword(const char* ssid) {
  beepWait(300, 250); beepWait(220, 350);
  d.clearDisplay();
  headerBar("ERROR");
  d.setTextColor(1); d.setTextSize(1);
  d.setCursor(4, 18); d.print("WRONG PASSWORD!");
  d.setCursor(4, 32); d.print("net:"); d.print(ssid);
  d.setCursor(4, 44); d.print("removed from db");
  d.display();
  forgetWifi();
  delay(1600);
}

void connectingAnim(const char* ssid, uint32_t t) {
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

bool connectAnimated(const char* ssid, const char* pass, uint32_t timeoutMs = 15000) {
  Serial.printf("[wifi] connecting to '%s'...\n", ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    connectingAnim(ssid, millis());
    if (millis() - t0 > timeoutMs) {
      Serial.printf("[wifi] FAIL after %lums status=%d\n", (unsigned long)timeoutMs, WiFi.status());
      WiFi.disconnect();
      beepWait(200, 300);
      return false;
    }
    if (pollEvent() == EV_EXIT) {
      Serial.println("[wifi] cancelled by user");
      WiFi.disconnect();
      beep(400, 100);
      return false;
    }
  }
  Serial.printf("[wifi] CONNECTED ip=%s\n", WiFi.localIP().toString().c_str());
  beep(1300, 60); delay(30); beep(1700, 80);
  ntpConfig();
  return true;
}

// ==================================================================
//  KEYBOARD
// ==================================================================
const char* KB_L[3] = { "abcdefghij", "klmnopqrst", "uvwxyz.-_+" };
const char* KB_U[3] = { "ABCDEFGHIJ", "KLMNOPQRST", "UVWXYZ,!?#" };
const char* KB_D[3] = { "0123456789", "@#$%&*()-=", ":;<>/\\'\"" };

void drawKey(int x, int y, int w, const char* label, bool cur) {
  if (cur) { d.fillRect(x, y, w, 10, 1); d.setTextColor(0); }
  else     { d.drawRect(x, y, w, 10, 1); d.setTextColor(1); }
  u8g2.setFont(FONT_KB);
  int lw = u8g2.getStrWidth(label);
  u8g2.drawUTF8(x + (w - lw) / 2, y + 1 + u8g2.getAscent(), label);
}

void drawKeyboard(int row, int col, int page, const char* typed) {
  d.clearDisplay();
  headerBar(page == 0 ? "PASSWORD abc" : page == 1 ? "PASSWORD ABC" : "PASSWORD 123");

  u8g2.setFont(FONT_KB);
  d.setTextColor(1);
  d.drawRect(2, 14, 124, 11, 1);
  int tl = strlen(typed);
  int show = tl > 20 ? 20 : tl;
  int xx = 5;
  d.setCursor(xx, 16);
  for (int i = 0; i < show; i++) {
    bool last = (i == show - 1);
    if (last && (millis() / 400) % 2) u8g2.drawUTF8(xx + i * 6, 16 + u8g2.getAscent(), &typed[tl - 1]);
    else d.print('*');
  }
  if ((millis() / 400) % 2) d.fillRect(5 + show * 6, 16, 5, 7, 1);
  char cnt[4]; snprintf(cnt, 4, "%d", tl);
  u8g2.drawUTF8(122 - u8g2.getStrWidth(cnt), 16 + u8g2.getAscent(), cnt);

  const char** rows = page == 0 ? KB_L : page == 1 ? KB_U : KB_D;
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 10; c++) {
      char key[2] = { rows[r][c], 0 };
      drawKey(3 + c * 12, 26 + r * 10, 11, key, row == r && col == c);
    }
  const char* nextLbl = page == 0 ? "ABC" : page == 1 ? "123" : "abc";
  bool c0 = (row == 3 && col < 2);
  bool c1 = (row == 3 && col >= 2 && col < 6);
  bool c2 = (row == 3 && col >= 6 && col < 8);
  bool c3 = (row == 3 && col >= 8);
  drawKey(3,  56, 22, nextLbl, c0);
  drawKey(27, 56, 40, "SPACE", c1);
  drawKey(69, 56, 26, "<-",    c2);
  drawKey(97, 56, 28, "OK",    c3);
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
        const char** rows = page == 0 ? KB_L : page == 1 ? KB_U : KB_D;
        if (len < maxLen - 1) { out[len++] = rows[row][col]; out[len] = 0; beep(1100, 20); }
        else beep(200, 60);
      } else if (col < 2) { page = (page + 1) % 3; beep(900, 30); }
      else if (col < 6) { if (len < maxLen - 1) { out[len++] = ' '; out[len] = 0; beep(1100, 20); } }
      else if (col < 8) { if (len > 0) { len--; out[len] = 0; beep(700, 25); } }
      else { beep(1500, 50); return; }
    }
  }
}

// ==================================================================
//  NET APP
// ==================================================================
int askRetryForgetExit() {
  int sel = 0;
  const char* items[3] = { "RETRY same password", "FORGET network", "EXIT to menu" };
  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) return 2;
    if (e == EV_UP   && sel > 0) { sel--; beep(900, 15); }
    if (e == EV_DOWN && sel < 2) { sel++; beep(900, 15); }
    if (e == EV_OK) { beep(1200, 40); return sel; }
    d.clearDisplay();
    headerBar("CONNECT FAILED");
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(4, 16); d.print("Cannot connect. Why?");
    d.setCursor(4, 26); d.print("- wrong password?");
    d.setCursor(4, 36); d.print("- weak signal? 2.4GHz?");
    for (int i = 0; i < 3; i++) {
      int y = 40 + i * 9;
      if (y > 62) break;
      if (i == sel) { d.fillRect(0, y - 1, W, 10, 1); d.setTextColor(0); }
      else d.setTextColor(1);
      d.setCursor(4, y); d.print(items[i]);
    }
    d.display();
  }
}

void netListScreen(int& selOut, bool& chosenOut, int n) {
  int sel = 0, top = 0;
  chosenOut = false;
  while (!chosenOut) {
    Ev e = pollEvent();
    if (e == EV_EXIT) return;
    if (e == EV_UP   && sel > 0)     { sel--; if (sel < top) top = sel; beep(900, 15); }
    if (e == EV_DOWN && sel < n - 1) { sel++; if (sel > top + 3) top = sel - 3; beep(900, 15); }
    if (e == EV_OK) { chosenOut = true; }

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
  selOut = sel;
}

void appNet() {
  if (loadWifi()) {
    d.clearDisplay(); headerBar("NET");
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(4, 22); d.print("Connecting to:");
    d.setCursor(28, 36); d.print(wifiSSID);
    d.display(); delay(300);
    Serial.printf("[net] saved: ssid='%s'\n", wifiSSID);
    if (connectAnimated(wifiSSID, wifiPASS, 15000)) goto ntpScreen;
    int ch = askRetryForgetExit();
    if (ch == 0 && connectAnimated(wifiSSID, wifiPASS, 15000)) goto ntpScreen;
    if (ch == 1) showWrongPassword(wifiSSID);
    WiFi.mode(WIFI_OFF);
    return;
  }

  { // scan & choose
    d.clearDisplay(); headerBar("NET");
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(10, 30); d.print("Scanning...");
    d.display();
    WiFi.mode(WIFI_STA);
    WiFi.scanDelete();
    WiFi.scanNetworks(true);
    int n = WIFI_SCAN_RUNNING;
    while (n == WIFI_SCAN_RUNNING) {
      if (pollEvent() == EV_EXIT) { WiFi.scanDelete(); WiFi.mode(WIFI_OFF); return; }
      d.clearDisplay(); headerBar("NET: SCAN");
      int rx = 64, ry = 34, R = 16;
      d.drawCircle(rx, ry, R, 1);
      d.drawCircle(rx, ry, R * 2 / 3, 1);
      d.drawCircle(rx, ry, R / 3, 1);
      float a = (millis() % 1800) * 2.0 * PI / 1800.0;
      for (int r = 0; r <= R; r += 3) d.drawPixel(rx + r * cos(a), ry + r * sin(a), 1);
      d.fillCircle(rx, ry, 2, 1);
      footerBar("hold=cancel");
      d.display();
      n = WiFi.scanComplete();
      delay(30);
    }
    if (n <= 0) {
      d.clearDisplay(); headerBar("NET");
      d.setTextColor(1); d.setCursor(10, 30); d.print("No networks found");
      d.display(); beepWait(250, 400); delay(900);
      WiFi.mode(WIFI_OFF);
      return;
    }
    beep(1300, 80);

    int sel; bool chosen;
    netListScreen(sel, chosen, n);
    if (!chosen) { WiFi.scanDelete(); WiFi.mode(WIFI_OFF); return; }

    String s = WiFi.SSID(sel);
    bool openNet = WiFi.encryptionType(sel) == WIFI_AUTH_OPEN;
    strncpy(wifiSSID, s.c_str(), 32); wifiSSID[32] = 0;
    wifiPASS[0] = 0;
    if (!openNet) inputPassword(wifiPASS, 64);
    if (openNet || strlen(wifiPASS) > 0) {
      saveWifi();
      Serial.printf("[net] new: ssid='%s' pass='%s'\n", wifiSSID, wifiPASS);
      bool conn = connectAnimated(wifiSSID, wifiPASS, 15000);
      while (!conn) {
        int ch = askRetryForgetExit();
        if (ch == 2) { WiFi.mode(WIFI_OFF); return; }
        if (ch == 1) { showWrongPassword(wifiSSID); WiFi.mode(WIFI_OFF); return; }
        conn = connectAnimated(wifiSSID, wifiPASS, 15000);
      }
    } else { WiFi.mode(WIFI_OFF); return; }
  }

ntpScreen:
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
    d.setCursor(28, 30); d.printf("%02d:%02d", ti->tm_hour, ti->tm_min);
    d.setTextSize(1);
    d.setCursor(30, 52); d.printf("%02d.%02d.%d", ti->tm_mday, ti->tm_mon + 1, ti->tm_year + 1900);
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

// ==================================================================
//  CLOCK + MARQUEE
// ==================================================================
void drawClock() {
  struct tm tmi;
  if (ntpTime) {
    time_t tt = ntpTime + (millis() - ntpGotAt) / 1000;
    tmi = *localtime(&tt);
  } else {
    uint32_t t = millis() / 1000;
    tmi.tm_hour = (t / 3600) % 24; tmi.tm_min = (t / 60) % 60; tmi.tm_sec = t % 60;
  }
  d.clearDisplay();
  d.setTextColor(1);
  d.setTextSize(1);
  d.setCursor(ntpTime ? 16 : 34, 2);
  d.print(ntpTime ? "C3 XLAMPER [NTP]" : "C3 XLAMPER");
  d.setTextSize(3);
  bool colon = (millis() / 500) % 2;
  char buf[8];
  snprintf(buf, 8, "%02d:%02d", tmi.tm_hour, tmi.tm_min);
  if (!colon) buf[2] = ' ';
  d.setCursor(28, 16); d.print(buf);
  d.setTextSize(1);
  if (ntpTime) {
    d.setCursor(37, 40); d.printf("%02d.%02d.%d", tmi.tm_mday, tmi.tm_mon + 1, tmi.tm_year + 1900);
  } else {
    d.setCursor(46, 40); d.print("press OK");
  }

  char tick[96];
  if (ntpTime)
    snprintf(tick, sizeof tick, "%02d:%02d:%02d  %02d.%02d.%d  C3 XLAMPER  ESP32-C3 160MHz  ",
             tmi.tm_hour, tmi.tm_min, tmi.tm_sec, tmi.tm_mday, tmi.tm_mon + 1, tmi.tm_year + 1900);
  else
    snprintf(tick, sizeof tick, "C3 XLAMPER v0.11  ESP32-C3 RISC-V 160MHz  OLED 128x64  STICK 8-WAY  5 GAMES  ");

  u8g2.setFont(FONT1);
  int tw = u8g2.getStrWidth(tick);
  uint32_t span = (uint32_t)(tw + W);
  int x = W - (int)((millis() / 50) % span) * 2;

  d.drawFastHLine(0, 49, W, 1);
  u8g2.setClipWindow(0, 50, 127, 64);
  u8g2.drawUTF8(x, 61, tick);
  if (x + tw < W) u8g2.drawUTF8(x + tw, 61, tick);
  u8g2.setMaxClipWindow();
  d.display();
}

// ==================================================================
//  GAME: SNAKE
// ==================================================================
void appSnake() {
  const int CELL = 4, GW = 30, GH = 11;
  struct Pt { uint8_t x, y; };
  static Pt body[200];

restart:
  int len = 3, dirX = 1, dirY = 0, wantX = 1, wantY = 0;
  int score = 0, speed = 150;
  bool over = false;
  uint32_t tMove = 0;
  Pt food = {10, 5};
  for (int i = 0; i < len; i++) { body[i].x = 6 - i; body[i].y = 6; }

  uint32_t hi = 0;
  prefs.begin("c3flip", true); hi = prefs.getUInt("snake_hi", 0); prefs.end();

  d.clearDisplay(); headerBar("SNAKE");
  d.setTextColor(1); d.setTextSize(3); d.setCursor(44, 24); d.print("GO!");
  d.display(); beep(1500, 60); delay(350);

  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) { beep(400, 100); return; }
    if (e == EV_OK && over) { beep(1200, 40); goto restart; }

    if (!over) {
      int8_t dx = axX(), dy = axY();
      if (dx != 0 || dy != 0)
        if (!(dx == -dirX && dy == -dirY)) { wantX = dx; wantY = dy; }

      if (millis() - tMove >= (uint32_t)speed) {
        tMove = millis();
        dirX = wantX; dirY = wantY;
        for (int i = len - 1; i > 0; i--) body[i] = body[i - 1];
        body[0].x += dirX; body[0].y += dirY;
        if (body[0].x >= GW || body[0].y >= GH) over = true;
        for (int i = 1; i < len && !over; i++)
          if (body[i].x == body[0].x && body[i].y == body[0].y) over = true;
        if (over) {
          beepWait(250, 200); beepWait(150, 350);
          if ((uint32_t)score > hi) { hi = score; prefs.begin("c3flip", false); prefs.putUInt("snake_hi", hi); prefs.end(); }
        }
        if (!over && body[0].x == food.x && body[0].y == food.y) {
          if (len < 200) { body[len] = body[len - 1]; len++; }
          score++;
          beep(1400 + score * 20, 45);
          for (int tries = 0; tries < 50; tries++) {
            food.x = random(GW); food.y = random(GH);
            bool ok = true;
            for (int i = 0; i < len; i++) if (body[i].x == food.x && body[i].y == food.y) { ok = false; break; }
            if (ok) break;
          }
          if (score % 3 == 0 && speed > 70) speed -= 8;
        }
      }
    }

    d.clearDisplay();
    char hb[24]; snprintf(hb, sizeof hb, "SNAKE S:%d B:%lu", score, (unsigned long)hi);
    headerBar(over ? "GAME OVER" : hb);
    d.drawRect(0, 13, W, H - 13, 1);
    bool bigFood = (millis() / 180) % 2;
    d.fillRect(2 + food.x * CELL + (bigFood ? 0 : 1), 15 + food.y * CELL + (bigFood ? 0 : 1), bigFood ? 3 : 2, bigFood ? 3 : 2, 1);
    for (int i = len - 1; i >= 0; i--)
      d.fillRect(2 + body[i].x * CELL, 15 + body[i].y * CELL, 3, 3, 1);
    d.drawPixel(2 + body[0].x * CELL + 1, 15 + body[0].y * CELL + 1, 0);
    if (over) {
      d.fillRect(18, 24, 92, 18, 1);
      d.setTextColor(0); d.setTextSize(1);
      d.setCursor(26, 26); d.print("GAME OVER!");
      d.setCursor(26, 35); d.printf("S:%d B:%lu", score, (unsigned long)hi);
      d.setTextColor(1); d.setTextSize(1);
      d.setCursor(20, 46); d.print("click=retry hold=exit");
    }
    d.display();
  }
}

// ==================================================================
//  GAME: TETRIS
// ==================================================================
const uint16_t TET_SHAPES[7][4] = {
  {0x0F00, 0x2222, 0x00F0, 0x4444},  // I
  {0x8E00, 0x6440, 0x0E20, 0x44C0},  // J
  {0x2E00, 0x4460, 0x0E80, 0xC440},  // L
  {0x6600, 0x6600, 0x6600, 0x6600},  // O
  {0x6C00, 0x4620, 0x06C0, 0x8C40},  // S
  {0x4E00, 0x4640, 0x0E40, 0x4C40},  // T
  {0xC600, 0x2640, 0x0C60, 0x4C80}   // Z
};

void appTetris() {
  const int TW = 10, TH = 16, CELL = 3, OX = 6, OY = 15;
  uint8_t field[TH];                 // 10 бит в строке
  memset(field, 0, sizeof field);
  int score = 0, lines = 0, level = 1;
  int shape = random(7), rot = 0, nextShape = random(7);
  int px = 3, py = -1;
  int speed = 600;
  bool over = false, paused = false;
  uint32_t tFall = 0;

  auto collides = [&](int nx, int ny, int nrot) -> bool {
    uint16_t m = TET_SHAPES[shape][nrot];
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++)
        if (m & (0x8000 >> (r * 4 + c))) {
          int fx = nx + c, fy = ny + r;
          if (fx < 0 || fx >= TW || fy >= TH) return true;
          if (fy >= 0 && (field[fy] & (1 << fx))) return true;
        }
    return false;
  };

  auto spawn = [&]() {
    shape = nextShape; nextShape = random(7);
    rot = 0; px = 3; py = -1;
    if (collides(px, 0, rot)) over = true;
  };

  auto lockAndClear = [&]() {
    uint16_t m = TET_SHAPES[shape][rot];
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++)
        if (m & (0x8000 >> (r * 4 + c))) {
          int fy = py + r;
          if (fy < 0) { over = true; return; }
          field[fy] |= (1 << (px + c));
        }
    int cleared = 0;
    for (int y = TH - 1; y >= 0; y--) {
      bool full = (field[y] == 0x3FF);
      if (full) {
        cleared++;
        for (int yy = y; yy > 0; yy--) field[yy] = field[yy - 1];
        field[0] = 0;
        y++;
      }
    }
    if (cleared) {
      lines += cleared;
      score += cleared == 1 ? 100 : cleared == 2 ? 300 : cleared == 3 ? 500 : 800;
      level = 1 + lines / 8;
      speed = 600 - (level - 1) * 60;
      if (speed < 120) speed = 120;
      beep(1200 + cleared * 150, 90);
    } else beep(400, 30);
  };

  // заставка GO
  d.clearDisplay(); headerBar("TETRIS");
  d.setTextColor(1); d.setTextSize(3); d.setCursor(44, 24); d.print("GO!");
  d.display(); beep(1500, 60); delay(350);

  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) { beep(400, 100); return; }
    if (e == EV_OK && over) { appTetris(); return; }        // рестарт
    if (e == EV_OK && !over) { paused = !paused; beep(paused ? 700 : 1400, 40); }

    if (!over && !paused) {
      if (e == EV_LEFT  && !collides(px - 1, py, rot)) { px--; beep(800, 12); }
      if (e == EV_RIGHT && !collides(px + 1, py, rot)) { px++; beep(800, 12); }
      if (e == EV_UP) {  // поворот (с простым wall-kick)
        if (!collides(px, py, (rot + 1) % 4)) { rot = (rot + 1) % 4; beep(1000, 15); }
        else if (!collides(px - 1, py, (rot + 1) % 4)) { px--; rot = (rot + 1) % 4; beep(1000, 15); }
        else if (!collides(px + 1, py, (rot + 1) % 4)) { px++; rot = (rot + 1) % 4; beep(1000, 15); }
      }

      int fallDelay = (axY() == 1) ? speed / 5 : speed;   // стик вниз = быстрое падение
      if (millis() - tFall >= (uint32_t)fallDelay) {
        tFall = millis();
        if (!collides(px, py + 1, rot)) py++;
        else { lockAndClear(); if (!over) spawn(); }
      }
    }

    // ---- отрисовка ----
    d.clearDisplay();
    char hb[26];
    snprintf(hb, sizeof hb, over ? "GAME OVER" : "TETRIS L%d S:%d", level, score);
    headerBar(hb);
    d.drawRect(OX - 1, OY - 1, TW * CELL + 2, TH * CELL + 2, 1);
    // стакан
    for (int y = 0; y < TH; y++)
      for (int x = 0; x < TW; x++)
        if (field[y] & (1 << x))
          d.fillRect(OX + x * CELL, OY + y * CELL, CELL - 1, CELL - 1, 1);
    // текущая фигура
    if (!over && !paused) {
      uint16_t m = TET_SHAPES[shape][rot];
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
          if (m & (0x8000 >> (r * 4 + c))) {
            int fx = px + c, fy = py + r;
            if (fy >= 0) d.fillRect(OX + fx * CELL, OY + fy * CELL, CELL - 1, CELL - 1, 1);
          }
    }
    // справа: счёт и следующая фигура
    d.setTextColor(1); d.setTextSize(1);
    int sx = OX + TW * CELL + 8;
    d.setCursor(sx, 16); d.print("NEXT");
    uint16_t nm = TET_SHAPES[nextShape][0];
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++)
        if (nm & (0x8000 >> (r * 4 + c)))
          d.fillRect(sx + c * 4, 28 + r * 4, 3, 3, 1);
    d.setCursor(sx, 50); d.print("S:"); d.print(score);
    if (paused) {
      d.fillRect(30, 24, 68, 16, 1);
      d.setTextColor(0); d.setCursor(43, 27); d.print("PAUSE");
    }
    if (over) {
      d.fillRect(16, 24, 96, 18, 1);
      d.setTextColor(0); d.setTextSize(1);
      d.setCursor(24, 26); d.print("GAME OVER");
      d.setCursor(24, 35); d.printf("S:%d L%d", score, level);
      d.setTextColor(1);
      d.setCursor(20, 46); d.print("click=retry hold=exit");
    }
    d.display();
  }
}

// ==================================================================
//  GAME: 2048
// ==================================================================
void app2048() {
  uint8_t tiles[16];        // 0 = пусто; иначе log2(значение): 1=2, 2=4, 3=8...
  int score = 0;
  bool moved = false, over = false, won = false;

restart:
  memset(tiles, 0, sizeof tiles);
  score = 0; over = false; won = false;

  auto addRandom = [&]() {
    int empt[16], n = 0;
    for (int i = 0; i < 16; i++) if (tiles[i] == 0) empt[n++] = i;
    if (n == 0) return;
    tiles[empt[random(n)]] = (random(10) < 9) ? 1 : 2;
  };
  addRandom(); addRandom();

  auto canMove = [&]() -> bool {
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++) {
        int i = r * 4 + c;
        if (tiles[i] == 0) return true;
        if (c < 3 && (tiles[i] == tiles[i + 1] || tiles[i + 1] == 0)) return true;
        if (r < 3 && (tiles[i] == tiles[i + 4] || tiles[i + 4] == 0)) return true;
      }
    return false;
  };

  // сдвиг влево одной строки (4 ячейки), возврат: был ли ход
  auto slideRow = [&](int a[4]) -> bool {
    bool ch = false;
    int tmp[4] = {0,0,0,0};
    int t = 0;
    for (int i = 0; i < 4; i++)
      if (a[i]) tmp[t++] = a[i];
    for (int i = 0; i < 3; i++) {
      if (tmp[i] && tmp[i] == tmp[i + 1]) {
        tmp[i]++;
        score += (1 << tmp[i]);
        for (int j = i + 1; j < 3; j++) tmp[j] = tmp[j + 1];
        tmp[3] = 0;
        ch = true;
      }
    }
    for (int i = 0; i < 4; i++)
      if (a[i] != tmp[i]) ch = true;
    memcpy(a, tmp, sizeof tmp);
    return ch;
  };

  auto move = [&](int dir) -> bool {  // 0=left 1=right 2=up 3=down
    bool ch = false;
    int line[4];
    for (int k = 0; k < 4; k++) {
      if (dir == 0 || dir == 1) {
        int r = k;
        for (int i = 0; i < 4; i++) line[i] = tiles[r * 4 + i];
        if (dir == 1) { int x[4]; for (int i = 0; i < 4; i++) x[i] = line[3 - i]; memcpy(line, x, sizeof x); }
        if (slideRow(line)) ch = true;
        if (dir == 1) { int x[4]; for (int i = 0; i < 4; i++) x[i] = line[3 - i]; memcpy(line, x, sizeof x); }
        for (int i = 0; i < 4; i++) tiles[r * 4 + i] = line[i];
      } else {
        int c = k;
        for (int i = 0; i < 4; i++) line[i] = tiles[i * 4 + c];
        if (dir == 3) { int x[4]; for (int i = 0; i < 4; i++) x[i] = line[3 - i]; memcpy(line, x, sizeof x); }
        if (slideRow(line)) ch = true;
        if (dir == 3) { int x[4]; for (int i = 0; i < 4; i++) x[i] = line[3 - i]; memcpy(line, x, sizeof x); }
        for (int i = 0; i < 4; i++) tiles[i * 4 + c] = line[i];
      }
    }
    return ch;
  };

  auto tileLabel = [&](uint8_t v, char* out) {
    if (v == 0) { out[0] = 0; return; }
    int num = 1 << v;
    if (num >= 1024) snprintf(out, 6, "%dK", num / 1024);
    else snprintf(out, 6, "%d", num);
  };

  d.clearDisplay(); headerBar("2048");
  d.setTextColor(1); d.setTextSize(1);
  d.setCursor(38, 30); d.print("GO!");
  d.display(); beep(1500, 60); delay(350);

  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) { beep(400, 100); return; }
    if (e == EV_OK && over) { beep(1200, 40); goto restart; }

    if (!over) {
      int dir = -1;
      if (e == EV_LEFT)  dir = 0;
      if (e == EV_RIGHT) dir = 1;
      if (e == EV_UP)    dir = 2;
      if (e == EV_DOWN)  dir = 3;
      if (dir >= 0) {
        if (move(dir)) {
          addRandom();
          beep(900, 20);
          if (!won) for (int i = 0; i < 16; i++) if (tiles[i] >= 11) { won = true; beepWait(1000, 80); beepWait(1500, 120); }
          if (!canMove()) { over = true; beepWait(250, 200); beepWait(150, 350); }
        } else beep(200, 40);
      }
    }

    d.clearDisplay();
    char hb[22]; snprintf(hb, sizeof hb, "2048  S:%d", score);
    headerBar(over ? "GAME OVER" : (won ? "YOU WIN 2048!" : hb));

    // поле 4x4, клетки 12px, центр: x=40, y=16
    const int BX = 40, BY = 16, TS = 12;
    for (int r = 0; r <= 4; r++) {
      d.drawFastHLine(BX, BY + r * TS, 4 * TS + 1, 1);
      d.drawFastVLine(BX + r * TS, BY, 4 * TS + 1, 1);
    }
    for (int r = 0; r < 4; r++)
      for (int c = 0; c < 4; c++) {
        uint8_t v = tiles[r * 4 + c];
        if (v == 0) continue;
        char lbl[6];
        tileLabel(v, lbl);
        if (v >= 7) {            // большие числа — заливка + инверсный текст
          d.fillRect(BX + c * TS + 1, BY + r * TS + 1, TS - 1, TS - 1, 1);
          d.setTextColor(0);
        } else {
          d.drawRect(BX + c * TS + 1, BY + r * TS + 1, TS - 1, TS - 1, 1);
          d.setTextColor(1);
        }
        u8g2.setFont(FONT4);
        int tw2 = u8g2.getStrWidth(lbl);
        u8g2.drawUTF8(BX + c * TS + (TS - tw2) / 2, BY + r * TS + (TS - 5) / 2 + u8g2.getAscent(), lbl);
      }
    // подсказка слева
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(4, 30); d.print("2048");
    d.setCursor(4, 44); d.printf("S:%d", score);
    if (over) {
      d.fillRect(30, 26, 68, 14, 1);
      d.setTextColor(0); d.setTextSize(1);
      d.setCursor(34, 29); d.print("GAME OVER");
      d.setTextColor(1);
      d.setCursor(30, 44); d.print("click=retry");
    }
    d.display();
  }
}

// ==================================================================
//  GAME: PONG (vs CPU)
// ==================================================================
void appPong() {
  const int PW = 20, PH = 3;         // ракетка 20x3
  int plX = 54, cpuX = 54;          // позиции ракеток (центр)
  float bx = 64, by = 36, vx = 1.6, vy = 1.1;
  int plScore = 0, cpuScore = 0;
  bool over = false, paused = false;
  const int WINS = 5;
  uint32_t tPrev = 0;

  d.clearDisplay(); headerBar("PONG");
  d.setTextColor(1); d.setTextSize(1);
  d.setCursor(20, 28); d.print("first to 5 wins");
  d.display(); beep(1200, 60); delay(700);

  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) { beep(400, 100); return; }
    if (e == EV_OK && over) { plScore = cpuScore = 0; over = false; bx = 64; by = 36; vx = 1.6; vy = 1.1; beep(1200, 40); }
    if (e == EV_OK && !over) { paused = !paused; beep(paused ? 700 : 1400, 40); }

    if (!over && !paused && millis() - tPrev >= 16) {
      tPrev = millis();
      // игрок
      if (e == EV_LEFT)  plX -= 3;
      if (e == EV_RIGHT) plX += 3;
      if (plX < PW / 2) plX = PW / 2;
      if (plX > W - PW / 2) plX = W - PW / 2;
      // CPU: догоняет мяч с макс скоростью 2 (не идеален)
      int target = (int)bx - PW / 2;
      if (cpuX < target - 1) cpuX += 2;
      else if (cpuX > target + 1) cpuX -= 2;
      if (cpuX < PW / 2) cpuX = PW / 2;
      if (cpuX > W - PW / 2) cpuX = W - PW / 2;

      bx += vx; by += vy;
      // стены
      if (bx < 2) { bx = 2; vx = -vx; beep(700, 12); }
      if (bx > W - 3) { bx = W - 3; vx = -vx; beep(700, 12); }
      // ракетки
      if (by <= 16 && vy < 0) {
        if (bx > cpuX - PW / 2 - 2 && bx < cpuX + PW / 2 + 2) {
          vy = -vy; vx += (bx - cpuX) * 0.15; beep(1000, 15);
          if (vx > 2.4) vx = 2.4; if (vx < -2.4) vx = -2.4;
        }
      }
      if (by >= 60 && vy > 0) {
        if (bx > plX - PW / 2 - 2 && bx < plX + PW / 2 + 2) {
          vy = -vy; vx += (bx - plX) * 0.15; beep(1000, 15);
          if (vx > 2.4) vx = 2.4; if (vx < -2.4) vx = -2.4;
        }
      }
      // голы
      if (by < 14) { plScore++; beep(1400, 60); delay(300); bx = 64; by = 36; vx = 1.6; vy = 1.1; }
      if (by > 61) { cpuScore++; beep(300, 100); delay(300); bx = 64; by = 36; vx = -1.6; vy = -1.1; }
      if (plScore >= WINS || cpuScore >= WINS) {
        over = true;
        beepWait(over && plScore > cpuScore ? 1500 : 250, 200);
      }
    }

    d.clearDisplay();
    char hb[22]; snprintf(hb, sizeof hb, "PONG  %d : %d", plScore, cpuScore);
    headerBar(hb);
    d.drawFastHLine(0, 14, W, 1);
    // центральная разметка
    for (int y = 16; y < 62; y += 6) d.drawFastHLine(63, y, 2, 1);
    // ракетки
    d.fillRect(cpuX - PW / 2, 15, PW, PH, 1);
    d.fillRect(plX - PW / 2, 60, PW, PH, 1);
    // мяч
    d.fillCircle((int)bx, (int)by, 2, 1);
    if (paused) {
      d.fillRect(40, 30, 48, 14, 1);
      d.setTextColor(0); d.setCursor(52, 33); d.print("PAUSE");
    }
    if (over) {
      d.fillRect(20, 24, 88, 16, 1);
      d.setTextColor(0); d.setTextSize(1);
      d.setCursor(24, 27); d.print(plScore > cpuScore ? "YOU WIN!" : "CPU WINS");
      d.setCursor(28, 36); d.printf("%d : %d", plScore, cpuScore);
      d.setTextColor(1);
      d.setCursor(24, 46); d.print("click=again hold=exit");
    }
    d.display();
  }
}

// ==================================================================
//  WIFI SCANNER
// ==================================================================
void appWifi() {
  d.clearDisplay(); headerBar("WIFI SCAN");
  d.setTextColor(1); d.setTextSize(1);
  d.setCursor(10, 30); d.print("Scanning...");
  d.display(); delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  int n = WIFI_SCAN_RUNNING;
  while (n == WIFI_SCAN_RUNNING) {
    if (pollEvent() == EV_EXIT) { WiFi.scanDelete(); WiFi.mode(WIFI_OFF); beep(400, 100); return; }
    d.clearDisplay(); headerBar("WIFI SCAN");
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
  if (n < 0) n = 0;
  beep(1300, 80);

  int sel, n2 = n;
  bool chosen = false;
  netListScreen(sel, chosen, n2);
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
  if (chosen) return appWifi();
}

// ==================================================================
//  CUBE 3D (hidden app, доступен через ABOUT? нет — оставим в коде, вызовем из CUBE пункта нет)
//  (куб остался в прошивке как easter egg — запускается из ABOUT стр.5)
// ==================================================================
void miniCube(int mx, int my, uint32_t t) {
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

void appCube() {
  float angX = 0.4, angY = 0.2;
  bool autoRot = true;
  uint32_t tFps = millis(); int frames = 0, fps = 0;
  const int8_t P[8][3] = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},{-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
  const uint8_t E[12][2] = {{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};

  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) { beep(400, 100); return; }
    if (e == EV_OK) { autoRot = !autoRot; beep(autoRot ? 1400 : 700, 40); }

    float vx = axX() * 0.06, vy = axY() * 0.06;
    if (vx != 0 || vy != 0) { angY += vx; angX += vy; }
    else if (autoRot) { angY += 0.022; angX += 0.013; }

    d.clearDisplay();
    headerBar("CUBE 3D");
    float cA = cos(angX), sA = sin(angX), cB = cos(angY), sB = sin(angY);
    int px[8], py[8];
    for (int i = 0; i < 8; i++) {
      float x = P[i][0], y = P[i][1], z = P[i][2];
      float ry = y * cA - z * sA, rz = y * sA + z * cA;
      float rx = x * cB + rz * sB;
      float per = 4.0f / (4.0f + (-x * sB + rz * cB));
      px[i] = 64 + (int)(rx * 16 * per);
      py[i] = 34 - (int)(ry * 16 * per);
    }
    for (int i = 0; i < 12; i++) d.drawLine(px[E[i][0]], py[E[i][0]], px[E[i][1]], py[E[i][1]]);
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(2, 54); d.printf("FPS %d  %s", fps, autoRot ? "auto" : "stick");
    d.display();
    frames++;
    if (millis() - tFps >= 1000) { fps = frames; frames = 0; tFps = millis(); }
  }
}

// ==================================================================
//  ABOUT
// ==================================================================
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
      d.setCursor(2, 16); d.print("C3 XLAMPER v0.11");
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
      int x = readX(), y = readY();
      d.setTextColor(1); d.setTextSize(1);
      d.setCursor(2, 16); d.printf("X=%4d Y=%4d", x, y);
      d.setCursor(2, 28); d.printf("c %d/%d  %d|%d", cx, cy, loX, hiX);
      int px = map(x, loX, hiX, 66, 126), py = map(y, loY, hiY, 42, 62);
      d.drawRect(64, 40, 64, 24, 1);
      d.fillRect(px - 2, py - 2, 5, 5, 1);
      d.drawFastHLine(px - 8, py, 5, 1); d.drawFastHLine(px + 4, py, 5, 1);
      d.drawFastVLine(px, py - 8, 5, 1); d.drawFastVLine(px, py + 4, 5, 1);
    } else if (page == 3) {
      headerBar("STICK ROTATION");
      d.setTextColor(1); d.setTextSize(1);
      d.setCursor(2, 16); d.print("stick angle:");
      d.setTextSize(3);
      d.setCursor(52, 28); d.printf("%d", stickRot);
      d.setTextSize(1);
      d.setCursor(2, 52); d.print("click = rotate 90");
      if (e == EV_OK && !actH) {
        actH = true;
        stickRot = (stickRot + 90) % 360;
        prefs.begin("c3flip", false); prefs.putUChar("rot", stickRot); prefs.end();
        beep(900 + stickRot, 60);
      }
    } else if (page == 4) {
      headerBar("DISPLAY ROT");
      d.setTextColor(1); d.setTextSize(1);
      d.setCursor(2, 16); d.print("screen orientation:");
      d.setTextSize(3);
      d.setCursor(52, 28); d.printf("R%d", dispRot);
      d.setTextSize(1);
      d.setCursor(2, 52); d.print("click = rotate 90");
      if (e == EV_OK && !actH) {
        actH = true;
        d.setRotation((dispRot + 1) % 4);
        prefs.begin("c3flip", false); prefs.putUChar("drot", dispRot); prefs.end();
        beep(900 + dispRot * 100, 60);
      }
    } else if (page == 5) {
      // easter egg: интерактивный куб
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

// ==================================================================
//  SETTINGS
// ==================================================================
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
        prefs.begin("c3flip", false); prefs.putUChar("rot", stickRot); prefs.end();
        beep(900 + stickRot, 60);
        d.clearDisplay(); headerBar("SETTINGS");
        d.setTextColor(1); d.setTextSize(1);
        d.setCursor(4, 26); d.print("Stick angle -> 90");
        d.display(); delay(900);
      } else if (sel == 1) {
        prefs.begin("c3flip", false); prefs.remove("snake_hi"); prefs.end();
        beep(1200, 60);
        d.clearDisplay(); headerBar("SETTINGS");
        d.setTextColor(1); d.setTextSize(1);
        d.setCursor(4, 26); d.print("Snake record cleared");
        d.display(); delay(900);
      } else if (sel == 2) {
        forgetWifi();
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
          d.setCursor(4, 26); d.print("(rotation, wifi, record)");
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
          prefs.begin("c3flip", false); prefs.clear(); prefs.end();
          stickRot = 90;
          dispRot = 0;
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

// POUR.XLA -> C array (generated by gen_pour_array.py, не редактировать руками)
// 1553 bytes
static const uint8_t POUR_XLA[] PROGMEM = {
  0x58,0x4C,0x41,0x31,0x01,0x00,0xB1,0x05,0x18,0x00,0x34,0x00,0x00,0x00,0x04,0x00,
  0x50,0x4F,0x55,0x52,0x01,0x00,0x00,0x45,0x00,0x00,0x01,0x00,0x00,0x45,0x01,0x00,
  0x01,0x00,0x00,0x45,0x02,0x00,0x01,0x00,0x00,0x45,0x03,0x00,0x01,0x00,0x00,0x45,
  0x05,0x00,0x01,0x00,0x00,0x75,0x2F,0x00,0x45,0x04,0x00,0x5F,0x82,0x02,0x01,0x02,
  0x00,0x30,0x52,0x12,0x00,0x02,0x01,0x06,0x00,0x30,0x52,0x14,0x00,0x03,0x01,0x00,
  0x00,0x45,0x02,0x00,0x50,0x11,0x00,0x03,0x01,0x01,0x00,0x45,0x02,0x00,0x50,0x07,
  0x00,0x03,0x01,0xFF,0xFF,0x45,0x02,0x00,0x83,0x02,0x01,0x06,0x00,0x30,0x52,0x0C,
  0x00,0x02,0x01,0x05,0x00,0x30,0x52,0x06,0x00,0x03,0x50,0xAF,0x00,0x03,0x73,0x03,
  0x46,0x05,0x00,0x51,0x1F,0x00,0x46,0x05,0x00,0x01,0x01,0x00,0x30,0x52,0x31,0x00,
  0x01,0x00,0x00,0x45,0x05,0x00,0x01,0x00,0x00,0x45,0x00,0x00,0x01,0x00,0x00,0x45,
  0x01,0x00,0x50,0x87,0x00,0x01,0x01,0x00,0x45,0x05,0x00,0x01,0x00,0x00,0x45,0x00,
  0x00,0x01,0x00,0x00,0x45,0x01,0x00,0x01,0x84,0x03,0x01,0x50,0x00,0x72,0x50,0x6B,
  0x00,0x46,0x00,0x00,0x01,0x55,0x00,0x35,0x51,0x30,0x00,0x01,0x02,0x00,0x45,0x05,
  0x00,0x46,0x03,0x00,0x01,0x03,0x00,0x20,0x45,0x03,0x00,0x46,0x03,0x00,0x46,0x04,
  0x00,0x34,0x51,0x0C,0x00,0x46,0x03,0x00,0x45,0x04,0x00,0x46,0x04,0x00,0x74,0x2F,
  0x00,0x01,0x40,0x06,0x01,0x78,0x00,0x72,0x50,0x31,0x00,0x46,0x00,0x00,0x01,0x32,
  0x00,0x35,0x51,0x1A,0x00,0x01,0x02,0x00,0x45,0x05,0x00,0x46,0x03,0x00,0x01,0x01,
  0x00,0x20,0x45,0x03,0x00,0x01,0x4C,0x04,0x01,0x64,0x00,0x72,0x50,0x0D,0x00,0x01,
  0x02,0x00,0x45,0x05,0x00,0x01,0xDC,0x00,0x01,0x2C,0x01,0x72,0x46,0x05,0x00,0x01,
  0x01,0x00,0x30,0x52,0x0B,0x00,0x70,0x01,0xC8,0x00,0x24,0x45,0x06,0x00,0x50,0x49,
  0x00,0x46,0x02,0x00,0x52,0x09,0x00,0x01,0x02,0x00,0x45,0x07,0x00,0x50,0x06,0x00,
  0x01,0x05,0x00,0x45,0x07,0x00,0x46,0x00,0x00,0x46,0x07,0x00,0x20,0x02,0x01,0x64,
  0x00,0x34,0x51,0x04,0x00,0x03,0x01,0x64,0x00,0x45,0x00,0x00,0x46,0x01,0x00,0x46,
  0x07,0x00,0x20,0x02,0x01,0x64,0x00,0x34,0x51,0x04,0x00,0x03,0x01,0x64,0x00,0x45,
  0x01,0x00,0x70,0x01,0x64,0x00,0x24,0x45,0x06,0x00,0x6A,0x01,0x16,0x00,0x01,0x2A,
  0x00,0x01,0x18,0x00,0x01,0x10,0x00,0x01,0x01,0x00,0x62,0x01,0x2E,0x00,0x01,0x2D,
  0x00,0x01,0x04,0x00,0x01,0x08,0x00,0x01,0x00,0x00,0x62,0x01,0x2F,0x00,0x01,0x2C,
  0x00,0x01,0x03,0x00,0x01,0x0A,0x00,0x01,0x01,0x00,0x62,0x01,0x2E,0x00,0x01,0x2E,
  0x00,0x01,0x02,0x00,0x01,0x06,0x00,0x01,0x00,0x00,0x62,0x46,0x00,0x00,0x01,0x0E,
  0x00,0x22,0x01,0x64,0x00,0x23,0x45,0x08,0x00,0x46,0x08,0x00,0x01,0x01,0x00,0x32,
  0x51,0x06,0x00,0x01,0x01,0x00,0x45,0x08,0x00,0x01,0x38,0x00,0x46,0x08,0x00,0x21,
  0x45,0x09,0x00,0x01,0x00,0x00,0x45,0x0A,0x00,0x46,0x0A,0x00,0x46,0x08,0x00,0x32,
  0x51,0x27,0x00,0x01,0x37,0x00,0x46,0x0A,0x00,0x21,0x45,0x0B,0x00,0x01,0x18,0x00,
  0x46,0x0B,0x00,0x01,0x14,0x00,0x01,0x01,0x00,0x01,0x01,0x00,0x62,0x46,0x0A,0x00,
  0x01,0x01,0x00,0x20,0x45,0x0A,0x00,0x50,0xCF,0xFF,0x46,0x01,0x00,0x51,0xAC,0x00,
  0x46,0x01,0x00,0x01,0x0A,0x00,0x34,0x51,0x3F,0x00,0x70,0x01,0x64,0x00,0x24,0x01,
  0x02,0x00,0x24,0x51,0x66,0x00,0x01,0x1A,0x00,0x01,0x28,0x00,0x01,0x02,0x00,0x01,
  0x02,0x00,0x01,0x01,0x00,0x66,0x01,0x22,0x00,0x01,0x28,0x00,0x01,0x02,0x00,0x01,
  0x02,0x00,0x01,0x01,0x00,0x66,0x01,0x2A,0x00,0x01,0x28,0x00,0x01,0x02,0x00,0x01,
  0x02,0x00,0x01,0x01,0x00,0x66,0x50,0x63,0x00,0x01,0x1A,0x00,0x01,0x29,0x00,0x01,
  0x02,0x00,0x01,0x01,0x00,0x01,0x01,0x00,0x66,0x01,0x22,0x00,0x01,0x29,0x00,0x01,
  0x02,0x00,0x01,0x01,0x00,0x01,0x01,0x00,0x66,0x01,0x2A,0x00,0x01,0x29,0x00,0x01,
  0x02,0x00,0x01,0x01,0x00,0x01,0x01,0x00,0x66,0x50,0x30,0x00,0x01,0x1C,0x00,0x01,
  0x27,0x00,0x01,0x02,0x00,0x01,0x02,0x00,0x01,0x01,0x00,0x66,0x01,0x24,0x00,0x01,
  0x27,0x00,0x01,0x02,0x00,0x01,0x02,0x00,0x01,0x01,0x00,0x66,0x01,0x2C,0x00,0x01,
  0x27,0x00,0x01,0x02,0x00,0x01,0x02,0x00,0x01,0x01,0x00,0x66,0x01,0x62,0x00,0x01,
  0x16,0x00,0x01,0x09,0x00,0x01,0x01,0x00,0x65,0x01,0x5A,0x00,0x01,0x0F,0x00,0x01,
  0x03,0x00,0x01,0x01,0x00,0x65,0x01,0x6A,0x00,0x01,0x0F,0x00,0x01,0x03,0x00,0x01,
  0x01,0x00,0x65,0x70,0x01,0x60,0x09,0x24,0x01,0xC8,0x00,0x32,0x52,0x1D,0x00,0x01,
  0x5E,0x00,0x01,0x14,0x00,0x01,0x01,0x00,0x01,0x01,0x00,0x65,0x01,0x66,0x00,0x01,
  0x14,0x00,0x01,0x01,0x00,0x01,0x01,0x00,0x65,0x50,0x20,0x00,0x01,0x5D,0x00,0x01,
  0x14,0x00,0x01,0x5F,0x00,0x01,0x14,0x00,0x01,0x01,0x00,0x61,0x01,0x65,0x00,0x01,
  0x14,0x00,0x01,0x67,0x00,0x01,0x14,0x00,0x01,0x01,0x00,0x61,0x01,0x62,0x00,0x01,
  0x19,0x00,0x01,0x03,0x00,0x01,0x01,0x00,0x65,0x01,0x61,0x00,0x01,0x18,0x00,0x01,
  0x00,0x00,0x60,0x01,0x63,0x00,0x01,0x18,0x00,0x01,0x00,0x00,0x60,0x01,0x5F,0x00,
  0x01,0x1D,0x00,0x01,0x01,0x00,0x60,0x01,0x60,0x00,0x01,0x1E,0x00,0x01,0x01,0x00,
  0x60,0x01,0x61,0x00,0x01,0x1F,0x00,0x01,0x01,0x00,0x60,0x01,0x62,0x00,0x01,0x1F,
  0x00,0x01,0x01,0x00,0x60,0x01,0x63,0x00,0x01,0x1F,0x00,0x01,0x01,0x00,0x60,0x01,
  0x64,0x00,0x01,0x1E,0x00,0x01,0x01,0x00,0x60,0x01,0x65,0x00,0x01,0x1D,0x00,0x01,
  0x01,0x00,0x60,0x01,0x62,0x00,0x01,0x2C,0x00,0x01,0x09,0x00,0x01,0x0C,0x00,0x01,
  0x01,0x00,0x66,0x01,0x62,0x00,0x01,0x2E,0x00,0x01,0x05,0x00,0x01,0x07,0x00,0x01,
  0x00,0x00,0x66,0x46,0x02,0x00,0x51,0xB0,0x00,0x46,0x02,0x00,0x01,0x00,0x00,0x34,
  0x52,0x53,0x00,0x01,0x5A,0x00,0x01,0x26,0x00,0x01,0x54,0x00,0x01,0x22,0x00,0x01,
  0x01,0x00,0x61,0x01,0x54,0x00,0x01,0x22,0x00,0x01,0x52,0x00,0x01,0x22,0x00,0x01,
  0x01,0x00,0x61,0x01,0x52,0x00,0x01,0x22,0x00,0x01,0x52,0x00,0x01,0x28,0x00,0x01,
  0x01,0x00,0x61,0x01,0x52,0x00,0x01,0x28,0x00,0x01,0x56,0x00,0x01,0x28,0x00,0x01,
  0x01,0x00,0x61,0x01,0x56,0x00,0x01,0x28,0x00,0x01,0x56,0x00,0x01,0x22,0x00,0x01,
  0x01,0x00,0x61,0x50,0xA3,0x00,0x01,0x5A,0x00,0x01,0x26,0x00,0x01,0x5E,0x00,0x01,
  0x20,0x00,0x01,0x01,0x00,0x61,0x01,0x5E,0x00,0x01,0x20,0x00,0x01,0x60,0x00,0x01,
  0x1E,0x00,0x01,0x01,0x00,0x61,0x01,0x60,0x00,0x01,0x1E,0x00,0x01,0x60,0x00,0x01,
  0x18,0x00,0x01,0x01,0x00,0x61,0x01,0x60,0x00,0x01,0x18,0x00,0x01,0x64,0x00,0x01,
  0x18,0x00,0x01,0x01,0x00,0x61,0x01,0x64,0x00,0x01,0x18,0x00,0x01,0x64,0x00,0x01,
  0x1E,0x00,0x01,0x01,0x00,0x61,0x50,0x50,0x00,0x01,0x5A,0x00,0x01,0x26,0x00,0x01,
  0x56,0x00,0x01,0x22,0x00,0x01,0x01,0x00,0x61,0x01,0x56,0x00,0x01,0x22,0x00,0x01,
  0x52,0x00,0x01,0x22,0x00,0x01,0x01,0x00,0x61,0x01,0x52,0x00,0x01,0x22,0x00,0x01,
  0x52,0x00,0x01,0x28,0x00,0x01,0x01,0x00,0x61,0x01,0x52,0x00,0x01,0x28,0x00,0x01,
  0x56,0x00,0x01,0x28,0x00,0x01,0x01,0x00,0x61,0x01,0x56,0x00,0x01,0x28,0x00,0x01,
  0x56,0x00,0x01,0x22,0x00,0x01,0x01,0x00,0x61,0x46,0x05,0x00,0x01,0x01,0x00,0x30,
  0x52,0x03,0x00,0x50,0x49,0x00,0x46,0x06,0x00,0x01,0x02,0x00,0x24,0x51,0x21,0x00,
  0x01,0x3C,0x00,0x01,0x24,0x00,0x01,0x01,0x00,0x60,0x01,0x3A,0x00,0x01,0x26,0x00,
  0x01,0x01,0x00,0x60,0x01,0x37,0x00,0x01,0x28,0x00,0x01,0x01,0x00,0x60,0x50,0x1E,
  0x00,0x01,0x3D,0x00,0x01,0x24,0x00,0x01,0x01,0x00,0x60,0x01,0x3B,0x00,0x01,0x26,
  0x00,0x01,0x01,0x00,0x60,0x01,0x38,0x00,0x01,0x28,0x00,0x01,0x01,0x00,0x60,0x46,
  0x05,0x00,0x01,0x02,0x00,0x30,0x52,0x03,0x00,0x50,0x6D,0x00,0x01,0x00,0x00,0x69,
  0x01,0x0E,0x00,0x01,0x0C,0x00,0x01,0x64,0x00,0x01,0x28,0x00,0x01,0x01,0x00,0x62,
  0x01,0x18,0x00,0x01,0x10,0x00,0x01,0x01,0x00,0x67,0x00,0x00,0x46,0x00,0x00,0x01,
  0x55,0x00,0x35,0x51,0x0F,0x00,0x01,0x28,0x00,0x01,0x1E,0x00,0x01,0x01,0x00,0x67,
  0x05,0x00,0x50,0x25,0x00,0x46,0x00,0x00,0x01,0x32,0x00,0x35,0x51,0x0F,0x00,0x01,
  0x28,0x00,0x01,0x1E,0x00,0x01,0x01,0x00,0x67,0x0E,0x00,0x50,0x0C,0x00,0x01,0x28,
  0x00,0x01,0x1E,0x00,0x01,0x01,0x00,0x67,0x13,0x00,0x01,0x18,0x00,0x01,0x2A,0x00,
  0x01,0x01,0x00,0x67,0x1A,0x00,0x50,0x18,0x00,0x01,0x04,0x00,0x01,0x37,0x00,0x01,
  0x01,0x00,0x67,0x1A,0x00,0x01,0x18,0x00,0x01,0x10,0x00,0x01,0x01,0x00,0x67,0x00,
  0x00,0x6B,0x50,0x76,0xFA,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
  0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x50,0x4F,0x55,
  0x52,0x00,0x50,0x45,0x52,0x46,0x45,0x43,0x54,0x21,0x00,0x47,0x4F,0x4F,0x44,0x00,
  0x42,0x41,0x44,0x2E,0x2E,0x2E,0x00,0x63,0x6C,0x69,0x63,0x6B,0x3A,0x70,0x6F,0x75,
  0x72,0x20,0x68,0x6F,0x6C,0x64,0x3A,0x65,0x78,0x69,0x74,0x00,0x62,0x65,0x73,0x74,
  0x00
};
#define POUR_XLA_LEN 1553
// ==================================================================
//  XLA VM + STORE — облачные плагины (v1)
// ==================================================================
// (заголовки FS/SPIFFS/HTTPClient/WiFiClientSecure — в преамбуле скетча)

#define XLA_STACK   96
#define XLA_RET     16
#define XLA_FRAME_INSN 6000
#define XLA_MAXCODE 20000
#define XLA_MAXDATA 4096
#define XLA_MAXSTR  8000
#define XLA_TITLELEN 12
// репозиторий приложений:
#define XLA_MANIFEST "https://raw.githubusercontent.com/Olegu621/xlamper/main/manifest.txt"
#define XLA_APPBASE  "https://raw.githubusercontent.com/Olegu621/xlamper/main/apps/"

static uint8_t* xlaCode = nullptr;
static uint16_t xlaCodeSize = 0;
static uint8_t* xlaData = nullptr;
static uint16_t xlaDataSize = 0;
static int16_t* xlaDataW = nullptr;
static char*    xlaStrPool = nullptr;
static uint16_t xlaStrSize = 0;
static uint16_t xlaEntry = 0;
static uint16_t xlaPC = 0;
static int16_t  xlaStack[XLA_STACK];
static int16_t  xlaSP = 0;
static uint16_t xlaRetSt[XLA_RET];
static uint8_t  xlaRetSP = 0;
static Ev       xlaEv = EV_NONE;
static bool     xlaRunning = false;
static char     xlaTitle[XLA_TITLELEN + 1] = "";
static char     xlaErr[64] = "";
static uint16_t xlaErrPC = 0;

#define XR_FRAME 1
#define XR_HALT  2
#define XR_EXIT  3

static inline uint8_t xF8() {
  if (xlaPC >= xlaCodeSize) { snprintf(xlaErr, sizeof xlaErr, "PC OOB"); return 0; }
  return xlaCode[xlaPC++];
}
static inline int16_t xF16() {
  uint8_t lo = xF8(); uint8_t hi = xF8();
  return (int16_t)((hi << 8) | lo);
}
static inline const char* xStr(uint16_t off) {
  if (off >= xlaStrSize) { snprintf(xlaErr, sizeof xlaErr, "str OOB"); return ""; }
  return xlaStrPool + off;
}
static inline void xPUSH(int16_t v) {
  if (xlaSP < XLA_STACK) { xlaStack[xlaSP++] = v; return; }
  snprintf(xlaErr, sizeof xlaErr, "stack ovf");
}
static inline int16_t xPOP() {
  if (xlaSP > 0) return xlaStack[--xlaSP];
  snprintf(xlaErr, sizeof xlaErr, "stack und"); return 0;
}
#define XBAD() (xlaErr[0] != 0)

static int16_t xStick8() {
  int8_t dx = axX(), dy = axY();
  if (dx == 0 && dy == 0) return -1;
  if (dy == -1) return (dx == 0) ? 0 : ((dx == 1) ? 1 : 7);
  if (dy == 0)  return (dx == 1) ? 2 : 6;
  return (dx == 0) ? 4 : ((dx == 1) ? 3 : 5);
}

static uint8_t xlaStep() {
  uint32_t budget = XLA_FRAME_INSN;
  while (budget--) {
    if (XBAD()) return XR_HALT;
    uint8_t op = xF8();
    switch (op) {
      case 0x00: return XR_HALT;
      case 0x01: xPUSH(xF16()); break;
      case 0x02: { int16_t v = xPOP(); xPUSH(v); xPUSH(v); } break;
      case 0x03: xPOP(); break;
      case 0x04: { int16_t b = xPOP(), a = xPOP(); xPUSH(b); xPUSH(a); } break;
      case 0x05: { int16_t b = xPOP(), a = xPOP(); xPUSH(a); xPUSH(b); xPUSH(a); } break;
      case 0x06: { int16_t n = xPOP(); int16_t i = xlaSP - 1 - n;
                   xPUSH((n >= 0 && i >= 0 && i < XLA_STACK) ? xlaStack[i] : 0); } break;

      case 0x20: { int16_t b = xPOP(), a = xPOP(); xPUSH((int16_t)(a + b)); } break;
      case 0x21: { int16_t b = xPOP(), a = xPOP(); xPUSH((int16_t)(a - b)); } break;
      case 0x22: { int16_t b = xPOP(), a = xPOP(); xPUSH((int16_t)((int32_t)a * b)); } break;
      case 0x23: { int16_t b = xPOP(), a = xPOP();
                   if (b == 0) { snprintf(xlaErr, sizeof xlaErr, "div0"); } else xPUSH((int16_t)(a / b)); } break;
      case 0x24: { int16_t b = xPOP(), a = xPOP();
                   if (b == 0) { snprintf(xlaErr, sizeof xlaErr, "mod0"); } else xPUSH((int16_t)(a % b)); } break;
      case 0x25: { int16_t a = xPOP(); xPUSH((int16_t)-a); } break;
      case 0x26: { int16_t b = xPOP(), a = xPOP(); xPUSH(a < b ? a : b); } break;
      case 0x27: { int16_t b = xPOP(), a = xPOP(); xPUSH(a > b ? a : b); } break;
      case 0x28: { int16_t a = xPOP(); xPUSH(a < 0 ? (int16_t)-a : a); } break;

      case 0x30: { int16_t b = xPOP(), a = xPOP(); xPUSH(a == b); } break;
      case 0x31: { int16_t b = xPOP(), a = xPOP(); xPUSH(a != b); } break;
      case 0x32: { int16_t b = xPOP(), a = xPOP(); xPUSH(a <  b); } break;
      case 0x33: { int16_t b = xPOP(), a = xPOP(); xPUSH(a <= b); } break;
      case 0x34: { int16_t b = xPOP(), a = xPOP(); xPUSH(a >  b); } break;
      case 0x35: { int16_t b = xPOP(), a = xPOP(); xPUSH(a >= b); } break;
      case 0x36: { int16_t b = xPOP(), a = xPOP(); xPUSH((a != 0 && b != 0) ? 1 : 0); } break;
      case 0x37: { int16_t b = xPOP(), a = xPOP(); xPUSH((a != 0 || b != 0) ? 1 : 0); } break;
      case 0x38: { int16_t b = xPOP(), a = xPOP(); xPUSH(((a != 0) != (b != 0)) ? 1 : 0); } break;
      case 0x39: { int16_t a = xPOP(); xPUSH(a == 0 ? 1 : 0); } break;

      case 0x45: { int16_t idx = xF16(); int16_t v = xPOP();
                   if (idx < 0 || idx >= (int)(xlaDataSize / 2)) snprintf(xlaErr, sizeof xlaErr, "g OOB");
                   else xlaDataW[idx] = v; } break;
      case 0x46: { int16_t idx = xF16();
                   if (idx < 0 || idx >= (int)(xlaDataSize / 2)) snprintf(xlaErr, sizeof xlaErr, "g OOB");
                   else xPUSH(xlaDataW[idx]); } break;
      case 0x47: { int16_t idx = xPOP(); int16_t v = xPOP();
                   if (idx < 0 || idx >= (int)(xlaDataSize / 2)) snprintf(xlaErr, sizeof xlaErr, "g OOB");
                   else xlaDataW[idx] = v; } break;  // GSTOREI: (val, idx) со стека
      case 0x48: { int16_t idx = xPOP();
                   if (idx < 0 || idx >= (int)(xlaDataSize / 2)) snprintf(xlaErr, sizeof xlaErr, "g OOB");
                   else xPUSH(xlaDataW[idx]); } break;  // GLOADI: idx со стека

      case 0x50: { int16_t rel = xF16(); xlaPC = (uint16_t)(xlaPC + rel); } break;
      case 0x51: { int16_t rel = xF16(); int16_t c = xPOP(); if (!c) xlaPC = (uint16_t)(xlaPC + rel); } break;
      case 0x52: { int16_t rel = xF16(); int16_t c = xPOP(); if (c)  xlaPC = (uint16_t)(xlaPC + rel); } break;
      case 0x53: { int16_t rel = xF16();
                   if (xlaRetSP >= XLA_RET) snprintf(xlaErr, sizeof xlaErr, "ret ovf");
                   else { xlaRetSt[xlaRetSP++] = xlaPC; xlaPC = (uint16_t)(xlaPC + rel); } } break;
      case 0x54: { if (xlaRetSP == 0) snprintf(xlaErr, sizeof xlaErr, "ret und");
                   else xlaPC = xlaRetSt[--xlaRetSP]; } break;
      case 0x5F: return XR_FRAME;

      case 0x60: { int16_t c = xPOP(), y = xPOP(), x = xPOP();
                   if (x >= 0 && x < W && y >= 0 && y < H) d.drawPixel(x, y, c); } break;
      case 0x61: { int16_t c = xPOP(), y1 = xPOP(), x1 = xPOP(), y0 = xPOP(), x0 = xPOP();
                   d.drawLine(x0, y0, x1, y1, c); } break;
      case 0x62: { int16_t c = xPOP(), h = xPOP(), w = xPOP(), y = xPOP(), x = xPOP();
                   d.drawRect(x, y, w, h, c); } break;
      case 0x63: { int16_t c = xPOP(), h = xPOP(), w = xPOP(), y = xPOP(), x = xPOP();
                   d.fillRect(x, y, w, h, c); } break;
      case 0x64: { int16_t c = xPOP(), r = xPOP(), y = xPOP(), x = xPOP();
                   d.drawCircle(x, y, r, c); } break;
      case 0x65: { int16_t c = xPOP(), r = xPOP(), y = xPOP(), x = xPOP();
                   d.fillCircle(x, y, r, c); } break;
      case 0x66: { int16_t c = xPOP(), ry = xPOP(), rx = xPOP(), y = xPOP(), x = xPOP();
                   d.fillEllipse(x, y, rx, ry, c); } break;
      case 0x67: { const char* s = xStr((uint16_t)xF16()); int16_t f = xPOP(), y = xPOP(), x = xPOP();
                   d.setCursor(x, y); d.setTextSize(f); d.print(s); } break;
      case 0x68: d.invertDisplay(true); d.invertDisplay(false); break;
      case 0x69: { int16_t c = xPOP(); d.fillRect(0, 0, W, H, c); } break;
      case 0x6A: d.clearDisplay(); break;
      case 0x6B: d.display(); break;

      case 0x70: xPUSH((int16_t)(millis() & 0x7FFF)); break;
      case 0x71: { int16_t m = xPOP(); xPUSH(m <= 0 ? 0 : (int16_t)(esp_random() % m)); } break;
      case 0x72: { int16_t ms = xPOP(), f = xPOP(); beep(f, ms); } break;
      case 0x73: return XR_EXIT;
      case 0x74: { const char* k = xStr((uint16_t)xF16()); int16_t v = xPOP();
                   char key[32]; snprintf(key, sizeof key, "%s_%s", xlaTitle, k);
                   prefs.begin("xla", false);
                   prefs.putShort(key, v);
                   prefs.end(); } break;
      case 0x75: { const char* k = xStr((uint16_t)xF16()); int16_t def = xPOP();
                   char key[32]; snprintf(key, sizeof key, "%s_%s", xlaTitle, k);
                   prefs.begin("xla", true);
                   int16_t v = prefs.getShort(key, def);
                   prefs.end(); xPUSH(v); } break;
      case 0x76: { int16_t v = xPOP(); Serial.printf("[xla:%s] %d\n", xlaTitle, v); } break;
      case 0x77: { int16_t v = xPOP(); int16_t f = xPOP(); int16_t y = xPOP(); int16_t x = xPOP();
                   char nb[8]; snprintf(nb, sizeof nb, "%d", v);
                   d.setCursor(x, y); d.setTextSize(f); d.print(nb); } break;  // NUM

      case 0x80: xPUSH((int16_t)readX()); break;
      case 0x81: xPUSH((int16_t)readY()); break;
      case 0x82: xPUSH(xStick8()); break;
      case 0x83: xPUSH((int16_t)xlaEv); break;
      case 0x84: xPUSH((int16_t)holdProgress()); break;

      case 0x90: { int16_t a = xPOP(); xPUSH((int16_t)(sinf(a * PI / 180.0f) * 1000.0f)); } break;
      case 0x91: { int16_t a = xPOP(); xPUSH((int16_t)(cosf(a * PI / 180.0f) * 1000.0f)); } break;
      case 0x92: { int16_t v = xPOP(); xPUSH(v <= 0 ? 0 : (int16_t)(sqrtf((float)v) + 0.5f)); } break;

      default:
        snprintf(xlaErr, sizeof xlaErr, "bad op %02X", op);
        xlaErrPC = xlaPC;
        return XR_HALT;
    }
  }
  return 0;
}

static void xlaFree() {
  if (xlaCode) free(xlaCode);
  if (xlaData) free(xlaData);
  if (xlaStrPool) free(xlaStrPool);
  xlaCode = nullptr; xlaData = nullptr; xlaStrPool = nullptr; xlaDataW = nullptr;
  xlaCodeSize = xlaDataSize = xlaStrSize = 0;
  xlaErr[0] = 0;
}

static bool xlaLoadBuf(const uint8_t* buf, size_t len) {
  if (len < 18) return false;
  if (buf[0] != 'X' || buf[1] != 'L' || buf[2] != 'A' || buf[3] != '1') return false;
  if (buf[4] != 1) return false;
  uint16_t codeSz = buf[6] | (buf[7] << 8);
  uint16_t dataSz = buf[8] | (buf[9] << 8);
  uint16_t strSz  = buf[10] | (buf[11] << 8);
  uint16_t entry  = buf[12] | (buf[13] << 8);
  uint16_t titleLen = buf[14] | (buf[15] << 8);
  if (titleLen == 0 || titleLen > XLA_TITLELEN) return false;
  if (codeSz == 0 || codeSz > XLA_MAXCODE) return false;
  if (dataSz > XLA_MAXDATA || (strSz != 0 && strSz > XLA_MAXSTR)) return false;
  if (dataSz & 1) return false;              // data size must be even (int16 units)
  if (entry >= codeSz) return false;
  size_t need = 16 + titleLen + (size_t)codeSz + dataSz + strSz;
  if (len < need) return false;

  xlaFree();
  xlaCode = (uint8_t*)malloc(codeSz);
  xlaData = (uint8_t*)malloc(dataSz ? dataSz : 2);
  xlaStrPool = (char*)malloc(strSz ? strSz : 1);
  if (!xlaCode || !xlaData || !xlaStrPool) { xlaFree(); return false; }
  const uint8_t* p = buf + 16;
  memcpy(xlaTitle, p, titleLen); p += titleLen; xlaTitle[titleLen] = 0;
  memcpy(xlaCode, p, codeSz); p += codeSz;
  memcpy(xlaData, p, dataSz); p += dataSz;
  memcpy(xlaStrPool, p, strSz);
  if (dataSz == 0) memset(xlaData, 0, 2);
  xlaCodeSize = codeSz; xlaDataSize = dataSz ? dataSz : 2; xlaStrSize = strSz;
  xlaDataW = (int16_t*)xlaData;
  xlaEntry = entry;
  return true;
}

static bool xlaRun() {
  bool ok = true;
  xlaPC = xlaEntry; xlaSP = 0; xlaRetSP = 0; xlaErr[0] = 0;
  xlaRunning = true;
  d.clearDisplay();
  uint32_t lastFrame = 0;
  while (xlaRunning) {
    Ev e = pollEvent();
    if (e == EV_EXIT) break;
    xlaEv = e;
    uint8_t r = xlaStep();
    if (XBAD()) { ok = false; xlaRunning = false; break; }
    if (r == XR_HALT || r == XR_EXIT) { xlaRunning = false; break; }
    uint32_t dt = millis() - lastFrame;
    if (dt < 16) delay(16 - dt);
    lastFrame = millis();
  }
  xlaRunning = false;
  if (xlaErr[0]) {
    Serial.printf("[xla] ERROR %s (pc=%u)\n", xlaErr, xlaPC);
    d.clearDisplay();
    headerBar("PLUGIN ERROR");
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(4, 22); d.print(xlaTitle);
    d.setCursor(4, 34); d.print(xlaErr);
    d.setCursor(4, 46); d.printf("pc=%u", xlaPC);
    d.display();
    beepWait(250, 300);
    delay(1400);
  }
  return ok;
}

static bool xlaRunFile(const char* path) {
  File f = SPIFFS.open(path, "r");
  if (!f) return false;
  size_t sz = f.size();
  uint8_t* buf = (uint8_t*)malloc(sz);
  if (!buf) { f.close(); return false; }
  size_t got = f.read(buf, sz);
  f.close();
  bool ok = (got == sz) && xlaLoadBuf(buf, sz);
  free(buf);
  if (!ok) {
    Serial.printf("[xla] bad file %s\n", path);
    return false;
  }
  ok = xlaRun();
  xlaFree();
  return ok;
}

// перечисление /p/*.xla; возвращает число, имена в title
static int xlaListInstalled(char names[][13], int maxN) {
  File root = SPIFFS.open("/p");
  if (!root) return 0;
  int n = 0;
  File f = root.openNextFile();
  while (f && n < maxN) {
    String nm = f.name();
    if (nm.endsWith(".xla") && !f.isDirectory()) {
      uint8_t hdr[32];
      size_t got = f.read(hdr, 32);
      if (got >= 18 && hdr[0] == 'X' && hdr[1] == 'L' && hdr[2] == 'A' && hdr[3] == '1') {
        uint16_t tl = hdr[14] | (hdr[15] << 8);
        if (tl >= 1 && tl <= 12) {
          memcpy(names[n], hdr + 16, tl);
          names[n][tl] = 0;
          n++;
        }
      }
    }
    f = root.openNextFile();
  }
  return n;
}

void appStore() {
  if (!SPIFFS.begin(true)) {
    d.clearDisplay(); headerBar("STORE");
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(4, 26); d.print("FS mount failed");
    d.display(); delay(1200);
    return;
  }
  // первый запуск: устанавливаем встроенный POUR
  if (!SPIFFS.exists("/p/pour.xla") && POUR_XLA_LEN > 0) {
    File f = SPIFFS.open("/p/pour.xla", "w");
    if (f) {
      uint8_t* buf = (uint8_t*)malloc(POUR_XLA_LEN);
      if (buf) {
        memcpy_P(buf, POUR_XLA, POUR_XLA_LEN);
        f.write(buf, POUR_XLA_LEN);
        free(buf);
        Serial.println("[store] installed built-in POUR");
      }
      f.close();
    }
  }

  char names[8][13];
  int nApps = xlaListInstalled(names, 8);
  int sel = 0;

  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) { beep(400, 100); return; }
    int nItems = nApps + 1; // [0]=catalog, [1..nApps]=installed
    if (e == EV_UP)   { sel = (sel + nItems - 1) % nItems; beep(900, 15); }
    if (e == EV_DOWN) { sel = (sel + 1) % nItems; beep(900, 15); }

    if (e == EV_OK) {
      if (sel == 0) {
        beep(1200, 30);
        if (!loadWifi()) {
          d.clearDisplay(); headerBar("STORE");
          d.setTextColor(1); d.setTextSize(1);
          d.setCursor(4, 20); d.print("No saved WiFi.");
          d.setCursor(4, 32); d.print("Connect in NET first");
          d.display(); delay(1500);
          continue;
        }
        WiFi.mode(WIFI_STA);
        bool conn = (WiFi.status() == WL_CONNECTED) || connectAnimated(wifiSSID, wifiPASS);
        if (!conn) {
          d.clearDisplay(); headerBar("STORE");
          d.setTextColor(1); d.setTextSize(1);
          d.setCursor(4, 26); d.print("wifi connect failed");
          d.display(); delay(1200);
          continue;
        }
        WiFiClientSecure cl;
        cl.setInsecure();
        HTTPClient http;
        if (!(http.begin(cl, XLA_MANIFEST) && http.GET() == 200)) {
          http.end();
          WiFi.mode(WIFI_OFF);
          d.clearDisplay(); headerBar("STORE");
          d.setTextColor(1); d.setTextSize(1);
          d.setCursor(4, 26); d.print("catalog unreachable");
          d.display(); delay(1200);
          continue;
        }
        String body = http.getString();
        http.end();
        String files[8]; String tits[8]; int rn = 0;
        int from = 0;
        while (rn < 8) {
          int nl = body.indexOf('\n', from);
          String line = body.substring(from, nl < 0 ? (int)body.length() : nl);
          line.trim();
          if (line.length()) {
            int br = line.indexOf('|');
            if (br > 0) { files[rn] = line.substring(0, br); tits[rn] = line.substring(br + 1); rn++; }
          }
          if (nl < 0) break;
          from = nl + 1;
        }
        WiFi.mode(WIFI_OFF);
        int rsel = 0;
        while (true) {
          Ev e2 = pollEvent();
          if (e2 == EV_EXIT) break;
          if (e2 == EV_UP)   { if (rn) rsel = (rsel + rn - 1) % rn; beep(900, 15); }
          if (e2 == EV_DOWN) { if (rn) rsel = (rsel + 1) % rn; beep(900, 15); }
          if (e2 == EV_OK && rn) {
            String url = String(XLA_APPBASE) + files[rsel] + ".xla";
            d.clearDisplay(); headerBar("DOWNLOAD");
            d.setTextColor(1); d.setTextSize(1);
            d.setCursor(4, 24); d.print(files[rsel]);
            d.setCursor(4, 36); d.print("downloading...");
            d.display();
            if (http.begin(cl, url) && http.GET() == 200) {
              String payload = http.getString();
              http.end();
              String path = "/p/" + files[rsel] + ".xla";
              File fo = SPIFFS.open(path, "w");
              if (fo) { fo.print(payload); fo.close();
                Serial.printf("[store] downloaded %s (%u b)\n", files[rsel].c_str(), (unsigned)payload.length());
                nApps = xlaListInstalled(names, 8);
                beep(1600, 60);
              }
            } else {
              http.end();
              d.clearDisplay(); headerBar("STORE");
              d.setTextColor(1); d.setTextSize(1);
              d.setCursor(4, 26); d.print("download failed");
              d.display(); delay(1200);
            }
            break;
          }
          d.clearDisplay(); headerBar("CLOUD CATALOG");
          d.setTextColor(1); d.setTextSize(1);
          if (rn == 0) d.setCursor(4, 26), d.print("catalog empty/404");
          for (int i = 0; i < rn; i++) {
            int y = 16 + i * 12;
            if (i == rsel) { d.fillRect(0, y - 1, W, 11, 1); d.setTextColor(0); }
            else d.setTextColor(1);
            d.setTextSize(1); d.setCursor(4, y); d.print(tits[i]);
          }
          d.setTextColor(1); d.setTextSize(1);
          d.setCursor(4, 55); d.print("click=install hold=back");
          d.display();
        }
      } else if (sel >= 1 && sel <= nApps) {
        beep(1500, 40);
        flashInvert();
        // ищем файл по индексу
        File root = SPIFFS.open("/p");
        int idx = 0; String target = "";
        if (root) {
          File f = root.openNextFile();
          while (f) {
            String nm = f.name();
            if (nm.endsWith(".xla") && !f.isDirectory()) {
              if (idx == sel - 1) { target = nm; f.close(); break; }
              idx++;
            }
            f = root.openNextFile();
          }
        }
        if (target.length()) xlaRunFile(target.c_str());
        flashInvert();
        beep(700, 20);
      }
    }

    d.clearDisplay();
    headerBar("STORE");
    int nItems2 = nApps + 1;
    if (nApps > 0) {
      int y = 16;
      if (sel == 0) { d.fillRect(0, y - 1, W, 10, 1); d.setTextColor(0); } else d.setTextColor(1);
      d.setTextSize(1); d.setCursor(4, y); d.print("* cloud catalog");
      for (int i = 0; i < nApps; i++) {
        y = 27 + i * 11;
        if (i + 1 == sel) { d.fillRect(0, y - 1, W, 10, 1); d.setTextColor(0); } else d.setTextColor(1);
        d.setTextSize(1); d.setCursor(4, y); d.print("R "); d.print(names[i]);
      }
    } else {
      if (sel == 0) { d.fillRect(0, 15, W, 10, 1); d.setTextColor(0); } else d.setTextColor(1);
      d.setTextSize(1); d.setCursor(4, 16); d.print("* cloud catalog");
      d.setTextColor(1); d.setTextSize(1);
      d.setCursor(4, 32); d.print("(no local plugins)");
    }
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(4, 55); d.print("click=run/inst hold=exit");
    char c2[8]; snprintf(c2, sizeof c2, "%d pl", nApps);
    d.setCursor(W - 7 * (int)strlen(c2) - 2, 1); d.print(c2);
    d.display();
  }
}

// ==================================================================
//  APPS TABLE + SECTIONS
// ==================================================================
// индексы: 0 SNAKE, 1 TETRIS, 2 2048, 3 PONG, 4 WIFI, 5 NET, 6 MUSIC, 7 ABOUT, 8 SETTINGS
const char* appNames[] = {"SNAKE", "TETRIS", "2048", "PONG", "WIFI SCAN", "NET", "MUSIC", "STORE", "ABOUT", "SETTINGS"};
void (*appFuncs[])() = {appSnake, appTetris, app2048, appPong, appWifi, appNet, appSongs, appStore, appAbout, appSettings};
const int N_APPS = 10;

// секции
const char* secNames[] = {"GAMES", "NET", "MEDIA", "CLOUD", "SYSTEM"};
const int N_SECS = 5;
// для каждого приложения — номер секции
const uint8_t appSec[N_APPS] = {0, 0, 0, 0, 1, 1, 2, 3, 3, 4};

int curSec = 0;          // текущая секция
int secSel[4] = {0, 0, 0, 0};  // выбранный пункт внутри каждой секции

// список индексов приложений секции
int secItems(int sec, int listOut[], int maxN) {
  int n = 0;
  for (int i = 0; i < N_APPS && n < maxN; i++)
    if (appSec[i] == sec) listOut[n++] = i;
  return n;
}

// ==================================================================
//  MENU (sections < > , items v ^)
// ==================================================================
void drawIcon(int appIdx, uint32_t t) {
  int cxp = 107, cyp = 32;
  if (appIdx == 0) {   // snake
    uint8_t ph = (t / 130) % 4;
    for (int s = 0; s < 5; s++) {
      int yy = 22 + s * 5 + ((s + ph) % 4 < 2 ? 0 : 2);
      d.fillRect(90 + s * 6, yy, 5, 4, 1);
    }
    d.fillCircle(120, 44 + (ph % 2 ? 1 : -1), 2, 1);
  } else if (appIdx == 1) {  // tetris: падающие блоки
    uint8_t ph = (t / 200) % 6;
    d.drawRect(89, 20, 16, 26, 1);
    d.fillRect(92, 22 + ph * 4, 6, 3, 1);
    d.fillRect(99, 26, 6, 3, 1);
    d.fillRect(92, 40, 6, 3, 1);
    d.fillRect(99, 40, 6, 3, 1);
    d.fillRect(95, 44, 6, 3, 1);
  } else if (appIdx == 2) {  // 2048: плитки
    d.drawRect(91, 20, 14, 14, 1);
    d.drawRect(107, 20, 14, 14, 1);
    d.drawRect(91, 36, 14, 14, 1);
    uint8_t ph = (t / 300) % 2;
    if (ph) d.fillRect(107, 36, 14, 14, 1);
  } else if (appIdx == 3) {  // pong
    d.drawFastHLine(89, 22, 4, 1); d.drawFastHLine(111, 22, 4, 1);
    d.drawFastHLine(89, 44, 4, 1); d.drawFastHLine(111, 44, 4, 1);
    d.fillRect(89, 28, 14, 3, 1);
    d.fillRect(103, 38, 14, 3, 1);
    uint8_t ph = (t / 180) % 4;
    d.fillCircle(96 + ph * 4, 40 - ph * 4, 2, 1);
  } else if (appIdx == 4) {  // wifi
    d.fillCircle(cxp, 46, 3, 1);
    uint8_t ph = (t / 200) % 3;
    for (int k = 0; k <= ph; k++)
      for (int a = 200; a <= 340; a += 4) {
        float r = 8 + k * 7, rad = a * PI / 180.0;
        d.drawPixel(cxp + r * cos(rad), 46 + r * sin(rad), 1);
      }
  } else if (appIdx == 5) {  // globe
    d.drawCircle(cxp, cyp, 12, 1);
    d.drawCircle(cxp, cyp, 7, 1);
    d.drawFastVLine(cxp, cyp - 12, 24, 1);
    d.drawFastVLine(cxp - 6, cyp - 10, 20, 1);
    d.drawFastVLine(cxp + 6, cyp - 10, 20, 1);
    d.drawFastHLine(cxp - 12, cyp, 24, 1);
  } else if (appIdx == 6) {  // music note
    d.fillEllipse(cxp - 5, cyp + 9, 4, 3, 1);
    d.fillRect(cxp - 2, cyp - 11, 2, 20, 1);
    d.drawFastHLine(cxp - 2, cyp - 11, 11, 1);
    d.drawFastVLine(cxp + 9, cyp - 11, 5, 1);
    uint8_t ph = (t / 220) % 2;
    if (ph) d.fillRect(cxp - 2, cyp + 5, 2, 3, 0);
  } else if (appIdx == 7) {  // store: облако + стрелка вниз
    d.drawCircle(cxp, cyp - 4, 9, 1);
    d.drawCircle(cxp, cyp - 4, 5, 1);
    d.fillCircle(cxp, cyp - 4, 2, 0);
    for (int k = 0; k < 7; k++) {
      float rad = (k * 8 + t / 30) * PI / 180.0;
      d.drawPixel(cxp + 9 * cos(rad), cyp - 4 + 9 * sin(rad), 0);
    }
    d.fillCircle(cxp, cyp + 10, 2, 1);
    d.drawFastHLine(cxp - 3, cyp + 9, 7, 1);
    d.drawFastVLine(cxp, cyp + 12, 4, 1);
    d.drawPixel(cxp - 1, cyp + 13, 1);
    d.drawPixel(cxp + 1, cyp + 13, 1);
  } else if (appIdx == 8) {  // info (about)
    uint8_t ph = (t / 250) % 2;
    d.drawCircle(cxp, cyp, 11 + ph, 1);
    d.fillCircle(cxp, cyp - 5, 2, 1);
    d.fillRect(cxp - 1, cyp - 1, 2, 9, 1);
  } else if (appIdx == 9) {  // gear (settings)
    d.drawCircle(cxp, cyp, 8, 1);
    d.fillCircle(cxp, cyp, 3, 1);
    for (int k = 0; k < 8; k++) {
      float a = k * PI / 4.0 + ((t / 400) % 2) * (PI / 8.0);
      d.drawLine(cxp + 8 * cos(a), cyp + 8 * sin(a), cxp + 12 * cos(a), cyp + 12 * sin(a), 1);
    }
  }
}

int sel = 0;   // глобальный индекс выбранного приложения (для запуска)
uint32_t lastActivity = 0;
float menuOff = 0;   // анимация вертикального скролла внутри секции
int secAnim = 0;     // анимация смены секции: -1 (влево) / +1 (вправо), затухает

void drawMenu() {
  d.clearDisplay();
  d.fillRect(0, 0, W, 13, 1);
  d.setTextColor(0); d.setTextSize(1);

  // шапка: < СЕКЦИЯ >
  char sh[16];
  snprintf(sh, sizeof sh, "< %s >", secNames[curSec]);
  d.setCursor(2, 1); d.print(sh);

  // количество пунктов справа
  int list[10];
  int n = secItems(curSec, list, 10);
  char cnt[8]; snprintf(cnt, sizeof cnt, "%d", n);
  d.setCursor(W - 7 * strlen(cnt) - 2, 1); d.print(cnt);

  if (bgPlaying) {
    d.setCursor(60 + (curSec == 2 ? 20 : 0), 1); d.print((millis()/250)%2 ? "*" : "+");
  }

  // анимации
  if (menuOff > 0.01f || menuOff < -0.01f) menuOff *= 0.72f; else menuOff = 0;
  if (secAnim > 0.01f || secAnim < -0.01f) secAnim *= 0.70f; else secAnim = 0;
  int secShift = (int)(secAnim * 10.0f);

  // пункты секции (со сдвигом анимации)
  int baseY = 17 - (int)menuOff;
  int selInSec = secSel[curSec];
  for (int i = 0; i < n; i++) {
    int y = baseY + i * 12;
    if (y > 64 || y < -12) continue;
    bool cur = (i == selInSec);
    int x = 4 - secShift;
    if (cur) { d.fillRect(x - 2, y - 1, 86, 12, 1); d.setTextColor(0); }
    else d.setTextColor(1);
    d.setTextSize(1); d.setCursor(x + 4, y);
    d.print(appNames[list[i]]);
  }

  // панель иконки
  d.drawRect(88, 14, 40, 38, 1);
  drawIcon(list[selInSec], millis());
  d.setTextColor(1); d.setTextSize(1);
  d.display();
}

// ==================================================================
//  BOOT + CALIBRATION
// ==================================================================
void drawPerimeter(int n) {
  int drawn = 0;
  for (int x = 0; x < W && drawn < n; x++, drawn++) d.drawPixel(x, 0, 1);
  for (int y = 1; y < H && drawn < n; y++, drawn++) d.drawPixel(W - 1, y, 1);
  for (int x = W - 2; x >= 0 && drawn < n; x--, drawn++) d.drawPixel(x, H - 1, 1);
  for (int y = H - 2; y > 0 && drawn < n; y--, drawn++) d.drawPixel(0, y, 1);
}

void bootAnimation() {
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

void setup() {
  Serial.begin(115200);
  pinMode(PIN_SW, INPUT_PULLUP);
  ledcAttach(PIN_BUZZER, 2000, 10);
  randomSeed(esp_random());

  d.begin();

  prefs.begin("c3flip", true);
  stickRot = prefs.getUChar("rot", 90);
  dispRot = prefs.getUChar("drot", 0);
  prefs.end();
  d.setRotation(dispRot);
  Serial.printf("stickRot=%d dispRot=%d\n", stickRot, dispRot);

  bootAnimation();

  // ---- calibration ----
  d.clearDisplay();
  d.setTextColor(1); d.setTextSize(1);
  d.setCursor(4, 10); d.print("STICK CALIBRATION");
  d.setCursor(4, 28); d.print("leave stick alone...");
  d.display();
  {
    long sx = 0, sy = 0; int n = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < 700) { sx += readX(); sy += readY(); n++; delay(3); }
    cx = sx / n; cy = sy / n;
    loX = hiX = cx; loY = hiY = cy;
  }
  d.setCursor(4, 42); d.print("now draw circles!");
  d.display();
  beep(1200, 60);
  {
    uint32_t t0 = millis();
    while (millis() - t0 < 3000) {
      int x = readX(), y = readY();
      if (x < loX) loX = x;
      if (x > hiX) hiX = x;
      if (y < loY) loY = y;
      if (y > hiY) hiY = y;
      d.drawRect(56, 54, 16, 8, 1);
      d.fillRect(58, 56, (millis() - t0) * 12 / 3000, 4, 1);
      d.display();
      delay(15);
    }
  }
  if (hiX - loX < 300) { loX = (cx > 900) ? cx - 900 : 0; hiX = (cx < 3195) ? cx + 900 : 4095; }
  if (hiY - loY < 300) { loY = (cy > 900) ? cy - 900 : 0; hiY = (cy < 3195) ? cy + 900 : 4095; }
  Serial.printf("calib c=%d/%d x[%d..%d] y[%d..%d]\n", cx, cy, loX, hiX, loY, hiY);
  beep(1600, 80);
}

enum Mode { M_CLOCK, M_MENU, M_APP };
Mode mode = M_CLOCK;

void loop() {
  Ev e = pollEvent();
  bgSongTick();
  static uint32_t lastClockSec = 0xFFFFFFFF;

  if (mode == M_CLOCK) {
    if (e == EV_OK || e == EV_UP || e == EV_DOWN || e == EV_LEFT || e == EV_RIGHT) {
      mode = M_MENU; lastActivity = millis();
      beep(1400, 30);
      flashInvert();
      drawMenu();
    } else {
      uint32_t clk = millis() / 60;
      if (clk != lastClockSec) { lastClockSec = clk; drawClock(); }
    }
  }
  else if (mode == M_MENU) {
    int list[10];
    int n = secItems(curSec, list, 10);
    int selInSec = secSel[curSec];

    if (e == EV_LEFT) {          // секция влево
      curSec = (curSec + N_SECS - 1) % N_SECS;
      secAnim = -1.0f;
      beep(600, 20);
    }
    if (e == EV_RIGHT) {         // секция вправо
      curSec = (curSec + 1) % N_SECS;
      secAnim = 1.0f;
      beep(800, 20);
    }
    if (e == EV_UP) {            // пункт вверх (внутри секции)
      secSel[curSec] = (secSel[curSec] + n - 1) % n;
      menuOff = 12.0f;
      beep(900, 15);
    }
    if (e == EV_DOWN) {          // пункт вниз
      secSel[curSec] = (secSel[curSec] + 1) % n;
      menuOff = -12.0f;
      beep(900, 15);
    }

    // текущее приложение для запуска
    n = secItems(curSec, list, 10);
    sel = list[secSel[curSec]];

    if (e == EV_OK) {
      beep(1500, 40);
      flashInvert();
      mode = M_APP;
      appFuncs[sel]();
      flashInvert();
      mode = M_MENU; lastActivity = millis();
      beep(700, 20);
    }
    if (e != EV_NONE) lastActivity = millis();

    static uint32_t lastFrame = 0;
    if (millis() - lastFrame >= 16) { lastFrame = millis(); drawMenu(); }
    if (millis() - lastActivity > 15000) { mode = M_CLOCK; lastClockSec = 0xFFFFFFFF; }
  }
}
