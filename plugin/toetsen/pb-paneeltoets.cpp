// pb-paneeltoets — toets van fase 3 (PROMPTS.md): het model van het frontpaneel met de echte koppeling van de plugin.
//
//   pb-paneeltoets <frontpaneel.svg> [uitmap]
//
// Linkt de statische impl-bibliotheek (dezelfde code als in het .clap, de .vst3 en de .component) en maakt de plugin
// via de factory; het paneel praat met de echte rij (koppeling.h). Draait op Linux en macOS, zonder venster. De
// Cocoa-laag (tekenen, muis, venster in een host) toetst pb-paneel op macOS door het gebouwde binary.
//   - Indeling tegen de mockup: per knop label, het eind van de boog, de wijzer en de waardetekst zoals in
//     frontpaneel.svg (op ware grootte, alle knoppen op hun standaard), en de kleuren van de bogen.
//   - Knoppen via de host (valkuil 14): slepen, Shift, dubbelklik, klemmen, een gebaar dat het venster afbreekt;
//     elke wijziging komt als GESTURE_BEGIN/PARAM_VALUE/GESTURE_END uit params.flush (niet actief) en uit process
//     (actief), de plugin vraagt request_flush, en get_value volgt pas als process/flush hem toepaste.
//   - De DSP kreeg de waarde langs dezelfde weg als een event van de host: de render is bit voor bit gelijk aan een
//     verse instantie die dezelfde waarde als event kreeg.
//   - Een volle rij verliest niets (wachtrij op de mainthread, in volgorde).
//   - Een verandering door de host (automatisering) staat bij de volgende tik op het paneel.
// Met een uitmap schrijft hij paneel-standaard.svg en paneel-bediend.svg (de tekenopdrachten, om te bekijken).
//
// Exitcode 0 = alles groen; anders 1, met per mislukte controle een regel "MISLUKT".
#include <clap/clap.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "entry.h"
#include "koppeling.h"
#include "paneel.h"

using namespace pb::paneel;

namespace {

int fouten = 0, goed = 0;
void controle(bool ok, const std::string& wat) {
  if (ok) ++goed;
  else ++fouten;
  std::printf("%s %s\n", ok ? "ok      " : "MISLUKT ", wat.c_str());
}
[[noreturn]] void fout(const std::string& t) {
  std::fprintf(stderr, "pb-paneeltoets: %s\n", t.c_str());
  std::exit(2);
}
std::string g(double v) {
  char t[64];
  std::snprintf(t, sizeof t, "%.6g", v);
  return t;
}

// --- host --------------------------------------------------------------------------------------------------------
int flushVragen = 0;
void hpRescan(const clap_host_t*, clap_param_rescan_flags) {}
void hpClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hpFlush(const clap_host_t*) { ++flushVragen; }
const clap_host_params_t HOST_PARAMS = {hpRescan, hpClear, hpFlush};
const void* hostExt(const clap_host_t*, const char* id) {
  return !std::strcmp(id, CLAP_EXT_PARAMS) ? &HOST_PARAMS : nullptr;
}
void hostNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-paneeltoets", "Pluginbouwer", "https://varve.nl", "0.1",
                          hostExt, hostNiets, hostNiets, hostNiets};

union Ev {
  clap_event_header_t h;
  clap_event_param_value_t p;
  clap_event_param_gesture_t g;
  clap_event_note_t n;
};
struct Lijst { std::vector<Ev> ev; };
uint32_t lSize(const clap_input_events_t* l) { return (uint32_t)static_cast<Lijst*>(l->ctx)->ev.size(); }
const clap_event_header_t* lGet(const clap_input_events_t* l, uint32_t i) {
  return &static_cast<Lijst*>(l->ctx)->ev[i].h;
}
bool lPush(const clap_output_events_t* l, const clap_event_header_t* h) {
  Ev e{};
  if (h->size > sizeof e) return false;
  std::memcpy(&e, h, h->size);
  static_cast<Lijst*>(l->ctx)->ev.push_back(e);
  return true;
}
Ev paramEv(clap_id id, double v) {
  Ev e{};
  e.p.header = {sizeof(e.p), 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
  e.p.param_id = id; e.p.note_id = -1; e.p.port_index = -1; e.p.channel = -1; e.p.key = -1; e.p.value = v;
  return e;
}
Ev nootEv(bool aan, int16_t key, uint32_t t) {
  Ev e{};
  e.n.header = {sizeof(e.n), t, CLAP_CORE_EVENT_SPACE_ID, (uint16_t)(aan ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 0};
  e.n.note_id = -1; e.n.port_index = 0; e.n.channel = 0; e.n.key = key; e.n.velocity = aan ? 0.8 : 0.0;
  return e;
}

// een verwacht uitvoerevent: 'B' begin, 'V' waarde, 'E' eind
struct Verwacht { char soort; clap_id id; double waarde; };
std::string beschrijf(const std::vector<Ev>& ev) {
  std::string s;
  for (const Ev& e : ev) {
    if (e.h.type == CLAP_EVENT_PARAM_GESTURE_BEGIN) s += "B" + std::to_string(e.g.param_id) + " ";
    else if (e.h.type == CLAP_EVENT_PARAM_GESTURE_END) s += "E" + std::to_string(e.g.param_id) + " ";
    else if (e.h.type == CLAP_EVENT_PARAM_VALUE) s += "V" + std::to_string(e.p.param_id) + "=" + g(e.p.value) + " ";
    else s += "?" + std::to_string(e.h.type) + " ";
    if (e.h.time != 0) s += "@" + std::to_string(e.h.time) + " ";
  }
  return s.empty() ? "(geen)" : s;
}
bool zoals(const std::vector<Ev>& ev, const std::vector<Verwacht>& w) {
  if (ev.size() != w.size()) return false;
  for (size_t k = 0; k < ev.size(); ++k) {
    const Ev& e = ev[k];
    if (e.h.time != 0 || e.h.space_id != CLAP_CORE_EVENT_SPACE_ID) return false;
    if (w[k].soort == 'B' && !(e.h.type == CLAP_EVENT_PARAM_GESTURE_BEGIN && e.g.param_id == w[k].id)) return false;
    if (w[k].soort == 'E' && !(e.h.type == CLAP_EVENT_PARAM_GESTURE_END && e.g.param_id == w[k].id)) return false;
    if (w[k].soort == 'V' &&
        !(e.h.type == CLAP_EVENT_PARAM_VALUE && e.p.param_id == w[k].id &&
          std::fabs(e.p.value - w[k].waarde) <= 1e-9 * std::fmax(1.0, std::fabs(w[k].waarde))))
      return false;
  }
  return true;
}
std::string beschrijf(const std::vector<Verwacht>& w) {
  std::string s;
  for (auto& x : w) s += std::string(1, x.soort) + std::to_string(x.id) + (x.soort == 'V' ? "=" + g(x.waarde) : "") + " ";
  return s;
}

// --- de plugin uit de statische bibliotheek ----------------------------------------------------------------------
struct Inst {
  const clap_plugin_t* pl = nullptr;
  const clap_plugin_params_t* pp = nullptr;
  Koppeling k;
  bool actief = false;
  Inst() {
    auto* fab = static_cast<const clap_plugin_factory_t*>(pbEntryGetFactory(CLAP_PLUGIN_FACTORY_ID));
    if (!fab || fab->get_plugin_count(fab) != 1) fout("geen factory");
    pl = fab->create_plugin(fab, &HOST, fab->get_plugin_descriptor(fab, 0)->id);
    if (!pl || !pl->init(pl)) fout("plugin start niet");
    pp = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    if (!pp || !pbPaneelKoppeling(pl, &k)) fout("geen params of geen koppeling");
  }
  ~Inst() {
    if (actief) pl->deactivate(pl);
    pl->destroy(pl);
  }
  std::vector<Ev> flush(const std::vector<Ev>& in = {}) {
    Lijst li{in}, uit;
    clap_input_events_t i = {&li, lSize, lGet};
    clap_output_events_t o = {&uit, lPush};
    pp->flush(pl, &i, &o);
    return uit.ev;
  }
  double waarde(clap_id id) {
    double v = NAN;
    pp->get_value(pl, id, &v);
    return v;
  }
  void activeer() {
    if (!pl->activate(pl, 48000, 1, 512)) fout("activate mislukt");
    if (!pl->start_processing(pl)) fout("start_processing mislukt");
    actief = true;
  }
  // één blok van 512; geeft de uitvoerevents, en de audio in `audio`
  std::vector<Ev> blok(const std::vector<Ev>& in, std::vector<float>* audio, int64_t steady) {
    std::vector<float> l(512), r(512);
    float* uitK[2] = {l.data(), r.data()};
    Lijst li{in}, uit;
    clap_input_events_t i = {&li, lSize, lGet};
    clap_output_events_t o = {&uit, lPush};
    clap_audio_buffer_t au = {uitK, nullptr, 2, 0, 0};
    clap_process_t pr = {steady, 512, nullptr, nullptr, &au, 0, 1, &i, &o};
    if (pl->process(pl, &pr) == CLAP_PROCESS_ERROR) fout("process gaf een fout");
    if (audio) {
      audio->insert(audio->end(), l.begin(), l.end());
      audio->insert(audio->end(), r.begin(), r.end());
    }
    return uit.ev;
  }
};

// --- de mockup lezen ---------------------------------------------------------------------------------------------
struct MockKnop {
  std::string naam, label, waarde;
  double x, y, boogX, boogY, startX, startY, wijzerX, wijzerY;
};
std::vector<MockKnop> leesMockup(const std::string& svg) {
  std::vector<MockKnop> k;
  std::regex groep(
      R"re(<g transform="translate\(([-0-9.]+) ([-0-9.]+)\)"><title>([^<]*)</title>\s*<text y="-34" class="l">([^<]*)</text>\s*)re"
      R"re(<path d="[^"]*" class="spoor0"/><path d="M([-0-9.]+) ([-0-9.]+) A22 22 0 [01] 1 ([-0-9.]+) ([-0-9.]+)" class="spoor"/>\s*)re"
      R"re(<circle r="16" class="knop"/><line x1="0" y1="0" x2="([-0-9.]+)" y2="([-0-9.]+)" class="wijzer"/>\s*)re"
      R"re(<text y="44" class="w">([^<]*)</text></g>)re");
  for (std::sregex_iterator it(svg.begin(), svg.end(), groep), e; it != e; ++it) {
    const std::smatch& m = *it;
    k.push_back({m[3], m[4], m[11], std::stod(m[1]), std::stod(m[2]), std::stod(m[7]), std::stod(m[8]),
                 std::stod(m[5]), std::stod(m[6]), std::stod(m[9]), std::stod(m[10])});
  }
  return k;
}
std::string kleurVanKlasse(const std::string& svg, const std::string& klasse, const std::string& eigenschap) {
  std::regex r("\\." + klasse + "\\{[^}]*" + eigenschap + ":(#[0-9a-f]{6})");
  std::smatch m;
  return std::regex_search(svg, m, r) ? std::string(m[1]) : "";
}
std::string hex(Kleur c) {
  char t[8];
  std::snprintf(t, sizeof t, "#%02x%02x%02x", c.r, c.g, c.b);
  return t;
}

// --- tekenopdrachten als SVG (om te bekijken) ---------------------------------------------------------------------
std::string alsSvg(const std::vector<Opdracht>& ops) {
  std::ostringstream o;
  o << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"560\" height=\"220\" viewBox=\"0 0 560 220\">\n";
  for (const Opdracht& d : ops) {
    switch (d.type) {
      case Opdracht::Vlak:
        o << "<rect x=\"" << d.x << "\" y=\"" << d.y << "\" width=\"" << d.b << "\" height=\"" << d.h << "\" fill=\""
          << hex(d.kleur) << "\"/>\n";
        break;
      case Opdracht::Rand:
        o << "<rect x=\"" << d.x << "\" y=\"" << d.y << "\" width=\"" << d.b << "\" height=\"" << d.h
          << "\" fill=\"none\" stroke=\"" << hex(d.kleur) << "\" stroke-width=\"" << d.dikte << "\"/>\n";
        break;
      case Opdracht::Pad:
        o << "<polyline fill=\"none\" stroke=\"" << hex(d.kleur) << "\" stroke-width=\"" << d.dikte << "\" points=\"";
        for (auto& p : d.punten) o << p.first << "," << p.second << " ";
        o << "\"/>\n";
        break;
      case Opdracht::Cirkel:
        o << "<circle cx=\"" << d.x << "\" cy=\"" << d.y << "\" r=\"" << d.b << "\" fill=\"" << hex(d.kleur)
          << "\" stroke=\"" << hex(d.rand) << "\" stroke-width=\"" << d.dikte << "\"/>\n";
        break;
      case Opdracht::Tekst:
        o << "<text x=\"" << d.x << "\" y=\"" << d.y << "\" fill=\"" << hex(d.kleur) << "\" font-size=\""
          << LETTER_PX[(int)d.letter] << "\" font-family=\"" << (d.letter == Letter::Kop ? "Silkscreen" : "IBM Plex Mono")
          << ",monospace\" text-anchor=\"" << (d.midden ? "middle" : "start") << "\">" << d.tekst << "</text>\n";
        break;
    }
  }
  o << "</svg>\n";
  return o.str();
}

bool dichtbij(double a, double b, double tol = 0.006) { return std::fabs(a - b) <= tol; }

// de opdrachten van knop i: label, groef, (spoor), cirkel, wijzer, waarde
struct KnopOps { const Opdracht *label = nullptr, *groef = nullptr, *spoor = nullptr, *cirkel = nullptr,
                 *wijzer = nullptr, *waarde = nullptr; };
KnopOps opsVanKnop(const std::vector<Opdracht>& ops, double cx, double cy) {
  KnopOps k;
  for (const Opdracht& d : ops) {
    if (d.type == Opdracht::Tekst && d.x == cx && d.y == cy - 34) k.label = &d;
    if (d.type == Opdracht::Tekst && d.x == cx && d.y == cy + 44) k.waarde = &d;
    if (d.type == Opdracht::Cirkel && d.x == cx && d.y == cy) k.cirkel = &d;
    if (d.type == Opdracht::Pad && !d.punten.empty()) {
      const auto& p0 = d.punten.front();
      if (d.punten.size() == 2 && p0.first == cx && p0.second == cy) k.wijzer = &d;
      else if (dichtbij(p0.first, cx - 15.556, 0.01) && dichtbij(p0.second, cy + 15.556, 0.01)) {
        if (d.kleur.r == GROEF.r && d.kleur.g == GROEF.g && d.kleur.b == GROEF.b) k.groef = &d;
        else k.spoor = &d;
      }
    }
  }
  return k;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) fout("gebruik: pb-paneeltoets <frontpaneel.svg> [uitmap]");
  std::ifstream f(argv[1]);
  if (!f) fout(std::string("kan ") + argv[1] + " niet lezen");
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string svg = ss.str();
  const std::string uitmap = argc > 2 ? argv[2] : "";

  const auto knoppen = pbPaneelKnoppen();
  const int n = (int)knoppen.size();

  // --- 1. indeling tegen de mockup (op ware grootte, alle knoppen op hun standaard) -----------------------------
  {
    Inst in;
    Paneel p(in.k, knoppen, pbPaneelNaam());
    p.zetMaat(ONTWERP_B, ONTWERP_H);
    controle(p.schaal() == 1 && p.ox() == 0 && p.oy() == 0, "op 560 × 220 is het paneel de mockup op ware grootte");
    const auto ops = p.teken();
    if (!uitmap.empty()) std::ofstream(uitmap + "/paneel-standaard.svg") << alsSvg(ops);
    const auto mock = leesMockup(svg);
    controle((int)mock.size() == n, "de mockup heeft " + std::to_string(mock.size()) + " knoppen, de plugin " +
                                        std::to_string(n));
    for (int i = 0; i < n && i < (int)mock.size(); ++i) {
      const MockKnop& m = mock[(size_t)i];
      const double cx = KNOP_X0 + KOLOM * i, cy = KNOP_Y;
      controle(cx == m.x && cy == m.y, m.naam + ": knop op (" + g(cx) + ", " + g(cy) + "), mockup (" + g(m.x) +
                                            ", " + g(m.y) + ")");
      controle(knoppen[(size_t)i].naam == m.naam, m.naam + ": volle naam (tooltip) " + knoppen[(size_t)i].naam);
      const KnopOps k = opsVanKnop(ops, cx, cy);
      controle(k.label && k.label->tekst == m.label, m.naam + ": label \"" + (k.label ? k.label->tekst : "") +
                                                          "\" (mockup \"" + m.label + "\")");
      controle(k.waarde && k.waarde->tekst == m.waarde, m.naam + ": waarde \"" + (k.waarde ? k.waarde->tekst : "") +
                                                             "\" (mockup \"" + m.waarde + "\")");
      controle(k.groef && dichtbij(k.groef->punten.back().first - cx, 15.56) &&
                   dichtbij(k.groef->punten.back().second - cy, 15.56) && k.groef->dikte == 3,
               m.naam + ": groef van −135° tot +135°, 3 px");
      const bool leeg = m.boogX == m.startX && m.boogY == m.startY;  // knop op zijn minimum: geen spoor
      if (leeg) controle(!k.spoor, m.naam + ": op het minimum geen spoor (zoals de mockup)");
      else
        controle(k.spoor && dichtbij(k.spoor->punten.back().first - cx, m.boogX) &&
                     dichtbij(k.spoor->punten.back().second - cy, m.boogY) && k.spoor->dikte == 3,
                 m.naam + ": spoor eindigt op (" +
                     (k.spoor ? g(k.spoor->punten.back().first - cx) + ", " + g(k.spoor->punten.back().second - cy)
                              : std::string("-")) +
                     "), mockup (" + g(m.boogX) + ", " + g(m.boogY) + ")");
      controle(k.wijzer && dichtbij(k.wijzer->punten.back().first - cx, m.wijzerX) &&
                   dichtbij(k.wijzer->punten.back().second - cy, m.wijzerY),
               m.naam + ": wijzer naar (" +
                   (k.wijzer ? g(k.wijzer->punten.back().first - cx) + ", " + g(k.wijzer->punten.back().second - cy)
                             : std::string("-")) +
                   "), mockup (" + g(m.wijzerX) + ", " + g(m.wijzerY) + ")");
      controle(k.cirkel && k.cirkel->b == 16 && k.cirkel->dikte == 2, m.naam + ": knop r = 16, rand 2 px");
      if (k.spoor) controle(hex(k.spoor->kleur) == kleurVanKlasse(svg, "spoor", "stroke"),
                            m.naam + ": spoor " + hex(k.spoor->kleur) + " (niet gekozen)");
      if (k.groef) controle(hex(k.groef->kleur) == kleurVanKlasse(svg, "spoor0", "stroke"),
                            m.naam + ": groef " + hex(k.groef->kleur));
    }
    bool naam = false, tape = false, schaduw = false, vlak = false;
    for (const Opdracht& d : ops) {
      if (d.type == Opdracht::Tekst && d.tekst == "PB TOETSENPAD" && d.x == 30 && d.y == 35 && d.letter == Letter::Kop)
        naam = true;
      if (d.type == Opdracht::Vlak && d.x == 24 && d.y == 18 && d.h == 24 && hex(d.kleur) == "#f2c200") tape = true;
      if (d.type == Opdracht::Vlak && d.x == 3 && d.y == 3 && d.b == 554 && d.h == 214 && hex(d.kleur) == "#0d0e0f")
        schaduw = true;
      if (d.type == Opdracht::Vlak && d.x == 0 && d.y == 0 && d.b == 554 && d.h == 214 && hex(d.kleur) == "#232629")
        vlak = true;
    }
    controle(naam && tape, "naamlabel \"PB TOETSENPAD\" (Silkscreen) op tape bij (24, 18)");
    controle(schaduw && vlak, "paneel 554 × 214 met harde schaduw 3 px rechts en onder");
    controle(svg.find("<text x=\"30\" y=\"35\" class=\"n\">PB TOETSENPAD</text>") != std::string::npos,
             "de mockup heeft hetzelfde naamlabel");

    // beginmaat en schalen
    p.zetMaat(START_B, START_H);
    controle(dichtbij(p.schaal(), 250.0 / 220.0, 1e-12) && dichtbij(p.oy(), 0, 1e-12),
             "640 × 250: gelijk geschaald (" + g(p.schaal()) + "), in het midden");
    const double s = p.schaal();
    controle(p.knopOp(p.ox() + 214 * s, p.oy() + 130 * s) == 2, "640 × 250: muis op het midden van Filter raakt Filter");
    controle(p.knopOp(p.ox() + 400 * s, p.oy() + 100 * s) == -1, "640 × 250: naast de knoppen raakt niets");
    const auto v = p.knopVlak(3);
    controle(dichtbij(v.x, p.ox() + (290 - 38) * s, 1e-9) && dichtbij(v.b, 76 * s, 1e-9),
             "tooltipvlak van Loslaten is zijn kolom van 76 px");
  }

  // --- 2. knoppen via de host, niet actief: params.flush ----------------------------------------------------------
  {
    Inst in;
    Paneel p(in.k, knoppen, pbPaneelNaam());
    p.zetMaat(ONTWERP_B, ONTWERP_H);
    const clap_id NOOT = 1, POORT = 2, FILTER = 3, LOS = 4;

    flushVragen = 0;
    p.muisNeer(214, 130, 1, false);
    controle(flushVragen >= 1, "muis neer op Filter: de plugin vraagt request_flush");
    controle(in.waarde(FILTER) == 900, "voor flush ziet de host nog niets veranderen (get_value 900)");
    p.muisSleep(214, 80, false);
    p.muisSleep(214, 30, false);
    controle(p.getoond(2) == 4850 && in.waarde(FILTER) == 900,
             "tijdens het slepen toont het paneel 4850, de host heeft nog 900 tot flush");
    p.muisOp();
    auto ev = in.flush();
    std::vector<Verwacht> w = {{'B', FILTER, 0}, {'V', FILTER, 2875}, {'V', FILTER, 4850}, {'E', FILTER, 0}};
    controle(zoals(ev, w), "Filter 50 en 100 px omhoog: " + beschrijf(ev) + "(verwacht " + beschrijf(w) + ")");
    controle(in.waarde(FILTER) == 4850, "na flush get_value(Filter) = " + g(in.waarde(FILTER)) + " (200 px = bereik)");
    controle(p.gekozen() == 2, "de laatst aangeraakte knop (Filter) is de gekozen knop");
    {
      const auto ops = p.teken();
      const KnopOps k = opsVanKnop(ops, 214, 130), k2 = opsVanKnop(ops, 290, 130);
      controle(k.spoor && hex(k.spoor->kleur) == "#f2c200", "de boog van de gekozen knop is tape");
      controle(k2.spoor && hex(k2.spoor->kleur) == "#40cbda", "de boog van de andere knoppen is spoor");
      controle(k.waarde && k.waarde->tekst == "4,85 kHz", "waardetekst Filter \"" + (k.waarde ? k.waarde->tekst : "") +
                                                               "\" (value_to_text van de plugin)");
    }
    controle(in.flush().empty(), "een tweede flush geeft niets meer");

    // Shift = 10× fijner, en loslaten van Shift midden in het slepen springt niet
    p.muisNeer(290, 130, 1, false);
    p.muisSleep(290, 130, true);   // Shift ingedrukt, nog niet bewogen
    p.muisSleep(290, 30, true);    // 100 px met Shift: 0,05 van het bereik
    p.muisSleep(290, 10, false);   // Shift los, 20 px: 0,1 van het bereik erbij
    p.muisOp();
    ev = in.flush();
    w = {{'B', LOS, 0}, {'V', LOS, 450 + 199.5}, {'V', LOS, 450 + 199.5 + 399}, {'E', LOS, 0}};
    controle(zoals(ev, w), "Loslaten met Shift: " + beschrijf(ev) + "(verwacht " + beschrijf(w) + ")");

    // dubbelklik = standaard
    p.muisNeer(214, 130, 1, false);
    p.muisOp();
    p.muisNeer(214, 130, 2, false);
    p.muisOp();
    ev = in.flush();
    w = {{'B', FILTER, 0}, {'E', FILTER, 0}, {'B', FILTER, 0}, {'V', FILTER, 900}, {'E', FILTER, 0}};
    controle(zoals(ev, w), "dubbelklik op Filter: " + beschrijf(ev) + "(verwacht " + beschrijf(w) + ")");
    controle(in.waarde(FILTER) == 900, "na de dubbelklik staat Filter op zijn standaard (900)");

    // klemmen op het bereik
    p.muisNeer(62, 130, 1, false);
    p.muisSleep(62, -170, false);  // 300 px omhoog
    p.muisSleep(62, 830, false);   // ver omlaag
    p.muisOp();
    ev = in.flush();
    w = {{'B', NOOT, 0}, {'V', NOOT, 4}, {'V', NOOT, 0.25}, {'E', NOOT, 0}};
    controle(zoals(ev, w), "Noot voorbij beide uiteinden: " + beschrijf(ev) + "(verwacht " + beschrijf(w) + ")");

    // naast de knoppen gebeurt niets
    flushVragen = 0;
    p.muisNeer(450, 100, 1, false);
    p.muisSleep(450, 0, false);
    p.muisOp();
    controle(in.flush().empty() && flushVragen == 0 && p.gekozen() == 0, "klikken en slepen naast de knoppen: niets");

    // het venster gaat weg midden in een gebaar: het gebaar wordt gesloten
    p.muisNeer(138, 130, 1, false);
    p.muisSleep(138, 110, false);
    p.stop();
    ev = in.flush();
    w = {{'B', POORT, 0}, {'V', POORT, 0.1}, {'E', POORT, 0}};
    controle(zoals(ev, w), "venster dicht tijdens slepen: " + beschrijf(ev) + "(verwacht " + beschrijf(w) + ")");

    // de host verandert een waarde (automatisering): het paneel laat hem zien
    p.tik();
    p.teken();
    in.flush({paramEv(NOOT, 2.0)});
    controle(p.tik(), "na een verandering door de host vraagt de tik om opnieuw tekenen");
    controle(p.getoond(0) == 2.0 && p.waardeTekst(0) == "200 %", "het paneel toont de waarde van de host: " +
                                                                      p.waardeTekst(0));
    p.teken();
    controle(!p.tik(), "zonder verandering geen nieuwe tekening");
    if (!uitmap.empty()) std::ofstream(uitmap + "/paneel-bediend.svg") << alsSvg(p.teken());

    // komt een waarde niet aan (geen flush), dan toont het paneel na ±0,5 s weer wat de host heeft
    p.muisNeer(138, 130, 1, false);
    p.muisSleep(138, 30, false);
    p.muisOp();
    controle(p.getoond(1) == 0.6, "net losgelaten: het paneel toont wat het stuurde (0,1 + 0,5)");
    for (int t = 0; t < 30; ++t) p.tik();
    controle(p.getoond(1) == 0.1, "zonder flush toont het paneel na de wachttijd weer de waarde van de host (" +
                                      g(p.getoond(1)) + ")");
    in.flush();
    controle(p.getoond(1) == 0.6 && in.waarde(POORT) == 0.6, "na de flush staat Poort op 0,6");
  }

  // --- 3. actief: process geeft de events en past ze toe; de DSP hoort hetzelfde als bij een event van de host ---
  {
    Inst a, b;
    Paneel p(a.k, knoppen, pbPaneelNaam());
    p.zetMaat(ONTWERP_B, ONTWERP_H);
    a.activeer();
    b.activeer();
    p.muisNeer(214, 130, 1, false);
    p.muisSleep(214, 30, false);
    p.muisOp();
    std::vector<float> ua, ub;
    auto ev = a.blok({nootEv(true, 57, 3)}, &ua, 0);
    std::vector<Verwacht> w = {{'B', 3, 0}, {'V', 3, 4850}, {'E', 3, 0}};
    controle(zoals(ev, w), "actief: process geeft " + beschrijf(ev) + "(verwacht " + beschrijf(w) + ")");
    controle(a.waarde(3) == 4850, "actief: na process get_value(Filter) = 4850");
    b.blok({paramEv(3, 4850), nootEv(true, 57, 3)}, &ub, 0);
    for (int k = 1; k < 4; ++k) {
      a.blok({}, &ua, 512 * k);
      b.blok({}, &ub, 512 * k);
    }
    controle(ua == ub, "de render na de knop is bit voor bit gelijk aan die na een event van de host");
    bool klinkt = false;
    for (float x : ua) klinkt = klinkt || x != 0;
    controle(klinkt, "de render is niet stil (de noot klinkt)");
    controle(a.blok({}, nullptr, 2048).empty(), "een volgend blok geeft geen events meer");
  }

  // --- 4. een volle rij verliest niets en houdt de volgorde --------------------------------------------------------
  {
    Inst in;
    const int veel = 5000;
    in.k.stuur(in.k.ctx, {Soort::Begin, 2, 0});
    for (int k = 1; k <= veel; ++k) in.k.stuur(in.k.ctx, {Soort::Waarde, 2, 100.0 + k});
    in.k.stuur(in.k.ctx, {Soort::Eind, 2, 0});
    std::vector<Ev> alles;
    for (int ronde = 0; ronde < 20; ++ronde) {
      auto ev = in.flush();
      alles.insert(alles.end(), ev.begin(), ev.end());
      in.k.tik(in.k.ctx);
    }
    bool volgorde = alles.size() == (size_t)veel + 2 && alles.front().h.type == CLAP_EVENT_PARAM_GESTURE_BEGIN &&
                    alles.back().h.type == CLAP_EVENT_PARAM_GESTURE_END;
    for (int k = 1; volgorde && k <= veel; ++k)
      volgorde = alles[(size_t)k].h.type == CLAP_EVENT_PARAM_VALUE && alles[(size_t)k].p.value == 100.0 + k;
    controle(volgorde, std::to_string(veel) + " waarden zonder flush ertussen: alle " + std::to_string(alles.size()) +
                           " events komen aan, in volgorde");
    controle(in.waarde(3) == 100.0 + veel, "en de laatste waarde staat (" + g(in.waarde(3)) + ")");
  }

  // --- 5. labels hooguit 11 tekens, de volle naam blijft --------------------------------------------------------
  {
    Koppeling k;
    Paneel p(k, {{"Terugkoppeling", 0, 1, 0}, {"Galm", 0, 1, 0}}, "x");
    controle(p.label(0) == "Terugkoppel" && p.knop(0).naam == "Terugkoppeling",
             "label van 14 tekens wordt \"" + p.label(0) + "\" (11), de volle naam blijft voor de tooltip");
    controle(p.label(1) == "Galm", "een kort label blijft heel");
  }

  std::printf("pb-paneeltoets: %d controles, %d geslaagd, %d mislukt\n", goed + fouten, goed, fouten);
  return fouten ? 1 : 0;
}
