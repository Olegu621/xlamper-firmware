// ==================================================================
//  apps.cpp — v0.13: локальные СИСТЕМНЫЕ приложения оболочки.
//  Всё игровое/инструментальное — в облаке (XLA, каталог манифеста).
//  Локально только прошивочные функции: сеть, радар, медиа-железо,
//  настройки, инфо, ресинк каталога.
// ==================================================================
#include "apps.h"

void appNet();        // net.cpp: wifi скан/подключение/NTP
void appRadar();      // radar.cpp: визуал окружения Wi-Fi
void appMidi();       // midi.cpp: обл-каталог bitmidi + плеер
void appSynth();      // synth.cpp: пьезо-синтезатор (железо)
void appSongs();      // media.cpp: встроенные мелодии пьезо
void appAbout();      // system.cpp
void appSettings();   // system.cpp

const char* secNames[N_SECS] = { "SYSTEM" };   // локальные — только SYSTEM

static const AppDef APPS[] = {
  // name          fn           sec  icon
  { "NET",         appNet,      0,  IC_GLOBE  },
  { "WIFI RADAR",  appRadar,    0,  IC_RADAR  },
  { "MIDI",        appMidi,     0,  IC_MIDI   },
  { "SYNTH",       appSynth,    0,  IC_MUSIC  },
  { "MUSIC",       appSongs,    0,  IC_MUSIC  },
  { "SETTINGS",    appSettings, 0,  IC_GEAR   },
  { "ABOUT",       appAbout,    0,  IC_INFO   },
  { "RESYNC",      nullptr,     0,  IC_STORE  },   // спец-пункт: каталог+NTP
};

constexpr int N_APPS = sizeof(APPS) / sizeof(APPS[0]);
constexpr int RESYNC_IDX = 7;

int appCount() { return N_APPS; }
const AppDef* appGet(int i) { return (i >= 0 && i < N_APPS) ? &APPS[i] : nullptr; }
int secAppList(int sec, int listOut[], int maxN) {
  int n = 0;
  for (int i = 0; i < N_APPS && n < maxN; i++)
    if (APPS[i].sec == sec) listOut[n++] = i;
  return n;
}
bool appIsResync(int i) { return i == RESYNC_IDX; }
