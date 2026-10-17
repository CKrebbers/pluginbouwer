# design.md — frontpaneel van Dub-echo

Zelfde taal als de Pluginbouwer: hoeken 0, rand 2 px, harde schaduw (3 px 3 px 0), geen gradiënten, geen
glassmorphism, geen Inter. Lettertype IBM Plex Mono (cijfers tabular), kopjes Silkscreen.

| token | waarde | gebruik |
|---|---|---|
| paneel | #232629 | achtergrond |
| inkt | #e7e5dd | tekst, knopranden |
| tape | #f2c200 | naamlabel, de boog van de gekozen knop |
| spoor | #40cbda | boog van de knoppen |

Indeling: één rij van 8 draaiknoppen (270°, −135° tot +135°), naam erboven, waarde in gewone eenheden
eronder, in de volgorde van SPEC.md. Dubbelklik = standaard. Shift + slepen = fijn.
Minimaal 560 × 220 px, schaalbaar. De mockup staat in de tool (stand Ontwerpen, "Frontpaneel").
