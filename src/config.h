#pragma once
// ==================================================================
//  C3 XLAMPER v0.13 — центральные константы (единственное место правды
//  для пинов, лимитов VM, URL облака и имён NVS-пространств).
//  v0.13 = облачная оболочка: игры только в облаке, download->run->delete.
//  Железо: ESP32-C3 Super Mini + SSD1306 128x64 + аналоговый стик + пьезо
// ==================================================================
#include <Arduino.h>

// ---------- железо ----------
constexpr int PIN_VRX    = 1;    // стик X (ADC1_CH1)
constexpr int PIN_VRY    = 3;    // стик Y (ADC1_CH3)
constexpr int PIN_SW     = 10;   // кнопка стика (INPUT_PULLUP)
constexpr int PIN_BUZZER = 5;    // пьезо-спикер (LEDC)

constexpr int W = 128;           // OLED ширина
constexpr int H = 64;            // OLED высота
// I2C OLED: SDA=IO8 SCL=IO9, адрес 0x3C — задан в конструкторе U8g2 (display.cpp)

// ---------- версия ----------
#define XLAMPER_VERSION "v0.13"

// ---------- облако плагинов ----------
#define XLA_MANIFEST "https://raw.githubusercontent.com/Olegu621/xlamper/main/manifest.txt"
#define XLA_APPBASE  "https://raw.githubusercontent.com/Olegu621/xlamper/main/apps/"

// ---------- лимиты XLA VM ----------
constexpr int XLA_STACK     = 96;     // глубина стека (int16)
constexpr int XLA_RET       = 16;     // глубина стека возвратов
constexpr int XLA_FRAME_INSN = 6000;   // бюджет опкодов на кадр (~60 fps)
constexpr int XLA_MAXCODE   = 20000;
constexpr int XLA_MAXDATA   = 4096;   // data-секция (чётная, int16-слоты)
constexpr int XLA_MAXSTR    = 8000;
constexpr int XLA_TITLELEN  = 12;
constexpr int XLA_HTTPBUF   = 4096;   // буфер HTTP-ответа для опкодов 0x78/0x79
constexpr int XLA_DELAY_CAP = 50;     // VM DELAY ограничен: кадр не должен виснуть

// ---------- NVS-пространства (совместимо с v0.11: существующие записи живы) ----------
#define NS_USER "c3flip"   // rot, drot, snake_hi, ssid, pass
#define NS_XLA  "xla"      // рекорды плагинов: ключ "<TITLE>_<name>" (I16)
