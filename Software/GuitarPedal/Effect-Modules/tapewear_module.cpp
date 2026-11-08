#include "tapewear_module.h"
#include <algorithm>
#include <array>
#include <cmath>

using namespace bkshepherd;

namespace {
constexpr float kTwoPi = 6.28318530718f;
constexpr float kRandom24Max = 16777215.0f;
constexpr float kSmoothing = 0.002f;
constexpr int kControlBlock = 16; // the controls and what depends on them are updated every 16 samples

// The values of the original (terrarium-tape): delay and pitch motion, bandwidth, saturation, texture and transport
constexpr float kBaseDelayMs = 14.0f;
constexpr float kWowDepthMs = 6.0f;
constexpr float kUnstableWowDepthMs = 12.0f;
constexpr float kFlutterDepthMs = 1.2f;
constexpr float kWowRateHz = 0.28f;
constexpr float kUnstableWowRateHz = 0.43f;
constexpr float kFlutterRateHz = 5.4f;
constexpr float kUnstableFlutterRateHz = 7.1f;
constexpr float kSecondaryWowRateRatio = 0.37f;
constexpr float kSecondaryWowDepthRatio = 0.42f;
constexpr float kSecondaryWowPhase = 1.1f;
constexpr float kMinimumDelaySamples = 2.0f;

constexpr float kMinimumCutoffHz = 900.0f;
constexpr float kMaximumCutoffHz = 18000.0f;
constexpr float kWearCutoffReductionHz = 13000.0f;
constexpr float kDarkCutoffRatio = 0.58f;
constexpr float kBaseDrive = 1.0f;
constexpr float kWearDrive = 4.5f;
constexpr float kExtraDrive = 2.0f;

constexpr float kHissBase = 0.0004f;
constexpr float kHissWear = 0.006f;
constexpr float kHissWithoutSwitch = 0.0007f;
constexpr float kDropoutRateAtMax = 2.0f; // events per second at full Dropouts
constexpr float kDropoutMinMs = 20.0f;
constexpr float kDropoutRangeMs = 50.0f;
constexpr float kDropoutGain = 0.08f;

constexpr float kTapeStopSpeed = 0.025f;
constexpr float kTapeRecoverySpeed = 1.7f;
constexpr float kStopAttack = 0.0002f;
constexpr float kStopRelease = 0.00025f;
constexpr float kMaxStopDelaySeconds = 0.25f;
constexpr float kStopResumeThreshold = 1.0f;

constexpr size_t kMask = TapeWearModule::kBufferSize - 1;
float DSY_SDRAM_BSS s_delay[TapeWearModule::kBufferSize];
} // namespace

static const auto s_metaData = [] {
    std::array<ParameterMetaData, TapeWearModule::PARAM_COUNT> params{};

    params[TapeWearModule::WEAR] = {
        name : "Wear",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.4f},
        knobMapping : 0,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    }; // darker bandwidth, more saturation and more hiss

    params[TapeWearModule::WOW] = {
        name : "Wow",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.3f},
        knobMapping : 1,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    }; // depth of the slow pitch wobble

    params[TapeWearModule::FLUTTER] = {
        name : "Flutter",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.3f},
        knobMapping : 2,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    }; // depth of the fast pitch wobble

    params[TapeWearModule::DROPOUTS] = {
        name : "Dropouts",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.0f},
        knobMapping : 3,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    }; // how often the tape drops out (up to twice a second)

    params[TapeWearModule::MIX] = {
        name : "Mix",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 1.0f},
        knobMapping : 4,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    };

    params[TapeWearModule::LEVEL] = {
        name : "Level",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.8f},
        knobMapping : 5,
        midiCCMapping : -1,
        unit : " dB",
        decimals : 1,
        showSign : true,
        displayFn : [](float v) { return ParamDisplay::Lin(v, -24.0f, 6.0f); }
    };

    params[TapeWearModule::HISS] = {
        name : "Hiss",
        valueType : ParameterValueType::Bool,
        valueBinCount : 0,
        defaultValue : {.uint_value = 0},
        knobMapping : -1,
        midiCCMapping : -1
    }; // extra tape hiss

    params[TapeWearModule::DARK] = {
        name : "Dark",
        valueType : ParameterValueType::Bool,
        valueBinCount : 0,
        defaultValue : {.uint_value = 0},
        knobMapping : -1,
        midiCCMapping : -1
    }; // a darker tape bandwidth

    params[TapeWearModule::UNSTABLE] = {
        name : "Unstable",
        valueType : ParameterValueType::Bool,
        valueBinCount : 0,
        defaultValue : {.uint_value = 0},
        knobMapping : -1,
        midiCCMapping : -1
    }; // deeper, faster and less regular wow and flutter

    params[TapeWearModule::SATURATION] = {
        name : "Extra Sat",
        valueType : ParameterValueType::Bool,
        valueBinCount : 0,
        defaultValue : {.uint_value = 0},
        knobMapping : -1,
        midiCCMapping : -1
    }; // extra saturation

    return params;
}();

TapeWearModule::TapeWearModule()
    : MonoEffectModule(), m_sampleRate(48000.0f), m_dirty(true), m_hiss(false), m_dark(false), m_unstable(false), m_extraSat(false), m_hold(false),
      m_wearTarget(0.4f), m_wear(0.4f), m_wowTarget(0.3f), m_wow(0.3f), m_flutterTarget(0.3f), m_flutter(0.3f), m_dropTarget(0.0f), m_drop(0.0f),
      m_mixTarget(1.0f), m_mix(1.0f), m_levelTarget(1.0f), m_level(1.0f), m_ctrlCount(0), m_lpCoeff(1.0f), m_drive(1.0f), m_invDrive(1.0f),
      m_wowDepth(0.0f), m_flutterDepth(0.0f), m_hissAmp(0.0f), m_dropProb(0.0f), m_wowInc(0.0f), m_flutterInc(0.0f), m_baseDelay(0.0f),
      m_stopLimit(0.0f), m_write(0), m_wowPhase(0.0f), m_flutterPhase(0.0f), m_speed(1.0f), m_stopDelay(0.0f), m_lp(0.0f), m_dropSamples(0.0f),
      m_noise(0x6d2b79f5u) {
    m_name = "Tape Wear";
    m_paramMetaData = s_metaData.data();
    this->InitParams(static_cast<int>(s_metaData.size()));
}

TapeWearModule::~TapeWearModule() {}

void TapeWearModule::Init(float sample_rate) {
    BaseEffectModule::Init(sample_rate);
    m_sampleRate = sample_rate;
    std::fill(s_delay, s_delay + kBufferSize, 0.0f);
    m_write = 0;
    m_wowPhase = m_flutterPhase = 0.0f;
    m_speed = 1.0f;
    m_stopDelay = 0.0f;
    m_lp = 0.0f;
    m_dropSamples = 0.0f;
    m_noise = 0x6d2b79f5u;
    m_hold = false;
    Recompute();
    m_dirty = false;
    m_wear = m_wearTarget;
    m_wow = m_wowTarget;
    m_flutter = m_flutterTarget;
    m_drop = m_dropTarget;
    m_mix = m_mixTarget;
    m_level = m_levelTarget;
    UpdateDerived();
    m_ctrlCount = kControlBlock;
}

void TapeWearModule::ParameterChanged(int /*parameter_id*/) { m_dirty = true; }

void TapeWearModule::SetEnabled(bool isEnabled) {
    BaseEffectModule::SetEnabled(isEnabled);
    if (!isEnabled) m_hold = false; // a release that comes while the effect is off must not leave the tape stopped
}

float TapeWearModule::GetBrightnessForLED(int led_id) const {
    const float value = BaseEffectModule::GetBrightnessForLED(led_id);
    if (led_id == 1) return value * (m_hold ? 1.0f : 0.2f); // bright while the tape is held
    return value;
}

void TapeWearModule::Recompute() {
    m_wearTarget = GetParameterAsFloat(WEAR);
    m_wowTarget = GetParameterAsFloat(WOW);
    m_flutterTarget = GetParameterAsFloat(FLUTTER);
    m_dropTarget = GetParameterAsFloat(DROPOUTS);
    m_mixTarget = GetParameterAsFloat(MIX);
    m_levelTarget = std::pow(10.0f, ParamDisplay::Lin(GetParameterAsFloat(LEVEL), -24.0f, 6.0f) * 0.05f);
    m_hiss = GetParameterAsBool(HISS);
    m_dark = GetParameterAsBool(DARK);
    m_unstable = GetParameterAsBool(UNSTABLE);
    m_extraSat = GetParameterAsBool(SATURATION);
}

void TapeWearModule::UpdateDerived() {
    const float sr = m_sampleRate;
    const float cutoff = std::max(kMinimumCutoffHz, (kMaximumCutoffHz - m_wear * kWearCutoffReductionHz) * (m_dark ? kDarkCutoffRatio : 1.0f));
    m_lpCoeff = 1.0f - std::exp(-kTwoPi * cutoff / sr);
    m_drive = kBaseDrive + m_wear * kWearDrive + (m_extraSat ? kExtraDrive : 0.0f);
    m_invDrive = 1.0f / m_drive; // unity gain for small signals
    m_wowDepth = m_wow * (m_unstable ? kUnstableWowDepthMs : kWowDepthMs) * 0.001f * sr;
    m_flutterDepth = m_flutter * kFlutterDepthMs * 0.001f * sr;
    m_hissAmp = m_hiss ? kHissBase + m_wear * kHissWear : m_wear * kHissWithoutSwitch;
    m_dropProb = m_drop * (kDropoutRateAtMax / sr) * kRandom24Max;
    m_wowInc = kTwoPi * (m_unstable ? kUnstableWowRateHz : kWowRateHz) / sr;
    m_flutterInc = kTwoPi * (m_unstable ? kUnstableFlutterRateHz : kFlutterRateHz) / sr;
    m_baseDelay = sr * kBaseDelayMs * 0.001f;
    m_stopLimit = sr * kMaxStopDelaySeconds;
}

uint32_t TapeWearModule::NextRandom() {
    m_noise ^= m_noise << 13;
    m_noise ^= m_noise >> 17;
    m_noise ^= m_noise << 5;
    return m_noise;
}

float TapeWearModule::ReadDelay(float delaySamples) const {
    float pos = static_cast<float>(m_write) - delaySamples;
    if (pos < 0.0f) pos += static_cast<float>(kBufferSize);
    const size_t i0 = static_cast<size_t>(pos);
    const size_t i1 = (i0 + 1) & kMask;
    const float frac = pos - static_cast<float>(i0);
    return s_delay[i0] + (s_delay[i1] - s_delay[i0]) * frac;
}

void TapeWearModule::Process(const float *in, float *out, size_t n) {
    if (m_dirty) {
        m_dirty = false;
        Recompute();
    }
    const float maxDelay = static_cast<float>(kBufferSize - 2);

    for (size_t i = 0; i < n; i++) {
        if (--m_ctrlCount <= 0) {
            m_ctrlCount = kControlBlock;
            UpdateDerived();
        }
        m_wear += (m_wearTarget - m_wear) * kSmoothing;
        m_wow += (m_wowTarget - m_wow) * kSmoothing;
        m_flutter += (m_flutterTarget - m_flutter) * kSmoothing;
        m_drop += (m_dropTarget - m_drop) * kSmoothing;
        m_mix += (m_mixTarget - m_mix) * kSmoothing;
        m_level += (m_levelTarget - m_level) * kSmoothing;

        const float input = in[i];
        const uint32_t randomValue = NextRandom();
        const float noise = static_cast<float>(randomValue & 0xffffu) * (2.0f / 65535.0f) - 1.0f;

        // A dropout starts at random and lasts 20 to 70 ms; the signal is written to the tape at a low level meanwhile
        // (the length comes from a new random number: the one that started the dropout is always small, which would make every dropout the shortest)
        if (m_dropSamples <= 0.0f && m_drop > 0.0f && static_cast<float>(randomValue & 0x00ffffffu) < m_dropProb)
            m_dropSamples = m_sampleRate * (kDropoutMinMs + static_cast<float>(NextRandom() & 0xffffu) * (1.0f / 65535.0f) * kDropoutRangeMs) * 0.001f;
        const float dropoutLevel = m_dropSamples > 0.0f ? kDropoutGain : 1.0f;
        m_dropSamples = std::max(0.0f, m_dropSamples - 1.0f);

        s_delay[m_write] = std::tanh(input * m_drive) * m_invDrive * dropoutLevel;

        const float wowMotion = std::sin(m_wowPhase) * m_wowDepth;
        const float unstableMotion = m_unstable ? std::sin(m_wowPhase * kSecondaryWowRateRatio + kSecondaryWowPhase) * m_wowDepth * kSecondaryWowDepthRatio : 0.0f;
        const float flutterMotion = std::sin(m_flutterPhase) * m_flutterDepth;
        const float delaySamples = std::min(maxDelay, std::max(kMinimumDelaySamples, m_baseDelay + wowMotion + unstableMotion + flutterMotion + m_stopDelay));

        const float delayed = ReadDelay(delaySamples);
        m_lp += m_lpCoeff * (delayed + noise * m_hissAmp - m_lp);

        // The tape stop: the speed falls towards a crawl while the footswitch is held; the delay grows by what the tape has not played, and on release
        // the tape runs faster than normal until the delay is caught up
        const bool hold = m_hold;
        const float targetSpeed = hold ? kTapeStopSpeed : (m_stopDelay > kStopResumeThreshold ? kTapeRecoverySpeed : 1.0f);
        m_speed += (targetSpeed - m_speed) * (hold ? kStopAttack : kStopRelease);
        m_stopDelay = std::min(m_stopLimit, std::max(0.0f, m_stopDelay + 1.0f - m_speed));

        const float wet = m_lp * m_speed * m_level;
        out[i] = input + (wet - input) * m_mix;

        m_wowPhase += m_wowInc;
        m_flutterPhase += m_flutterInc;
        if (m_wowPhase >= kTwoPi) m_wowPhase -= kTwoPi;
        if (m_flutterPhase >= kTwoPi) m_flutterPhase -= kTwoPi;
        m_write = (m_write + 1) & kMask;
    }
}
