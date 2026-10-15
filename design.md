# design.md — Pluginbouwer

Zelfde gereedschapstaal als de Stackbouwer (varve-site `stackbouwer-bron/design.md`): de tool is een werkbank,
het frontpaneel van de plugin mag zijn eigen kleur hebben.

## Kleuren (tokens)
| token | licht | donker | gebruik |
|---|---|---|---|
| --bg / --panel | #e3e3de / #eeeee9 | #1b1d1f / #232629 | achtergrond, werkbank |
| --ink | #17181a | #e7e5dd | tekst, kabels |
| --muted | #5c5f63 | #9a9c9e | bijschrift |
| --tape | #f2c200 | #f2c200 | labels, grepen, geselecteerd blok |
| --trace | #0a7a87 | #40cbda | audio-kabels, reeks 1 in grafieken |
| --trace2 | #b0450b | #ff9455 | modulatie-kabels, reeks 2 |
| --ok / --bad | #2f7d32 / #b3261e | #6fcf73 / #ff6b5e | toets gehaald / niet gehaald, betere/slechtere richting |

## Type
Silkscreen voor logo, tape-labels en kopjes; IBM Plex Mono voor de rest, `tabular-nums`. (Google Fonts.)

## Vorm
- Rand 2px, hoeken 0. Diepte als harde offset (`3px 3px 0`), geen blur.
- Blokken in de keten: rechthoek met tape-label linksboven; ingangen links, uitgangen rechts als vierkantjes van 10 px.
  Audio-kabel --trace, modulatie-kabel --trace2 gestippeld (6/3).
- Draaiknoppen zoals de werkbank: 270°, van −135° tot +135°, boog in --trace, waarde in gewone eenheden eronder.
- Readout bovenin: vaste cijfers (CPU, latency, staart, aliasing, toetsen gehaald), met ▲▼ bij de laatste wijziging
  (dat zijn data, geen emoji).
- Grafieken: log-frequentie, lijnen 2 px, doel en grenzen als gestippelde --ink-lijn met label. Small multiples in plaats
  van twee assen.
- Toetsen als rij blokjes: gevuld --ok = gehaald, rand --bad = niet gehaald, grijs = niet gedraaid.

## Wat er níet in mag
- Gradiënten, glassmorphism, blur.
- Inter, Space Grotesk of andere "veilige" sans.
- Ronde kaartjes, pillen, zachte schaduwen.
- Emoji als markers.
- Logo's of namen van bestaande producten als ontwerp (archetypes heten naar wat ze doen, niet naar een merk).
