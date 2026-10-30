#include "test_tone_module.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

using namespace bkshepherd;

namespace {
// The correction that makes a step in a waveform band-limited (the polyBLEP residual); t is the phase (0 .. 1) relative to the step, dt the phase increment per sample
inline float polyBlep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

const char *s_typeNames[4] = {"Sine", "Square", "Triangle", "Noise"};

constexpr float kGainSmoothing = 0.002f;      // per sample: about 10 ms at 48 kHz
constexpr float kTypeFadeSeconds = 0.005f;    // the level goes down and up again over this long when the type changes
constexpr float kNoiseToSineRms = 1.2247449f; // a uniform noise of amplitude 1 has an RMS of 0.5774; a sine of peak 1 has 0.7071: scale the noise to the same RMS

constexpr float kSettleSeconds = 0.25f;  // silence before the burst: longer than the timeout, so the tone that was playing has gone round the loop and out of the input
constexpr float kBurstSeconds = 0.005f;
constexpr float kBurstHz = 2000.0f;
constexpr float kBurstGain = 0.8f;
constexpr float kTimeoutSeconds = 0.2f;
constexpr float kMuteSeconds = 0.002f;
constexpr float kMinThreshold = 0.02f;   // about -34 dBFS
constexpr float kMaxThreshold = 0.25f;
} // namespace

static const auto s_metaData = [] {
    std::array<ParameterMetaData, TestToneModule::PARAM_COUNT> params{};

    params[TestToneModule::LEVEL] = {
        name : "Level",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = -20.0f},
        knobMapping : 0,
        midiCCMapping : 14,
        minValue : -60,
        maxValue : 0,
        fineStepSize : 0.01f,
        unit : " dB",
        decimals : 1
    };

    params[TestToneModule::TYPE] = {
        name : "Type",
        valueType : ParameterValueType::Binned,
        valueBinCount : 4,
        valueBinNames : s_typeNames,
        defaultValue : {.uint_value = 0},
        knobMapping : 1,
        midiCCMapping : 15
    };

    params[TestToneModule::FREQUENCY] = {
        name : "Frequency",
        valueType : ParameterValueType::Float,
        valueCurve : ParameterValueCurve::Log,
        valueBinCount : 0,
        defaultValue : {.float_value = 1000.0f},
        knobMapping : 2,
        midiCCMapping : 16,
        minValue : 20,
        maxValue : 20000,
        fineStepSize : 0.01f,
        unit : " Hz",
        decimals : 0
    };

    return params;
}();

TestToneModule::TestToneModule()
    : BaseEffectModule(), m_phase(0.0f), m_phaseInc(1000.0f / 48000.0f), m_sampleRate(48000.0f), m_type(SINE), m_targetGain(0.1f), m_gain(0.1f), m_measureRequested(false), m_result(RESULT_NONE), m_delayTenthsMs(0), m_mState(M_IDLE), m_mCount(0), m_mNoisePeak(0.0f), m_mThreshold(kMinThreshold), m_mFound(false), m_mFoundAt(0), m_burstPhase(0.0f), m_mute(1.0f), m_fade(1.0f) {
    m_name = "TestTone";
    m_paramMetaData = s_metaData.data();
    this->InitParams(static_cast<int>(s_metaData.size()));
}

TestToneModule::~TestToneModule() {}

void TestToneModule::Init(float sample_rate) {
    BaseEffectModule::Init(sample_rate);
    m_sampleRate = sample_rate;
    m_phase = 0.0f;
    m_noise.state = 0x2545F491u;

    // ParameterChanged only fires on a change, so the defaults (or the stored values) are applied here
    m_type = std::min(std::max(GetParameterAsBinnedValue(TYPE) - 1, 0), 3);
    ParameterChanged(FREQUENCY);
    ParameterChanged(LEVEL);
    m_gain = m_targetGain; // start at the level, not gliding up from silence
    m_fade = 1.0f;
    m_mute = 1.0f;
    m_mState = M_IDLE;
    m_measureRequested = false;
    m_result = RESULT_NONE;
}

void TestToneModule::ParameterChanged(int parameter_id) {
    if (parameter_id == LEVEL) {
        const float db = std::min(std::max(GetParameterAsFloat(LEVEL), -60.0f), 0.0f);
        m_targetGain = std::pow(10.0f, db / 20.0f);
    } else if (parameter_id == FREQUENCY) {
        const float hz = std::min(std::max(GetParameterAsFloat(FREQUENCY), 1.0f), 0.45f * m_sampleRate);
        m_phaseInc = hz / m_sampleRate;
    }
    // TYPE is read in ProcessSamples (the switch is faded)
}

void TestToneModule::AlternateFootswitchPressed() { m_measureRequested = true; }

// One sample of the measurement. outSample: the burst to add to the output; toneMute: where the tone's mute should go (1 plays, 0 silent)
void TestToneModule::StepMeasurement(float in, float &outSample, float &toneMute) {
    const float sr = m_sampleRate;
    outSample = 0.0f;

    if (m_measureRequested && m_mState == M_IDLE) {
        m_measureRequested = false;
        m_mState = M_SETTLE;
        m_mCount = 0;
        m_mNoisePeak = 0.0f;
        m_mFound = false;
        m_result = RESULT_MEASURING;
    }
    if (m_mState == M_IDLE) {
        toneMute = 1.0f;
        return;
    }
    toneMute = 0.0f;

    const float a = std::fabs(in);
    switch (m_mState) {
    case M_SETTLE:
        if (m_mCount >= static_cast<size_t>(kSettleSeconds * sr * 0.5f)) m_mNoisePeak = std::max(m_mNoisePeak, a);
        if (++m_mCount >= static_cast<size_t>(kSettleSeconds * sr)) {
            m_mThreshold = std::min(std::max(4.0f * m_mNoisePeak, kMinThreshold), kMaxThreshold);
            m_mState = M_BURST;
            m_mCount = 0;
            m_burstPhase = 0.0f;
        }
        break;
    case M_BURST:
    case M_WAIT:
        if (m_mState == M_BURST) {
            // The first burst sample is the time zero (m_mCount = 0), and the detection looks at the input from the same sample on
            outSample = kBurstGain * std::sin(6.2831853f * m_burstPhase);
            m_burstPhase += kBurstHz / sr;
            if (m_burstPhase >= 1.0f) m_burstPhase -= 1.0f;
        }
        if (!m_mFound && a > m_mThreshold) {
            m_mFound = true;
            m_mFoundAt = m_mCount;
        }
        m_mCount++;
        if (m_mState == M_BURST && m_mCount >= static_cast<size_t>(kBurstSeconds * sr)) m_mState = M_WAIT; // (the wait goes on counting from the burst's start)
        if (m_mState == M_WAIT && (m_mFound || m_mCount >= static_cast<size_t>(kTimeoutSeconds * sr))) {
            if (m_mFound) {
                m_delayTenthsMs = static_cast<int>(std::lround(static_cast<float>(m_mFoundAt) * 10000.0f / sr));
                m_result = RESULT_DONE;
            } else {
                m_result = RESULT_TIMEOUT;
            }
            m_mState = M_IDLE; // the tone fades back in
        }
        break;
    default:
        break;
    }
}

void TestToneModule::ProcessSamples(const float *in, float *outL, float *outR, size_t n) {
    const float muteStep = 1.0f / (kMuteSeconds * m_sampleRate);
    const int wanted = std::min(std::max(GetParameterAsBinnedValue(TYPE) - 1, 0), 3);
    const float fadeStep = 1.0f / (kTypeFadeSeconds * m_sampleRate);

    for (size_t i = 0; i < n; i++) {
        // A new type: fade out, switch, fade in
        if (wanted != m_type) {
            m_fade -= fadeStep;
            if (m_fade <= 0.0f) {
                m_fade = 0.0f;
                m_type = wanted;
            }
        } else if (m_fade < 1.0f) {
            m_fade = std::min(1.0f, m_fade + fadeStep);
        }

        m_gain += (m_targetGain - m_gain) * kGainSmoothing;

        float burst, muteTarget;
        StepMeasurement(in[i], burst, muteTarget);
        if (m_mute < muteTarget) m_mute = std::min(muteTarget, m_mute + muteStep);
        else if (m_mute > muteTarget) m_mute = std::max(muteTarget, m_mute - muteStep);

        float x;
        switch (m_type) {
        case NOISE:
            x = m_noise.randSigned(-1.0f, 1.0f) * kNoiseToSineRms; // (peaks up to 1.22 x the level: above about -1.8 dB the noise is limited to full scale below)
            break;
        case SQUARE: { // band-limited, so the harmonics above half the sample rate do not fold back into the audio band
            x = m_phase < 0.5f ? 1.0f : -1.0f;
            x += polyBlep(m_phase, m_phaseInc);
            const float shifted = m_phase >= 0.5f ? m_phase - 0.5f : m_phase + 0.5f;
            x -= polyBlep(shifted, m_phaseInc);
            break;
        }
        case TRIANGLE:
            x = 4.0f * std::fabs(m_phase - 0.5f) - 1.0f; // +1 at phase 0, -1 at 0.5 (its harmonics fall off as 1/n^2, so it needs no correction)
            break;
        default:
            x = std::sin(6.2831853f * m_phase);
            break;
        }
        m_phase += m_phaseInc;
        if (m_phase >= 1.0f) m_phase -= 1.0f;

        const float y = std::min(1.0f, std::max(-1.0f, x * m_gain * m_fade * m_mute + burst)); // never beyond full scale
        outL[i] = y;
        outR[i] = y;
    }
}

void TestToneModule::ProcessMono(float in) {
    float l, r;
    ProcessSamples(&in, &l, &r, 1);
    m_audioLeft = l;
    m_audioRight = r;
}

void TestToneModule::ProcessStereo(float inL, float /*inR*/) { ProcessMono(inL); }

void TestToneModule::ProcessMonoBlock(const float *in, float *outL, float *outR, size_t n) {
    ProcessSamples(in, outL, outR, n);
    m_audioLeft = outL[n - 1];
    m_audioRight = outR[n - 1];
}

void TestToneModule::ProcessStereoBlock(const float *inL, const float * /*inR*/, float *outL, float *outR, size_t n) { ProcessMonoBlock(inL, outL, outR, n); }

void TestToneModule::DrawUI(OneBitGraphicsDisplay &display, int currentIndex, int numItemsTotal, Rectangle boundsToDrawIn, bool isEditing) {
    BaseEffectModule::DrawUI(display, currentIndex, numItemsTotal, boundsToDrawIn, isEditing);
    if (!IsEnabled()) return;

    char text[24];
    switch (m_result) {
    case RESULT_MEASURING:
        snprintf(text, sizeof(text), "Measuring...");
        break;
    case RESULT_DONE: {
        const int t = m_delayTenthsMs; // no float printf on target
        snprintf(text, sizeof(text), "Delay %d.%d ms", t / 10, t % 10);
        break;
    }
    case RESULT_TIMEOUT:
        snprintf(text, sizeof(text), "Timeout >%d ms", static_cast<int>(kTimeoutSeconds * 1000.0f + 0.5f));
        break;
    default:
        snprintf(text, sizeof(text), "LB: test latency");
        break;
    }
    display.WriteStringAligned(text, Font_7x10, boundsToDrawIn.WithTrimmedTop(boundsToDrawIn.GetHeight() / 2), Alignment::centered, true);
}
