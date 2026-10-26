#pragma once
#ifndef AUTO_FILTER_MODULE_H
#define AUTO_FILTER_MODULE_H

#include "../Util/dist_dsp.h"
#include "mono_effect_module.h"
#ifdef __cplusplus

/** @file auto_filter_module.h */

namespace bkshepherd {

// Auto Filter: an envelope filter ("auto wah"). How hard you play opens (Up) or closes (Down) a resonant filter: a loud note sweeps the filter
// over a wide range, and as the note dies away the filter falls back. The classic units differ in the range they sweep, how far the resonance
// can go, how fast the filter follows the playing, and in what they add (the Mu-Tron's opto-coupled envelope and its overdriven input stage, for
// example). The Type sets those, the other controls are the same for all of them:
//   Dynamic   Boss FT-2 Dynamic Filter: a clean, fast, medium-range filter with moderate resonance; the "do everything" envelope filter.
//   Mutron    Mu-Tron III: a slower, optical envelope that opens gradually and lingers, a lot of resonance and a soft-saturating input stage
//             that compresses the loud notes, so the quack is round and fat.
//   Q-Tron    Electro-Harmonix Q-Tron: a wide sweep and a lot of resonance with a quick attack; the funk machine.
//   Auto Wah  a wah pedal driven by the envelope: a narrow, vocal range around 350 Hz to 2 kHz, little resonance, very fast.
//   Bass      an envelope filter for bass: a low range, and the lows of the dry signal are kept so the note does not disappear when the filter
//             is open high.
// Sens is how hard the playing has to be to move the filter (the gain into the envelope detector, 0 to 48 dB), Range is where the sweep starts
// (the filter's rest frequency for Up, its lowest point for Down), Peak is the resonance (Q), Decay is how slowly the filter falls back after a
// note. Filter chooses Low Pass, Band Pass or High Pass; Direction Up or Down; Mix blends in the dry signal; Level is the output level.
class AutoFilterModule : public MonoEffectModule {
  public:
    enum Param { SENS = 0, RANGE, PEAK, DECAY, TYPE, LEVEL, DIRECTION, FILTER, MIX, PARAM_COUNT };
    enum Type { DYNAMIC = 0, MUTRON, QTRON, AUTOWAH, BASS, TYPE_COUNT };
    enum FilterMode { LOWPASS = 0, BANDPASS, HIGHPASS, FILTER_COUNT };

    AutoFilterModule();
    ~AutoFilterModule();

    void Init(float sample_rate) override;
    void ParameterChanged(int parameter_id) override;
    float GetBrightnessForLED(int led_id) const override;

  protected:
    void Process(const float *in, float *out, size_t n) override;

  private:
    void Recompute();
    void SetFrequency(float pos);

    float m_sampleRate;
    bool m_dirty;

    // From the parameters (Recompute)
    float m_sens;                 // gain into the envelope detector
    float m_base;                 // Hz: the filter's lowest frequency
    float m_octaves;              // the sweep
    float m_k;                    // 1 / Q
    float m_bpScale;              // the gain of the band-pass output
    float m_attack, m_release;    // per-sample coefficients of the envelope
    float m_curve;                // 0: the envelope moves the filter linearly in octaves, 1: slow start (an opto's response)
    float m_drive;                // the input stage
    bool m_up;
    int m_mode;
    float m_mix;
    float m_lowKeep;              // how much of the dry lows is added (Bass)
    float m_outTarget, m_out;

    // State
    float m_env;
    float m_g, m_a1, m_a2, m_a3;
    int m_count;
    float m_ic1, m_ic2;
    float m_led;
    distdsp::OnePole m_low;
};
} // namespace bkshepherd
#endif
#endif
