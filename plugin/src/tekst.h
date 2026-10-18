// tekst.h — parameterwaarde ↔ tekst volgens SPEC.md "Tekst ↔ waarde" (komma als decimaalteken, spatie voor de eenheid).
// De regel volgt uit de eenheid in PARAMS: "ms", "Hz", "Q", "dB" of "" (eenheidloos: procenten).
// Zonder printf/strtod: die volgen de locale van de host, en dan wordt de komma soms een punt of andersom.
#pragma once
#include <string>

namespace dub {

std::string waardeNaarTekst(const char* eenheid, double waarde);
// geklemd op [min, max]; false als de tekst onleesbaar is
bool tekstNaarWaarde(const char* eenheid, const char* tekst, double min, double max, double& waarde);

}  // namespace dub
