#pragma once
// ==================================================================
//  input.h — события ввода (стик + кнопка).
// ==================================================================
#include <Arduino.h>

enum Ev : uint8_t { EV_NONE = 0, EV_UP, EV_DOWN, EV_LEFT, EV_RIGHT, EV_OK, EV_EXIT };

Ev pollEvent();            // опрос ввода; дергает beepTick()
int  holdProgress();       // 0..100 прогресс удержания кнопки
int8_t stick8();           // 8-way направление (для XLA VM)
int  readX();              // сырой X с учётом поворота
int  readY();              // сырой Y с учётом поворота
void stickCalibrate(bool interactive);
void stickStretch(int x, int y);   // инкрементально расширить границы размаха
void stickCalibrateGuard();        // минимальные границы для дрожащего стика
void stickDiag(int& x, int& y, int& cx, int& cy, int& lox, int& hix, int& loy, int& hiy);
