// ==================================================================
//  menu.cpp — v0.13: ЕДИНОЕ меню.
//  Секции 0..2 = облачные категории (GAMES/MEDIA/TOOLS) из манифеста,
//  секция 3 = ALL (весь каталог), секция 4 = SYSTEM (локальные
//  функции прошивки: сеть, настройки...).
//  Запуск облачного пункта = download -> xlaRun -> delete (cloud.cpp).
// ==================================================================
#include "menu.h"
#include "apps.h"
#include "cloud.h"
#include "config.h"
#include "display.h"
#include "input.h"
#include "sound.h"
#include "ui.h"
#include <string.h>

static constexpr int SEC_GAMES = 0, SEC_MEDIA = 1, SEC_TOOLS = 2, SEC_ALL = 3, SEC_SYS = 4;
static constexpr int N_CATS = 5;

static const char* CATS[N_CATS] = { "GAMES", "MEDIA", "TOOLS", "ALL", "SYSTEM" };
static int curSec = 0;
static int sel[N_CATS] = { 0, 0, 0, 0, 0 };

void menuReset() {}

// ---------- список пунктов секции ----------
// out[] — для облака индексы каталога, для SYSTEM — индексы apps.cpp.
// возвращает количество; name(i) — заголовок пункта.
struct SecList {
  int idx[20];
  int n;
};

static void buildList(int sec, SecList* L) {
  L->n = 0;
  if (sec == SEC_SYS) {
    int tmp[20];
    L->n = secAppList(0, tmp, 20);
    for (int i = 0; i < L->n; i++) L->idx[i] = tmp[i];
    return;
  }
  for (int i = 0; i < cloudCount() && L->n < 20; i++) {
    const char* cat = cloudCat(i);
    bool take = (sec == SEC_ALL)
             || (sec == SEC_GAMES && strcasecmp(cat, "games") == 0)
             || (sec == SEC_MEDIA && strcasecmp(cat, "media") == 0)
             || (sec == SEC_TOOLS && strcasecmp(cat, "tools") == 0);
    if (take) L->idx[L->n++] = i;
  }
}

static const char* itemTitle(int sec, const SecList& L, int i) {
  if (sec == SEC_SYS) return appGet(L.idx[i])->name;
  return cloudTitle(L.idx[i]);
}

// ---------- окно прокрутки (чистая функция, без состояния) ----------
// 4 видимых строки; держим выбор в окне с запасом в 1 строку.
static int visTop(int n, int s) {
  int maxTop = n > 4 ? n - 4 : 0;
  int top = s - 1;               // запас сверху
  if (top < 0) top = 0;
  if (top > maxTop) top = maxTop;
  if (s - top > 3) top = s - 3;  // вдруг выбор ниже окна
  if (top > maxTop) top = maxTop;
  return top;
}

static void drawFrame(int sec, const SecList& L, int s) {
  d.clearDisplay();
  d.fillRect(0, 0, W, 13, 1);
  d.setTextColor(0); d.setTextSize(1);
  char sh[20];
  snprintf(sh, sizeof sh, "< %s >", CATS[sec]);
  d.setCursor(2, 1); d.print(sh);
  // счётчик «sel+1 / n» — понятнее голого числа
  char cnt[10]; snprintf(cnt, sizeof cnt, "%d/%d", s + 1, L.n);
  d.setCursor(W - 7 * strlen(cnt) - 2, 1); d.print(cnt);

  if (sec != SEC_SYS && !cloudCatalogOk()) {
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(10, 28); d.print("cloud not synced");
    d.setCursor(16, 40); d.print("RESYNC in SYSTEM");
    d.display();
    return;
  }
  if (L.n == 0) {
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(28, 30); d.print("(empty)");
    d.display();
    return;
  }

  // ---------- плавная прокрутка (easing к целевому окну) ----------
  static float offY = 0;      // текущее смещение (px), общее для секций
  float target = -visTop(L.n, s) * 12.0f;
  offY += (target - offY) * 0.35f;               // экспоненциальное приближение
  if (fabsf(target - offY) < 0.4f) offY = target;

  // окно пунктов: 4 строки по 12px, начиная с y=17
  d.setClip(0, 14, 127, 62);
  for (int i = 0; i < L.n; i++) {
    int y = (int)(17.0f + i * 12.0f + offY);
    if (y > 62 || y < 4) continue;
    bool cur = (i == s);
    if (cur) {
      d.fillRect(0, y - 1, 84, 12, 1);            // подсветка (не до края — дальше скроллбар)
      d.setTextColor(0);
    } else {
      d.setTextColor(1);
    }
    d.setTextSize(1); d.setCursor(5, y);
    d.print(itemTitle(sec, L, i));
  }
  d.clearClip();

  // ---------- скроллбар ----------
  if (L.n > 4) {
    d.drawFastVLine(126, 15, 47, 1);
    int tb = 15 + (47 - 7) * s / (L.n - 1);
    d.fillRect(123, tb, 4, 7, 1);                 // бегунок
    if (s > 0)        d.fillTriangle(120, 18, 125, 18, 122, 14, 1);   // стрелка вверх
    if (s < L.n - 1) d.fillTriangle(120, 59, 125, 59, 122, 63, 1);   // вниз
  }

  // ---------- превью-рамка справа ----------
  d.drawRect(88, 14, 40, 49, 1);
  d.setTextColor(1); d.setTextSize(1);
  const char* t = itemTitle(sec, L, s);
  if (t && t[0]) {
    // перенос по словам, максимум 6 симв/строку в рамке 40px
    int cx = 92, cy = 26;
    char b[2] = { 0, 0 };
    for (int i = 0; t[i] && i < 24; i++) {
      if (cx > 118) { cx = 92; cy += 9; }
      if (cy > 50) break;
      b[0] = t[i];
      d.drawStr(cx, cy, b);
      cx += 6;
    }
  }
  // иконка категории в рамке сверху
  const char* ic = (sec == SEC_SYS) ? "SYS" : (sec == SEC_GAMES ? "GAME" : (sec == SEC_MEDIA ? "MEDIA" : (sec == SEC_TOOLS ? "TOOL" : "ALL")));
  d.setFontTiny();
  d.drawStr(90, 16, ic);
  d.setTextSize(1);

  d.setTextColor(1); d.setTextSize(1);
  d.setCursor(4, 55); d.print("ok=run hold=exit");
  d.display();
}

// ---------- главный цикл ----------
bool menuRun(MenuPick* out) {
  SecList L;
  buildList(curSec, &L);
  uint32_t lastActivity = millis();
  uint32_t lastFrame = 0;

  while (true) {
    Ev e = pollEvent();
    if (e == EV_LEFT)  { curSec = (curSec + N_CATS - 1) % N_CATS; buildList(curSec, &L); beep(600, 20); }
    if (e == EV_RIGHT) { curSec = (curSec + 1) % N_CATS; buildList(curSec, &L); beep(800, 20); }
    if (e == EV_UP   && L.n > 0) { sel[curSec] = (sel[curSec] + L.n - 1) % L.n; beep(900, 15); }
    if (e == EV_DOWN && L.n > 0) { sel[curSec] = (sel[curSec] + 1) % L.n; beep(900, 15); }

    if (e == EV_OK && L.n > 0) {
      beep(1500, 40);
      int pickIdx = L.idx[sel[curSec]];
      int sec = curSec;   // для колбэка
      // ctx = {sec, pick} — динамический блок (стек меню)
      struct PickCtx { int sec, pick; } ctx = { curSec, pickIdx };
      uiTransition(LaunchStyle::ZOOM, [](void* c) {
        PickCtx* p = (PickCtx*)c;
        d.clearDisplay();
        const char* nm = (p->sec == 4) ? appGet(p->pick)->name : cloudTitle(p->pick);
        headerBar(nm);
        d.setTextColor(1); d.setTextSize(1);
        d.setCursor(4, 30); d.print("loading...");
      }, &ctx);
      if (curSec == SEC_SYS) {
        if (appIsResync(pickIdx)) { out->type = PICK_NONE; out->idx = -1; return true; }  // сигнал RESYNC
        out->type = PICK_LOCAL; out->idx = pickIdx;
      } else {
        out->type = PICK_CLOUD; out->idx = pickIdx;
      }
      return true;
    }

    if (e != EV_NONE) lastActivity = millis();
    if (millis() - lastActivity > 15000) return false;   // таймаут -> часы

    if (millis() - lastFrame >= 16) {
      lastFrame = millis();
      drawFrame(curSec, L, sel[curSec]);
    }
  }
}
