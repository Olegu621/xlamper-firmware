// ==================================================================
//  sound.cpp — пьезо-спикер: короткие бипы, фоновые мелодии.
//  Инвариант: одновременно звучит ЛИБО бип, ЛИБО мелодия
//  (как в v0.11: beep() молчит, когда играет песня).
// ==================================================================
#include "sound.h"
#include "config.h"

struct Note { uint16_t f; uint8_t t; };

// ---------- мелодии (PROGMEM, совместимы с v0.11) ----------
const Note SONG1[] PROGMEM = {
  {659,3},{659,3},{0,1},{659,6},{0,1},{523,4},{587,4},
  {659,3},{659,3},{0,1},{659,6},{0,1},{523,4},{587,4},
  {587,3},{587,3},{0,1},{587,6},{0,1},{494,4},{523,4},
  {587,3},{587,3},{0,1},{587,6},{0,1},{494,4},{523,4},
  {659,3},{659,3},{0,1},{659,6},{0,1},{523,4},{587,4},
  {587,3},{587,3},{0,1},{587,6},{0,1},{494,4},{523,4},
  {659,8},{587,8},{523,8},{494,8},
  {0,0}
};
const Note SONG2[] PROGMEM = {
  {784,4},{659,2},{587,2},{523,4},{587,4},
  {659,2},{587,2},{523,2},{494,2},{523,2},
  {587,4},{523,2},{494,2},{440,4},{494,4},
  {523,2},{494,2},{440,2},{415,2},{440,2},
  {494,4},{440,2},{392,2},{349,4},{392,4},
  {440,8},{392,8},{349,16},
  {784,4},{880,4},{784,4},{659,8},
  {587,4},{659,4},{523,8},
  {440,4},{494,4},{523,4},{587,4},
  {523,8},{0,2},
  {0,0}
};
const Note SONG3[] PROGMEM = {
  {392,2},{392,2},{392,4},{392,2},{392,2},{392,4},
  {392,2},{440,2},{494,4},{494,2},{440,2},
  {494,2},{523,2},{587,4},
  {587,2},{523,2},{494,2},{440,2},{392,4},
  {392,2},{440,2},{494,4},{494,2},{440,2},{392,4},
  {392,2},{440,2},{494,4},{523,2},{494,2},
  {440,2},{392,2},{392,2},{392,2},{440,4},
  {494,2},{523,2},{494,2},{440,2},{392,4},
  {0,0}
};
const Note SONG4[] PROGMEM = {
  {523,2},{494,2},{440,4},{392,2},
  {440,2},{494,4},{523,2},{494,2},
  {440,4},{392,8},
  {494,2},{440,2},{392,2},{349,2},{392,4},
  {440,2},{392,2},{349,2},{330,2},{349,4},
  {392,2},{349,2},{330,2},{294,2},{330,4},
  {349,4},{392,4},{440,8},
  {523,2},{494,2},{440,4},{392,2},
  {440,2},{494,4},{523,2},{494,2},
  {440,4},{392,8},
  {0,0}
};
const Note SONG5[] PROGMEM = {
  {698,2},{587,2},{523,2},{466,2},{392,4},{392,2},{392,2},
  {466,2},{392,2},{349,2},{349,2},{349,2},
  {466,2},{349,2},{311,2},{311,2},{311,2},
  {466,2},{349,2},{698,2},{587,2},{523,2},{466,2},
  {784,4},{698,2},{587,2},{523,2},{466,2},{392,4},
  {831,4},{784,2},{698,2},{659,2},{622,2},
  {659,4},{587,2},{523,2},{466,2},
  {392,8},{0,2},
  {0,0}
};

static const char* const songNames[] = { "Grasshopper", "Kalinka", "Xmas Tree", "Swings", "Harry P." };
static const Note* const songs[] = { SONG1, SONG2, SONG3, SONG4, SONG5 };
constexpr int N_SONGS = 5;

static uint32_t sndUntil = 0;
static bool     bgPlaying = false;
static int      curSong = 0;
static uint32_t songPos = 0, songTUntil = 0;

void soundBegin() {
  ledcAttach(PIN_BUZZER, 2000, 10);
}

void beep(int f, int ms) {
  if (bgPlaying) return;
  ledcWriteTone(PIN_BUZZER, f);
  sndUntil = millis() + ms;
}

void beepWait(int f, int ms) {
  ledcWriteTone(PIN_BUZZER, f);
  delay(ms);
  ledcWriteTone(PIN_BUZZER, 0);
}

void beepTick() {
  if (sndUntil && (int32_t)(millis() - sndUntil) >= 0) {
    ledcWriteTone(PIN_BUZZER, 0);
    sndUntil = 0;
  }
}

void bgSongTick() {
  if (!bgPlaying) return;
  if ((int32_t)(millis() - songTUntil) < 0) return;
  Note nt;
  memcpy_P(&nt, &songs[curSong][songPos], sizeof(Note));
  if (nt.f == 0 && nt.t == 0) { songPos = 0; return; }
  songTUntil = millis() + (uint32_t)nt.t * 90;
  songPos++;
  ledcWriteTone(PIN_BUZZER, nt.f);
}

void bgSongStart(int idx) {
  if (idx < 0 || idx >= N_SONGS) return;
  curSong = idx; songPos = 0; songTUntil = 0;
  bgPlaying = true; sndUntil = 0;
}
void bgSongStop() { bgPlaying = false; ledcWriteTone(PIN_BUZZER, 0); }
bool bgSongActive() { return bgPlaying; }
int  bgSongIndex() { return curSong; }
int  bgSongCount() { return N_SONGS; }
const char* bgSongName(int i) { return (i >= 0 && i < N_SONGS) ? songNames[i] : ""; }
