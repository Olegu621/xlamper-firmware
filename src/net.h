#pragma once
// ==================================================================
//  net.h — Wi-Fi приложение и общий доступ к сети.
// ==================================================================
#include <Arduino.h>

extern time_t ntpTime;       // зафиксированное NTP-время
extern uint32_t ntpGotAt;    // когда зафиксировано

// Подключение с анимацией и лестницей TX (фикс SuperMini).
bool wifiConnectAnimated(const char* ssid, const char* pass, uint32_t timeoutMs);

// Пароль через экранную клавиатуру.
void inputPassword(char* out, int maxLen);

// Асинхронный скан с анимацией; -1 = отмена пользователем.
int wifiScanAsync();

// Мы в сети (для облачных функций)?
bool netOnline();

void appNet();     // NET: wifi + NTP
void appWifi();    // WIFI SCAN
