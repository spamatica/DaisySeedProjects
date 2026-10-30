#pragma once
#ifndef TEST_TONE_MODULE_H
#define TEST_TONE_MODULE_H

#include "../Util/XorShift32.h"
#include "base_effect_module.h"
#include <stdint.h>
#ifdef __cplusplus

/** @file test_tone_module.h */

namespace bkshepherd {

// Test Tone: a signal generator for calibrating levels and checking the signal path (together with the input and output gain settings and the level readout of Show Debug).
// It replaces the input with a sine, square or triangle wave of a chosen frequency, or white noise, at a chosen level. When the effect is off (bypassed) the input passes as usual.
// The left (alternate) footswitch measures the delay from the output to the input with a cable between them: the tone stops, a short burst is sent and the time until
// it shows up on the input is displayed (no answer within 200 ms is a timeout).
class TestToneModule : public BaseEffectModule {
  public:
    enum Param {
        LEVEL = 0, // dB, peak level relative to full scale (for noise: the same RMS as a sine of that peak level)
        TYPE,      // Sine, Square, Triangle, Noise
        FREQUENCY, // Hz, not used by the noise
        PARAM_COUNT
    };

    enum Type { SINE = 0, SQUARE, TRIANGLE, NOISE };

    TestToneModule();
    ~TestToneModule();

    void Init(float sample_rate) override;
    void ParameterChanged(int parameter_id) override;
    void ProcessMono(float in) override;
    void ProcessStereo(float inL, float inR) override;
    void ProcessMonoBlock(const float *in, float *outL, float *outR, size_t n) override;
    void ProcessStereoBlock(const float *inL, const float *inR, float *outL, float *outR, size_t n) override;
    void AlternateFootswitchPressed() override;
    bool AlternateFootswitchForTempo() const override { return false; }
    void DrawUI(OneBitGraphicsDisplay &display, int currentIndex, int numItemsTotal, Rectangle boundsToDrawIn, bool isEditing) override;

    /** The delay measurement: Idle (nothing measured yet), Measuring, Done (the delay is in GetDelayTenthsMs) or Timeout */
    enum MeasureResult { RESULT_NONE = 0, RESULT_MEASURING, RESULT_DONE, RESULT_TIMEOUT };
    int GetMeasureResult() const { return m_result; }
    /** The measured delay in tenths of a millisecond (valid when the result is Done) */
    int GetDelayTenthsMs() const { return m_delayTenthsMs; }

  private:
    enum MeasureState { M_IDLE = 0, M_SETTLE, M_BURST, M_WAIT };
    void ProcessSamples(const float *in, float *outL, float *outR, size_t n);
    void StepMeasurement(float in, float &outSample, float &toneMute);

    float m_phase;      // 0 .. 1
    float m_phaseInc;   // frequency / sample rate
    XorShift32 m_noise;
    float m_sampleRate;
    int m_type;          // the waveform that is playing
    float m_targetGain;  // linear peak gain from the Level parameter
    float m_gain;        // the smoothed gain
    // Delay measurement (run in the audio path)
    volatile bool m_measureRequested;
    volatile int m_result;
    volatile int m_delayTenthsMs;
    int m_mState;
    size_t m_mCount;     // samples in the current state (in the wait: since the burst started)
    float m_mNoisePeak;  // the largest input during the second half of the settle time
    float m_mThreshold;
    bool m_mFound;
    size_t m_mFoundAt;
    float m_burstPhase;
    float m_mute;        // 1 = the tone plays, 0 = silent; follows the measurement with a short ramp
    float m_fade;        // 1 normally; goes to 0 and back when the type changes, so switching the type does not click
};
} // namespace bkshepherd
#endif
#endif
