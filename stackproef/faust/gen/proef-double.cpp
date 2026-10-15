/* ------------------------------------------------------------
name: "proef_delay"
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


class FaustProef : public dsp {
	
 private:
	
	FAUSTFLOAT fHslider0;
	int fSampleRate;
	double fConst0;
	double fConst1;
	FAUSTFLOAT fHslider1;
	FAUSTFLOAT fHslider2;
	double fRec0[3];
	double fConst2;
	FAUSTFLOAT fHslider3;
	int IOTA0;
	double fVec0[262144];
	double fRec1[3];
	double fVec1[262144];
	
 public:
	FaustProef() {
	}
	
	FaustProef(const FaustProef&) = default;
	
	virtual ~FaustProef() = default;
	
	FaustProef& operator=(const FaustProef&) = default;
	
	void metadata(Meta* m) { 
		m->declare("compile_options", "-lang cpp -fpga-mem-th 4 -ct 1 -cn FaustProef -es 1 -mcd 16 -mdd 1024 -mdy 33 -double -ftz 0");
		m->declare("delays.lib/fdelay3:author", "Julius O. Smith III");
		m->declare("delays.lib/fdelayltv:author", "Julius O. Smith III");
		m->declare("delays.lib/name", "Faust Delay Library");
		m->declare("delays.lib/version", "1.3.2");
		m->declare("filename", "proef_delay.dsp");
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
		m->declare("name", "proef_delay");
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
	}
	
	virtual void instanceConstants(int sample_rate) {
		fSampleRate = sample_rate;
		fConst0 = std::min<double>(1.92e+05, std::max<double>(5e+01, static_cast<double>(fSampleRate)));
		fConst1 = 6.283185307179586 / fConst0;
		fConst2 = 0.001 * fConst0;
	}
	
	virtual void instanceResetUserInterface() {
		fHslider0 = static_cast<FAUSTFLOAT>(0.5);
		fHslider1 = static_cast<FAUSTFLOAT>(4e+03);
		fHslider2 = static_cast<FAUSTFLOAT>(0.70703125);
		fHslider3 = static_cast<FAUSTFLOAT>(2.5e+02);
	}
	
	virtual void instanceClear() {
		for (int l0 = 0; l0 < 3; l0 = faust_wrap_add(l0, 1)) {
			fRec0[l0] = 0.0;
		}
		IOTA0 = 0;
		for (int l1 = 0; l1 < 262144; l1 = faust_wrap_add(l1, 1)) {
			fVec0[l1] = 0.0;
		}
		for (int l2 = 0; l2 < 3; l2 = faust_wrap_add(l2, 1)) {
			fRec1[l2] = 0.0;
		}
		for (int l3 = 0; l3 < 262144; l3 = faust_wrap_add(l3, 1)) {
			fVec1[l3] = 0.0;
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
		ui_interface->openVerticalBox("proef_delay");
		ui_interface->addHorizontalSlider("Frequentie", &fHslider1, FAUSTFLOAT(4e+03), FAUSTFLOAT(2e+01), FAUSTFLOAT(2e+04), FAUSTFLOAT(0.001));
		ui_interface->addHorizontalSlider("Mix", &fHslider0, FAUSTFLOAT(0.5), FAUSTFLOAT(0.0), FAUSTFLOAT(1.0), FAUSTFLOAT(0.0001));
		ui_interface->addHorizontalSlider("Resonantie", &fHslider2, FAUSTFLOAT(0.70703125), FAUSTFLOAT(0.25), FAUSTFLOAT(1e+01), FAUSTFLOAT(0.0001));
		ui_interface->addHorizontalSlider("Tijd", &fHslider3, FAUSTFLOAT(2.5e+02), FAUSTFLOAT(1.0), FAUSTFLOAT(1e+03), FAUSTFLOAT(0.0001));
		ui_interface->closeBox();
	}
	
	virtual void compute(int count, FAUSTFLOAT** RESTRICT inputs, FAUSTFLOAT** RESTRICT outputs) {
		FAUSTFLOAT* input0 = inputs[0];
		FAUSTFLOAT* input1 = inputs[1];
		FAUSTFLOAT* output0 = outputs[0];
		FAUSTFLOAT* output1 = outputs[1];
		double fSlow0 = static_cast<double>(fHslider0);
		double fSlow1 = fConst1 * static_cast<double>(fHslider1);
		double fSlow2 = std::cos(fSlow1);
		double fSlow3 = 1.0 - fSlow2;
		double fSlow4 = 0.5 * (std::sin(fSlow1) / static_cast<double>(fHslider2));
		double fSlow5 = fSlow4 + 1.0;
		double fSlow6 = (1.0 - fSlow0) * fSlow3 / fSlow5;
		double fSlow7 = 1.0 / fSlow5;
		double fSlow8 = 1.0 - fSlow4;
		double fSlow9 = 2.0 * fSlow2;
		double fSlow10 = fConst2 * static_cast<double>(fHslider3);
		double fSlow11 = fSlow10 + -0.999995;
		double fSlow12 = std::floor(fSlow11);
		double fSlow13 = fSlow10 + (-3.0 - fSlow12);
		double fSlow14 = fSlow10 + (-2.0 - fSlow12);
		double fSlow15 = fSlow10 - fSlow12;
		double fSlow16 = 0.5 * fSlow15;
		double fSlow17 = fSlow3 / fSlow5;
		int iSlow18 = static_cast<int>(fSlow11);
		int iSlow19 = std::min<int>(262147, std::max<int>(0, faust_wrap_add(iSlow18, 1)));
		double fSlow20 = fSlow10 + (-1.0 - fSlow12);
		double fSlow21 = 0.16666666666666666 * fSlow20;
		int iSlow22 = std::min<int>(262147, std::max<int>(0, iSlow18));
		double fSlow23 = fSlow15 * fSlow20;
		double fSlow24 = 0.5 * fSlow23;
		int iSlow25 = std::min<int>(262147, std::max<int>(0, faust_wrap_add(iSlow18, 2)));
		double fSlow26 = 0.16666666666666666 * fSlow23 * fSlow14;
		int iSlow27 = std::min<int>(262147, std::max<int>(0, faust_wrap_add(iSlow18, 3)));
		for (int i0 = 0; i0 < count; i0 = faust_wrap_add(i0, 1)) {
			fRec0[0] = static_cast<double>(input0[i0]) - fSlow7 * (fSlow8 * fRec0[2] - fSlow9 * fRec0[1]);
			double fTemp0 = fRec0[1] + 0.5 * fRec0[0] + 0.5 * fRec0[2];
			double fTemp1 = fSlow17 * fTemp0;
			fVec0[IOTA0 & 262143] = fTemp1;
			output0[i0] = static_cast<FAUSTFLOAT>(fSlow6 * fTemp0 + fSlow0 * (fSlow13 * (fSlow14 * (fSlow16 * fVec0[(faust_wrap_sub(IOTA0, iSlow19)) & 262143] - fSlow21 * fVec0[(faust_wrap_sub(IOTA0, iSlow22)) & 262143]) - fSlow24 * fVec0[(faust_wrap_sub(IOTA0, iSlow25)) & 262143]) + fSlow26 * fVec0[(faust_wrap_sub(IOTA0, iSlow27)) & 262143]));
			fRec1[0] = static_cast<double>(input1[i0]) - fSlow7 * (fSlow8 * fRec1[2] - fSlow9 * fRec1[1]);
			double fTemp2 = fRec1[1] + 0.5 * fRec1[0] + 0.5 * fRec1[2];
			double fTemp3 = fSlow17 * fTemp2;
			fVec1[IOTA0 & 262143] = fTemp3;
			output1[i0] = static_cast<FAUSTFLOAT>(fSlow6 * fTemp2 + fSlow0 * (fSlow13 * (fSlow14 * (fSlow16 * fVec1[(faust_wrap_sub(IOTA0, iSlow19)) & 262143] - fSlow21 * fVec1[(faust_wrap_sub(IOTA0, iSlow22)) & 262143]) - fSlow24 * fVec1[(faust_wrap_sub(IOTA0, iSlow25)) & 262143]) + fSlow26 * fVec1[(faust_wrap_sub(IOTA0, iSlow27)) & 262143]));
			fRec0[2] = fRec0[1];
			fRec0[1] = fRec0[0];
			IOTA0 = faust_wrap_add(IOTA0, 1);
			fRec1[2] = fRec1[1];
			fRec1[1] = fRec1[0];
		}
	}

};

#endif
