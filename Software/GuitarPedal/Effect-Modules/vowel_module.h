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
// changes the vowel, in one of two ways (Footswitch):
//   Vowel 2  momentary: while it is held the filter glides to Vowel 2, and it glides back to Vowel 1 when released, so pressing it in time with
//         the playing makes the guitar "speak" (A to I is "ai", A to U is "au", and so on)
//   Step  every press steps to the next of the first Steps vowels (Vowel 1 to Vowel 5) and round again after the last; the filter always
//         starts at Vowel 1 when the effect is switched on
//   Auto  the vowel changes by itself, to the next of the first Steps vowels every Interval, round and round; while the second footswitch is
//         held down the current vowel is held (the sequence stops) and it goes on when the footswitch is released
//   Vowel 1 to 5  the vowels (A, E, I, O, U); the Vowel 2 mode uses the first two, the Step and Auto modes the first Steps of them
//   Interval    the time between the vowels in the Auto mode (60 ms to 2 s)
//   Steps       how many vowels the Step mode goes through (2 to 5)
//   Glide       how long it takes to move from one vowel to the other (5 ms to 800 ms)
//   Sharp       the width of the formants: low is soft and breathy, high is narrow, nasal and more pronounced
//   Mix         the filtered signal against the dry one
//   Level       the output level
//   Voice       Male, Female or Child: the formants are scaled up (the shorter the vocal tract, the higher the formants)
class VowelModule : public MonoEffectModule {
  public:
    enum Param { VOWEL_1 = 0, VOWEL_2, GLIDE, SHARP, MIX, LEVEL, VOICE, VOWEL_3, VOWEL_4, VOWEL_5, STEPS, FOOTSWITCH, INTERVAL, PARAM_COUNT };
    enum Vowel { VOWEL_A = 0, VOWEL_E, VOWEL_I, VOWEL_O, VOWEL_U, VOWEL_COUNT };
    enum Voice { MALE = 0, FEMALE, CHILD, VOICE_COUNT };
    enum FootswitchMode { TO_VOWEL_2 = 0, STEP, AUTO, FOOTSWITCH_COUNT };
    static constexpr int kMaxSteps = 5;
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
    int CurrentIndex() const; // the index (0-based) of the vowel the filter should have now
    void UpdateFilters();

    float m_sampleRate;
    bool m_dirty;
    volatile bool m_hold; // the second footswitch is held (set from the UI loop, read in the audio callback)
    volatile int m_step;  // the vowel of the Step and Auto modes (0-based), advanced by every press (Step) or interval (Auto)
    volatile bool m_frozen; // Auto: the current vowel is held (the footswitch is down)

    // From the parameters (Recompute)
    int m_seq[kMaxSteps]; // Vowel 1 to Vowel 5
    int m_steps, m_mode, m_voice;
    float m_glide;   // per-sample smoothing coefficient
    float m_sharp;   // the formant widths are divided by this
    float m_mix;
    int m_intervalSamples; // Auto: the samples between the vowels
    int m_autoCount;       // Auto: the samples since the last change
    float m_pulse;         // Auto: a pulse for the LED at every change, dying away
    float m_outTarget, m_out;
    int m_lastIndex; // which of the vowels the targets were last set for

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
