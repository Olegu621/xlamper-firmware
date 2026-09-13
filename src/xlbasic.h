#pragma once
// ==================================================================
//  xlbasic.h — XLA BASIC: игры обычным текстом (без ассемблера).
//  Формат: TITLE: <имя> первой строкой, дальше — по команде на строку.
//  Подробнее: xlbasic.cpp + README apps-репо (раздел «игра за 5 минут»).
// ==================================================================
#include <Arduino.h>
#include <FS.h>

// лимиты (int16-мир консоли)
constexpr int XLB_MAXLINES    = 512;    // строк программы
constexpr int XLB_MAXLINELEN  = 60;     // символов на строку
constexpr int XLB_MAXSRC      = 32768;  // байт исходника
constexpr int XLB_MAXSTEPS    = 200000; // защита от зависания (без WAIT)
constexpr int XLB_TITLELEN    = 12;

// загрузить исходник из RAM (текст). true = программа готова.
bool xlbLoadBuf(const uint8_t* buf, size_t len);

// освободить
void xlbFree();

// исполнить до END/EXIT/ошибки. Показывает экран ошибки.
bool xlbRun();

// запустить из SPIFFS (download -> run -> delete)
bool xlbRunFile(const char* path);

// титул из «TITLE:» — для NVS-рекордов и меню
const char* xlbGetTitle();
