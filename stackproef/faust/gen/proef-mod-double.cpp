/* ------------------------------------------------------------
name: "proef_mod"
Code generated with Faust 2.90.8 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn FaustProef -es 1 -mcd 16 -mdd 1024 -mdy 33 -double -ftz 0
------------------------------------------------------------ */

#ifndef  __FaustProef_H__
#define  __FaustProef_H__

#ifndef FAUSTFLOAT
#define FAUSTFLOAT float
#endif 

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <math.h>
#ifndef FAUST_INT_WRAP
#define FAUST_INT_WRAP
inline int faust_wrap_add(int a, int b) { return int((unsigned int)a + (unsigned int)b); }
inline int faust_wrap_sub(int a, int b) { return int((unsigned int)a - (unsigned int)b); }
inline int faust_wrap_mul(int a, int b) { return int((unsigned int)a * (unsigned int)b); }
#endif


#ifndef FAUSTCLASS 
#define FAUSTCLASS FaustProef
#endif

#ifdef __APPLE__ 
#define exp10f __exp10f
#define exp10 __exp10
#endif

#if defined(_WIN32)
#define RESTRICT __restrict
#else
#define RESTRICT __restrict__
#endif

class FaustProefSIG0 {
	
  private:
	
	int iVec0[2];
	int iRec0[2];
	int fSampleRate;
	
  public:
	
	int getNumInputsFaustProefSIG0() {
		return 0;
	}
	int getNumOutputsFaustProefSIG0() {
		return 1;
	}
	
	void instanceInitFaustProefSIG0(int sample_rate) {
		fSampleRate = sample_rate;
		for (int l0 = 0; l0 < 2; l0 = faust_wrap_add(l0, 1)) {
			iVec0[l0] = 0;
		}
		for (int l1 = 0; l1 < 2; l1 = faust_wrap_add(l1, 1)) {
			iRec0[l1] = 0;
		}
	}
	
	void fillFaustProefSIG0(int count, double* table) {
		for (int i1 = 0; i1 < count; i1 = faust_wrap_add(i1, 1)) {
			iVec0[0] = 1;
			iRec0[0] = (faust_wrap_add(iVec0[1], iRec0[1])) % 65536;
			table[i1] = std::sin(9.587379924285257e-05 * static_cast<double>(iRec0[0]));
			iVec0[1] = iVec0[0];
			iRec0[1] = iRec0[0];
		}
	}

};

static FaustProefSIG0* newFaustProefSIG0() { return (FaustProefSIG0*)new FaustProefSIG0(); }
static void deleteFaustProefSIG0(FaustProefSIG0* dsp) { delete dsp; }

static double ftbl0FaustProefSIG0[65536];

class FaustProef : public dsp {
	
 private:
	
	int fSampleRate;
	double fConst0;
	double fConst1;
	FAUSTFLOAT fHslider0;
	int iVec1[2];
	double fConst2;
	double fRec1[2];
	double fRec2[3];
	double fRec3[3];
	
 public:
	FaustProef() {
	}
	
	FaustProef(const FaustProef&) = default;
	
	virtual ~FaustProef() = default;
	
	FaustProef& operator=(const FaustProef&) = default;
	
	void metadata(Meta* m) { 
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.23.6");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn FaustProef -es 1 -mcd 16 -mdd 1024 -mdy 33 -double -ftz 0");
		m->declare("filename", "proef_mod.dsp");
		m->declare("filters.lib/fir:author", "Julius O. Smith III");
		m->declare("filters.lib/fir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/fir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/iir:author", "Julius O. Smith III");
		m->declare("filters.lib/iir:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/iir:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/name", "Faust Filters Library");
		m->declare("filters.lib/tf2:author", "Julius O. Smith III");
		m->declare("filters.lib/tf2:copyright", "Copyright (C) 2003-2019 by Julius O. Smith III <jos@ccrma.stanford.edu>");
		m->declare("filters.lib/tf2:license", "LicenseRef-STK-4.3");
		m->declare("filters.lib/version", "1.13.0");
		m->declare("maths.lib/author", "GRAME");
		m->declare("maths.lib/copyright", "GRAME");
		m->declare("maths.lib/license", "LicenseRef-LGPL-2.1-or-later-with-Faust-exception");
		m->declare("maths.lib/name", "Faust Math Library");
		m->declare("maths.lib/version", "2.9.3");
		m->declare("name", "proef_mod");
		m->declare("oscillators.lib/name", "Faust Oscillator Library");
		m->declare("oscillators.lib/version", "1.9.4");
		m->declare("platform.lib/name", "Generic Platform Library");
		m->declare("platform.lib/version", "1.3.1");
	}

	virtual int getNumInputs() {
		return 2;
	}
	virtual int getNumOutputs() {
		return 2;
	}
	
	static void classInit(int sample_rate) {
		FaustProefSIG0* sig0 = newFaustProefSIG0();
		sig0->instanceInitFaustProefSIG0(sample_rate);
		sig0->fillFaustProefSIG0(65536, ftbl0FaustProefSIG0);
		deleteFaustProefSIG0(sig0);
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<double>(1.92e+05, std::max<double>(5e+01, static_cast<double>(fSampleRate)));
		fConst1 = 6283.185307179586 / fConst0;
		fConst2 = 0.7 / fConst0;
	}
	
	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.5);
	}
	
	virtual void instanceClear() {
		for (int l2 = 0; l2 < 2; l2 = faust_wrap_add(l2, 1)) {
			iVec1[l2] = 0;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			fRec1[l3] = 0.0;
		}
		for (int l4 = 0; l4 < 3; l4 = faust_wrap_add(l4, 1)) {
			fRec2[l4] = 0.0;
		}
		for (int l5 = 0; l5 < 3; l5 = faust_wrap_add(l5, 1)) {
			fRec3[l5] = 0.0;
		}
	}
	
	virtual void init(int sample_rate) {
		classInit(sample_rate);
		instanceInit(sample_rate);
	}
	
	virtual void instanceInit(int sample_rate) {
		instanceConstants(sample_rate);
		instanceResetUserInterface();
		instanceClear();
	}
	
	virtual FaustProef* clone() {
		return new FaustProef(*this);
	}
	
	virtual int getSampleRate() {
		return fSampleRate;
	}
	
	virtual void buildUserInterface(UI* ui_interface) {
		ui_interface->openVerticalBox("proef_mod");
		ui_interface->addHorizontalSlider("Mod", &fHslider0, FAUSTFLOAT(0.5), FAUSTFLOAT(0.0), FAUSTFLOAT(1.0), FAUSTFLOAT(0.0001));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		double fSlow0 = static_cast<double>(fHslider0);
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			iVec1[0] = 1;
			double fTemp0 = ((faust_wrap_sub(1, iVec1[1])) ? 0.0 : fConst2 + fRec1[1]);
			fRec1[0] = fTemp0 - std::floor(fTemp0);
			double fTemp1 = fConst1 * std::pow(4.0, fSlow0 * ftbl0FaustProefSIG0[std::max<int>(0, std::min<int>(static_cast<int>(65536.0 * fRec1[0]), 65535))]);
			double fTemp2 = std::cos(fTemp1);
			double fTemp3 = 1.0 - fTemp2;
			double fTemp4 = 0.125 * std::sin(fTemp1);
			double fTemp5 = 1.0 - fTemp4;
			double fTemp6 = 1.0 + fTemp4;
			fRec2[0] = static_cast<double>(input0[i0]) - (fRec2[2] * fTemp5 - 2.0 * fRec2[1] * fTemp2) / fTemp6;
			output0[i0] = static_cast<FAUSTFLOAT>(fTemp3 * (fRec2[1] + 0.5 * fRec2[0] + 0.5 * fRec2[2]) / fTemp6);
			fRec3[0] = static_cast<double>(input1[i0]) - (fTemp5 * fRec3[2] - 2.0 * fTemp2 * fRec3[1]) / fTemp6;
			output1[i0] = static_cast<FAUSTFLOAT>(fTemp3 * (fRec3[1] + 0.5 * fRec3[0] + 0.5 * fRec3[2]) / fTemp6);
			iVec1[1] = iVec1[0];
			fRec1[1] = fRec1[0];
			fRec2[2] = fRec2[1];
			fRec2[1] = fRec2[0];
			fRec3[2] = fRec3[1];
			fRec3[1] = fRec3[0];
		}
	}

};

#endif
