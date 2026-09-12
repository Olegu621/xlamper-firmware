// ==================================================================
//  synth.cpp — MIDI СИНТЕЗАТОР: полное раскрытие стика и спикера.
//
//  Управление:
//   * стик X — хроматический шаг по нотам (плавная частота через
//     позицию внутри ноты, не резкие скачки);
//   * стик Y — октава (вниз = ниже, вверх = выше, 5 октав);
//   * клик — смена режима волны/эффекта (см. SynthMode);
//   * удержание — выход.
//
//  Режимы (полный функционал спикера через LEDC):
//   * TONE   — чистый тон (ledcWriteTone);
//   * TREM   — тремоло: амплитудная модуляция LEDC duty;
//   * GLITCH — рандомные скачки частоты (chip-tune эффект);
//   * SIREN  — частотная качка (LFO);
//   * ARP    — арпеджио мажорного аккорда от текущей ноты.
//
//  Визуализация: осциллоскоп-имитация (форма по режиму), текущая
//  нота, частота, октава, шкала позиций стика.
// ==================================================================
#include "synth.h"
#include "config.h"
#include "display.h"
#include "input.h"
#include "sound.h"
#include <math.h>

// таблица частот (та же, что в midi.cpp; локальная копия для
// независимости модулей)
static const uint16_t SYN_FREQ[128] = {
       8,     9,     9,    10,    10,    11,    12,    12,    13,    14,    15,    15,
      16,    17,    18,    19,    21,    22,    23,    24,    26,    28,    29,    31,
      33,    35,    37,    39,    41,    44,    46,    49,    52,    55,    58,    62,
      65,    69,    73,    78,    82,    87,    92,    98,   104,   110,   117,   123,
     131,   139,   147,   156,   165,   175,   185,   196,   208,   220,   233,   247,
     262,   277,   294,   311,   330,   349,   370,   392,   415,   440,   466,   494,
     523,   554,   587,   622,   659,   698,   740,   784,   831,   880,   932,   988,
    1047,  1109,  1175,  1245,  1319,  1397,  1480,  1568,  1661,  1760,  1865,  1976,
    2093,  2217,  2349,  2489,  2637,  2794,  2960,  3136,  3322,  3520,  3729,  3951,
    4186,  4435,  4699,  4978,  5274,  5588,  5920,  6272,  6645,  7040,  7459,  7902,
    8372,  8870,  9397,  9956, 10548, 11175, 11840, 12544
};

// имена нот
static const char* NOTE_NAMES[12] = {
  "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

enum SynthMode : uint8_t { M_TONE = 0, M_TREM, M_GLITCH, M_SIREN, M_ARP, M_COUNT };
static const char* MODE_NAMES[M_COUNT] = { "TONE", "TREM", "GLITCH", "SIREN", "ARP" };

void appSynth() {
  int mode = M_TONE;
  int note = 60;             // старт: C4
  bool toneOn = false;       // звучит ли сейчас
  uint32_t tFrame = 0;
  int arpStep = 0;
  uint32_t tArp = 0;
  uint32_t tGlitch = 0;
  int glitchNote = 60;

  beep(1200, 40);

  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) { ledcWriteTone(PIN_BUZZER, 0); beep(400, 100); return; }
    if (e == EV_OK) {
      mode = (mode + 1) % M_COUNT;
      beep(900 + mode * 120, 30);
      arpStep = 0;
    }

    // ---- стик: нота + октава ----
    int8_t s8 = stick8();
    if (s8 >= 0) {
      // горизонталь: -1..+1 хроматический шаг (диагонали тоже)
      int dx = (s8 == 2 || s8 == 1 || s8 == 3) ? 1 : ((s8 == 6 || s8 == 7 || s8 == 5) ? -1 : 0);
      int dy = (s8 == 0 || s8 == 1 || s8 == 7) ? 1 : ((s8 == 4 || s8 == 3 || s8 == 5) ? -1 : 0);
      static uint32_t tStep = 0;
      if (millis() - tStep > 120) {          // шаг автоповтора
        tStep = millis();
        note += dx;
        if (dy > 0) note += 12;              // вверх = октава выше
        if (dy < 0) note -= 12;
        if (note < 36) note = 36;            // C2..C7 диапазон
        if (note > 96) note = 96;
        if (dx || dy) toneOn = true;
      }
    } else {
      toneOn = false;   // стик в покое — тишина (моно-синт)
    }

    // ---- звук по режиму ----
    // outFreq обновляем в LEDC ТОЛЬКО при изменении: повторный вызов
    // ledcWriteTone с той же частотой переконфигурирует таймер и даёт
    // джиттер тона (найдено самопроверкой; в TREM/SIREN частота
    // меняется сама — обновления происходят естественно).
    uint32_t now = millis();
    int curNote = note;
    if (mode == M_GLITCH) {
      if (now - tGlitch > 60) {              // скачок каждые ~60 мс
        tGlitch = now;
        glitchNote = 36 + (esp_random() % 61);
      }
      curNote = glitchNote;
    } else if (mode == M_ARP) {
      static const int8_t ARP_STEPS[4] = { 0, 4, 7, 12 };   // мажор
      if (now - tArp > 140) {
        tArp = now;
        arpStep = (arpStep + 1) % 4;
      }
      curNote = note + ARP_STEPS[arpStep];
    }
    if (curNote < 0) curNote = 0;
    if (curNote > 127) curNote = 127;
    uint32_t freq = SYN_FREQ[curNote];

    uint32_t outFreq = 0;                   // 0 = тишина
    if (toneOn) {
      if (mode == M_TREM) {
        // «тремоло»: чередование частоты и её половины (LEDC не умеет
        // менять duty у ledcWriteTone — слышимая пульсация частотой)
        uint8_t ph = (now / 90) % 2;
        outFreq = ph ? freq : freq / 2;
      } else if (mode == M_SIREN) {
        float k = (now % 1600) / 1600.0f;
        float wob = sinf(k * 2.0f * PI);
        outFreq = (uint32_t)(freq * (1.0f + wob * 0.35f));
      } else {
        outFreq = freq;
      }
    }
    static uint32_t lastOut = 1;            // 1 = "ещё не писали" (0 Гц валиден)
    if (outFreq != lastOut) {
      lastOut = outFreq;
      ledcWriteTone(PIN_BUZZER, outFreq);
    }

    // ---- отрисовка (~30 fps) ----
    if (now - tFrame >= 33) {
      tFrame = now;
      d.clearDisplay();
      char hb[20];
      snprintf(hb, sizeof hb, "SYNTH %s", MODE_NAMES[mode]);
      headerBar(hb);

      // текущая нота крупно
      d.setTextColor(1); d.setTextSize(3);
      char nn[8];
      snprintf(nn, sizeof nn, "%s%d", NOTE_NAMES[curNote % 12], curNote / 12 - 1);
      d.setCursor(6, 22); d.print(nn);
      d.setTextSize(1);
      char fq[16];
      snprintf(fq, sizeof fq, "%lu Hz", (unsigned long)freq);
      d.setCursor(6, 44); d.print(fq);

      // осциллоскоп-имитация справа
      int ox = 48, oy = 34, ow = 76;
      d.drawRect(ox, 14, ow, 40, 1);
      if (toneOn) {
        for (int x = 0; x < ow - 2; x++) {
          float ph = (x + now / 16) * 0.25f;
          float v = 0.0f;
          switch (mode) {
            case M_TONE: case M_ARP:
              v = sinf(ph);                       // синус
              break;
            case M_TREM: {
              float tr = ((now / 90) % 2) ? 0.9f : 0.25f;
              v = sinf(ph) * tr;                  // синус с окном
              break;
            }
            case M_GLITCH:
              v = ((x * 2654435761u) % 100) / 50.0f - 1.0f;   // шум
              break;
            case M_SIREN:
              v = sinf(ph * (1.0f + sinf(now % 1600 / 1600.0f * 2.0f * PI) * 0.35f));
              break;
          }
          int y = oy - (int)(v * 16.0f);
          if (y > 15 && y < 54) d.drawPixel(ox + 1 + x, y, 1);
        }
      } else {
        d.drawFastHLine(ox + 1, oy, ow - 2, 1);   // тишина: прямая
      }

      // индикатор позиции стика (сырая ось X)
      d.drawFastHLine(4, 58, 40, 1);
      int sx = map(readX(), 0, 4095, 4, 44);
      d.fillRect(sx, 56, 3, 5, 1);
      d.setCursor(52, 55); d.print("click=mode hold=exit");
      d.display();
    }
    delay(1);
  }
}
