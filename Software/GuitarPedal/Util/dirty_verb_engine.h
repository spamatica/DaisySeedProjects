#pragma once
#ifndef DIRTY_VERB_ENGINE_H
#define DIRTY_VERB_ENGINE_H

#include <algorithm>
#include <cmath>
#include <cstddef>

/** @file dirty_verb_engine.h */

namespace bkshepherd {
namespace dirtyverb {

// A light, dark stereo reverb for long ambient tails: a mono input (the sum of the two channels) goes through a pre-delay, a low cut and four allpass diffusers into
// a feedback delay network of eight delay lines. The lines are mixed with a Householder matrix (lossless), so the decay is set by one gain per line (from the decay
// time and the line's length), and every line has a low-pass filter in its loop: the highs die away faster than the lows and the tail gets darker as it fades. The
// delay reads are modulated by slow, unrelated low frequency oscillators, which keeps the tail from ringing. The two outputs are different mixes of the eight lines
// (rows of a Hadamard matrix), so they are decorrelated. An optional soft-clipping distortion works on the reverb signal only, never on the dry signal, in one of
// two places and in three kinds (Soft: a tanh-like clip of the whole band; Hard: a hard clip of the whole band; Fizz: only the highs, above a settable frequency (300 Hz), are hard
// clipped with a lot of gain and the lows pass clean, so the mud of clipping a dark, dense tail is avoided): on the output of the reverb (after the tail, before the output low-pass; the tail keeps its own decay, but clipping a dense tail gives a thick, muddy
// sound), or on the input of the reverb (after the low cut: the reverb smears a distorted signal, which stays clearer). A second, separate distortion works on
// each of the eight delay lines separately (Feedback Dist): the output of every line is clipped and the difference (input minus clipped: what the clipper takes off, the distortion, zero for quiet
// signals) is added to the output at the Dist Level. The lines themselves stay linear, so the Decay time and the stability are not affected (a clipper in the feedback
// path has to be passive, which only makes the tail quieter and hardly audible). Its drive can follow the level of each line (Line Comp), so the same part of the tail
// is clipped at any level and the distortion does not clear up as the tail fades. It has its own amount and follows Dist Type (with Fizz, only the highs of the
// distortion go to the output).
//
// The engine has no memory of its own: the owner gives it kMemFloats floats (in SDRAM for the pedal; they are cleared by Init()). No heap, no tables, no
// transcendental functions in the sample loop. Everything that depends on the controls is recomputed every kControlBlock samples, and the controls are smoothed per
// block, so the result does not depend on how the samples are divided into calls.
constexpr int kLines = 8;
constexpr int kAllpasses = 4;
constexpr size_t kLineSize = 8192; // a power of two: the longest line is 3727 samples at 48 kHz and Size 2.0 doubles it, plus the modulation
constexpr size_t kPreSize = 32768; // 0.68 s at 48 kHz
constexpr size_t kAllpassSize = 1024;
constexpr size_t kMemFloats = kLines * kLineSize + kPreSize + kAllpasses * kAllpassSize;
constexpr int kControlBlock = 16;
constexpr int kLineGainBlock = 64; // the gains of Feedback Dist are updated every 64 samples
enum DistPlace { kDistOutput = 0, kDistInput = 1 };
enum DistType { kDistSoft = 0, kDistHard = 1, kDistFizz = 2 };

class Reverb {
  public:
    /** mem: kMemFloats floats that stay valid for as long as the reverb is used. */
    void Init(float sampleRate, float *mem) {
        m_sr = sampleRate;
        m_scale = sampleRate / 48000.0f;
        m_mem = mem;
        m_idleActive = false;
        float *p = mem;
        for (int i = 0; i < kLines; i++, p += kLineSize) m_line[i] = p;
        m_pre = p;
        p += kPreSize;
        for (int i = 0; i < kAllpasses; i++, p += kAllpassSize) m_ap[i] = p;
        std::fill(mem, mem + kMemFloats, 0.0f);

        static constexpr float apLen[kAllpasses] = {229.0f, 173.0f, 611.0f, 447.0f}; // at 48 kHz
        for (int i = 0; i < kAllpasses; i++) m_apLen[i] = std::min(static_cast<size_t>(apLen[i] * m_scale + 0.5f), kAllpassSize - 1);
        for (int i = 0; i < kLines; i++) {
            m_phase[i] = (static_cast<float>(i) + 0.5f) * (1.0f / kLines);
            m_lp[i] = 0.0f;
        }
        m_w = 0;
        m_preW = 0;
        for (int i = 0; i < kAllpasses; i++) m_apW[i] = 0;
        m_hp = m_outLpL = m_outLpR = 0.0f;
        m_inLo = m_outLoL = m_outLoR = m_tapL = m_tapR = Split();
        for (int i = 0; i < kLines; i++) {
            m_ldMs[i] = 0.0f;
            m_ldG[i] = 1.0f;
            m_ldInv[i] = 1.0f;
        }
        m_dn = 1e-15f;
        // the first control block starts from the targets
        m_decay = m_decayT;
        m_size = m_sizeT;
        m_damp = m_dampT;
        m_tone = m_toneT;
        m_mod = m_modT;
        m_modRate = m_modRateT;
        m_preMs = m_preMsT;
        m_lowCut = m_lowCutT;
        m_diff = m_diffT;
        m_dist = m_distT;
        m_distLvl = m_distLvlT;
        m_ld = m_ldT;
        m_comp = m_compT;
        m_preDelay = m_preMs * 0.001f * m_sr;
        UpdateDerived();
        m_ctrl = kControlBlock;
    }

    void SetDecaySeconds(float s) { m_decayT = s; }
    void SetSize(float scale) { m_sizeT = scale; }             // delay lengths, 0.5 to 2
    void SetDampHz(float hz) { m_dampT = hz; }                 // the low-pass in the loops
    void SetToneHz(float hz) { m_toneT = hz; }                 // the low-pass on the output
    void SetModDepthMs(float ms) { m_modT = ms; }              // 0 to 2
    void SetModRateHz(float hz) { m_modRateT = hz; }
    void SetPreDelayMs(float ms) { m_preMsT = ms; }            // 0 to 600
    void SetLowCutHz(float hz) { m_lowCutT = hz; }             // high-pass on the input
    void SetDiffusion(float d) { m_diffT = d; }                // 0 to 1
    void SetDistortion(float amount) { m_distT = amount; }     // 0 (off) to 1
    void SetDistortionPlace(int place) { m_distInput = place == kDistInput; }
    void SetDistortionType(int type) { m_distType = type; }
    void SetDistLevelDb(float db) { m_distLvlT = db; }         // the level of the clipped highs (Fizz) and of the distortion of Feedback Dist
    void SetFizzHz(float hz) { m_fizzHz = hz; }                // where Fizz splits the lows from the highs
    void SetLineDistortion(float amount) { m_ldT = amount; } // 0 (off) to 1
    void SetLineComp(float c) { m_compT = c; }                 // 0 (fixed drive) to 1 (the drive follows the level of the tail)

    /** Stands in for Process() while the reverb is not heard (n samples go by): the memory and the states are cleared, 64 floats per sample (the whole reverb in about
     *  33 ms), so that it starts from silence, without a stale tail, when it is used again. The cost is a fill instead of the reverb. */
    void Idle(size_t n) {
        if (!m_idleActive) {
            m_idleActive = true;
            m_idlePos = 0;
            for (int i = 0; i < kLines; i++) {
                m_lp[i] = 0.0f;
                m_ldMs[i] = 0.0f;
            }
            m_hp = m_outLpL = m_outLpR = 0.0f;
            m_inLo = m_outLoL = m_outLoR = m_tapL = m_tapR = Split();
        }
        size_t todo = n * 64;
        while (todo > 0 && m_idlePos < kMemFloats) {
            const size_t c = std::min(todo, kMemFloats - m_idlePos);
            std::fill(m_mem + m_idlePos, m_mem + m_idlePos + c, 0.0f);
            m_idlePos += c;
            todo -= c;
        }
    }

    /** Wet signal only. Any number of samples; the outputs may be the input arrays (every input sample is read before its output is written). */
    void Process(const float *inL, const float *inR, float *outL, float *outR, size_t n) {
        static constexpr float sgnL[kLines] = {1, 1, -1, -1, 1, 1, -1, -1};
        static constexpr float sgnR[kLines] = {1, -1, -1, 1, 1, -1, -1, 1};
        static constexpr float sgnIn[kLines] = {1, -1, 1, -1, -1, 1, -1, 1};
        m_idleActive = false;
        for (size_t s = 0; s < n; s++) {
            if (--m_ctrl <= 0) {
                m_ctrl = kControlBlock;
                SmoothControls();
                UpdateDerived();
            }

            if (m_ldOn) {
                if (--m_ldCtrl <= 0) {
                    m_ldCtrl = kLineGainBlock;
                    UpdateLineGains();
                }
            } else {
                m_ldCtrl = 0; // (the gains are worked out at once when it is switched on)
            }

            // input: mono sum, low cut, pre-delay, diffusers
            float x = 0.5f * (inL[s] + inR[s]);
            m_hp += m_hpCoeff * (x - m_hp);
            x -= m_hp;
            if (m_distOn && m_distInput) x = Shape(x, m_inLo); // (see below for the scaling)
            m_pre[m_preW] = x;
            const float pp = static_cast<float>(m_preW + kPreSize) - m_preDelay; // (always positive: no wrap test)
            const size_t pi = static_cast<size_t>(pp);
            const float pf = pp - static_cast<float>(pi);
            const size_t p0 = pi & (kPreSize - 1);
            x = m_pre[p0] + (m_pre[(p0 + 1) & (kPreSize - 1)] - m_pre[p0]) * pf;
            m_preW = (m_preW + 1) & (kPreSize - 1);
            for (int a = 0; a < kAllpasses; a++) {
                float *b = m_ap[a];
                size_t r = m_apW[a] + kAllpassSize - m_apLen[a];
                const float z = b[r & (kAllpassSize - 1)];
                const float w = x + m_diffCoeff * z;
                x = z - m_diffCoeff * w;
                b[m_apW[a]] = w;
                m_apW[a] = (m_apW[a] + 1) & (kAllpassSize - 1);
            }
            x += m_dn;
            m_dn = -m_dn;

            // the lines
            float v[kLines];
            float sum = 0.0f, wl = 0.0f, wr = 0.0f, resL = 0.0f, resR = 0.0f;
            for (int i = 0; i < kLines; i++) {
                float ph = m_phase[i] + m_phaseInc[i];
                ph -= static_cast<float>(ph >= 1.0f);
                m_phase[i] = ph;
                const float t = ph * 2.0f - 1.0f;
                const float lfo = 4.0f * t * (1.0f - std::fabs(t)); // a sine-like wave
                const float pos = static_cast<float>(m_w + kLineSize) - (m_delay[i] + m_modSamples * lfo); // (always positive: no wrap test)
                const size_t pi = static_cast<size_t>(pos);
                const float f = pos - static_cast<float>(pi);
                const size_t i0 = pi & (kLineSize - 1);
                const float *b = m_line[i];
                const float out = b[i0] + (b[(i0 + 1) & (kLineSize - 1)] - b[i0]) * f;
                m_lp[i] += m_dampCoeff * (out - m_lp[i]);
                v[i] = m_lp[i] * m_gain[i];
                if (m_ldOn) {
                    const float r = out - LineShape(i, out); // what the clipper takes off the output of this line (the distortion; in phase with the line, so that this reverb and a clean one can be blended without cancelling); the lines themselves stay linear
                    resL += sgnL[i] * r;
                    resR += sgnR[i] * r;
                }
                sum += v[i];
                wl += sgnL[i] * out;
                wr += sgnR[i] * out;
            }
            const float mix = sum * (2.0f / kLines);
            for (int i = 0; i < kLines; i++) m_line[i][m_w] = v[i] - mix + sgnIn[i] * kInject * x;
            m_w = (m_w + 1) & (kLineSize - 1);

            if (m_ldOn) {
                if (m_distType == kDistFizz) { // only the highs of what the clipper changed: the fizz (the lows of the distortion and the lows that it took out are left)
                    resL = Hp3(resL, m_tapL);
                    resR = Hp3(resR, m_tapR);
                }
                // (a soft ceiling at 3 on the distortion that is added, so that the top of the Dist Level range cannot give absurd levels)
                wl += 3.0f * SoftClip(m_ldTap * resL * (1.0f / 3.0f)) / kOut;
                wr += 3.0f * SoftClip(m_ldTap * resR * (1.0f / 3.0f)) / kOut;
            }
            wl *= kOut;
            wr *= kOut;
            if (m_distOn && !m_distInput) {
                // small signals get the gain sqrt(drive), loud ones are clipped at 1 / sqrt(drive): the level stays about the same while the tail is distorted
                wl = Shape(wl, m_outLoL);
                wr = Shape(wr, m_outLoR);
            }
            m_outLpL += m_toneCoeff * (wl - m_outLpL);
            m_outLpR += m_toneCoeff * (wr - m_outLpR);
            outL[s] = m_outLpL;
            outR[s] = m_outLpR;
        }
    }

  private:
    static constexpr float kInject = 0.35f;
    static constexpr float kFizzTapDb = 12.0f; // Fizz sends only the highs of the distortion, which are much smaller: more gain
    static constexpr float kLineTapDb = 6.0f; // the distortion added from the lines is this much louder than the Dist Level says (calibrated by ear and by test)
    static constexpr float kOut = 1.3f; // wet output level: about the level of the input for noise at the default settings
    static constexpr float kTwoPi = 6.28318530718f;

    struct Split {
        float l1 = 0.0f, l2 = 0.0f, l3 = 0.0f;
    };

    // A high-pass of three one-pole stages (18 dB per octave) at the split frequency
    float Hp3(float x, Split &sp) const {
        sp.l1 += m_splitCoeff * (x - sp.l1);
        const float h1 = x - sp.l1;
        sp.l2 += m_splitCoeff * (h1 - sp.l2);
        const float h2 = h1 - sp.l2;
        sp.l3 += m_splitCoeff * (h2 - sp.l3);
        return h2 - sp.l3;
    }

    // The tail distortion: Soft clips the whole band, Hard hard-clips the whole band, Fizz hard-clips what is above the split (the highs are found with a high-pass) and passes the lows. Small signals are lifted by the drive (less for Soft: sqrt), loud ones are clipped, and the output scale keeps the level.
    float Shape(float x, Split &sp) const {
        if (m_distType == kDistSoft) return SoftClip(x * m_distDrive) * m_distNorm;
        if (m_distType == kDistHard) return std::min(std::max(x * m_distDrive, -1.0f), 1.0f) * m_distNorm;
        // a high-pass of three one-pole stages (18 dB per octave): with the drive, a leak of the low notes would be clipped to a full-size square wave (mud)
        const float hp = Hp3(x, sp);
        return (x - hp) + std::min(std::max(hp * m_distDrive, -1.0f), 1.0f) * m_distNorm;
    }

    // The distortion of a delay line (Feedback Dist): the line output is clipped (Soft: a soft clip; Hard and Fizz: a hard clip) and the difference is what is added to the
    // output (see Process()). The lines themselves are not changed, so the Decay time and the stability are not affected. The gain of a line follows the level of the line
    // (see UpdateDerived()), so the same part of the tail is clipped at any level: the distortion does not clear up as the tail fades. m_ldMs is a mean square of the level.
    float LineShape(int i, float v) {
        const float g = m_ldG[i], inv = m_ldInv[i];
        float out;
        if (m_distType == kDistSoft) { // (a statement, not ?: : the compiler would work out both clippers and choose, and the soft one has a division)
            out = SoftClip(v * g) * inv;
        } else {
            out = std::min(std::max(v * g, -1.0f), 1.0f) * inv;
        }
        m_ldMs[i] += m_envCoeff * (v * v - m_ldMs[i]);
        return out;
    }

    // A rational tanh: x (27 + x^2) / (27 + 9 x^2), clipped at 3 where it reaches 1 (the error is small, and it is smooth)
    static float SoftClip(float x) {
        x = std::min(std::max(x, -3.0f), 3.0f);
        const float x2 = x * x;
        return x * (27.0f + x2) / (27.0f + 9.0f * x2);
    }

    void SmoothControls() {
        constexpr float k = 0.03f;       // per control block: about 10 ms
        constexpr float kSlow = 0.004f;  // Size and the pre-delay move slowly (their change is a change of pitch)
        m_decay += (m_decayT - m_decay) * k;
        m_damp += (m_dampT - m_damp) * k;
        m_tone += (m_toneT - m_tone) * k;
        m_mod += (m_modT - m_mod) * k;
        m_modRate += (m_modRateT - m_modRate) * k;
        m_lowCut += (m_lowCutT - m_lowCut) * k;
        m_diff += (m_diffT - m_diff) * k;
        m_dist += (m_distT - m_dist) * k;
        m_distLvl += (m_distLvlT - m_distLvl) * k;
        m_ld += (m_ldT - m_ld) * k;
        m_comp += (m_compT - m_comp) * k;
        m_size += (m_sizeT - m_size) * kSlow;
        m_preMs += (m_preMsT - m_preMs) * kSlow;
        m_preDelay = m_preMs * 0.001f * m_sr;
    }

    // The gain of every line for Feedback Dist (every kLineGainBlock samples, while it is on): the drive that follows the level of the line, in the log domain (1 log and
    // 1 exp per line, no square root), blended with the fixed drive by Line Comp. The following gain is limited to 0 to 72 dB.
    void UpdateLineGains() {
        for (int i = 0; i < kLines; i++) {
            const float lnAuto = std::min(std::max(m_ldLnTarget - 0.5f * std::log(m_ldMs[i] + 1e-14f), 0.0f), 8.294f); // 8.294 = ln 4000
            m_ldG[i] = std::exp((1.0f - m_ldComp) * m_ldLnFixed + m_ldComp * lnAuto);
            m_ldInv[i] = 1.0f / m_ldG[i];
        }
    }

    void UpdateDerived() {
        static constexpr float lineBase[kLines] = {1087.0f, 1399.0f, 1759.0f, 2131.0f, 2539.0f, 2927.0f, 3331.0f, 3727.0f}; // at 48 kHz, no common factors
        static constexpr float rateRatio[kLines] = {1.00f, 1.13f, 0.87f, 1.29f, 0.79f, 1.41f, 0.93f, 1.19f};
        m_modSamples = m_mod * 0.001f * m_sr;
        const float maxDelay = static_cast<float>(kLineSize) - 8.0f - m_modSamples;
        const float rt = std::max(m_decay, 0.05f);
        for (int i = 0; i < kLines; i++) {
            const float d = std::min(lineBase[i] * m_scale * m_size, maxDelay);
            m_delay[i] = d;
            // -60 dB after the decay time
            m_gain[i] = std::min(std::exp(-6.907755f * d / (m_sr * rt)), 0.99995f);
            m_phaseInc[i] = m_modRate * rateRatio[i] / m_sr;
        }
        m_dampCoeff = 1.0f - std::exp(-kTwoPi * std::min(m_damp, 0.45f * m_sr) / m_sr);
        m_toneCoeff = 1.0f - std::exp(-kTwoPi * std::min(m_tone, 0.45f * m_sr) / m_sr);
        m_hpCoeff = 1.0f - std::exp(-kTwoPi * m_lowCut / m_sr); // the low-pass part of the high-pass
        m_diffCoeff = 0.75f * std::min(std::max(m_diff, 0.0f), 1.0f);
        const float dist = std::min(std::max(m_dist, 0.0f), 1.0f);
        m_distOn = dist > 0.002f;
        if (m_distType == kDistSoft) {
            m_distDrive = 1.0f + 39.0f * dist * dist; // up to 32 dB
            m_distNorm = 1.0f / std::sqrt(m_distDrive);
        } else {
            m_distDrive = 1.0f + 999.0f * dist * dist; // up to 60 dB: the highs of a dark tail are small
            m_distNorm = std::pow(m_distDrive, -0.25f);
        }
        if (m_distType == kDistFizz) m_distNorm *= std::pow(10.0f, m_distLvl * 0.05f); // (the level of the clipped highs; Soft and Hard are the whole signal)
        m_splitCoeff = 1.0f - std::exp(-kTwoPi * std::min(m_fizzHz, 0.45f * m_sr) / m_sr);
        const float line = std::min(std::max(m_ld, 0.0f), 1.0f);
        m_ldOn = line > 0.002f;
        m_ldDrive = 1.0f + (m_distType == kDistSoft ? 63.0f : 255.0f) * line * line; // the fixed drive (Line Comp 0%): up to 36 dB for Soft, 48 dB for Hard and Fizz
        // The drive that keeps the level that the clipper sees at a fixed level above the clipping level: the amount sets that level (an rms of 0.5 to 2 times the
        // clipping level). Line Comp blends the two drives in the log domain. The gain is limited to 72 dB.
        const float target = 0.5f + 1.5f * line * line;
        const float comp = std::min(std::max(m_comp, 0.0f), 1.0f);
        m_ldLnFixed = std::log(m_ldDrive);
        m_ldLnTarget = std::log(target);
        m_ldComp = comp;
        m_envCoeff = 1.0f - std::exp(-1.0f / (0.1f * m_sr)); // the level that the clipper sees is averaged over about 100 ms
        m_ldTap = std::pow(10.0f, (m_distLvl + kLineTapDb + (m_distType == kDistFizz ? kFizzTapDb : 0.0f)) * 0.05f);
    }

    float m_sr = 48000.0f, m_scale = 1.0f;
    float *m_line[kLines] = {};
    float *m_mem = nullptr;
    bool m_idleActive = false;
    size_t m_idlePos = 0;
    float *m_pre = nullptr;
    float *m_ap[kAllpasses] = {};
    size_t m_apLen[kAllpasses] = {};
    size_t m_apW[kAllpasses] = {};
    size_t m_w = 0, m_preW = 0;
    int m_ctrl = 0;

    // targets and smoothed values
    float m_decayT = 7.9f, m_sizeT = 2.0f, m_dampT = 2500.0f, m_toneT = 6000.0f, m_modT = 0.8f, m_modRateT = 0.6f, m_preMsT = 20.0f, m_lowCutT = 80.0f, m_diffT = 0.7f, m_distT = 0.0f, m_ldT = 0.0f, m_distLvlT = 6.0f, m_compT = 1.0f;
    float m_decay = 7.9f, m_size = 2.0f, m_damp = 2500.0f, m_tone = 6000.0f, m_mod = 0.8f, m_modRate = 0.6f, m_preMs = 20.0f, m_lowCut = 80.0f, m_diff = 0.7f, m_dist = 0.0f, m_ld = 0.0f, m_distLvl = 6.0f, m_comp = 1.0f;
    float m_preDelay = 0.0f;

    // from the smoothed values
    float m_delay[kLines] = {}, m_gain[kLines] = {}, m_phaseInc[kLines] = {};
    float m_dampCoeff = 1.0f, m_toneCoeff = 1.0f, m_hpCoeff = 0.0f, m_diffCoeff = 0.0f, m_modSamples = 0.0f, m_distDrive = 1.0f, m_distNorm = 1.0f, m_ldDrive = 1.0f, m_ldTap = 1.0f, m_envCoeff = 0.0001f;
    float m_ldLnFixed = 0.0f, m_ldLnTarget = 0.0f, m_ldComp = 1.0f;
    int m_ldCtrl = 0;
    float m_ldG[kLines] = {}, m_ldInv[kLines] = {}, m_ldMs[kLines] = {};
    bool m_distOn = false, m_distInput = false, m_ldOn = false;
    int m_distType = kDistHard;
    Split m_inLo, m_outLoL, m_outLoR, m_tapL, m_tapR;
    float m_splitCoeff = 0.1f, m_fizzHz = 300.0f;

    // state
    float m_lp[kLines] = {}, m_phase[kLines] = {};
    float m_hp = 0.0f, m_outLpL = 0.0f, m_outLpR = 0.0f, m_dn = 1e-15f;
};

} // namespace dirtyverb
} // namespace bkshepherd
#endif
