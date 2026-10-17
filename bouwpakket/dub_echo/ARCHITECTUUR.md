# ARCHITECTUUR — Dub-echo

## Lagen
```
dsp/                 DE DSP (gegeven, niet herschrijven): pb_*.h + de keten als constante (ontwerpen/keten_dub_echo.h)
plugin/              de CLAP-schil (fase 0–2), clap-wrapper maakt VST3 en AUv2
gereedschap/         pb-render (CLAP-host), vergelijk.mjs (null-test + statistisch), ci-null.sh
testsignalen/        invoer + verwachte uitvoer (uit de WASM van de tool) + pluginval-configs
```

## De keten
10 knopen, uitgevoerd in een vaste volgorde per sample (SPEC.md "Keten"). De ketenmotor
(`dsp/pb_graaf.h`) leest de beschrijving `PB_KETEN`; `dsp/graaf_ontwerp.h` zet die achter het Ontwerp-contract:
`geheugenNodig`, `activeer`, `reset`, `zetParam`, `leesParam`, `verwerk`, `latentie`, `staartSec`, `PARAMS`, `META`.
- `zetParam(i, v)` en `leesParam(i)` nemen de INDEX in `PARAMS` (0..n−1), niet het parameter-id; de schil zoekt de
  index bij het id.
- `staartSec()` leest de toestand van de smoothers en hoort dus bij de audiothread: reken hem aan het eind van process
  (en in activate) uit en bewaar hem atomisch; `tail.get` leest die waarde.

## Threads
- **Audiothread**: alleen `process`. Events (parameterwaarden) worden op hun sample toegepast: het blok wordt bij elk
  event gesplitst; de DSP rekent per sample, dus de uitvoer is gelijk aan die zonder splitsen.
- **Mainthread**: `init`, `activate` (hier wordt geheugen gereserveerd), state, parameters lezen en tekst.
- Geen locks, geen allocatie, geen syscalls in `process` (RTSan bewaakt het in CI).
- Parameteroverdracht naar het Ontwerp: alleen via CLAP-events (`process` en `params.flush`). Wat de mainthread
  moet lezen (`params.get_value`, `state.save`) komt uit een atomaire kopie per parameter die de audiothread
  bijwerkt; `state.load` terwijl de plugin actief is zet de waarden in die kopie plus een atomaire vlag, en
  `process`/`flush` neemt ze over vóór de events van dat blok. Verder geen gedeelde variabelen.
- `staartSec()` geeft −1 voor oneindig (lusversterking ≥ 1): meld dan `INT32_MAX` of meer (CLAP: oneindig).
- De null-test `ci-null.sh clap` heeft een geval `testsignalen/automatie/`: 720 parameter-events, ook midden in een
  blok en twee op dezelfde sample. Bit voor bit gelijk = het splitsen bij events klopt.

## State
Binair, klein en vooruit-compatibel: `"PBST"`, versie (u32), aantal (u32), dan per parameter (id u32, waarde f64).
Laden gaat per id: een onbekend id (van een nieuwere versie) wordt overgeslagen, een ontbrekend id houdt zijn
standaard. Parameter-id's veranderen nooit. Versie is nu 1. Laden leest de versie: andere magic, versie 0 of een
blob korter dan zijn `aantal` belooft = false (niets veranderd); een hogere versie laadt gewoon per id (een nieuwere
plugin mag er velden bij hebben, nooit een id een andere betekenis geven). Na een geslaagde load: `host_params->rescan(CLAP_PARAM_RESCAN_VALUES)`
(clap-validator eist het in state-reproducibility), en als de plugin actief is `request_flush`.

## Presets (fase 4)
Een fabriekspreset is `presets/<sleutel>.json`: `{ naam, uitleg, waarden: { <parameternaam>: waarde } }` (geen
state-blob). Bak ze bij het bouwen in het binary (CMake maakt er een tabel van; een onbekende naam of een waarde buiten
het bereik is een bouwfout), dan hoeft er niets naast de plugin te staan en geen JSON-lezer in de plugin.
- CLAP: `preset-discovery` met locaties van soort `CLAP_PRESET_DISCOVERY_LOCATION_PLUGIN` en `preset-load`. Laden =
  hetzelfde pad als state.load (ontbrekende namen naar hun standaard), daarna rescan en `loaded`.
- Per preset een eigen provider met een container van één (zie valkuil 15: clap-validator loopt vast op een container
  met meer presets).
- AU: `patches/clap-wrapper-auv2-presets.patch` leest dezelfde factory en geeft ze als factory presets
  (`GetPresets`, `NewFactoryPresetSet`). VST3 krijgt van clap-wrapper geen presetlijst.
- Een verse plugin staat op de standaarden; laat de AU daarom op "Untitled" (−1) beginnen, niet op preset 0.

## Determinisme (waarom browser en plugin gelijk zijn)
- Geen libm in de DSP: `pb_math.h` (alleen + − × ÷ sqrt floor). Vlaggen `-ffp-contract=off`, geen `-ffast-math`.
- Toeval (bandbeweging) uit een geseede xorshift op gehele getallen.
- Denormals: de DSP poetst ze zelf weg; invoer wordt op de bits getest (`pb::invoer`).
- Smoothers springen bij de eerste sample na activate/reset: het maakt niet uit of de host parameters vóór of na
  activate zet.
