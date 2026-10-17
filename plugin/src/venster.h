// venster.h — het frontpaneel in een venster van de host: Cocoa (macOS, paneel_mac.mm) of X11 (Linux,
// paneel_x11.cpp). Alleen de mainthread. Het venster zet de pixels van het paneel op het scherm en geeft de muis door;
// het praat niet met het Ontwerp en niet met de audiothread.
#pragma once
#include <clap/clap.h>

#include "paneel.h"

namespace paneel {

class Venster {
 public:
  static const char* api();                // CLAP_WINDOW_API_COCOA of CLAP_WINDOW_API_X11 (nullptr: geen GUI)
  static Venster* maak(Paneel* p, const clap_host_t* host, uint32_t b, uint32_t h);  // nullptr als het niet kan
  virtual ~Venster() = default;

  // maten in de eenheden van de host: punten op Cocoa, pixels op X11 (CLAP gui.h)
  virtual bool zetOuder(const clap_window_t* w) = 0;
  virtual void zetMaat(uint32_t b, uint32_t h) = 0;
  virtual bool zetSchaal(double s) = 0;  // false: het venster vraagt de schaal zelf aan het OS (Cocoa)
  virtual void toon(bool ja) = 0;

  // X11: de host roept deze aan via timer-support en posix-fd-support
  virtual void opTimer(clap_id) {}
  virtual void opFd(int) {}
};

}  // namespace paneel
