# Pluginbouwer

Een ontwerptool voor audioplugins. Je bouwt een ontwerp uit blokken in de browser (WASM), hoort en meet het. Daarna
exporteer je een **bouwpakket**: daarmee bouwt een vers Claude Code-gesprek de plugin als AU, VST3 en CLAP, met het
bewijs uit CI. Browser en plugin rekenen met dezelfde C++ (`dsp/`), en de null-test eist dat ze bit voor bit gelijk zijn.

- De tool: `tool/index.html` (losse pagina) of `tool/artifact.html` (claude.ai).
- De besluiten en valkuilen staan in `CLAUDE.md`, de volgorde in `TASKS.md` en de stackkeuze met metingen in `KEUZE-STACK.md`.
- Het plan om dieper te experimenteren (experimenten als bestanden, sondes, codeblok, werkbank-plugin, bruikbare open
  source met licentie): `PLAN-WERKBANK.md`.
- Wat Clay zelf doet staat in `LEERPAD.md` van elk bouwpakket.
- Voorbeeldpakketten: `bouwpakket/dub_echo/` (effect) en `bouwpakket/toetsenpad/` (instrument).

## Takken: wat elke tak bewijst
Er wordt niets weggegooid: de proeftakken zijn het bewijs. Deze tabel zegt wat er op staat, zodat niemand ze hoeft te
openen. Een proeftak begint met één commit met alleen het bouwpakket ("onaangeroerd"). Daarna bouwt per fase een vers
gesprek, dat alleen die checkout ziet. Elke fase eindigt met een `UITSLAG-faseN.md`.

| tak | wat | bewijst |
|---|---|---|
| `main` | de Pluginbouwer: tool, DSP, schil, CI | alles wat CI hier toetst (null-test, validators, RTSan, au-proef), voor effect en instrument |
| `geluid-maken` | fase F: de motor draait in de browser, 15 blokken, 17 voorbeelden | tussenstand; via `integratie/geluid-maken` in main (PR 1, 18 okt) |
| `integratie/geluid-maken` | `geluid-maken` met main samengevoegd | dat PR 1 groen was vóór main |
| `proef/e1`, `e2`, `e3` | pakket v1 (dub-echo), fase 0 en 1 door drie verse gesprekken | pass@3 voor fase 0–1; de thread_local-patch (valkuil 7) |
| `proef/f1`, `f2`, `f3` | pakket v2 (dub-echo), fase 0–6 door drie verse gesprekken | pass@3 voor fase 0–6; valkuilen 13–18, de patches voor AU-presets en mono/stereo |
| `onderzoek/tls-zonder-patch` | GUI-toetsen aan zonder de tweede clap-wrapper-patch | bewust rood: zonder de patch vangt pluginval `--rtcheck` de calloc; niet samenvoegen |
| `referentie/steekproef` | steekproef uit het Akai-archief (Aalto, CC-BY-4.0) | de meting waarop de bandklank is geijkt (`ref/referentie/RAPPORT.md`) |
| `proef/f4` | pakket v3 (dub-echo), fase 0–6 door één ketting verse gesprekken | 6 van 7 fasen de eerste push groen; fase 3 (paneel) in vier, waarvan twee door het pakket (nu gedicht) |
| `proef/g1`, `g2`, `g3` | pakket v3 voor een instrument (toetsenpad), fase 0–6 | elke fase 3/3 gehaald: note port, aumu, noten als CLAP en MIDI bit voor bit; wat het pakket miste (presets, paneel, tekst, relocatie) staat nu in v3.1 |
