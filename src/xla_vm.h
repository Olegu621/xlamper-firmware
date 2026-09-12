#pragma once
// ==================================================================
//  xla_vm.h — XLA VM v2 (см. xla_vm.cpp и xla_opcodes.h).
// ==================================================================
#include <Arduino.h>
#include <FS.h>
#include "input.h"   // Ev

// Загрузка образа из RAM (проверка заголовка + лимитов). true = образ в VM.
bool xlaLoadBuf(const uint8_t* buf, size_t len);

// Освобождение образа (безопасно вызывать всегда).
void xlaFree();

// Запуск загруженного образа до EXIT/HALT/ошибки. Показывает экран ошибки.
bool xlaRun();

// Отладочная инъекция ввода по USB (консоль во время игры):
// xlaDbgEvent — поставить событие в очередь VM (1..6);
// xlaDbgStick — подсунуть значение стика (-2 = выкл, реальный стик).
void xlaDbgEvent(Ev e);
void xlaDbgStick(int8_t s);

// Загрузить .xla из SPIFFS и запустить; освобождает образ после.
bool xlaRunFile(const char* path);

// Список установленных плагинов /p/*.xla.
// names[i] — титул из заголовка (до 12 симв.), paths[i] — полный путь.
// Фикс v0.11: раньше путь искали повторной нумерацией каталога —
// рассинхрон с names при изменении FS. Теперь оба из одного прохода.
int xlaListInstalled(char names[][13], char paths[][24], int maxN);
