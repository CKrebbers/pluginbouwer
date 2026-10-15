/* ------------------------------------------------------------
name: "proef_mod"
Code generated with Faust 2.90.8 (https://faust.grame.fr)
Compilation options: -lang cpp -fpga-mem-th 4 -ct 1 -cn FaustProef -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0
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
	
	void fillFaustProefSIG0(int count, float* table) {
		for (int i1 = 0; i1 < count; i1 = faust_wrap_add(i1, 1)) {
			iVec0[0] = 1;
			iRec0[0] = (faust_wrap_add(iVec0[1], iRec0[1])) % 65536;
			table[i1] = std::sin(9.58738e-05f * static_cast<float>(iRec0[0]));
			iVec0[1] = iVec0[0];
			iRec0[1] = iRec0[0];
		}
	}

};

static FaustProefSIG0* newFaustProefSIG0() { return (FaustProefSIG0*)new FaustProefSIG0(); }
static void deleteFaustProefSIG0(FaustProefSIG0* dsp) { delete dsp; }

static float ftbl0FaustProefSIG0[65536];

class FaustProef : public dsp {
	
 private:
	
	int fSampleRate;
	float fConst0;
	float fConst1;
	FAUSTFLOAT fHslider0;
	int iVec1[2];
	float fConst2;
	float fConst3;
	int iConst4;
	int iConst5;
	int iRec2[2];
	int iRec1[2];
	float fRec3[3];
	float fRec4[3];
	
 public:
	FaustProef() {
	}
	
	FaustProef(const FaustProef&) = default;
	
	virtual ~FaustProef() = default;
	
	FaustProef& operator=(const FaustProef&) = default;
	
	void metadata(Meta* m) { 
		m->declare("basics.lib/name", "Faust Basic Element Library");
		m->declare("basics.lib/version", "1.23.6");
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn FaustProef -es 1 -mcd 16 -mdd 1024 -mdy 33 -single -ftz 0");
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
		fConst0 = std::min<float>(1.92e+05f, std::max<float>(5e+01f, static_cast<float>(fSampleRate)));
		fConst1 = 6283.1855f / fConst0;
		fConst2 = 1.0737418e+09f * (0.7f / fConst0);
		fConst3 = std::floor(fConst2);
		iConst4 = static_cast<int>(fConst3);
		iConst5 = static_cast<int>(16777216.0f * (fConst2 - fConst3));
	}
	
	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.5f);
	}
	
	virtual void instanceClear() {
		for (int l2 = 0; l2 < 2; l2 = faust_wrap_add(l2, 1)) {
			iVec1[l2] = 0;
		}
		for (int l3 = 0; l3 < 2; l3 = faust_wrap_add(l3, 1)) {
			iRec2[l3] = 0;
		}
		for (int l4 = 0; l4 < 2; l4 = faust_wrap_add(l4, 1)) {
			iRec1[l4] = 0;
		}
		for (int l5 = 0; l5 < 3; l5 = faust_wrap_add(l5, 1)) {
			fRec3[l5] = 0.0f;
		}
		for (int l6 = 0; l6 < 3; l6 = faust_wrap_add(l6, 1)) {
			fRec4[l6] = 0.0f;
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
		ui_interface->addHorizontalSlider("Mod", &fHslider0, FAUSTFLOAT(0.5f), FAUSTFLOAT(0.0f), FAUSTFLOAT(1.0f), FAUSTFLOAT(0.0001f));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		float fSlow0 = static_cast<float>(fHslider0);
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			iVec1[0] = 1;
			int iTemp0 = faust_wrap_sub(1, iVec1[1]);
			int iTemp1 = faust_wrap_add(iConst5, iRec2[1]);
			iRec2[0] = ((iTemp0) ? 0 : iTemp1 & 16777215);
			iRec1[0] = ((iTemp0) ? 0 : (faust_wrap_add(iConst4, faust_wrap_add(iRec1[1], iTemp1 >> 24))) & 1073741823);
			float fTemp2 = fConst1 * std::pow(4.0f, fSlow0 * ftbl0FaustProefSIG0[std::max<int>(0, std::min<int>(static_cast<int>(0.00390625f * static_cast<float>(iRec1[0] >> 6)), 65535))]);
			float fTemp3 = std::cos(fTemp2);
			float fTemp4 = 1.0f - fTemp3;
			float fTemp5 = 0.125f * std::sin(fTemp2);
			float fTemp6 = 1.0f - fTemp5;
			float fTemp7 = 1.0f + fTemp5;
			fRec3[0] = static_cast<float>(input0[i0]) - (fRec3[2] * fTemp6 - 2.0f * fRec3[1] * fTemp3) / fTemp7;
			output0[i0] = static_cast<FAUSTFLOAT>(fTemp4 * (fRec3[1] + 0.5f * fRec3[0] + 0.5f * fRec3[2]) / fTemp7);
			fRec4[0] = static_cast<float>(input1[i0]) - (fTemp6 * fRec4[2] - 2.0f * fTemp3 * fRec4[1]) / fTemp7;
			output1[i0] = static_cast<FAUSTFLOAT>(fTemp4 * (fRec4[1] + 0.5f * fRec4[0] + 0.5f * fRec4[2]) / fTemp7);
			iVec1[1] = iVec1[0];
			iRec2[1] = iRec2[0];
			iRec1[1] = iRec1[0];
			fRec3[2] = fRec3[1];
			fRec3[1] = fRec3[0];
			fRec4[2] = fRec4[1];
			fRec4[1] = fRec4[0];
		}
	}

};

#endif
