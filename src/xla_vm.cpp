// ==================================================================
//  xla_vm.cpp — XLA VM v2: стековый интерпретатор .xla плагинов.
//
//  Отличия от v0.11 (починенные баги):
//   * полный набор опкодов из xla_opcodes.h (единый реестр):
//     GCPY(0x49), HTTPGET/HTTPCH/WGET(0x78-0x7B), DELAY(0x7A) —
//     раньше облачный SNAKE падал "bad op 49";
//   * xlaErrPC пишется при ЛЮБОЙ ошибке, не только bad op;
//   * HTTP-опкоды неблокирующие для кадра: загружают в буфер
//     с таймаутом и не рвут бюджет кадра;
//   * задержка DELAY ограничена (кадр не может зависнуть).
// ==================================================================
#include "xla_vm.h"
#include "xla_opcodes.h"
#include "config.h"
#include "settings.h"
#include "display.h"
#include "input.h"
#include "sound.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <SPIFFS.h>

// ---------- состояние ----------
static uint8_t*  xCode = nullptr;
static uint16_t  xCodeSize = 0;
static int16_t*  xData = nullptr;      // data-секция (int16-слоты)
static uint16_t  xDataSlots = 0;
static char*     xStr = nullptr;       // пул строк
static uint16_t  xStrSize = 0;
static uint16_t  xEntry = 0;
static uint16_t  xPC = 0;
static int16_t   xStack[XLA_STACK];
static int16_t   xSP = 0;
static uint16_t  xRetSt[XLA_RET];
static uint8_t   xRetSP = 0;
static Ev        xEv = EV_NONE;
static bool      xRunning = false;
static char       xTitle[XLA_TITLELEN + 1] = "";
static char       xErr[64] = "";
static uint16_t   xErrPC = 0;
static uint32_t   xOpBudget = XLA_FRAME_INSN;
// HTTP-буфер для опкодов 0x78/0x79 (последний GET)
static uint8_t*  xHttpBuf = nullptr;
static int16_t   xHttpLen = -1;

#define XBAD() (xErr[0] != 0)

static inline void xPush(int16_t v) {
  if (xSP < XLA_STACK) { xStack[xSP++] = v; return; }
  snprintf(xErr, sizeof xErr, "stack ovf");
}
static inline int16_t xPop() {
  if (xSP > 0) return xStack[--xSP];
  snprintf(xErr, sizeof xErr, "stack und");
  return 0;
}
static inline uint8_t xF8() {
  if (xPC >= xCodeSize) { snprintf(xErr, sizeof xErr, "PC OOB"); return 0; }
  return xCode[xPC++];
}
static inline int16_t xF16() {
  uint8_t lo = xF8(); uint8_t hi = xF8();
  return (int16_t)((hi << 8) | lo);
}
static inline const char* xStrAt(uint16_t off) {
  if (off >= xStrSize) { snprintf(xErr, sizeof xErr, "str OOB"); return ""; }
  return xStr + off;
}
static inline bool gOk(int idx) {
  return idx >= 0 && idx < (int)xDataSlots;
}
static void xErrAt(const char* msg) {
  snprintf(xErr, sizeof xErr, "%s", msg);
  xErrPC = xPC;
}

// ---------- HTTP helpers ----------
static void httpFreeBuf() {
  if (xHttpBuf) { free(xHttpBuf); xHttpBuf = nullptr; }
  xHttpLen = -1;
}
static int16_t httpFetch(const char* url) {
  httpFreeBuf();
  if (WiFi.status() != WL_CONNECTED) { xHttpLen = -1; return -1; }
  WiFiClientSecure cl;
  cl.setInsecure();
  HTTPClient http;
  if (!http.begin(cl, url)) return -1;
  int code = http.GET();
  if (code != 200) { http.end(); return -1; }
  int total = http.getSize();
  if (total <= 0 || total > XLA_HTTPBUF) { http.end(); return -1; }
  xHttpBuf = (uint8_t*)malloc(total);
  if (!xHttpBuf) { http.end(); return -1; }
  WiFiClient* st = http.getStreamPtr();
  int got = 0;
  uint32_t t0 = millis();
  while (got < total && millis() - t0 < 8000) {
    size_t avail = st ? st->available() : 0;
    if (avail) {
      int nrd = st->readBytes(xHttpBuf + got, min((size_t)64, avail));
      if (nrd <= 0) break;
      got += nrd; t0 = millis();
    } else delay(2);
  }
  http.end();
  if (got != total) { httpFreeBuf(); return -1; }
  xHttpLen = total;
  return (int16_t)total;
}

// ---------- шаг VM ----------
enum Xr : uint8_t { XR_NONE = 0, XR_FRAME, XR_HALT, XR_EXIT };

static Xr xStep() {
  uint32_t budget = xOpBudget;
  while (budget--) {
    if (XBAD()) return XR_HALT;
    uint8_t op = xF8();
    switch (op) {
      // -- стек --
      case 0x00: return XR_HALT;
      case 0x01: xPush(xF16()); break;
      case 0x02: { int16_t v = xPop(); xPush(v); xPush(v); } break;
      case 0x03: xPop(); break;
      case 0x04: { int16_t b = xPop(), a = xPop(); xPush(b); xPush(a); } break;
      case 0x05: { int16_t b = xPop(), a = xPop(); xPush(a); xPush(b); xPush(a); } break;
      case 0x06: { int16_t n = xPop(); int16_t i = xSP - 1 - n;
                   xPush((n >= 0 && i >= 0 && i < XLA_STACK) ? xStack[i] : 0); } break;
      // -- арифметика --
      case 0x20: { int16_t b = xPop(), a = xPop(); xPush((int16_t)(a + b)); } break;
      case 0x21: { int16_t b = xPop(), a = xPop(); xPush((int16_t)(a - b)); } break;
      case 0x22: { int16_t b = xPop(), a = xPop(); xPush((int16_t)((int32_t)a * b)); } break;
      case 0x23: { int16_t b = xPop(), a = xPop();
                   if (b == 0) xErrAt("div0"); else xPush((int16_t)(a / b)); } break;
      case 0x24: { int16_t b = xPop(), a = xPop();
                   if (b == 0) xErrAt("mod0"); else xPush((int16_t)(a % b)); } break;
      case 0x25: { int16_t a = xPop(); xPush((int16_t)-a); } break;
      case 0x26: { int16_t b = xPop(), a = xPop(); xPush(a < b ? a : b); } break;
      case 0x27: { int16_t b = xPop(), a = xPop(); xPush(a > b ? a : b); } break;
      case 0x28: { int16_t a = xPop(); xPush(a < 0 ? (int16_t)-a : a); } break;
      // -- сравнения --
      case 0x30: { int16_t b = xPop(), a = xPop(); xPush(a == b); } break;
      case 0x31: { int16_t b = xPop(), a = xPop(); xPush(a != b); } break;
      case 0x32: { int16_t b = xPop(), a = xPop(); xPush(a <  b); } break;
      case 0x33: { int16_t b = xPop(), a = xPop(); xPush(a <= b); } break;
      case 0x34: { int16_t b = xPop(), a = xPop(); xPush(a >  b); } break;
      case 0x35: { int16_t b = xPop(), a = xPop(); xPush(a >= b); } break;
      case 0x36: { int16_t b = xPop(), a = xPop(); xPush((a != 0 && b != 0) ? 1 : 0); } break;
      case 0x37: { int16_t b = xPop(), a = xPop(); xPush((a != 0 || b != 0) ? 1 : 0); } break;
      case 0x38: { int16_t b = xPop(), a = xPop(); xPush(((a != 0) != (b != 0)) ? 1 : 0); } break;
      case 0x39: { int16_t a = xPop(); xPush(a == 0 ? 1 : 0); } break;
      // -- память --
      case 0x45: { int16_t idx = xF16(); int16_t v = xPop();
                   if (gOk(idx)) xData[idx] = v; else xErrAt("g OOB"); } break;
      case 0x46: { int16_t idx = xF16();
                   if (gOk(idx)) xPush(xData[idx]); else xErrAt("g OOB"); } break;
      case 0x47: { int16_t idx = xPop(); int16_t v = xPop();
                   if (gOk(idx)) xData[idx] = v; else xErrAt("g OOB"); } break;
      case 0x48: { int16_t idx = xPop();
                   if (gOk(idx)) xPush(xData[idx]); else xErrAt("g OOB"); } break;
      case 0x49: {  // GCPY: src dst n (со стека, в порядке src dst n после push src dst n)
                   int16_t n = xPop(), dst = xPop(), src = xPop();
                   if (n < 0 || src < 0 || dst < 0 || src + n > (int)xDataSlots || dst + n > (int)xDataSlots)
                     xErrAt("gcpy OOB");
                   else if (dst > src) { for (int i = n - 1; i >= 0; i--) xData[dst + i] = xData[src + i]; }
                   else if (dst < src) { for (int i = 0; i < n; i++) xData[dst + i] = xData[src + i]; } } break;
      // -- переходы --
      case 0x50: { int16_t rel = xF16(); xPC = (uint16_t)(xPC + rel); } break;
      case 0x51: { int16_t rel = xF16(); int16_t c = xPop(); if (!c) xPC = (uint16_t)(xPC + rel); } break;
      case 0x52: { int16_t rel = xF16(); int16_t c = xPop(); if (c)  xPC = (uint16_t)(xPC + rel); } break;
      case 0x53: { int16_t rel = xF16();
                   if (xRetSP >= XLA_RET) xErrAt("ret ovf");
                   else { xRetSt[xRetSP++] = xPC; xPC = (uint16_t)(xPC + rel); } } break;
      case 0x54: { if (xRetSP == 0) xErrAt("ret und"); else xPC = xRetSt[--xRetSP]; } break;
      case 0x5F: return XR_FRAME;
      // -- графика --
      case 0x60: { int16_t c = xPop(), y = xPop(), x = xPop();
                   if (x >= 0 && x < W && y >= 0 && y < H) d.drawPixel(x, y, c); } break;
      case 0x61: { int16_t c = xPop(), y1 = xPop(), x1 = xPop(), y0 = xPop(), x0 = xPop();
                   d.drawLine(x0, y0, x1, y1, c); } break;
      case 0x62: { int16_t c = xPop(), h = xPop(), w = xPop(), y = xPop(), x = xPop();
                   d.drawRect(x, y, w, h, c); } break;
      case 0x63: { int16_t c = xPop(), h = xPop(), w = xPop(), y = xPop(), x = xPop();
                   d.fillRect(x, y, w, h, c); } break;
      case 0x64: { int16_t c = xPop(), r = xPop(), y = xPop(), x = xPop();
                   d.drawCircle(x, y, r, c); } break;
      case 0x65: { int16_t c = xPop(), r = xPop(), y = xPop(), x = xPop();
                   d.fillCircle(x, y, r, c); } break;
      case 0x66: { int16_t c = xPop(), ry = xPop(), rx = xPop(), y = xPop(), x = xPop();
                   d.fillEllipse(x, y, rx, ry, c); } break;
      case 0x67: { const char* s = xStrAt((uint16_t)xF16()); int16_t f = xPop(), y = xPop(), x = xPop();
                   if (!XBAD()) { d.setCursor(x, y); d.setTextSize(f); d.print(s); } } break;
      case 0x68: d.invertDisplay(true); d.invertDisplay(false); break;
      case 0x69: { int16_t c = xPop(); d.fillRect(0, 0, W, H, c); } break;
      case 0x6A: d.clearDisplay(); break;
      case 0x6B: d.display(); break;
      // -- система --
      case 0x70: xPush((int16_t)(millis() & 0x7FFF)); break;
      case 0x71: { int16_t m = xPop(); xPush(m <= 0 ? 0 : (int16_t)(esp_random() % m)); } break;
      case 0x72: { int16_t ms = xPop(), f = xPop(); beep(f, ms); } break;
      case 0x73: return XR_EXIT;
      case 0x74: { const char* k = xStrAt((uint16_t)xF16()); int16_t v = xPop();
                   if (!XBAD()) xlaScoreSave(xTitle, k, v); } break;
      case 0x75: { const char* k = xStrAt((uint16_t)xF16()); int16_t def = xPop();
                   xPush(XBAD() ? def : xlaScoreLoad(xTitle, k, def)); } break;
      case 0x76: { int16_t v = xPop(); Serial.printf("[xla:%s] %d\n", xTitle, v); } break;
      case 0x77: { int16_t v = xPop(), f = xPop(), y = xPop(), x = xPop();
                   char nb[8]; snprintf(nb, sizeof nb, "%d", v);
                   d.setCursor(x, y); d.setTextSize(f); d.print(nb); } break;
      case 0x78: {  // HTTPGET strIdx: длина ответа | -1
                   const char* url = xStrAt((uint16_t)xF16());
                   if (!XBAD()) xPush(httpFetch(url)); } break;
      case 0x79: {  // HTTPCH i: байт i ответа (0 при OOB/нет ответа)
                   int16_t i = xPop();
                   if (xHttpBuf && i >= 0 && i < xHttpLen) xPush((int16_t)xHttpBuf[i]);
                   else xPush(0); } break;
      case 0x7A: {  // DELAY ms (кап, кадр не зависает)
                   int16_t ms = xPop();
                   if (ms > XLA_DELAY_CAP) ms = XLA_DELAY_CAP;
                   if (ms > 0) delay(ms); } break;
      case 0x7B: {  // WGET url-str dst cnt: слова HTTP-ответа -> data, возврат числа
                   int16_t cnt = xPop(), dst = xPop();
                   const char* url = xStrAt((uint16_t)xF16());
                   if (XBAD()) break;
                   int16_t r = httpFetch(url);
                   if (r < 0) { xPush(-1); break; }
                   int words = (xHttpLen + 1) / 2;
                   if (cnt <= 0 || !gOk(dst) || dst + min(cnt, (int16_t)words) > (int)xDataSlots) { xErrAt("wget OOB"); xPush(-1); break; }
                   int n = min(cnt, (int16_t)words);
                   memcpy(xData + dst, xHttpBuf, (size_t)n * 2);
                   xPush((int16_t)n); } break;
      // -- ввод --
      case 0x80: xPush((int16_t)readX()); break;
      case 0x81: xPush((int16_t)readY()); break;
      case 0x82: xPush(stick8()); break;
      case 0x83: xPush((int16_t)xEv); break;
      case 0x84: xPush((int16_t)holdProgress()); break;
      // -- математика --
      case 0x90: { int16_t a = xPop(); xPush((int16_t)(sinf(a * PI / 180.0f) * 1000.0f)); } break;
      case 0x91: { int16_t a = xPop(); xPush((int16_t)(cosf(a * PI / 180.0f) * 1000.0f)); } break;
      case 0x92: { int16_t v = xPop(); xPush(v <= 0 ? 0 : (int16_t)(sqrtf((float)v) + 0.5f)); } break;

      default:
        snprintf(xErr, sizeof xErr, "bad op %s %02X", xlaOpName(op), op);
        xErrPC = xPC;
        return XR_HALT;
    }
  }
  return XR_NONE;
}

// ---------- публичный API ----------
bool xlaLoadBuf(const uint8_t* buf, size_t len) {
  if (len < 18) return false;
  if (memcmp(buf, "XLA1", 4) != 0) return false;
  if (buf[4] != 1) return false;
  uint16_t codeSz  = buf[6] | (buf[7] << 8);
  uint16_t dataSz  = buf[8] | (buf[9] << 8);
  uint16_t strSz   = buf[10] | (buf[11] << 8);
  uint16_t entry   = buf[12] | (buf[13] << 8);
  uint16_t titleLen = buf[14] | (buf[15] << 8);
  if (titleLen == 0 || titleLen > XLA_TITLELEN) return false;
  if (codeSz == 0 || codeSz > XLA_MAXCODE) return false;
  if (dataSz > XLA_MAXDATA || (strSz != 0 && strSz > XLA_MAXSTR)) return false;
  if (dataSz % 2 != 0) return false;   // data size must be even (int16 units)
  if (entry >= codeSz) return false;
  size_t need = 16 + titleLen + (size_t)codeSz + dataSz + strSz;
  if (len < need) return false;

  xlaFree();
  xCode = (uint8_t*)malloc(codeSz);
  xData = (int16_t*)malloc(dataSz ? dataSz : 2);
  xStr  = (char*)malloc(strSz ? strSz : 1);
  if (!xCode || !xData || !xStr) { xlaFree(); return false; }
  const uint8_t* p = buf + 16;
  memcpy(xTitle, p, titleLen); p += titleLen; xTitle[titleLen] = 0;
  memcpy(xCode, p, codeSz); p += codeSz;
  memcpy(xData, p, dataSz); p += dataSz;
  memcpy(xStr, p, strSz);
  if (dataSz == 0) memset(xData, 0, 2);
  xCodeSize = codeSz;
  xDataSlots = dataSz ? dataSz / 2 : 1;
  xStrSize = strSz;
  xEntry = entry;
  return true;
}

void xlaFree() {
  if (xCode) free(xCode);
  if (xData) free(xData);
  if (xStr) free(xStr);
  httpFreeBuf();
  xCode = nullptr; xData = nullptr; xStr = nullptr;
  xCodeSize = xStrSize = xDataSlots = 0;
  xErr[0] = 0;
}

bool xlaRun() {
  bool ok = true;
  xPC = xEntry; xSP = 0; xRetSP = 0; xErr[0] = 0; xErrPC = 0;
  xRunning = true;
  d.clearDisplay();
  uint32_t lastFrame = 0;
  while (xRunning) {
    Ev e = pollEvent();
    if (e == EV_EXIT) break;
    xEv = e;
    Xr r = xStep();
    if (XBAD()) { ok = false; break; }
    if (r == XR_HALT || r == XR_EXIT) break;
    uint32_t dt = millis() - lastFrame;
    if (dt < 16) delay(16 - dt);
    lastFrame = millis();
  }
  xRunning = false;
  if (xErr[0]) {
    Serial.printf("[xla] ERROR %s (pc=%u)\n", xErr, xErrPC);
    d.clearDisplay();
    headerBar("PLUGIN ERROR");
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(4, 22); d.print(xTitle);
    d.setCursor(4, 34); d.print(xErr);
    d.setCursor(4, 46); d.format("pc=%u", xErrPC);
    d.display();
    beepWait(250, 300);
    delay(1400);
  }
  return ok;
}

bool xlaRunFile(const char* path) {
  File f = SPIFFS.open(path, "r");
  if (!f) return false;
  size_t sz = f.size();
  uint8_t* buf = (uint8_t*)malloc(sz);
  if (!buf) { f.close(); return false; }
  size_t got = f.read(buf, sz);
  f.close();
  bool ok = (got == sz) && xlaLoadBuf(buf, sz) && xlaRun();
  free(buf);
  xlaFree();
  return ok;
}

// Список установленных плагинов /p/*.xla — имена и пути из ОДНОГО прохода
// по каталогу (фикс v0.11: раньше путь искали повторной нумерацией и
// он мог рассинхронизироваться с именами при изменении FS).
int xlaListInstalled(char names[][13], char paths[][24], int maxN) {
  File root = SPIFFS.open("/p");
  if (!root) return 0;
  int n = 0;
  for (File f = root.openNextFile(); f && n < maxN; f = root.openNextFile()) {
    String nm = f.name();
    if (nm.endsWith(".xla") && !f.isDirectory()) {
      uint8_t hdr[32];
      size_t got = f.read(hdr, 32);
      if (got >= 18 && memcmp(hdr, "XLA1", 4) == 0) {
        uint16_t tl = hdr[14] | (hdr[15] << 8);
        if (tl >= 1 && tl <= 12) {
          memcpy(names[n], hdr + 16, tl);
          names[n][tl] = 0;
          snprintf(paths[n], 24, "%s", nm.c_str());
          n++;
        }
      }
    }
  }
  return n;
}
