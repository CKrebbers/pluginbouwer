# design.md — frontpaneel van Toetsenpad

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

Indeling: één rij van 4 draaiknoppen (270°, −135° tot +135°), label erboven, waarde in gewone eenheden
eronder (tekst zoals SPEC.md "Tekst ↔ waarde"), in de volgorde van SPEC.md. Dubbelklik = standaard. Slepen: 200 px
omhoog = het hele bereik (schermpunten, ook als het paneel geschaald is), lineair in de CLAP-waarde (zoals de host
normaliseert); Shift + slepen = 10× fijner. "De gekozen knop" = de laatst aangeraakte; zijn boog is tape. Beginmaat
560 × 220 (precies `frontpaneel.svg`); kleiner of groter schaalt het paneel gelijkmatig.
Kolom per knop 76 px. Labels (hooguit 11 tekens, de volle naam in de tooltip en bij de host): Noot · Poort · Filter · Loslaten.
Minimaal 560 × 220 px, schaalbaar. Labels in de gewone tekststijl (IBM Plex Mono 11 px, inkt).
Toets het paneel in CI door de view in een eigen sRGB-bitmap te laten tekenen en daar de bytes te lezen (niet
`NSBitmapImageRep colorAtX:` met een kleurconversie: die geeft een gammaverschil), met steekproeven midden op een boog;
reken met een tolerantie, want je eigen toetscode wordt niet met `-ffp-contract=off` gecompileerd (arm64 maakt er FMA van). De mockup is `frontpaneel.svg` in dit pakket (op ware grootte, alle knoppen op
hun standaard); dezelfde staat in de tool (stand Ontwerpen, "Frontpaneel").
