// preset_toets — de eigen toets van fase 4, door het gebouwde .clap (dlopen, zoals een host).
//
//   preset_toets <plugin.clap>
//
// clap-validator 0.4.1 laadt in preset-discovery-load maar één preset: hij bewaart de gevonden presets per locatie,
// en alle providers hebben dezelfde locatie (soort PLUGIN, location = nullptr). Deze toets doet het voor allemaal:
//   - de preset-discovery-factory heeft een provider per preset (presets/*.json), elk met één locatie van soort
//     PLUGIN en een container van precies één preset (valkuil 15), voor deze plugin, met de vlag factory content;
//   - elke preset laadt via preset-load (inactief en actief), daarna staat elke parameter op de waarde uit de JSON
//     (gezocht op de naam die params.get_info geeft) en elke niet genoemde op zijn standaard; rescan en
//     host_preset_load->loaded zijn geroepen;
//   - een onbekende load key of een andere locatie geeft false en on_error, en verandert niets.
// De verwachte waarden komen uit presets_tabel.h (door CMake gemaakt uit dezelfde JSON-bestanden).
// Uitvoer: per controle "ok" of "FOUT", en de presetnamen; exitcode 0 alleen als alles ok is.
#include <clap/clap.h>
#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "presets_tabel.h"

namespace {

int fouten = 0, controles = 0;
void controle(bool goed, const std::string& wat) {
  ++controles;
  if (!goed) ++fouten;
  std::printf("%s  %s\n", goed ? "ok  " : "FOUT", wat.c_str());
}

// --- host: telt rescan, request_flush, loaded en on_error ---------------------------------------------------------
int rescans = 0, geladen = 0, laadfouten = 0;
std::string laatsteSleutel;
void hRescan(const clap_host_t*, clap_param_rescan_flags) { ++rescans; }
void hClear(const clap_host_t*, clap_id, clap_param_clear_flags) {}
void hRequestFlush(const clap_host_t*) {}
const clap_host_params_t HOST_PARAMS = {hRescan, hClear, hRequestFlush};
void hFout(const clap_host_t*, uint32_t, const char*, const char*, int32_t, const char*) { ++laadfouten; }
void hGeladen(const clap_host_t*, uint32_t, const char*, const char* sleutel) {
  ++geladen;
  laatsteSleutel = sleutel ? sleutel : "";
}
const clap_host_preset_load_t HOST_PRESET_LOAD = {hFout, hGeladen};
const void* hExt(const clap_host_t*, const char* id) {
  if (!std::strcmp(id, CLAP_EXT_PARAMS)) return &HOST_PARAMS;
  if (!std::strcmp(id, CLAP_EXT_PRESET_LOAD)) return &HOST_PRESET_LOAD;
  return nullptr;
}
void hNiets(const clap_host_t*) {}
const clap_host_t HOST = {CLAP_VERSION_INIT, nullptr, "pb-presettoets", "Pluginbouwer", "", "1", hExt, hNiets, hNiets, hNiets};

// --- indexer en metadata-ontvanger ----------------------------------------------------------------------------
struct Gevonden {
  std::vector<clap_preset_discovery_location_t> locaties;
  std::vector<std::string> locatieNamen;
  struct P { std::string naam, sleutel, uitleg; std::vector<std::string> plugins; uint32_t vlaggen = 0; };
  std::vector<P> presets;
};
Gevonden* van(const clap_preset_discovery_indexer_t* i) { return static_cast<Gevonden*>(i->indexer_data); }
Gevonden* van(const clap_preset_discovery_metadata_receiver_t* r) { return static_cast<Gevonden*>(r->receiver_data); }
std::string tekst(const char* s) { return s ? s : "(null)"; }

const clap_preset_discovery_metadata_receiver_t* maakOntvanger(Gevonden* g) {
  static clap_preset_discovery_metadata_receiver_t r;
  r = {g,
       [](const clap_preset_discovery_metadata_receiver_t*, int32_t, const char* m) {
         controle(false, "metadata on_error: " + tekst(m));
       },
       [](const clap_preset_discovery_metadata_receiver_t* r, const char* naam, const char* sleutel) {
         van(r)->presets.push_back({tekst(naam), tekst(sleutel), "", {}, 0});
         return true;
       },
       [](const clap_preset_discovery_metadata_receiver_t* r, const clap_universal_plugin_id_t* id) {
         if (!van(r)->presets.empty() && id) van(r)->presets.back().plugins.push_back(tekst(id->abi) + ":" + tekst(id->id));
       },
       [](const clap_preset_discovery_metadata_receiver_t*, const char*) {},
       [](const clap_preset_discovery_metadata_receiver_t* r, uint32_t v) {
         if (!van(r)->presets.empty()) van(r)->presets.back().vlaggen = v;
       },
       [](const clap_preset_discovery_metadata_receiver_t*, const char*) {},
       [](const clap_preset_discovery_metadata_receiver_t* r, const char* u) {
         if (!van(r)->presets.empty()) van(r)->presets.back().uitleg = tekst(u);
       },
       [](const clap_preset_discovery_metadata_receiver_t*, clap_timestamp, clap_timestamp) {},
       [](const clap_preset_discovery_metadata_receiver_t*, const char*) {},
       [](const clap_preset_discovery_metadata_receiver_t*, const char*, const char*) {}};
  return &r;
}

struct Info { clap_id id; std::string naam; double standaard; };

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { std::fprintf(stderr, "gebruik: preset_toets <plugin.clap>\n"); return 2; }
  std::string pad = argv[1];
#ifdef __APPLE__
  if (pad.size() > 5 && pad.compare(pad.size() - 5, 5, ".clap") == 0) {
    std::string naam = pad.substr(pad.find_last_of('/') + 1);
    naam = naam.substr(0, naam.size() - 5);
    pad += "/Contents/MacOS/" + naam;
  }
#endif
  void* lib = dlopen(pad.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!lib) { std::fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
  auto* entry = static_cast<const clap_plugin_entry_t*>(dlsym(lib, "clap_entry"));
  if (!entry || !entry->init(argv[1])) { std::fprintf(stderr, "geen clap_entry\n"); return 2; }
  auto* fab = static_cast<const clap_plugin_factory_t*>(entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
  const clap_plugin_descriptor_t* d = fab ? fab->get_plugin_descriptor(fab, 0) : nullptr;
  if (!d) { std::fprintf(stderr, "geen plugin\n"); return 2; }

  // --- discovery ---------------------------------------------------------------------------------------------
  auto* pf = static_cast<const clap_preset_discovery_factory_t*>(entry->get_factory(CLAP_PRESET_DISCOVERY_FACTORY_ID));
  controle(pf != nullptr, "de preset-discovery-factory bestaat");
  if (!pf) return 1;
  const uint32_t nProv = pf->count(pf);
  controle(nProv == (uint32_t)dub::N_PRESETS,
           "één provider per preset (" + std::to_string(nProv) + " voor " + std::to_string(dub::N_PRESETS) + ")");
  std::vector<Gevonden::P> alle;
  for (uint32_t i = 0; i < nProv; ++i) {
    const auto* pd = pf->get_descriptor(pf, i);
    if (!pd || !pd->id) { controle(false, "provider " + std::to_string(i) + " heeft een descriptor"); continue; }
    Gevonden g;
    clap_preset_discovery_indexer_t ix = {
        CLAP_VERSION_INIT, "pb-presettoets", "Pluginbouwer", "", "1", &g,
        [](const clap_preset_discovery_indexer_t*, const clap_preset_discovery_filetype_t*) { return true; },
        [](const clap_preset_discovery_indexer_t* i, const clap_preset_discovery_location_t* l) {
          van(i)->locaties.push_back(*l);
          van(i)->locatieNamen.push_back(tekst(l->name));
          return true;
        },
        [](const clap_preset_discovery_indexer_t*, const clap_preset_discovery_soundpack_t*) { return true; },
        [](const clap_preset_discovery_indexer_t*, const char*) -> const void* { return nullptr; }};
    const auto* prov = pf->create(pf, &ix, pd->id);
    if (!prov) { controle(false, "provider " + tekst(pd->id) + " laat zich maken"); continue; }
    controle(prov->desc == pd || (prov->desc && !std::strcmp(prov->desc->id, pd->id)), "provider " + tekst(pd->id) + ": desc gelijk aan de factory");
    const bool init = prov->init(prov);
    controle(init && g.locaties.size() == 1 && g.locaties[0].kind == CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN &&
                 g.locaties[0].location == nullptr && (g.locaties[0].flags & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT),
             "provider " + tekst(pd->id) + ": één locatie van soort PLUGIN, location = nullptr, factory content");
    if (init && !g.locaties.empty()) {
      const bool ok = prov->get_metadata(prov, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, maakOntvanger(&g));
      controle(ok && g.presets.size() == 1, "provider " + tekst(pd->id) + ": een container van precies één preset");
      controle(!prov->get_metadata(prov, CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/tmp/x.json", maakOntvanger(&g)),
               "provider " + tekst(pd->id) + ": een andere locatie geeft false");
      for (auto& p : g.presets) {
        controle(p.plugins.size() == 1 && p.plugins[0] == std::string("clap:") + d->id &&
                     (p.vlaggen & CLAP_PRESET_DISCOVERY_IS_FACTORY_CONTENT) && !p.sleutel.empty(),
                 "preset \"" + p.naam + "\" (load key " + p.sleutel + "): voor " + d->id + ", factory content");
        alle.push_back(p);
      }
    }
    prov->destroy(prov);
  }
  for (int k = 0; k < dub::N_PRESETS; ++k) {
    bool er = false;
    for (auto& p : alle) er = er || (p.naam == dub::PRESETS[k].naam && p.sleutel == dub::PRESETS[k].sleutel &&
                                     p.uitleg == dub::PRESETS[k].uitleg);
    controle(er, std::string("presets/") + dub::PRESETS[k].sleutel + ".json gevonden als \"" + dub::PRESETS[k].naam + "\" met zijn uitleg");
  }

  // --- laden, inactief en actief -----------------------------------------------------------------------------
  for (int ronde = 0; ronde < 2; ++ronde) {
    const char* hoe = ronde == 0 ? "inactief" : "actief";
    const clap_plugin_t* pl = fab->create_plugin(fab, &HOST, d->id);
    if (!pl || !pl->init(pl)) { std::fprintf(stderr, "init mislukt\n"); return 2; }
    auto* pp = static_cast<const clap_plugin_params_t*>(pl->get_extension(pl, CLAP_EXT_PARAMS));
    auto* plx = static_cast<const clap_plugin_preset_load_t*>(pl->get_extension(pl, CLAP_EXT_PRESET_LOAD));
    controle(plx && plx->from_location, std::string("preset-load-extensie (") + hoe + ")");
    if (!pp || !plx) return 1;
    std::vector<Info> par;
    for (uint32_t i = 0; i < pp->count(pl); ++i) {
      clap_param_info_t info{};
      pp->get_info(pl, i, &info);
      par.push_back({info.id, info.name, info.default_value});
    }
    auto waarde = [&](size_t i) { double v = NAN; pp->get_value(pl, par[i].id, &v); return v; };
    bool actief = false;
    if (ronde == 1) actief = pl->activate(pl, 48000, 1, 64);
    // één process-blok stilte: process neemt klaarstaande waarden over
    auto blok = [&]() {
      if (!actief) return;
      float l[64] = {}, r[64] = {}, ul[64], ur[64];
      float* in[2] = {l, r};
      float* uit[2] = {ul, ur};
      clap_audio_buffer_t bi{in, nullptr, 2, 0, 0}, bu{uit, nullptr, 2, 0, 0};
      clap_input_events_t ie{nullptr, [](const clap_input_events_t*) -> uint32_t { return 0; },
                             [](const clap_input_events_t*, uint32_t) -> const clap_event_header_t* { return nullptr; }};
      clap_output_events_t oe{nullptr, [](const clap_output_events_t*, const clap_event_header_t*) { return true; }};
      clap_process_t pr{};
      pr.steady_time = -1;
      pr.frames_count = 64;
      pr.audio_inputs = &bi; pr.audio_inputs_count = 1;
      pr.audio_outputs = &bu; pr.audio_outputs_count = 1;
      pr.in_events = &ie; pr.out_events = &oe;
      pl->process(pl, &pr);
    };
    if (actief) pl->start_processing(pl);

    // in een andere volgorde dan de factory, en elke preset twee keer: wat de vorige zette mag niet blijven hangen
    for (int herhaal = 0; herhaal < 2; ++herhaal) {
      for (int kk = dub::N_PRESETS - 1; kk >= 0; --kk) {
        const dub::Preset& p = dub::PRESETS[kk];
        const int r0 = rescans, g0 = geladen;
        const bool ok = plx->from_location(pl, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, p.sleutel);
        blok();
        bool waarden = true;
        std::string mis;
        for (size_t i = 0; i < par.size(); ++i) {
          double verwacht = par[i].standaard;
          for (int j = 0; j < p.n; ++j)
            if (par[i].naam == p.waarden[j].naam) verwacht = p.waarden[j].waarde;
          if (waarde(i) != verwacht) { waarden = false; mis += " " + par[i].naam; }
        }
        if (herhaal == 0 || !waarden || !ok)
          controle(ok && waarden && rescans > r0 && geladen == g0 + 1 && laatsteSleutel == p.sleutel,
                   std::string(hoe) + ": \"" + p.naam + "\" laadt, alle " + std::to_string(par.size()) +
                       " parameters zoals de JSON (of hun standaard), rescan en loaded" + (mis.empty() ? "" : "; mis:" + mis));
      }
    }
    // een onbekende load key, of een andere locatie: false, on_error, niets veranderd
    std::vector<double> voor;
    for (size_t i = 0; i < par.size(); ++i) voor.push_back(waarde(i));
    const int f0 = laadfouten, g0 = geladen;
    const bool a = plx->from_location(pl, CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN, nullptr, "bestaat-niet");
    const bool b = plx->from_location(pl, CLAP_PRESET_DISCOVERY_LOCATION_FILE, "/tmp/slapback.json", dub::PRESETS[0].sleutel);
    blok();
    bool zelfde = true;
    for (size_t i = 0; i < par.size(); ++i) zelfde = zelfde && waarde(i) == voor[i];
    controle(!a && !b && laadfouten == f0 + 2 && geladen == g0 && zelfde,
             std::string(hoe) + ": onbekende load key en een FILE-locatie geven false en on_error, niets veranderd");

    if (actief) { pl->stop_processing(pl); pl->deactivate(pl); }
    pl->destroy(pl);
  }

  entry->deinit();
  std::printf("presets:");
  for (int k = 0; k < dub::N_PRESETS; ++k) std::printf(" \"%s\"", dub::PRESETS[k].naam);
  std::printf("\npreset-toets: %d/%d controles ok, %d fout\n", controles - fouten, controles, fouten);
  return fouten == 0 ? 0 : 1;
}
