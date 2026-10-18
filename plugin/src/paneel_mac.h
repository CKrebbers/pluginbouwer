// paneel_mac.h — de Cocoa-view van het paneel (paneel_mac.mm). Alle functies: mainthread.
#pragma once

namespace pb_paneel {
class Paneel;
}

void* pbMacMaak(pb_paneel::Paneel* paneel);  // een NSView (eigendom van de aanroeper; pbMacWeg ruimt hem op)
void pbMacWeg(void* view);
void pbMacOuder(void* view, void* ouder);    // ouder: de NSView van de host
void pbMacMaat(void* view, double b, double h);
void pbMacToon(void* view, bool zichtbaar);
