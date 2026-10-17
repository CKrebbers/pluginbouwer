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
- Parameteroverdracht: alleen via CLAP-events (`process` en `params.flush`). Geen gedeelde variabelen tussen threads.

## State
Binair, klein en vooruit-compatibel: `"PBST"`, versie (u32), aantal (u32), dan per parameter (id u32, waarde f64).
Laden gaat per id: een onbekend id (van een nieuwere versie) wordt overgeslagen, een ontbrekend id houdt zijn
standaard. Parameter-id's veranderen nooit.
- Na `state.load` roept de schil `host_params->rescan(CLAP_PARAM_RESCAN_VALUES)` aan (clap-validator
  state-reproducibility eist het). Kapotte state (leeg, andere kop, te kort) geeft false en verandert niets.
- `state.load` komt op de mainthread. Is de plugin niet actief, dan krijgt het Ontwerp de waarden meteen; is hij actief,
  dan zet hij een atomische vlag en neemt de eerstvolgende plek die het Ontwerp bezit (process, `params.flush`, of
  `activate` na een deactivate) de waarden over.
- Getoetst door `gereedschap/pb-state-toets` met een bouw zonder parameter N (`build/toets/zonder-N.clap`, gebouwd met
  `-DPB_ZONDER_PARAM=N`): oud → nieuw, nieuw → oud, laden terwijl actief, versie 2 met onbekend id, kapotte state.

## Parameters als tekst
`params.value_to_text`/`text_to_value` (plugin/src/param_tekst.h) tonen gewone eenheden: "375 ms", "3.00 kHz",
"265 Hz", "6.0 dB", "0.71 Q", en 0..1-knoppen als procent ("37.5 %"). Alleen de tekst rondt af, de waarde niet.
Lezen kent ook "1.2 s", "3k", een komma en een kaal getal (in de getoonde eenheid; "50" bij Mix = 50 %).
Tekst → waarde → tekst geeft dezelfde tekst; de tekst hangt niet af van de locale van de host. Geen enkele parameter
heeft stappen (`STAPPEN`); zou er een komen, dan toont en leest hij hele getallen.

## Presets (fase 4)
Een preset is een state-blob plus naam, als JSON naast de plugin (`presets/<naam>.json` met de parameterwaarden
per naam). Fabriekspresets komen uit de varianten van de tool.

## Determinisme (waarom browser en plugin gelijk zijn)
- Geen libm in de DSP: `pb_math.h` (alleen + − × ÷ sqrt floor). Vlaggen `-ffp-contract=off`, geen `-ffast-math`.
- Toeval (bandbeweging) uit een geseede xorshift op gehele getallen.
- Denormals: de DSP poetst ze zelf weg; invoer wordt op de bits getest (`pb::invoer`).
- Smoothers springen bij de eerste sample na activate/reset: het maakt niet uit of de host parameters vóór of na
  activate zet.
