# C3 XLAMPER Apps

Облачные плагины для **C3 XLAMPER** — карманного «флиппера» на ESP32-C3 Super Mini
(OLED SSD1306 128x64, аналоговый стик, пьезо-спикер).

## Как это работает

- В прошивке XLAMPER (v0.10+) работает **XLA VM** — стековый байт-код-интерпретатор
  с доступом к экрану, стику, звуку и NVS (persistent storage).
- Плагины — компактные `.xla` файлы (байт-код + данные + строки).
- Меню устройства: секция **CLOUD → STORE** — каталог качает этот репозиторий,
  выбранный плагин загружается по WiFi и сохраняется в SPIFFS (кэш).
- Запуск: STORE → `R <имя>`. Плагин грузится в RAM, работает, при выходе память освобождается.

## Каталог (manifest.txt)

Формат: по строке на плагин — `file|Название` (file — без расширения):

```text
pour|POUR - Root Bear proto
```

## Плагины

| Плагин | Описание |
|---|---|
| **POUR** | Прототип Root Bear: медведь-бармен наливает root beer. Стик ←→ — наклон кувшина (скорость потока), клик — налив/подать кружку. |
| **ROOTBEAR** | Полная игра: 4 клиента-зверя (мишка/зайка/котик/утка) заказывают кружку с целевым уровнем. Наклон ←→ + высота кувшина ↑↓ (пена), оценка PERFECT/GOOD/BAD, серия PERFECT-ов, рекорд в NVS. |

## Инструменты (папка tools)

- `xlas.py` — ассемблер: `.xlas` (текст) → `.xla` (байт-код)
- `xla_sim.py` — симулятор VM на ПК (отладка без железа)
- `xla_disasm.py` — дизассемблер `.xla`

### Сборка плагина

```bash
python tools/xlas.py apps/pour.xlas apps/pour.xla
python tools/xla_sim.py apps/pour.xla      # проверка
```

## Формат .xla (v1)

```text
[0..3]   magic "XLA1"
[4]      version = 1
[5]      flags
[6..7]   codeSize (uint16 LE)
[8..9]   dataSize (uint16 LE, чётное — int16-глобалы)
[10..11] strSize (uint16 LE)
[12..13] entry (uint16 LE)
[14..15] titleLen (uint16 LE, 1..12)
[16..17] (reserved)
[18..]   title bytes
[..]     code
[..]     data
[..]     strings (NUL-разделённые)
```

Опкоды: стековая машина, int16. Группы:
`0x01` push/dup/drop/swap · `0x2x` арифметика · `0x3x` сравнения ·
`0x45/0x46` gstore/gload · `0x5x` переходы/frame · `0x6x` графика (px/line/rect/circ/text…) ·
`0x7x` sys (msec/rand/beep/exit/save/load) · `0x8x` ввод (stick/event/hold) ·
`0x9x` math (sin/cos/sqrt, x1000 fixed).

События: 1 up, 2 down, 3 left, 4 right, 5 ok, 6 exit.
Стик 8-way: -1 покой, 0 вверх, 2 вправо, 4 вниз, 6 влево, диагонали между.

## Добавить свой плагин

1. Написать `apps/mygame.xlas` (пример — `apps/pour.xlas`)
2. `python tools/xlas.py apps/mygame.xlas apps/mygame.xla`
3. Проверить: `python tools/xla_sim.py apps/mygame.xla`
4. Добавить строку в `manifest.txt`: `mygame|MY GAME`
5. PR или коммит в main

Требования: код ≤ 20 КБ, data ≤ 4 КБ (int16-слоты ≤ 2048), строки ≤ 8 КБ,
title 1–12 символов. Бюджет кадра 6000 опкодов (~60 fps).
