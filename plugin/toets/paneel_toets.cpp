// paneel_toets — de eigen toets van fase 3, deel 1 (elk platform): het paneelmodel en de brievenbus, zonder venster.
//
//   paneel_toets
//
// - Maten: met alle knoppen op hun standaard geeft het model dezelfde wijzer- en booguiteinden als frontpaneel.svg
//   (de mockup op ware grootte), op 0,01 px; knopOp vindt elke knop; schalen houdt het paneel heel en in het midden.
// - Muis (design.md): 200 px omhoog = het hele bereik, lineair in de CLAP-waarde; Shift = 10× fijner; dubbelklik =
//   standaard; de gekozen knop = de laatst aangeraakte.
// - Via de host: het model verandert de waarde van de plugin niet zelf. Het zet gebaren in de brievenbus en vraagt een
//   flush; wat de lezer (process/flush) eruit haalt is BEGIN, WAARDE…, EIND, in paren en met de laatste waarde.
// - Twee draden: een schrijver die gebaren maakt en een lezer die tegelijk leegt; elk gebaar blijft heel en de laatste
//   waarde komt aan (draai ook onder -fsanitize=thread).
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "paneel_model.h"

namespace {

using dub::Bus;
using pb::Ontwerp;
constexpr int N = dub::N_KNOPPEN;

int fouten = 0, controles = 0;
void controle(bool goed, const std::string& wat) {
  ++controles;
  if (!goed) ++fouten;
  std::printf("%s  %s\n", goed ? "ok  " : "FOUT", wat.c_str());
}
std::string getal(double v) {
  char b[64];
  std::snprintf(b, sizeof b, "%.6g", v);
  return b;
}

struct Gebeurtenis {
  Bus::Soort soort;
  int i;
  double v;
};
std::vector<Gebeurtenis> leeg(Bus& bus) {
  std::vector<Gebeurtenis> g;
  bus.leeg([&](Bus::Soort s, int i, double v) { g.push_back({s, i, v}); });
  return g;
}
std::string tekst(const std::vector<Gebeurtenis>& g) {
  std::string s;
  for (auto& e : g) {
    s += e.soort == Bus::BEGIN ? "B" : (e.soort == Bus::EIND ? "E" : "W");
    s += std::to_string(e.i);
    if (e.soort == Bus::WAARDE) s += "=" + getal(e.v);
    s += " ";
  }
  return s;
}

// de "plugin": een atomaire kopie die alleen de lezer bijwerkt, zoals in dub_echo.cpp
struct Opstelling {
  std::atomic<double> kopie[N];
  Bus bus;
  dub::PaneelModel m;
  int flushes = 0;
  Opstelling() {
    for (int i = 0; i < N; ++i) kopie[i].store(Ontwerp::PARAMS[i].standaard);
    m.kopie = kopie;
    m.bus = &bus;
    m.ctx = this;
    m.vraagFlush = [](void* c) { ++static_cast<Opstelling*>(c)->flushes; };
    m.begin();
  }
  std::vector<Gebeurtenis> lezer() {  // process/flush: leeg en pas toe
    auto g = leeg(bus);
    for (auto& e : g)
      if (e.soort == Bus::WAARDE) kopie[e.i].store(e.v);
    return g;
  }
};

void maten() {
  // uit frontpaneel.svg: einde van de boog en van de wijzer per knop, alle knoppen op hun standaard
  const double svg[N][4] = {{-17.75, -13.00, -11.29, -8.27}, {-9.99, -19.60, -6.36, -12.47}, {-21.00, -6.55, -13.37, -4.17},
                            {-20.26, 8.58, -12.89, 5.46},    {-19.24, 10.68, -12.24, 6.79},  {-20.33, -8.42, -12.93, -5.36},
                            {-20.33, -8.42, -12.93, -5.36},  {-12.22, -18.29, -7.78, -11.64}, {-15.56, 15.56, -9.90, 9.90}};
  double maxAf = 0;
  for (int i = 0; i < N; ++i) {
    const double a = dub::hoekVoor(dub::genormaliseerd(i, Ontwerp::PARAMS[i].standaard));
    const double p[4] = {dub::BOOG_R * std::sin(a), -dub::BOOG_R * std::cos(a), dub::WIJZER_R * std::sin(a),
                         -dub::WIJZER_R * std::cos(a)};
    for (int k = 0; k < 4; ++k) maxAf = std::fmax(maxAf, std::fabs(p[k] - svg[i][k]));
  }
  controle(maxAf <= 0.0051, "boog en wijzer op de standaard gelijk aan frontpaneel.svg (grootste afwijking " +
                                getal(maxAf) + " px)");
  // het begin van de boog (−135°) is het begin in de mockup
  const double a0 = dub::hoekVoor(0);
  controle(std::fabs(dub::BOOG_R * std::sin(a0) + 15.56) < 0.01 && std::fabs(-dub::BOOG_R * std::cos(a0) - 15.56) < 0.01,
           "boog begint op (−15,56; 15,56) zoals de mockup");
  bool alle = true;
  for (int i = 0; i < N; ++i) {
    const double cx = dub::KNOP_X0 + dub::KNOP_DX * i;
    alle = alle && dub::knopOp(cx, dub::KNOP_Y) == i && dub::knopOp(cx - 37, dub::KNOP_Y + 44) == i &&
           dub::knopOp(cx + 37, dub::KNOP_Y - 34) == i;
  }
  controle(alle, "knopOp vindt elke knop op zijn middelpunt, label en waarde");
  controle(dub::knopOp(60, 30) == -1 && dub::knopOp(10, 130) == -1 && dub::knopOp(725, 130) == -1,
           "naast de knoppen (naamlabel, rand) geen knop");
  bool labels = true;
  for (int i = 0; i < N; ++i) labels = labels && std::strlen(dub::LABELS[i]) <= 11;
  controle(labels && std::string(dub::LABELS[1]) == "Terug" && std::string(dub::LABELS[3]) == "Laag weg",
           "labels hooguit 11 tekens, in de volgorde van SPEC.md");
  const auto s1 = dub::schaalVoor(dub::BEGIN_B, dub::BEGIN_H);
  controle(s1.s == 1 && s1.ox == 0 && s1.oy == 0, "op de beginmaat (732 × 220) is het paneel op ware grootte");
  const auto s2 = dub::schaalVoor(dub::MIN_B, dub::MIN_H);
  controle(std::fabs(s2.s - 560.0 / 732) < 1e-12 && s2.ox == 0 && s2.oy > 0 && dub::ONTWERP_H * s2.s + 2 * s2.oy == 220,
           "op de kleinste maat (560 × 220) past het paneel heel, in het midden");
  const auto s3 = dub::schaalVoor(1464, 600);
  controle(s3.s == 2 && s3.oy == 80, "groter: gelijkmatig geschaald (1464 × 600 → 2×)");
}

void muis() {
  // slepen: 100 px omhoog = half het bereik
  {
    Opstelling o;
    const auto& p = Ontwerp::PARAMS[0];
    const double cx = dub::KNOP_X0;
    o.m.muisNeer(cx, dub::KNOP_Y, 300, 1);
    controle(o.m.gekozen == 0, "aanraken maakt de knop de gekozen knop");
    for (int k = 1; k <= 100; ++k) o.m.muisSleep(300 - k, false);
    const double verwacht = p.standaard + 0.5 * (p.max - p.min);
    controle(std::fabs(o.m.getoond[0] - verwacht) < 1e-9, "Tijd 100 px omhoog = half het bereik: " + getal(o.m.getoond[0]) +
                                                              " (verwacht " + getal(verwacht) + ")");
    controle(o.kopie[0].load() == p.standaard, "het paneel verandert de waarde van de plugin niet zelf (alleen via de lezer)");
    controle(o.flushes == 101, "elke verandering vraagt een flush (" + std::to_string(o.flushes) + ")");
    auto g = o.lezer();
    controle(tekst(g) == "B0 W0=" + getal(verwacht) + " ", "de lezer krijgt BEGIN en de laatste WAARDE: " + tekst(g));
    o.m.muisSleep(100, false);  // nog 100 px: tegen het maximum
    o.m.muisOp();
    g = o.lezer();
    controle(tekst(g) == "W0=1200 E0 ", "verder slepen klemt op het maximum, loslaten geeft EIND: " + tekst(g));
    controle(o.kopie[0].load() == 1200, "na de lezer staat de plugin op 1200 ms");
    controle(leeg(o.bus).empty(), "daarna is de bus leeg");
  }
  // 200 px = het hele bereik, ook omlaag
  {
    Opstelling o;
    o.m.muisNeer(dub::KNOP_X0 + dub::KNOP_DX * 1, dub::KNOP_Y, 0, 1);
    o.m.muisSleep(-1000, false);  // ver omhoog: max
    o.m.muisSleep(200 - 1000, false);  // 200 px omlaag: min
    controle(o.m.getoond[1] == 0, "Terugkoppeling: 200 px omlaag vanaf het maximum = het minimum (" + getal(o.m.getoond[1]) + ")");
    o.m.muisOp();
  }
  // Shift: 10× fijner
  {
    Opstelling o;
    const auto& p = Ontwerp::PARAMS[7];
    o.m.muisNeer(dub::KNOP_X0 + dub::KNOP_DX * 7, dub::KNOP_Y, 500, 1);
    o.m.muisSleep(400, true);
    o.m.muisOp();
    const double verwacht = p.standaard + 0.05 * (p.max - p.min);
    controle(std::fabs(o.m.getoond[7] - verwacht) < 1e-12, "Mix met Shift 100 px omhoog = 5 % van het bereik: " + getal(o.m.getoond[7]));
    controle(tekst(o.lezer()) == "B7 W7=" + getal(verwacht) + " E7 ", "één gebaar met de waarde");
  }
  // dubbelklik = standaard
  {
    Opstelling o;
    o.kopie[2].store(9000);
    o.m.ververs();
    controle(o.m.getoond[2] == 9000, "een waarde van de host komt op het paneel (timer)");
    o.m.muisNeer(dub::KNOP_X0 + dub::KNOP_DX * 2, dub::KNOP_Y, 0, 1);
    o.m.muisOp();
    o.m.muisNeer(dub::KNOP_X0 + dub::KNOP_DX * 2, dub::KNOP_Y, 0, 2);
    o.m.muisOp();
    controle(o.m.getoond[2] == 3000, "dubbelklik zet Toon op zijn standaard (3000)");
    const auto g = o.lezer();
    controle(tekst(g) == "B2 W2=3000 E2 ", "dubbelklik is één gebaar: " + tekst(g));
    o.m.ververs();
    controle(o.m.getoond[2] == 3000 && o.kopie[2].load() == 3000, "en de plugin staat daarna op 3000");
  }
  // wat de host nog niet ophaalde blijft op het paneel staan (een AU die niet rendert)
  {
    Opstelling o;
    o.m.muisNeer(dub::KNOP_X0 + dub::KNOP_DX * 5, dub::KNOP_Y, 100, 1);
    o.m.muisSleep(50, false);
    o.m.muisOp();
    const double v = o.m.getoond[5];
    o.m.ververs();
    controle(o.m.getoond[5] == v && v != 6, "zolang de host niet flusht, toont het paneel wat het stuurde (" + getal(v) + ")");
    o.lezer();
    o.kopie[5].store(1.5);  // daarna automatisering van de host
    o.m.ververs();
    controle(o.m.getoond[5] == 1.5, "na de lezer volgt het paneel de host weer");
    // tijdens het slepen wint de muis
    o.m.muisNeer(dub::KNOP_X0 + dub::KNOP_DX * 5, dub::KNOP_Y, 100, 1);
    o.m.muisSleep(90, false);
    const double w = o.m.getoond[5];
    o.lezer();
    o.kopie[5].store(20);
    o.m.ververs();
    controle(o.m.getoond[5] == w, "tijdens het slepen springt de knop niet naar de host");
    o.m.muisOp();
  }
  // naast een knop: niets
  {
    Opstelling o;
    controle(!o.m.muisNeer(60, 30, 0, 1) && o.m.gekozen == -1 && o.flushes == 0 && leeg(o.bus).empty(),
             "klikken op het naamlabel doet niets");
  }
}

void bus() {
  // samenvoegen zonder lezer: tien gebaren worden één paar met de laatste waarde
  {
    Bus b;
    for (int k = 0; k < 10; ++k) { b.begin(4); b.zet(4, 100 + k); b.eind(4); }
    controle(tekst(leeg(b)) == "B4 W4=109 E4 ", "tien gebaren zonder lezer: één BEGIN, de laatste WAARDE, één EIND");
  }
  // een open gebaar: BEGIN, en EIND pas later
  {
    Bus b;
    b.begin(8);
    controle(tekst(leeg(b)) == "B8 ", "een gebaar zonder waarde: alleen BEGIN");
    controle(leeg(b).empty(), "niets nieuws: niets");
    b.zet(8, 0.5);
    controle(tekst(leeg(b)) == "W8=0.5 ", "binnen het gebaar: alleen WAARDE");
    b.eind(8);
    b.begin(8);
    b.zet(8, 0.75);
    controle(tekst(leeg(b)) == "W8=0.75 E8 B8 ", "eind en nieuw begin tussen twee lezers: WAARDE, EIND, BEGIN");
    b.eind(8);
    controle(tekst(leeg(b)) == "E8 ", "dan EIND");
  }
  // twee draden: elk gebaar heel, de laatste waarde komt aan
  {
    Bus b;
    std::atomic<bool> klaar{false};
    double laatste[N];
    for (int i = 0; i < N; ++i) laatste[i] = -1;
    std::thread schrijver([&] {
      unsigned x = 12345;
      for (int k = 0; k < 200000; ++k) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        const int i = (int)(x % N);
        b.begin(i);
        const int n = 1 + (int)((x >> 8) % 4);
        for (int j = 0; j < n; ++j) { laatste[i] = k * 4 + j; b.zet(i, laatste[i]); }
        b.eind(i);
      }
      klaar.store(true);
    });
    bool open[N] = {};
    double gezien[N];
    for (int i = 0; i < N; ++i) gezien[i] = -1;
    int fout = 0, rondes = 0;
    long gebaren = 0;
    auto lees = [&] {
      b.leeg([&](Bus::Soort s, int i, double v) {
        if (s == Bus::BEGIN) { if (open[i]) ++fout; open[i] = true; ++gebaren; }
        if (s == Bus::WAARDE) { if (!open[i] || v < gezien[i]) ++fout; gezien[i] = v; }
        if (s == Bus::EIND) { if (!open[i]) ++fout; open[i] = false; }
      });
      ++rondes;
    };
    while (!klaar.load()) lees();
    schrijver.join();
    lees();
    bool dicht = true, gelijk = true;
    for (int i = 0; i < N; ++i) { dicht = dicht && !open[i]; gelijk = gelijk && gezien[i] == laatste[i] && !b.onderweg(i); }
    controle(fout == 0 && dicht, "twee draden: " + std::to_string(rondes) + " keer geleegd, " + std::to_string(gebaren) +
                                     " gebaren, elk BEGIN…EIND in volgorde en de waarden oplopend (" + std::to_string(fout) + " fout)");
    controle(gelijk, "twee draden: de laatste waarde van elke knop is aangekomen en niets is meer onderweg");
  }
}

}  // namespace

int main() {
  maten();
  muis();
  bus();
  std::printf("paneel-toets: %d/%d controles ok, %d fout\n", controles - fouten, controles, fouten);
  return fouten == 0 ? 0 : 1;
}
