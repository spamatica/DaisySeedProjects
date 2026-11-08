#pragma once
#ifndef TAPE_WEAR_MODULE_H
#define TAPE_WEAR_MODULE_H

#include "mono_effect_module.h"
#ifdef __cplusplus

/** @file tapewear_module.h */

namespace bkshepherd {

// Tape Wear: the sound of worn tape. A modulated delay gives wow (slow) and flutter (fast) pitch wobble, Wear darkens the tape and adds saturation
// (compression) and hiss, Dropouts mutes the signal now and then, and the second footswitch is a tape stop: while it is held the tape slows down
// (the pitch falls and the sound fades), and when it is released the tape runs up to speed again. Adapted from terrarium-tape by Aalenberg (MIT licence,
// see TapeWear/README.md). The saturation has unity gain for small signals here (the original gets louder with Wear).
class TapeWearModule : public MonoEffectModule {
  public:
    enum Param { WEAR = 0, WOW, FLUTTER, DROPOUTS, MIX, LEVEL, HISS, DARK, UNSTABLE, SATURATION, PARAM_COUNT };

    // The delay line holds 16384 samples (0.34 s at 48 kHz): the base delay, the modulation and the longest tape stop (0.25 s) fit in it
    static constexpr size_t kBufferSize = 16384;

    TapeWearModule();
    ~TapeWearModule();

    void Init(float sample_rate) override;
    void ParameterChanged(int parameter_id) override;
    void SetEnabled(bool isEnabled) override;
    float GetBrightnessForLED(int led_id) const override;
    bool AlternateFootswitchForTempo() const override { return false; }
    void AlternateFootswitchPressed() override { m_hold = true; }
    void AlternateFootswitchReleased() override { m_hold = false; }

  protected:
    void Process(const float *in, float *out, size_t n) override;

  private:
    void Recompute();      // reads the parameters
    void UpdateDerived();  // the values that depend on the smoothed controls (every kControlBlock samples)
    uint32_t NextRandom();
    float ReadDelay(float delaySamples) const;

    float m_sampleRate;
    bool m_dirty;
    bool m_hiss, m_dark, m_unstable, m_extraSat;
    volatile bool m_hold; // the tape stop footswitch is down

    float m_wearTarget, m_wear, m_wowTarget, m_wow, m_flutterTarget, m_flutter, m_dropTarget, m_drop;
    float m_mixTarget, m_mix, m_levelTarget, m_level;

    // from the smoothed controls
    int m_ctrlCount;
    float m_lpCoeff, m_drive, m_invDrive, m_wowDepth, m_flutterDepth, m_hissAmp, m_dropProb;
    float m_wowInc, m_flutterInc, m_baseDelay, m_stopLimit;

    size_t m_write;
    float m_wowPhase, m_flutterPhase, m_speed, m_stopDelay, m_lp, m_dropSamples;
    uint32_t m_noise;
};
} // namespace bkshepherd
#endif
#endif
