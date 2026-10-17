// paneel_x11.cpp — het frontpaneel als X11-kindvenster (Linux). Een eigen verbinding met de X-server; de host meldt
// via posix-fd-support dat er X-events zijn en roept via timer-support ±30 keer per seconde aan om de waarden te
// verversen. Maten zijn pixels; set_scale geeft het aantal pixels per punt.
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <clap/ext/posix-fd-support.h>
#include <clap/ext/timer-support.h>

#include <cmath>

#include "venster.h"

namespace paneel {

namespace {

class X11Venster final : public Venster {
 public:
  X11Venster(Paneel* p, const clap_host_t* host, Display* d, uint32_t b, uint32_t h)
      : paneel_(p), host_(host), dpy_(d), b_(b), h_(h) {
    fd_ = static_cast<const clap_host_posix_fd_support_t*>(host->get_extension(host, CLAP_EXT_POSIX_FD_SUPPORT));
    timer_ = static_cast<const clap_host_timer_support_t*>(host->get_extension(host, CLAP_EXT_TIMER_SUPPORT));
    const int scherm = DefaultScreen(dpy_);
    Visual* v = DefaultVisual(dpy_, scherm);
    // alleen TrueColor met 8 bits per kleur kan de buffer van het paneel rechtstreeks tonen; anders blijft het
    // venster effen (het paneel werkt dan wel, je ziet het alleen niet)
    kleuren_ = v->c_class == TrueColor && DefaultDepth(dpy_, scherm) >= 24 && v->red_mask == 0xff0000 &&
               v->green_mask == 0xff00 && v->blue_mask == 0xff;
  }

  ~X11Venster() override {
    if (timerId_ != CLAP_INVALID_ID && timer_) timer_->unregister_timer(host_, timerId_);
    if (fdGemeld_ && fd_) fd_->unregister_fd(host_, ConnectionNumber(dpy_));
    if (win_) XDestroyWindow(dpy_, win_);
    XCloseDisplay(dpy_);
  }

  bool zetOuder(const clap_window_t* w) override {
    if (!w || win_) return false;
    const int scherm = DefaultScreen(dpy_);
    win_ = XCreateSimpleWindow(dpy_, (Window)w->x11, 0, 0, b_, h_, 0, 0, 0x232629);
    if (!win_) return false;
    XSelectInput(dpy_, win_, ExposureMask | ButtonPressMask | ButtonReleaseMask | Button1MotionMask | StructureNotifyMask);
    gc_ = DefaultGC(dpy_, scherm);
    XMapWindow(dpy_, win_);
    XFlush(dpy_);
    if (fd_ && fd_->register_fd(host_, ConnectionNumber(dpy_), CLAP_POSIX_FD_READ)) fdGemeld_ = true;
    if (timer_ && !timer_->register_timer(host_, 30, &timerId_)) timerId_ = CLAP_INVALID_ID;
    return true;
  }

  void zetMaat(uint32_t b, uint32_t h) override {
    b_ = b; h_ = h;
    if (win_) { XResizeWindow(dpy_, win_, b_, h_); teken(); }
  }
  bool zetSchaal(double s) override {
    schaal_ = s > 0 ? s : 1;
    teken();
    return true;
  }
  void toon(bool ja) override {
    if (!win_) return;
    if (ja) XMapWindow(dpy_, win_); else XUnmapWindow(dpy_, win_);
    XFlush(dpy_);
  }

  void opTimer(clap_id) override {
    events();  // voor het geval de host geen fd bewaakt
    if (paneel_->ververs()) teken();
  }
  void opFd(int) override { events(); }

 private:
  void events() {
    bool tekenen = false;
    while (XPending(dpy_)) {
      XEvent e;
      XNextEvent(dpy_, &e);
      const double x = e.xbutton.x / schaal_, y = e.xbutton.y / schaal_;
      switch (e.type) {
        case Expose: tekenen = true; break;
        case ConfigureNotify: tekenen = true; break;
        case ButtonPress:
          if (e.xbutton.button == Button1) {
            // dubbelklik: twee keer binnen 400 ms op dezelfde knop, zonder slepen ertussen
            const int knop = paneel_->knopOp(x, y);
            const bool dubbel = knop >= 0 && knop == vorigeKnop_ && e.xbutton.time - vorigeTijd_ < 400;
            vorigeKnop_ = dubbel ? -1 : knop;
            vorigeTijd_ = e.xbutton.time;
            paneel_->muisNeer(x, y, e.xbutton.state & ShiftMask, dubbel);
            tekenen = true;
          }
          break;
        case MotionNotify:
          vorigeKnop_ = -1;  // na slepen telt de volgende klik niet als dubbelklik
          paneel_->muisSleep(e.xmotion.x / schaal_, e.xmotion.y / schaal_, e.xmotion.state & ShiftMask);
          tekenen = true;
          break;
        case ButtonRelease:
          if (e.xbutton.button == Button1) { paneel_->muisOp(); tekenen = true; }
          break;
        default: break;
      }
    }
    if (tekenen) teken();
  }

  void teken() {
    if (!win_ || !kleuren_) return;
    const Beeld& b = paneel_->teken(b_ / schaal_, h_ / schaal_, schaal_);
    XImage* im = XCreateImage(dpy_, DefaultVisual(dpy_, DefaultScreen(dpy_)), DefaultDepth(dpy_, DefaultScreen(dpy_)),
                              ZPixmap, 0, (char*)b.pixels(), b.breedte(), b.hoogte(), 32, b.breedte() * 4);
    if (!im) return;
    XPutImage(dpy_, win_, gc_, im, 0, 0, 0, 0, b.breedte(), b.hoogte());
    im->data = nullptr;  // de pixels zijn van het paneel
    XDestroyImage(im);
    XFlush(dpy_);
  }

  Paneel* paneel_;
  const clap_host_t* host_;
  const clap_host_posix_fd_support_t* fd_ = nullptr;
  const clap_host_timer_support_t* timer_ = nullptr;
  Display* dpy_;
  Window win_ = 0;
  GC gc_ = nullptr;
  uint32_t b_, h_;
  double schaal_ = 1;
  bool kleuren_ = false, fdGemeld_ = false;
  clap_id timerId_ = CLAP_INVALID_ID;
  int vorigeKnop_ = -1;
  Time vorigeTijd_ = 0;
};

}  // namespace

const char* Venster::api() { return CLAP_WINDOW_API_X11; }

Venster* Venster::maak(Paneel* p, const clap_host_t* host, uint32_t b, uint32_t h) {
  Display* d = XOpenDisplay(nullptr);
  if (!d) return nullptr;
  return new X11Venster(p, host, d, b, h);
}

}  // namespace paneel
