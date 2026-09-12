#pragma once
// ==================================================================
//  sound.h — пьезо-спикер (см. sound.cpp).
// ==================================================================
void soundBegin();
void beep(int f, int ms);      // короткий бип (не мешает мелодии)
void beepWait(int f, int ms);  // блокирующий бип
void beepTick();               // дергать из главного цикла

void bgSongTick();
void bgSongStart(int idx);
void bgSongStop();
bool bgSongActive();
int  bgSongIndex();            // что играет сейчас
int  bgSongCount();
const char* bgSongName(int i);
