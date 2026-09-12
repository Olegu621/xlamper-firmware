// ==================================================================
//  midi.cpp — MIDI PLAYER: облачный список песен (bitmidi.com),
//  скачивание .mid, воспроизведение одноголосной мелодии на пьезо.
//
//  Состав:
//   * SMF-парсер (прототип отлажен в midi/smf_proto.py):
//     - выбор трека с макс. числом note-on (drums ch9 исключены);
//     - running status, varlen-дельты, tempo-мета на лету
//       (интервал ДО смены темпа считается по СТАРОМУ темпу);
//     - MIDI_FREQ таблица (без float-степени в рантайме);
//   * MIDI LIST: страница bitmidi (случайная категория) -> до 15
//     названий + URL; повторный клик = новый список (другие песни);
//   * Плеер: бегущая строка названия, прогресс, пауза.
// ==================================================================
#include "midi.h"
#include "config.h"
#include "settings.h"
#include "display.h"
#include "input.h"
#include "sound.h"
#include "net.h"
#include "ui.h"
#include <SPIFFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

// ---------- таблица частот MIDI 0..127 (A4=440, 12-TET) ----------
static const uint16_t MIDI_FREQ[128] PROGMEM = {
       8,     9,     9,    10,    10,    11,    12,    12,    13,    14,    15,    15,    16,    17,    18,    19,
      21,    22,    23,    24,    26,    28,    29,    31,    33,    35,    37,    39,    41,    44,    46,    49,
      52,    55,    58,    62,    65,    69,    73,    78,    82,    87,    92,    98,   104,   110,   117,   123,
     131,   139,   147,   156,   165,   175,   185,   196,   208,   220,   233,   247,   262,   277,   294,   311,
     330,   349,   370,   392,   415,   440,   466,   494,   523,   554,   587,   622,   659,   698,   740,   784,
     831,   880,   932,   988,  1047,  1109,  1175,  1245,  1319,  1397,  1480,  1568,  1661,  1760,  1865,  1976,
    2093,  2217,  2349,  2489,  2637,  2794,  2960,  3136,  3322,  3520,  3729,  3951,  4186,  4435,  4699,  4978,
    5274,  5588,  5920,  6272,  6645,  7040,  7459,  7902,  8372,  8870,  9397,  9956, 10548, 11175, 11840, 12544,
};

static uint16_t midiFreq(uint8_t n) { return pgm_read_word(&MIDI_FREQ[n]); }

// ---------- SMF-структура в памяти ----------
// Экономно: после парсинга файл не нужен — события хранятся списком.
struct MidiEvent { uint16_t freq; uint16_t ms; };   // freq=0 -> пауза
static MidiEvent*  evSeq = nullptr;
static int         evCount = 0;

static void seqFree() {
  if (evSeq) free(evSeq);
  evSeq = nullptr;
  evCount = 0;
}

// varlen-чтение; возврат: 0=OK, -1=переполнение
static int varlen(const uint8_t* buf, size_t len, size_t& pos, uint32_t& out) {
  uint32_t v = 0;
  int n = 0;
  while (pos < len && n < 4) {
    uint8_t b = buf[pos++];
    v = (v << 7) | (b & 0x7F);
    n++;
    if (!(b & 0x80)) { out = v; return 0; }
  }
  return -1;
}

// Кандидат-трек для мелодии: (notes, tempos) — собраны за один проход.
struct ScanResult {
  uint16_t noteCnt;
  // заметки сразу переводим в (freq,ms) c копчением темпов:
  // компактный вариант — собираем события только на втором проходе,
  // здесь только счётчик и события tempos.
};
static const int MAX_TEMPOS = 8;
struct TempoNode { uint32_t tick; uint32_t us; };

// Первый проход по треку: подсчёт note-on (мелодийный потенциал)
static int scanCountNotes(const uint8_t* chunk, size_t len) {
  size_t pos = 0;
  uint32_t tick = 0;
  uint8_t status = 0;
  int cnt = 0;
  while (pos < len) {
    uint32_t d;
    if (varlen(chunk, len, pos, d)) return -1;
    tick += d;
    if (pos >= len) return -1;
    if (chunk[pos] & 0x80) status = chunk[pos++];
    else if (!status) return -1;
    uint8_t kind = status & 0xF0;
    uint8_t chan = status & 0x0F;
    if (status == 0xFF) {
      if (pos >= len) return -1;
      uint8_t mtype = chunk[pos++];
      uint32_t mlen;
      if (varlen(chunk, len, pos, mlen)) return -1;
      if (mtype == 0x2F) break;             // End of Track
      pos += mlen;
    } else if (kind == 0x90 || kind == 0x80) {
      if (pos + 1 >= len) return -1;
      uint8_t d1 = chunk[pos], d2 = chunk[pos + 1];
      pos += 2;
      if (kind == 0x90 && d2 > 0 && chan != 9) cnt++;
      (void)d1;
    } else if (kind == 0xA0 || kind == 0xB0 || kind == 0xE0) {
      pos += 2;
    } else if (kind == 0xC0 || kind == 0xD0) {
      pos += 1;
    } else return -1;
  }
  return cnt;
}

// Второй проход: события мелодии выбранного трека -> evSeq
// (два прохода вместо разбора всех 14 треков в RAM: 51 КБ Bohemian
// не влез бы в heap вместе с массивом событий)
static bool scanCollect(const uint8_t* chunk, size_t len, uint16_t division,
                       int maxEvents) {
  // заметки сохраняем как (midi, start_tick, dur_tick)
  struct RawNote { uint8_t m; uint32_t st, en; };
  RawNote* raw = (RawNote*)malloc(sizeof(RawNote) * maxEvents);
  if (!raw) return false;
  int nRaw = 0;
  TempoNode tempos[MAX_TEMPOS + 1];
  int nTempos = 0;

  size_t pos = 0;
  uint32_t tick = 0;
  uint8_t status = 0;
  // индексы открытых нот: простой стек (ch*128+note)
  int32_t openIdx[32];
  uint32_t openAt[32];
  int nOpen = 0;

  while (pos < len && nRaw < maxEvents) {
    uint32_t d;
    if (varlen(chunk, len, pos, d)) break;
    tick += d;
    if (pos >= len) break;
    if (chunk[pos] & 0x80) status = chunk[pos++];
    else if (!status) break;
    uint8_t kind = status & 0xF0;
    uint8_t chan = status & 0x0F;
    if (status == 0xFF) {
      if (pos >= len) break;
      uint8_t mtype = chunk[pos++];
      uint32_t mlen;
      if (varlen(chunk, len, pos, mlen)) break;
      if (mtype == 0x51 && mlen == 3 && pos + 3 <= len && nTempos <= MAX_TEMPOS) {
        uint32_t us = ((uint32_t)chunk[pos] << 16) | ((uint32_t)chunk[pos+1] << 8) | chunk[pos+2];
        tempos[nTempos++] = {tick, us};
      }
      if (mtype == 0x2F) break;
      pos += mlen;
    } else if (kind == 0x90 || kind == 0x80) {
      if (pos + 1 >= len) break;
      uint8_t d1 = chunk[pos], d2 = chunk[pos + 1];
      pos += 2;
      bool on = (kind == 0x90 && d2 > 0 && chan != 9);
      bool off = (kind == 0x80) || (kind == 0x90 && d2 == 0);
      if (on) {
        if (nOpen < 32) {
          openIdx[nOpen] = (int32_t)chan * 128 + d1;
          openAt[nOpen] = tick;
          nOpen++;
        }
      } else if (off) {
        int32_t key = (int32_t)chan * 128 + d1;
        for (int i = nOpen - 1; i >= 0; i--) {
          if (openIdx[i] == key) {
            raw[nRaw].m = (uint8_t)(key & 0x7F);
            raw[nRaw].st = openAt[i];
            raw[nRaw].en = tick;
            nRaw++;
            for (int j = i; j < nOpen - 1; j++) { openIdx[j] = openIdx[j+1]; openAt[j] = openAt[j+1]; }
            nOpen--;
            break;
          }
        }
      }
    } else if (kind == 0xA0 || kind == 0xB0 || kind == 0xE0) pos += 2;
    else if (kind == 0xC0 || kind == 0xD0) pos += 1;
    else break;
  }
  if (!nRaw) { free(raw); return false; }

  // сортировка по start (простая вставками: до ~2000 нот ок)
  for (int i = 1; i < nRaw; i++) {
    RawNote k = raw[i];
    int j = i - 1;
    while (j >= 0 && raw[j].st > k.st) { raw[j+1] = raw[j]; j--; }
    raw[j+1] = k;
  }

  // tempo-узлы: (tick, ms, us действующий С ЭТОГО tick)
  struct MsNode { uint32_t tick; uint32_t ms; uint32_t us; } nodes[MAX_TEMPOS + 2];
  int nNodes = 1;
  nodes[0] = {0, 0, 500000};
  if (nTempos && tempos[0].tick > 0) {
    // дефолтный темп до первого tempo-мета уже в nodes[0]
  }
  for (int i = 0; i < nTempos; i++) {
    MsNode& last = nodes[nNodes - 1];
    // интервал ДО смены — по СТАРОМУ темпу (прототип это чинил)
    uint32_t ms = last.ms + (tempos[i].tick - last.tick) * (last.us / 1000) / division;
    nodes[nNodes++] = {tempos[i].tick, ms, tempos[i].us};
  }

  auto tickMs = [&](uint32_t t) -> uint32_t {
    MsNode* nd = &nodes[0];
    for (int i = 0; i < nNodes; i++)
      if (nodes[i].tick <= t) nd = &nodes[i];
      else break;
    return nd->ms + (t - nd->tick) * (nd->us / 1000) / division;
  };

  // события: пауза + нота
  evSeq = (MidiEvent*)malloc(sizeof(MidiEvent) * (nRaw * 2 + 1));
  if (!evSeq) { free(raw); return false; }
  evCount = 0;
  uint32_t prevEnd = 0;
  bool first = true;
  for (int i = 0; i < nRaw; i++) {
    uint32_t msSt = tickMs(raw[i].st);
    uint32_t msEn = tickMs(raw[i].en);
    if (msEn <= msSt) continue;
    if (!first && msSt > prevEnd)
      evSeq[evCount++] = {0, (uint16_t)(msSt - prevEnd)};
    uint16_t f = midiFreq(raw[i].m);
    if (!f) continue;
    evSeq[evCount++] = {f, (uint16_t)(msEn - msSt)};
    prevEnd = msEn;
    first = false;
  }
  free(raw);
  return evCount > 0;
}

// Полный разбор файла из буфера. evSeq заполняется.
static bool smfParse(const uint8_t* data, size_t len) {
  seqFree();
  if (len < 14 || memcmp(data, "MThd", 4) != 0) return false;
  uint32_t hlen = ((uint32_t)data[4] << 24) | ((uint32_t)data[5] << 16) | ((uint32_t)data[6] << 8) | data[7];
  uint16_t ntrk = ((uint16_t)data[10] << 8) | data[11];
  uint16_t division = ((uint16_t)data[12] << 8) | data[13];
  if (division & 0x8000) return false;   // SMPTE не поддерживаем
  size_t pos = 8 + hlen;
  int bestCnt = -1;
  size_t bestOff = 0, bestLen = 0;
  for (int t = 0; t < ntrk && pos + 8 <= len; t++) {
    if (memcmp(data + pos, "MTrk", 4) != 0) break;
    uint32_t tlen = ((uint32_t)data[pos+4] << 24) | ((uint32_t)data[pos+5] << 16) | ((uint32_t)data[pos+6] << 8) | data[pos+7];
    const uint8_t* chunk = data + pos + 8;
    if (pos + 8 + tlen > len) break;
    int cnt = scanCountNotes(chunk, tlen);
    if (cnt > bestCnt) {
      bestCnt = cnt;
      bestOff = pos + 8;
      bestLen = tlen;
    }
    pos += 8 + tlen;
  }
  if (bestCnt <= 0) return false;
  return scanCollect(data + bestOff, bestLen, division, bestCnt + 8);
}

// ---------- воспроизведение ----------
static void playSequence(const char* title) {
  if (!evSeq || !evCount) return;
  int cur = 0;
  uint32_t tNote = 0;
  bool paused = false;
  uint32_t tPause = 0;
  uint32_t totalMs = 0;
  for (int i = 0; i < evCount; i++) totalMs += evSeq[i].ms;
  uint32_t elapsed = 0;

  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) { ledcWriteTone(PIN_BUZZER, 0); return; }
    if (e == EV_OK) {
      paused = !paused;
      beep(paused ? 700 : 1400, 40);
      if (paused) { ledcWriteTone(PIN_BUZZER, 0); tPause = millis(); }
      else tNote += millis() - tPause;   // сдвигаем план на длительность паузы
    }
    if (!paused) {
      uint32_t now = millis();
      while (cur < evCount && (int32_t)(now - tNote) >= evSeq[cur].ms) {
        tNote += evSeq[cur].ms;
        elapsed += evSeq[cur].ms;
        cur++;
        if (cur < evCount) {
          if (evSeq[cur].freq) ledcWriteTone(PIN_BUZZER, evSeq[cur].freq);
          else ledcWriteTone(PIN_BUZZER, 0);
        } else ledcWriteTone(PIN_BUZZER, 0);
      }
      if (cur >= evCount) { ledcWriteTone(PIN_BUZZER, 0); beep(1500, 60); delay(300); return; }
    }

    // отрисовка: заголовок, бегущая строка, прогресс
    d.clearDisplay();
    headerBar("MIDI PLAYER");
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(4, 16); d.print(paused ? "PAUSED" : "playing");
    char st[20];
    snprintf(st, sizeof st, "%d/%d", cur, evCount);
    d.setCursor(92, 16); d.print(st);
    // прогресс
    int pct = totalMs ? (int)(elapsed * 100 / totalMs) : 0;
    uiProgress(4, 26, 120, 8, pct);
    // бегущая строка названия
    int tw = d.strWidth(title);
    uint32_t span = (uint32_t)(tw + W);
    int x = W - (int)((millis() / 40) % span) * 2;
    d.setClip(0, 40, 127, 52);
    d.drawStr(x, 50, title);
    if (x + tw < W) d.drawStr(x + tw, 50, title);
    d.clearClip();
    d.setCursor(4, 55); d.print("click=pause hold=exit");
    d.display();
    delay(8);
  }
}

// ---------- список песен из bitmidi ----------
struct SongInfo { char title[26]; char url[64]; };
static SongInfo songs[15];
static int nSongs = 0;

// качает страницу-каталог и вытягивает до 15 ссылок песен
// (названия — из slug: "queen-bohemian-rhapsody-mid" -> "Queen Bohemian Rhapsody")
static bool fetchSongList() {
  nSongs = 0;
  // случайная «страница» каталога: bitmidi.com/?page=N
  int page = 1 + (esp_random() % 40);
  String url = "https://bitmidi.com/?page=" + String(page);
  Serial.printf("[midi] list %s\n", url.c_str());
  WiFiClientSecure cl;
  cl.setInsecure();
  HTTPClient http;
  if (!http.begin(cl, url)) return false;
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String body = http.getString();
  http.end();

  // паттерн: href="/xxx-mid" и на странице песни есть /uploads/N.mid
  // но у нас один запрос: вытащим прямые ссылки вида /uploads/N.mid
  // вместе с соседними title-подсказками НЕЛЬЗЯ (их нет на листинге).
  // План: собрать до 15 ссылок /xxx-mid; названия подтянем лениво
  // при выборе (одна страница песни = один запрос). Для скорости
  // покажем slug как название.
  int from = 0;
  while (nSongs < 15) {
    int a = body.indexOf("href=\"/", from);
    if (a < 0) break;
    int b = body.indexOf('"', a + 6);
    if (b < 0) break;
    String slug = body.substring(a + 6, b);
    from = b + 1;
    if (slug.endsWith("-mid") && !slug.startsWith("?") && slug.indexOf("//") < 0) {
      // красивое имя из slug: "queen-bohemian-rhapsody-mid" -> "Queen Bohemian Rhapsody"
      String name = slug.substring(0, slug.length() - 4);
      //capitalize words
      String pretty;
      int wstart = 0;
      while (wstart >= 0 && wstart < (int)name.length()) {
        int wend = name.indexOf('-', wstart);
        String word = (wend < 0) ? name.substring(wstart) : name.substring(wstart, wend);
        if (word.length()) {
          if (word[0] >= 'a' && word[0] <= 'z') word[0] = word[0] - 'a' + 'A';
          pretty += word + ' ';
        }
        wstart = (wend < 0) ? -1 : wend + 1;
      }
      pretty.trim();
      snprintf(songs[nSongs].title, sizeof songs[nSongs].title, "%s", pretty.c_str());
      snprintf(songs[nSongs].url, sizeof songs[nSongs].url, "https://bitmidi.com/%s", slug.c_str());
      nSongs++;
    }
  }
  Serial.printf("[midi] %d songs\n", nSongs);
  return nSongs > 0;
}

// качает страницу песни, находит прямой .mid, скачивает в буфер
static bool downloadSong(const SongInfo& s, uint8_t** outBuf, size_t* outLen) {
  WiFiClientSecure cl;
  cl.setInsecure();
  HTTPClient http;
  if (!http.begin(cl, s.url)) return false;
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String page = http.getString();
  http.end();
  int u = page.indexOf("/uploads/");
  if (u < 0) return false;
  int ue = page.indexOf('"', u);
  if (ue < 0) ue = page.indexOf('\'', u);
  if (ue < 0) return false;
  String midUrl = "https://bitmidi.com" + page.substring(u, ue);
  Serial.printf("[midi] mid: %s\n", midUrl.c_str());

  if (!http.begin(cl, midUrl)) return false;
  code = http.GET();
  if (code != 200) { http.end(); return false; }
  int total = http.getSize();
  if (total <= 0 || total > 120 * 1024) { http.end(); return false; }
  uint8_t* buf = (uint8_t*)malloc(total);
  if (!buf) { http.end(); return false; }
  WiFiClient* st = http.getStreamPtr();
  int got = 0;
  uint32_t t0 = millis();
  while (got < total && millis() - t0 < 20000) {
    size_t avail = st ? st->available() : 0;
    if (avail) {
      int nrd = st->readBytes(buf + got, min((size_t)512, avail));
      got += nrd; t0 = millis();
    } else delay(4);
  }
  http.end();
  if (got != total) { free(buf); return false; }
  *outBuf = buf;
  *outLen = got;
  return true;
}

// ---------- приложение ----------
void appMidi() {
  bool listLoaded = false;
  int sel = 0;

  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) { seqFree(); beep(400, 100); return; }

    if (e == EV_OK) {
      if (!listLoaded || sel == 0) {
        // MIDI LIST: загрузить/заменить список (повторный клик — новый)
        if (!netOnline()) {
          char ssid[33], pass[65];
          if (!wifiLoad(ssid, sizeof ssid, pass, sizeof pass)) {
            uiToast("No saved WiFi.", "Set up in NET", 1200);
            continue;
          }
          WiFi.mode(WIFI_STA);
          if (!wifiConnectAnimated(ssid, pass, 15000)) { WiFi.mode(WIFI_OFF); continue; }
        }
        beep(1200, 40);
        bool ok = fetchSongList();
        WiFi.mode(WIFI_OFF);
        if (!ok) { uiToast("list failed", "try again", 1200); continue; }
        listLoaded = true;
        sel = 1;               // сразу на первую песню
        beep(1600, 50);
      } else {
        // играть выбранную
        beep(1500, 40);
        if (!netOnline()) {
          char ssid[33], pass[65];
          if (!wifiLoad(ssid, sizeof ssid, pass, sizeof pass)) { uiToast("No WiFi", "", 1000); continue; }
          WiFi.mode(WIFI_STA);
          if (!wifiConnectAnimated(ssid, pass, 15000)) { WiFi.mode(WIFI_OFF); continue; }
        }
        uint8_t* buf = nullptr; size_t len = 0;
        d.clearDisplay(); headerBar("DOWNLOAD");
        d.setTextColor(1); d.setTextSize(1);
        d.setCursor(4, 26); d.print(songs[sel - 1].title);
        d.display();
        bool ok = downloadSong(songs[sel - 1], &buf, &len);
        if (ok) ok = smfParse(buf, len);
        if (buf) free(buf);
        WiFi.mode(WIFI_OFF);
        if (!ok) { uiToast("bad midi file", "try another", 1500); continue; }
        playSequence(songs[sel - 1].title);
        seqFree();
      }
    }
    if (e == EV_UP && sel > 0)   { sel--; beep(900, 15); }
    if (e == EV_DOWN && sel < nSongs) { sel++; beep(900, 15); }

    // отрисовка
    d.clearDisplay();
    headerBar("MIDI PLAYER");
    d.setTextColor(1); d.setTextSize(1);
    if (!listLoaded) {
      d.setCursor(4, 18); d.print("Cloud MIDI base.");
      d.setCursor(4, 30); d.print("bitmidi.com");
      d.setCursor(4, 42); d.print("click = MIDI LIST");
    } else {
      int top = (sel > 4) ? sel - 4 : 0;
      // пункт 0 = «обновить список»
      int y = 16;
      bool cur0 = (sel == 0);
      if (cur0) { d.fillRect(0, y - 1, W, 10, 1); d.setTextColor(0); } else d.setTextColor(1);
      d.setCursor(4, y); d.print("* MIDI LIST (new)");
      for (int i = 0; i < 5 && top + i < nSongs; i++) {
        y = 27 + i * 9;
        bool cur = (top + i + 1 == sel);
        if (cur) { d.fillRect(0, y - 1, W, 9, 1); d.setTextColor(0); } else d.setTextColor(1);
        d.setFontTiny();
        d.setCursor(4, y); d.print(songs[top + i].title);
        d.setTextSize(1);
      }
      d.setTextColor(1);
      char c[10]; snprintf(c, sizeof c, "%d/15", nSongs);
      d.setCursor(W - 7 * strlen(c) - 2, 1); d.print(c);
    }
    d.setCursor(4, 55); d.print(listLoaded ? "click=play/list" : "hold=exit");
    d.display();
    delay(8);
  }
}
