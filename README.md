<div align="center">

# 🕹️ C3 XLAMPER

**Карманный «флиппер» с облаком игр — собирается за вечер из деталей за ~$8**

[![Firmware](https://img.shields.io/badge/Firmware-v0.13-blue)](#-прошивка)
[![Apps](https://img.shields.io/badge/Apps-12%20облачных-orange)](https://github.com/Olegu621/xlamper)
[![XLA VM](https://img.shields.io/badge/VM-69%20опкодов-purple)](src/xla_opcodes.h)
[![Flash](https://img.shields.io/badge/Flash-30%25-brightgreen)](#-прошивка)
[![License: MIT](https://img.shields.io/badge/License-MIT-green)](LICENSE)

**ESP32-C3 · OLED 128×64 · аналоговый стик · пьезо · Wi-Fi · облако игр**

*Все игры живут в облаке: скачиваются, запускаются, удаляются. На борту — 0 байт игр.*

</div>

---

## ⚡ Что это

C3 XLAMPER — открытая карманная консоль в духе Flipper Zero, но из дешёвых
модулей: ESP32-C3 Super Mini, OLED SSD1306, аналоговый джойстик и пьезо-спикер.
Прошивка — **облачная оболочка**: при включении консоль подключается к Wi-Fi,
синхронизирует каталог приложений ([репозиторий xlamper](https://github.com/Olegu621/xlamper))
и точное время, а дальше — часы с красивым циферблатом и меню облака.
Запуск игры = скачать `.xla` → исполнить в XLA VM → удалить. Консоль ничего
не хранит: каталог бесконечен, флеш свободен.

| | |
| --- | --- |
| 🎮 **Игры** | Snake, Tetris, 2048, Pong, Slots, Mines, Wires, Spinner, Coin, Root Bear… |
| 🧩 **XLA VM** | Стековый байт-код, 69 опкодов, лимит 6000 опк/кадр (60 fps) |
| 📡 **Облако** | Каталог из GitHub raw, download→run→delete, оффлайн-режод с кэшем списка |
| 🕐 **Часы** | NTP, крупные 7-сегментные цифры, кольцо секунд, дата и день недели |
| 📶 **Системные** | Wi-Fi радар, MIDI-плеер (bitmidi.com), синтезатор, настройки |
| 🔊 **Звук** | Пьезо: тоны, Jingle при буте, beep-фишки во всех играх |

## 🛠 Собери свой (гайд, ~2 часа)

### Железо (~$8)

| Деталь | Цена | Где |
| -------- | ------ | ----- |
| ESP32-C3 Super Mini | ~$2 | AliExpress |
| OLED SSD1306 0.96" I2C 128×64 | ~$2 | AliExpress |
| Джойстик аналоговый (как на PS2) | ~$1 | AliExpress |
| Пьезо-излучатель (пассивный!) | ~$0.5 | AliExpress |
| Макетка, провода, кнопка | ~$2 | любое |

### Схема (5 проводов + стик + спикер)

```text
ESP32-C3 Super Mini          OLED SSD1306
 3V3  ──────────────────────► VCC
 GND  ──────────────────────► GND
 IO8  ──────────────────────► SDA
 IO9  ──────────────────────► SCL

ESP32-C3                     Джойстик
 3V3  ──────────────────────► VCC
 GND  ──────────────────────► GND
 IO1  ──────────────────────► VRX
 IO3  ──────────────────────► VRY
 IO10 ──────────────────────► SW (кнопка стика)

ESP32-C3                     Пьезо
 IO5  ──────────────────────► + (через пин)
 GND  ──────────────────────► −
```

> Пассивный пьезо обязателен (без встроенного генератора) — звук управляется PWM.

### Прошивка (15 минут)

1. **Установи [PlatformIO](https://platformio.org/)** (VS Code extension или CLI)
2. Клонируй и собери:

   ```bash
   git clone https://github.com/Olegu621/xlamper-firmware
   cd xlamper-firmware
   pio run                 # сборка (~2 мин)
   pio run -t upload       # прошивка (консоль на USB)
   ```

3. Первая загрузка: консоль откалибрует стик (не трогай 3 сек), потом —
   нарисуй круги стиком.
4. **Подключи Wi-Fi**: SYSTEM → NET → выбери сеть, введи пароль
   (экранная клавиатура). Всё, консоль в облаке!

<details>
<summary>Если <code>pio</code> падает с Unicode-ошибкой (Windows)</summary>

Прошивай напрямую esptool (COM4 — замени на свой порт):

```powershell
$env:PYTHONIOENCODING='utf-8'
python -m esptool --chip esp32c3 --port COM4 --baud 921600 write-flash `
  0x0 .pio\build\esp32c3\bootloader.bin `
  0x8000 .pio\build\esp32c3\partitions.bin `
  0x10000 .pio\build\esp32c3\firmware.bin
```

</details>

### Управление

| Действие | Как |
| ---------- | ----- |
| Перемещение | стик 8 направлений |
| Выбор | клик стика (OK) |
| Выход из приложения | удержание клика (~1 сек) |
| Секции меню | стик ←→ (GAMES / MEDIA / TOOLS / ALL / SYSTEM) |

## 📖 Архитектура

```text
main.cpp ─ boot: wifi → каталог → NTP → часы
   ├── menu.cpp   — 5 секций, плавная прокрутка, скроллбар
   ├── cloud.cpp  — download → xlaRun → delete (SPIFFS всегда пуст!)
   ├── xla_vm.cpp — интерпретатор XLA (бюджет кадра, стек 96)
   ├── clock.cpp  — 7-сегментные часы + кольцо секунд
   └── SYSTEM: net / radar / midi / synth / settings / about
```

- **21 модуль** в `src/`, каждый — одна задача, всё documented
- **Единая таблица опкодов** (`xla_opcodes.h`, X-макро): VM, ассемблер,
  дизассемблер и симулятор читают ОДИН источник — рассинхрон вида
  «VM не знает GCPY 0x49» (баг v0.11) структурно невозможен
- **0 warnings** при `-Wall -Wextra`, Flash 30.3%, RAM 13.4%

### Написать игру (XLA-ассемблер)

Игры пишутся в [`xlamper` репо](https://github.com/Olegu621/xlamper) —
там ассемблер, симулятор и гайд по `.xlas`. Тест без железа:

```bash
python tools/xlas.py apps/coin.xlas apps/coin.xla   # ассемблировать
python tools/lint_xlas.py apps/coin.xlas            # проверить
# симулятор: отладка на ПК, стек-баланс и логика
```

## 📜 История версий

| Версия | Что нового |
| -------- | ----------- |
| **v0.13** | Облачная оболочка: boot-sync, download→run→delete, 5 секций меню, новые часы, плавная прокрутка, анимации. Игры — только облако |
| v0.12 | Модульная архитектура (21 файл), фикс 6 багов v0.11, UI-анимации (ZOOM/SLIDE/WIPE/FADE), радар/MIDI/синтезатор, порты 5 игр Flipper |
| v0.11 | Оригинал автора: 2474 строки в одном .ino, XLA VM, STORE |

## 📄 Лицензия

MIT — делай что хочешь, лицензию оставь.

---

<div align="center">

**Игры**: [Olegu621/xlamper](https://github.com/Olegu621/xlamper) ·
**Схема/гайд**: см. «Собери свой» выше

*Сделано Olegu621 · ESP32-C3 · 69 опкодов · 💜*

</div>
