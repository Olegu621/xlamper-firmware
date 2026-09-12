#pragma once
// ==================================================================
//  apps.h — v0.13: локальные системные приложения оболочки.
//  Игры и инструменты — ТОЛЬКО в облаке (меню = каталог манифеста).
//  Здесь — прошивочные функции: сеть, радар, медиа, настройки.
//  Иконки: стабильный id (не позиция в таблице).
// ==================================================================
#include <Arduino.h>

typedef void (*AppFn)();

enum AppIcon : uint8_t {
  IC_GLOBE = 0, IC_RADAR, IC_MIDI, IC_MUSIC, IC_GEAR, IC_INFO,
  IC_STORE, IC_WIFI, IC_SNAKE, IC_TETRIS, IC_2048, IC_PONG,
  IC_SPIN, IC_SLOT, IC_WIRE, IC_MINE, IC_COIN,
  IC_NONE_ = 255
};

struct AppDef {
  const char* name;
  AppFn fn;
  uint8_t sec;      // индекс секции (0 = SYSTEM)
  uint8_t icon;     // AppIcon — стабилен
};

constexpr int N_SECS = 1;   // локально — только SYSTEM
extern const char* secNames[N_SECS];

int  appCount();
const AppDef* appGet(int i);
int  secAppList(int sec, int listOut[], int maxN);
bool appIsResync(int i);   // спец-пункт RESYNC (fn == nullptr)
