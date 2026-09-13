// ==================================================================
//  xlbasic.cpp — XLA BASIC: игры ОБЫЧНЫМ ТЕКСТОМ, без ассемблера.
//
//  Облако = GitHub. Игра = человекочитаемый .xlb, пишется в блокноте.
//  Прошивка парсит и исполняет напрямую. Ноль инструментов.
//
//  СИНТАКСИС (строки, регистронезависимо; ' и REM — комментарии):
//    TITLE: имя            — обязательно ПЕРВОЙ строкой (для меню/NVS)
//    метка:                — цель для GOTO/GOSUB (RETURN)
//    LET V = выражение     — V/VV — переменные; LET можно опустить
//    IF cond THEN команда
//    GOTO метка | GOSUB метка | RETURN | END
//    CLS / DISP
//    PSET x y c | LINE x1 y1 x2 y2 c
//    RECT x y w h c | FRECT x y w h c | CIRC x y r c | FCIRC x y r c
//    TEXT x y "текст" | NUM x y выражение
//    WAIT                  — конец кадра: дисплей + ввод (~60 fps)
//    STICK -> V            — 8-way: -1 покой 0 верх 2 право 4 низ 6 лево
//    KEY -> V              — событие 0..6 (5=OK, 6=EXIT)
//    BEEP герц мс
//    RND n -> V            — случайное 0..n-1
//    SCORE имя значение    — сохранить рекорд в NVS
//    SCORE имя -> V        — прочитать рекорд
//  Выражения: числа, переменные, + - * / %, ( ), сравнения == != < > <= >=
//  (истина = 1, ложь = 0).
// ==================================================================
#include "xlbasic.h"
#include "display.h"
#include "input.h"
#include "sound.h"
#include "settings.h"
#include <vector>
#include <string.h>
#include <SPIFFS.h>

// ---------- состояние ----------
static constexpr int NVARS = 702;      // A..Z (0..25) + AA..ZZ (26..701)
static constexpr int VRET = 701;      // слот GOSUB-возврата (ZZ занят — служебный)
static int16_t vars[NVARS];
static char lines[XLB_MAXLINES][XLB_MAXLINELEN + 1];
static int nLines = 0;
static int lineIdx[XLB_MAXLINES];        // исходный № строки для ошибок
static char labelName[XLB_MAXLINES][16];
static int labelLine[XLB_MAXLINES];
static int nLabels = 0;
static bool running = false;
static char xbTitle[XLB_TITLELEN + 1] = "APP";

const char* xlbGetTitle() { return xbTitle; }

// ---------- утилиты ----------
static void skipWs(const char*& p) { while (*p == ' ' || *p == '\t') p++; }
static bool isAl(int c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
static int up(int c) { return (c >= 'a' && c <= 'z') ? c - 'a' + 'A' : c; }

// имя переменной -> индекс; курсор двигается ЗА имя
static int varIndex(const char*& p) {
  if (!isAl(*p)) return -1;
  int c1 = up(*p++);
  int idx = c1 - 'A';
  if (isAl(*p)) { idx = 26 + (c1 - 'A') * 26 + (up(*p++) - 'A'); }
  return idx;
}

// ---------- парсер выражений (рекурсивный спуск) ----------
// cmp := add ((==|!=|<=|>=|<|>) add)?      — без оператора: вернуть левое
// add := mul ((+|-) mul)*
// mul := pri ((*|/|%) pri)*
// pri := число | имя | -pri | ( cmp )
static const char* xp;
static int evErr;

static int16_t evalCmpImpl();
static int16_t evalAdd();
static int16_t evalMul();
static int16_t evalPri();

static int16_t evalPri() {
  skipWs(xp);
  char c = *xp;
  if (c == '(') { xp++; int16_t v = evalCmpImpl(); skipWs(xp); if (*xp == ')') xp++; return v; }
  if (c == '-') { xp++; return (int16_t)-evalPri(); }
  if (c >= '0' && c <= '9') {
    int v = 0;
    while (*xp >= '0' && *xp <= '9') { v = v * 10 + (*xp++ - '0'); if (v > 32767) { evErr = 1; return 0; } }
    return (int16_t)v;
  }
  if (isAl(c)) {
    const char* q = xp;
    int idx = varIndex(xp);
    if (idx < 0 || idx >= NVARS) { xp = q; evErr = 1; return 0; }
    return vars[idx];
  }
  evErr = 1;
  return 0;
}
static int16_t evalMul() {
  int16_t v = evalPri();
  for (;;) {
    skipWs(xp);
    char op = *xp;
    if (op != '*' && op != '/' && op != '%') return v;
    xp++;
    int16_t b = evalPri();
    if (op == '*') v = (int16_t)(v * b);
    else if (b == 0) { evErr = 2; v = 0; }
    else if (op == '/') v = (int16_t)(v / b);
    else v = (int16_t)(v % b);
  }
}
static int16_t evalAdd() {
  int16_t v = evalMul();
  for (;;) {
    skipWs(xp);
    char op = *xp;
    if (op != '+' && op != '-') return v;
    xp++;
    int16_t b = evalMul();
    v = (int16_t)(op == '+' ? v + b : v - b);
  }
}
// сравнение: сначала пробуем двухсимвольные операторы
static int16_t evalCmpImpl() {
  int16_t a = evalAdd();
  skipWs(xp);
  if (xp[0] == '=' && xp[1] == '=') { xp += 2; return (int16_t)(evalAdd() == a); }
  if (xp[0] == '!' && xp[1] == '=') { xp += 2; return (int16_t)(evalAdd() != a); }
  if (xp[0] == '<' && xp[1] == '=') { xp += 2; return (int16_t)(a <= evalAdd()); }
  if (xp[0] == '>' && xp[1] == '=') { xp += 2; return (int16_t)(a >= evalAdd()); }
  if (xp[0] == '<') { xp++; return (int16_t)(a < evalAdd()); }
  if (xp[0] == '>') { xp++; return (int16_t)(a > evalAdd()); }
  return a;
}

static int16_t evalExpr(const char*& p) {
  xp = p; evErr = 0;
  int16_t v = evalCmpImpl();
  p = xp;
  return v;
}

// вырезать префикс ключевого слова: сравнивает без учёта регистра,
// требует границу (буква/цифра после — НЕ совпадение)
static bool kwIs(const char*& p, const char* kw) {
  const char* q = p;
  int k = 0;
  while (kw[k]) {
    if (up(*q) != up(kw[k])) return false;
    q++; k++;
  }
  if (isAl(*q) || (*q >= '0' && *q <= '9')) return false;  // TEXTX != TEXT
  p = q;
  return true;
}

// имя метки/переменной в буфер (заглавными)
static int readName(const char*& p, char* out, int maxN) {
  int k = 0;
  while (isAl(*p) && k < maxN - 1) out[k++] = up(*p++);
  out[k] = 0;
  return k;
}

// ---------- загрузка ----------
bool xlbLoadBuf(const uint8_t* buf, size_t len) {
  nLines = 0; nLabels = 0;
  xbTitle[0] = 0;
  if (len < 4 || len > XLB_MAXSRC) return false;
  const char* s = (const char*)buf;
  const char* end = s + len;

  // TITLE: имя — первой содержательной строкой
  const char* t = s;
  while (t < end && (*t == ' ' || *t == '\t' || *t == '\r' || *t == '\n')) t++;
  const char* tt = t;
  if (kwIs(tt, "TITLE:")) {
    tt = t + 6;
    while (tt < end && *tt == ' ') tt++;
    int n = 0;
    while (tt < end && *tt != '\n' && *tt != '\r' && n < XLB_TITLELEN) xbTitle[n++] = *tt++;
    xbTitle[n] = 0;
    s = tt;
  } else {
    snprintf(xbTitle, sizeof xbTitle, "APP");
  }

  while (s < end && nLines < XLB_MAXLINES) {
    const char* e = s;
    while (e < end && *e != '\n') e++;
    int L = (int)(e - s);
    if (L > XLB_MAXLINELEN) L = XLB_MAXLINELEN;
    int n = 0;
    for (int i = 0; i < L && n < XLB_MAXLINELEN; i++)
      if (s[i] != '\r') lines[nLines][n++] = s[i];
    lines[nLines][n] = 0;
    lineIdx[nLines] = nLines + 1;

    // метка?  имя:
    char* ln = lines[nLines];
    const char* c = ln;
    skipWs(c);
    if (isAl(*c)) {
      char nm[16];
      int k = 0;
      while (isAl(*c) && k < 15) nm[k++] = up(*c++);
      if (*c == ':' && k > 0 && nLabels < XLB_MAXLINES) {
        nm[k] = 0;
        strcpy(labelName[nLabels], nm);
        labelLine[nLabels] = nLines;
        nLabels++;
        c++;                                  // ':' убрать
        while (*c == ' ') c++;                 // и пробелы
        memmove(ln, c, strlen(c) + 1);         // строка = остаток после метки
      }
    }
    nLines++;
    s = (*e == '\n') ? e + 1 : e;
  }
  Serial.printf("[xlb] %d строк, %d меток, title=%s\n", nLines, nLabels, xbTitle);
  return nLines > 0;
}

void xlbFree() { nLines = 0; nLabels = 0; running = false; }

static int findLabel(const char* nm) {
  for (int i = 0; i < nLabels; i++)
    if (!strcmp(labelName[i], nm)) return labelLine[i];
  return -1;
}

// ---------- исполнение ----------
// возврат execLine: 0 ок | 1 END | 2 переход (gLabel) | 3 ошибка
static char gLabel[16];
static int16_t curEvent = 0;
static bool gotEnd = false;
static bool waited = false;      // WAIT в этом кадре — сброс сторожа

static int execLine(const char* s, int lineNo) {
  const char* p = s;
  skipWs(p);
  if (!*p || *p == '\'') return 0;
  if (kwIs(p, "REM")) return 0;

  if (kwIs(p, "END")) { gotEnd = true; return 1; }
  if (kwIs(p, "CLS")) { d.clearDisplay(); return 0; }
  if (kwIs(p, "DISP")) { d.display(); return 0; }

  if (kwIs(p, "BEEP")) {
    int16_t f = evalExpr(p); skipWs(p);
    int16_t ms = evalExpr(p);
    if (!evErr) beep(f, ms);
    return evErr ? 3 : 0;
  }
  if (kwIs(p, "PSET")) {
    int16_t x = evalExpr(p); skipWs(p);
    int16_t y = evalExpr(p); skipWs(p);
    int16_t c = evalExpr(p);
    if (evErr) return 3;
    d.drawPixel(x, y, c);
    return 0;
  }
  if (kwIs(p, "LINE")) {
    int16_t x1 = evalExpr(p); skipWs(p);
    int16_t y1 = evalExpr(p); skipWs(p);
    int16_t x2 = evalExpr(p); skipWs(p);
    int16_t y2 = evalExpr(p); skipWs(p);
    int16_t c = evalExpr(p);
    if (evErr) return 3;
    d.drawLine(x1, y1, x2, y2, c);
    return 0;
  }
  bool isF = false;
  if (kwIs(p, "FRECT")) isF = true;
  else if (kwIs(p, "RECT")) isF = false;
  else goto not_rect;
  {
    int16_t x = evalExpr(p); skipWs(p);
    int16_t y = evalExpr(p); skipWs(p);
    int16_t w = evalExpr(p); skipWs(p);
    int16_t h = evalExpr(p); skipWs(p);
    int16_t c = evalExpr(p);
    if (evErr) return 3;
    if (isF) d.fillRect(x, y, w, h, c); else d.drawRect(x, y, w, h, c);
    return 0;
  }
not_rect:
  if (kwIs(p, "FCIRC")) isF = true;
  else if (kwIs(p, "CIRC")) isF = false;
  else goto not_circ;
  {
    int16_t x = evalExpr(p); skipWs(p);
    int16_t y = evalExpr(p); skipWs(p);
    int16_t r = evalExpr(p); skipWs(p);
    int16_t c = evalExpr(p);
    if (evErr) return 3;
    if (isF) d.fillCircle(x, y, r, c); else d.drawCircle(x, y, r, c);
    return 0;
  }
not_circ:
  if (kwIs(p, "TEXT")) {
    int16_t x = evalExpr(p); skipWs(p);
    int16_t y = evalExpr(p); skipWs(p);
    if (evErr) return 3;
    if (*p == '"') {
      p++;
      d.setCursor(x, y); d.setTextColor(1); d.setTextSize(1);
      while (*p && *p != '"') d.print((char)*p++);
      if (*p == '"') p++;
    }
    return 0;
  }
  if (kwIs(p, "NUM")) {
    int16_t x = evalExpr(p); skipWs(p);
    int16_t y = evalExpr(p); skipWs(p);
    int16_t v = evalExpr(p);
    if (evErr) return 3;
    d.setCursor(x, y); d.setTextColor(1); d.setTextSize(1); d.print(v);
    return 0;
  }
  if (kwIs(p, "WAIT")) {
    // конец кадра: вывести буфер и опросить ввод
    d.display();
    Ev e = pollEvent();
    if (e == EV_EXIT) { gotEnd = true; return 1; }
    if (e != EV_NONE) curEvent = e;
    // ~60 fps
    static uint32_t lastT = 0;
    uint32_t now = millis();
    if (now - lastT < 16) delay(16 - (now - lastT));
    lastT = millis();
    waited = true;                     // кадр прожит: сторож сбрасывается
    return 0;
  }
  if (kwIs(p, "STICK")) {
    skipWs(p);
    if (*p == '-' && p[1] == '>') p += 2;
    skipWs(p);
    int idx = varIndex(p);
    if (idx < 0 || idx >= NVARS) return 3;
    vars[idx] = stick8();
    return 0;
  }
  if (kwIs(p, "KEY")) {
    skipWs(p);
    if (*p == '-' && p[1] == '>') p += 2;
    skipWs(p);
    int idx = varIndex(p);
    if (idx < 0 || idx >= NVARS) return 3;
    vars[idx] = curEvent;
    curEvent = 0;
    return 0;
  }
  if (kwIs(p, "RND")) {
    // RND <expr> -> V : ищем '->' ЯВНО (не полагаемся на expr-парсер)
    const char* arrow = strstr(p, "->");
    if (!arrow) return 3;
    // выражение = [p, arrow)
    char expr[32];
    int el = (int)(arrow - p);
    if (el < 1 || el > 30) return 3;
    memcpy(expr, p, el); expr[el] = 0;
    const char* ep = expr;
    int16_t n = evalExpr(ep);
    if (evErr) return 3;
    const char* vp = arrow + 2;
    skipWs(vp);
    int idx = varIndex(vp);
    if (idx < 0 || idx >= NVARS || n <= 0) return 3;
    vars[idx] = (int16_t)(esp_random() % n);
    return 0;
  }
  if (kwIs(p, "SCORE")) {
    skipWs(p);
    char nm[16];
    // имя ключа — как написан (регистр сохраняется для NVS-совместимости с .xla)
    int k = 0;
    while (*p && *p != ' ' && *p != '\t' && k < 15) nm[k++] = *p++;
    nm[k] = 0;
    if (k == 0) return 3;
    skipWs(p);
    if (*p == '-' && p[1] == '>') {
      // чтение: SCORE имя -> V (необязательный дефолт после? нет: чистое чтение)
      p += 2; skipWs(p);
      int idx = varIndex(p);
      if (idx < 0 || idx >= NVARS) return 3;
      vars[idx] = xlaScoreLoad(xbTitle, nm, 0);
    } else {
      // запись: SCORE имя значение
      int16_t v = evalExpr(p);
      if (evErr) return 3;
      xlaScoreSave(xbTitle, nm, v);
    }
    return 0;
  }
  bool isSub = kwIs(p, "GOSUB");
  bool isGo = isSub ? false : kwIs(p, "GOTO");
  if (isGo || isSub) {
    skipWs(p);
    char nm[16];
    if (readName(p, nm, 16) == 0) return 3;
    strcpy(gLabel, nm);
    if (isSub) vars[VRET] = (int16_t)lineNo;   // RETURN вернётся на lineNo+1
    return 2;
  }
  if (kwIs(p, "RETURN")) {
    gLabel[0] = 1; gLabel[1] = 0;             // маркер возврата
    return 2;
  }
  if (kwIs(p, "IF")) {
    int16_t cnd = evalExpr(p);
    if (evErr) return 3;
    skipWs(p);
    if (kwIs(p, "THEN")) skipWs(p);
    if (!cnd) return 0;
    return execLine(p, lineNo);               // истинно: исполнить хвост
  }

  // присваивание: [LET] V = expr
  {
    const char* q = p;
    if (kwIs(q, "LET")) skipWs(q);
    int idx = varIndex(q);
    if (idx < 0 || idx >= NVARS) return 3;
    skipWs(q);
    if (*q != '=') return 3;
    q++;
    int16_t v = evalExpr(q);
    if (evErr) return 3;
    vars[idx] = v;
    return 0;
  }
  return 3;
}

bool xlbRun() {
  running = true;
  gotEnd = false;
  curEvent = 0;
  memset(vars, 0, sizeof vars);
  int pc = 0;
  uint32_t steps = 0;
  uint32_t lastWatchdog = millis();

  while (running) {
    if (nLines <= 0) break;
    int r = execLine(lines[pc], pc);
    if (r == 1) break;                        // END/WAIT-EXIT
    if (r == 3) {
      Serial.printf("[xlb] ОШИБКА, строка %d: %.40s\n", lineIdx[pc], lines[pc]);
      d.clearDisplay();
      headerBar("BASIC ERROR");
      d.setTextColor(1); d.setTextSize(1);
      d.setCursor(4, 22); d.format("line %d", lineIdx[pc]);
      d.setCursor(4, 34); d.print(lines[pc]);
      d.display();
      beepWait(300, 200);
      delay(1200);
      break;
    }
    if (r == 2) {
      if (gLabel[0] == 1) pc = vars[VRET] + 1; // RETURN
      else {
        int t = findLabel(gLabel);
        if (t < 0) { Serial.printf("[xlb] метка нет: %s\n", gLabel); break; }
        pc = t;
      }
    } else {
      pc++;
      if (pc >= nLines) break;                // конец программы = END (не цикл!)
    }
    steps++;
    // защита от зависания: БЕЗ WAIT программа обязана дойти до конца;
    // каждый WAIT — прожитый кадр — сбрасывает счётчик (игра может идти часами)
    if (waited) { steps = 0; waited = false; }
    if (steps > XLB_MAXSTEPS) { Serial.println("[xlb] сторож: слишком долго без END"); break; }
    // USB-консоль: как в XLA VM — q (выход), ev N (событие)
    if ((steps & 15) == 0) {
      while (Serial.available()) {
        static char dbgl[12];
        static int dbgn = 0;
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
          dbgl[dbgn] = 0;
          if (dbgl[0] == 'q' && dbgn == 1) { running = false; Serial.println("[xlb] usb quit"); }
          else if (!strncmp(dbgl, "ev ", 3) && dbgn >= 3) {
            int v = atoi(dbgl + 3);
            if (v >= 1 && v <= 6) { curEvent = (int16_t)v; Serial.printf("[xlb] ev %d\n", v); }
          }
          dbgn = 0;
        } else if (dbgn < (int)sizeof(dbgl) - 1) dbgl[dbgn++] = c;
      }
    }
    // кнопка EXIT — живой выход даже в длинных циклах рисования
    if ((steps & 63) == 0) {
      Ev e = pollEvent();
      if (e == EV_EXIT) break;
      if (e != EV_NONE) curEvent = e;
      if (millis() - lastWatchdog > 300) { lastWatchdog = millis(); }
    }
  }
  running = false;
  return true;
}

bool xlbRunFile(const char* path) {
  File f = SPIFFS.open(path, "r");
  if (!f) return false;
  size_t sz = f.size();
  if (sz < 4 || sz > XLB_MAXSRC) { f.close(); return false; }
  std::vector<uint8_t> buf(sz);
  size_t got = f.read(buf.data(), sz);
  f.close();
  if (got != sz) return false;
  if (!xlbLoadBuf(buf.data(), sz)) return false;
  Serial.printf("[xlb] запуск: %s\n", xbTitle);
  bool ok = xlbRun();
  xlbFree();
  return ok;
}
