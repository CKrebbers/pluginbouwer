// arch.h — minimale Faust-architectuur (Meta, UI, dsp) voor de gegenereerde C++. Gedeeld door rend.cpp en het
// CLAP-ontwerp dsp/ontwerpen/faust_proef_delay.h. Het UI verzamelt alleen de zones per label.
#pragma once
#include <map>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstring>
#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif
struct Meta { virtual ~Meta() {} virtual void declare(const char*, const char*) {} };
struct Soundfile;
struct UI {
  std::map<std::string, FAUSTFLOAT*> zones;
  virtual ~UI() {}
  void openTabBox(const char*) {} void openHorizontalBox(const char*) {} void openVerticalBox(const char*) {} void closeBox() {}
  void addButton(const char* l, FAUSTFLOAT* z) { zones[l] = z; }
  void addCheckButton(const char* l, FAUSTFLOAT* z) { zones[l] = z; }
  void addVerticalSlider(const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) { zones[l] = z; }
  void addHorizontalSlider(const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) { zones[l] = z; }
  void addNumEntry(const char* l, FAUSTFLOAT* z, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT, FAUSTFLOAT) { zones[l] = z; }
  void addHorizontalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) {}
  void addVerticalBargraph(const char*, FAUSTFLOAT*, FAUSTFLOAT, FAUSTFLOAT) {}
  void addSoundfile(const char*, const char*, Soundfile**) {}
  void declare(FAUSTFLOAT*, const char*, const char*) {}
};
struct dsp { virtual ~dsp() {} };
