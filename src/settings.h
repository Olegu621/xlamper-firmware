#pragma once
// ==================================================================
//  settings.h — единая точка доступа к NVS (см. settings.cpp).
// ==================================================================
#include <Arduino.h>
#include <Preferences.h>

constexpr int XLA_KEY_MAX = 32;   // "<TITLE>_<name>" + NUL

extern uint8_t stickRot;   // 0/90/180/270 — угол стика
extern uint8_t dispRot;    // 0..3 — поворот экрана

void settingsLoad();
void saveRotation(uint8_t stick, uint8_t disp);

bool wifiLoad(char* ssid, size_t ssidCap, char* pass, size_t passCap);
void wifiSave(const char* ssid, const char* pass);
void wifiForget();

uint32_t snakeHiLoad();
void snakeHiSave(uint32_t hi);

int16_t xlaScoreLoad(const char* title, const char* key, int16_t def);
void xlaScoreSave(const char* title, const char* key, int16_t v);

void factoryReset();   // c3flip + xla + cloud — всё пользовательское
