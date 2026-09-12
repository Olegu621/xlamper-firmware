// ==================================================================
//  media.cpp — медиа-приложения: музыкальный плеер (v0.11 songs).
// ==================================================================
#include "media.h"
#include "config.h"
#include "settings.h"
#include "display.h"
#include "input.h"
#include "sound.h"

void appSongs() {
  int sel = 0;
  while (true) {
    Ev e = pollEvent();
    if (e == EV_EXIT) { bgSongStop(); beep(400, 100); return; }
    if (e == EV_UP)   { sel = (sel + bgSongCount() - 1) % bgSongCount(); beep(900, 15); }
    if (e == EV_DOWN) { sel = (sel + 1) % bgSongCount(); beep(900, 15); }
    if (e == EV_OK) {
      if (bgSongActive() && bgSongIndex() == sel) { bgSongStop(); beep(500, 80); }
      else { bgSongStart(sel); beep(1500, 40); }
    }
    bgSongTick();

    d.clearDisplay();
    d.fillRect(0, 0, W, 13, 1);
    d.setTextColor(0); d.setTextSize(1);
    d.setCursor(2, 1); d.print("MUSIC");
    d.setCursor(100, 1); d.format("%d/%d", sel + 1, bgSongCount());
    for (int i = 0; i < bgSongCount(); i++) {
      int y = 16 + i * 10;
      bool cur = (i == sel);
      if (cur) { d.fillRect(0, y - 1, W, 11, 1); d.setTextColor(0); }
      else d.setTextColor(1);
      d.setTextSize(1); d.setCursor(4, y); d.print(bgSongName(i));
      if (bgSongActive() && i == bgSongIndex()) {
        uint8_t ph = (millis() / 160) % 2;
        d.print(ph ? " *" : " +");
      }
    }
    d.setTextColor(1); d.setTextSize(1);
    d.setCursor(4, 54); d.print("click=play/stop hold=exit");
    d.display();
  }
}
