#pragma once
#ifndef VOWEL_MODULE_H
#define VOWEL_MODULE_H

#include "../Util/dist_dsp.h"
#include "mono_effect_module.h"
#ifdef __cplusplus

/** @file vowel_module.h */

namespace bkshepherd {

// Vowel (experimental): a vowel filter, the "talking guitar" of a talk box without the tube. A bank of four resonant band-pass filters is
// tuned to the formants of a sung vowel (A, E, I, O or U), so the guitar sounds as if it were sung through that vowel. The second footswitch
// is a momentary vowel change: while it is held the filter glides to the Hold Vowel, and it glides back to the Vowel when released, so
// pressing it in time with the playing makes the guitar "speak" (A to I is "ai", A to U is "au", and so on).
//   Vowel       the vowel of the filter (A, E, I, O, U)
//   Hold Vowel  the vowel it changes to while the second footswitch is held
//   Glide       how long it takes to move from one vowel to the other (5 ms to 800 ms)
//   Sharp       the width of the formants: low is soft and breathy, high is narrow, nasal and more pronounced
//   Mix         the filtered signal against the dry one
//   Level       the output level
//   Voice       Male, Female or Child: the formants are scaled up (the shorter the vocal tract, the higher the formants)
class VowelModule : public MonoEffectModule {
  public:
    enum Param { VOWEL = 0, HOLD_VOWEL, GLIDE, SHARP, MIX, LEVEL, VOICE, PARAM_COUNT };
    enum Vowel { VOWEL_A = 0, VOWEL_E, VOWEL_I, VOWEL_O, VOWEL_U, VOWEL_COUNT };
    enum Voice { MALE = 0, FEMALE, CHILD, VOICE_COUNT };
    static constexpr int kFormants = 4;

    VowelModule();
    ~VowelModule();

    void Init(float sample_rate) override;
    void ParameterChanged(int parameter_id) override;
    void SetEnabled(bool isEnabled) override;
    float GetBrightnessForLED(int led_id) const override;
    bool AlternateFootswitchForTempo() const override { return false; }
    void AlternateFootswitchPressed() override;
    void AlternateFootswitchReleased() override;

  protected:
    void Process(const float *in, float *out, size_t n) override;

  private:
    void Recompute();
    void SetTargets();
    void UpdateFilters();

    float m_sampleRate;
    bool m_dirty;
    volatile bool m_hold; // the second footswitch is held (set from the UI loop, read in the audio callback)

    // From the parameters (Recompute)
    int m_vowel, m_holdVowel, m_voice;
    float m_glide;   // per-sample smoothing coefficient
    float m_sharp;   // the formant widths are divided by this
    float m_mix;
    float m_outTarget, m_out;
    bool m_lastHold;

    // The formants: where they are now and where they are going (frequency in Hz, width in Hz, gain)
    float m_f[kFormants], m_bw[kFormants], m_g[kFormants];
    float m_fTarget[kFormants], m_bwTarget[kFormants], m_gTarget[kFormants];

    // The filters: Zavalishin's state variable filter (topology preserving transform), band-pass output
    float m_a1[kFormants], m_a2[kFormants], m_a3[kFormants], m_k[kFormants];
    float m_ic1[kFormants], m_ic2[kFormants];
    int m_count;
};
} // namespace bkshepherd
#endif
#endif
