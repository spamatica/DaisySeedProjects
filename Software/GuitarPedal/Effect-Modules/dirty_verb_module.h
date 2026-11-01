#pragma once
#ifndef DIRTY_VERB_MODULE_H
#define DIRTY_VERB_MODULE_H

#include "../Util/dirty_verb_engine.h"
#include "base_effect_module.h"
#ifdef __cplusplus

/** @file dirty_verb_module.h */

namespace bkshepherd {

// Dirty Verb: a dark, light-weight reverb for long tails (see Util/dirty_verb_engine.h). Stereo out; a mono input feeds both channels. Other effects are planned for this
// module, the reverb is the first part. There are two reverbs with the same settings: the main one, with the distortion, and a clean one without it, and Clean Blend
// sets the level between them (0% only the main reverb, 100% only the clean one). A reverb that is not heard is not run (see Block()), so at 0% the clean reverb costs
// nothing.
class DirtyVerbModule : public BaseEffectModule {
  public:
    enum Param { DECAY = 0, MIX, DAMP, SIZE, MOD, TONE, PREDELAY, LOWCUT, MODRATE, DIFFUSION, DIST, DISTPLACE, LINEDIST, DISTTYPE, FIZZFREQ, DISTLEVEL, LINECOMP, CLEANBLEND, PARAM_COUNT };

    DirtyVerbModule();
    ~DirtyVerbModule();

    void Init(float sample_rate) override;
    void ParameterChanged(int parameter_id) override;
    void ProcessMono(float in) override;
    void ProcessStereo(float inL, float inR) override;
    void ProcessMonoBlock(const float *in, float *outL, float *outR, size_t n) override;
    void ProcessStereoBlock(const float *inL, const float *inR, float *outL, float *outR, size_t n) override;
    float GetMaxTrailSeconds() const override { return 30.0f; }
    bool AlternateFootswitchForTempo() const override { return false; } // the second footswitch does nothing for now

  private:
    void Recompute();
    void Block(const float *inL, const float *inR, float *outL, float *outR, size_t n);

    static constexpr size_t kGate = 16; // whether a reverb is run is decided every 16 samples of the stream (not of the calls, so the result does not depend on how the samples are divided)

    bool m_dirty;
    float m_mixTarget, m_mix;
    float m_blendTarget, m_blend; // the level of the clean reverb
    size_t m_gatePos;
    bool m_runMain, m_runClean;
    dirtyverb::Reverb m_reverb, m_clean;
};
} // namespace bkshepherd
#endif
#endif
