# design.md — frontpaneel van Dub-echo

Zelfde taal als de Pluginbouwer: hoeken 0, rand 2 px, harde schaduw (3 px 3 px 0), geen gradiënten, geen
glassmorphism, geen Inter. Lettertype IBM Plex Mono (cijfers tabular), kopjes Silkscreen.

| token | waarde | gebruik |
|---|---|---|
| paneel | #232629 | achtergrond |
| inkt | #e7e5dd | tekst, knopranden |
| tape | #f2c200 | naamlabel, de boog van de gekozen knop |
| spoor | #40cbda | boog van de knoppen |
| groef | #3a3e42 | de boog onder het spoor (het hele bereik) |
| schaduw | #0d0e0f | harde schaduw, 3 px rechts en onder |

Indeling: één rij van 9 draaiknoppen (270°, −135° tot +135°), label erboven, waarde in gewone eenheden
eronder (tekst zoals SPEC.md "Tekst ↔ waarde"), in de volgorde van SPEC.md. Dubbelklik = standaard. Slepen: 200 px
omhoog = het hele bereik, lineair in de CLAP-waarde (zoals de host normaliseert); Shift + slepen = 10× fijner. "De
gekozen knop" = de laatst aangeraakte; zijn boog is tape. Beginmaat 640 × 250.
Kolom per knop 76 px. Labels (hooguit 11 tekens, de volle naam in de tooltip en bij de host): Tijd · Terug · Toon · Laag weg · Resonantie · Verzadiging · Haperen · Mix · Ruis.
Minimaal 560 × 220 px, schaalbaar. De mockup is `frontpaneel.svg` in dit pakket (op ware grootte, alle knoppen op
hun standaard); dezelfde staat in de tool (stand Ontwerpen, "Frontpaneel").
