// venster_x11.cpp — het paneel in een X11-venster (Linux), als kind van het venster van de host.
// Eigen verbinding met de X-server; de events komen via de host (CLAP posix-fd-support), het hertekenen via de
// timer van de host (timer-support). Alles op de mainthread.
#include "paneel.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

namespace pb {

namespace {

int stilFout(Display*, XErrorEvent*) { return 0; }

class VensterX11 : public Venster {
 public:
  VensterX11(Paneel& p, const clap_host_t* host, uint32_t b, uint32_t h) : paneel_(p), host_(host), b_(b), h_(h) {
    dpy_ = XOpenDisplay(nullptr);
    if (host_) {
      timers_ = static_cast<const clap_host_timer_support_t*>(host_->get_extension(host_, CLAP_EXT_TIMER_SUPPORT));
      fds_ = static_cast<const clap_host_posix_fd_support_t*>(host_->get_extension(host_, CLAP_EXT_POSIX_FD_SUPPORT));
    }
    paneel_.zetGrootte(b_, h_, 1);
  }
  ~VensterX11() override {
    if (timers_ && timer_ != CLAP_INVALID_ID) timers_->unregister_timer(host_, timer_);
    if (fds_ && fd_ >= 0) fds_->unregister_fd(host_, fd_);
    if (dpy_) {
      if (win_) {
        // het venster van de host kan al weg zijn (dan is het onze ook): een BadWindow mag de host niet stoppen
        XSync(dpy_, False);
        auto oud = XSetErrorHandler(stilFout);
        XDestroyWindow(dpy_, win_);
        XSync(dpy_, False);
        XSetErrorHandler(oud);
      }
      XCloseDisplay(dpy_);
    }
  }
  bool goed() const { return dpy_ != nullptr; }

  bool zetOuder(const clap_window_t* ouder) override {
    if (!dpy_ || !ouder || win_) return false;
    const int scherm = DefaultScreen(dpy_);
    if (DefaultDepth(dpy_, scherm) < 24) return false;
    win_ = XCreateSimpleWindow(dpy_, (Window)ouder->x11, 0, 0, b_, h_, 0, 0, 0x232629);
    XSelectInput(dpy_, win_, ExposureMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | StructureNotifyMask);
    gc_ = DefaultGC(dpy_, scherm);
    XMapWindow(dpy_, win_);
    XFlush(dpy_);
    // de host roept ons voor events (fd) en om te tekenen (timer); zonder die extensies is het paneel stil
    if (fds_ && fd_ < 0 && fds_->register_fd(host_, ConnectionNumber(dpy_), CLAP_POSIX_FD_READ)) fd_ = ConnectionNumber(dpy_);
    if (timers_ && timer_ == CLAP_INVALID_ID && !timers_->register_timer(host_, 30, &timer_)) timer_ = CLAP_INVALID_ID;
    return true;
  }
  bool zetGrootte(uint32_t b, uint32_t h) override {
    b_ = b; h_ = h;
    paneel_.zetGrootte(b_, h_, 1);
    if (dpy_ && win_) {
      XResizeWindow(dpy_, win_, b_, h_);
      zetOpScherm();
    }
    return true;
  }
  bool toon() override {
    if (!dpy_ || !win_) return false;
    XMapRaised(dpy_, win_);
    XFlush(dpy_);
    return true;
  }
  bool verberg() override {
    if (!dpy_ || !win_) return false;
    XUnmapWindow(dpy_, win_);
    XFlush(dpy_);
    return true;
  }
  void opTimer(clap_id id) override {
    if (id != timer_) return;
    verwerk();
    if (paneel_.tik()) zetOpScherm();
  }
  void opFd(int fd, clap_posix_fd_flags_t) override {
    if (fd == fd_) verwerk();
  }

 private:
  void verwerk() {
    if (!dpy_) return;
    bool teken = false;
    while (XPending(dpy_)) {
      XEvent e;
      XNextEvent(dpy_, &e);
      if (e.xany.window != win_) continue;
      switch (e.type) {
        case Expose: teken |= e.xexpose.count == 0; break;
        case ConfigureNotify:
          if ((uint32_t)e.xconfigure.width != b_ || (uint32_t)e.xconfigure.height != h_) {
            b_ = (uint32_t)e.xconfigure.width; h_ = (uint32_t)e.xconfigure.height;
            paneel_.zetGrootte(b_, h_, 1);
            teken = true;
          }
          break;
        case ButtonPress:
          if (e.xbutton.button == Button1) {
            const int knop = paneel_.knopOp(e.xbutton.x, e.xbutton.y);
            const bool dubbel = knop >= 0 && knop == laatsteKnop_ && e.xbutton.time - laatsteKlik_ < 400;
            paneel_.muisNeer(e.xbutton.x, e.xbutton.y, e.xbutton.state & ShiftMask, dubbel);
            laatsteKnop_ = dubbel ? -1 : knop;  // een derde klik begint opnieuw
            laatsteKlik_ = e.xbutton.time;
            teken = true;
          }
          break;
        case MotionNotify:
          if (paneel_.sleept()) {
            paneel_.muisSleep(e.xmotion.x, e.xmotion.y, e.xmotion.state & ShiftMask);
            teken = true;
          }
          break;
        case ButtonRelease:
          if (e.xbutton.button == Button1) paneel_.muisOp();
          break;
        default: break;
      }
    }
    if (teken) zetOpScherm();
  }
  void zetOpScherm() {
    if (!dpy_ || !win_) return;
    paneel_.teken();
    const int scherm = DefaultScreen(dpy_);
    XImage* beeld = XCreateImage(dpy_, DefaultVisual(dpy_, scherm), (unsigned)DefaultDepth(dpy_, scherm), ZPixmap, 0,
                                 (char*)paneel_.pixels(), (unsigned)paneel_.pixelB(), (unsigned)paneel_.pixelH(), 32,
                                 paneel_.pixelB() * 4);
    if (!beeld) return;
    XPutImage(dpy_, win_, gc_, beeld, 0, 0, 0, 0, (unsigned)paneel_.pixelB(), (unsigned)paneel_.pixelH());
    beeld->data = nullptr;  // de pixels zijn van het paneel
    XDestroyImage(beeld);
    XFlush(dpy_);
  }

  Paneel& paneel_;
  const clap_host_t* host_;
  const clap_host_timer_support_t* timers_ = nullptr;
  const clap_host_posix_fd_support_t* fds_ = nullptr;
  Display* dpy_ = nullptr;
  Window win_ = 0;
  GC gc_ = nullptr;
  clap_id timer_ = CLAP_INVALID_ID;
  int fd_ = -1;
  uint32_t b_, h_;
  Time laatsteKlik_ = 0;
  int laatsteKnop_ = -1;
};

}  // namespace

const char* vensterApi() { return CLAP_WINDOW_API_X11; }

Venster* maakVenster(Paneel& paneel, const clap_host_t* host, uint32_t b, uint32_t h) {
  auto* v = new VensterX11(paneel, host, b, h);
  if (!v->goed()) {
    delete v;
    return nullptr;
  }
  return v;
}

}  // namespace pb
