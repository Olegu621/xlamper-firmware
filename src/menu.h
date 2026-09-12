#pragma once
// ==================================================================
//  menu.h — v0.13: меню = облачный каталог + локальная секция SYSTEM.
// ==================================================================
#include <stdint.h>

enum PickType : uint8_t { PICK_NONE = 0, PICK_CLOUD, PICK_LOCAL };

struct MenuPick {
  uint8_t type;   // PickType
  int idx;        // PICK_CLOUD: индекс в каталоге облака
                  // PICK_LOCAL: индекс в реестре apps.cpp
};

// Главный цикл меню. false = таймаут (вернуться в часы).
bool menuRun(MenuPick* out);
void menuReset();
