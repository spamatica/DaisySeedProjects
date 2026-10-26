#include "vowel_module.h"
#include <algorithm>
#include <array>
#include <cmath>

using namespace bkshepherd;
using namespace distdsp;

namespace {
const char *s_vowelNames[VowelModule::VOWEL_COUNT] = {"A", "E", "I", "O", "U"};
const char *s_voiceNames[VowelModule::VOICE_COUNT] = {"Male", "Female", "Child"};

// The formants of the sung vowels (a tenor voice, the table used by Csound and many synthesizers): the frequency (Hz), the level (dB) and the
// bandwidth (Hz) of the first four. makeupDb is calibrated so that every vowel gives the same level at the default settings (the level test).
struct VowelData {
    float f[VowelModule::kFormants];
    float db[VowelModule::kFormants];
    float bw[VowelModule::kFormants];
    float makeupDb;
};
constexpr VowelData kVowel[VowelModule::VOWEL_COUNT] = {
    {{650.0f, 1080.0f, 2650.0f, 2900.0f}, {0.0f, -6.0f, -7.0f, -8.0f}, {80.0f, 90.0f, 120.0f, 130.0f}, 14.9f},   // A
    {{400.0f, 1700.0f, 2600.0f, 3200.0f}, {0.0f, -14.0f, -12.0f, -14.0f}, {70.0f, 80.0f, 100.0f, 120.0f}, 14.9f}, // E
    {{290.0f, 1870.0f, 2800.0f, 3250.0f}, {0.0f, -15.0f, -18.0f, -20.0f}, {40.0f, 90.0f, 100.0f, 120.0f}, 15.0f}, // I
    {{400.0f, 800.0f, 2600.0f, 2800.0f}, {0.0f, -10.0f, -12.0f, -12.0f}, {40.0f, 80.0f, 100.0f, 120.0f}, 18.6f},  // O
    {{350.0f, 600.0f, 2700.0f, 2900.0f}, {0.0f, -20.0f, -17.0f, -14.0f}, {40.0f, 60.0f, 100.0f, 120.0f}, 12.5f}}; // U

// Voice: the formants of a shorter vocal tract are higher
constexpr float kVoiceScale[VowelModule::VOICE_COUNT] = {1.0f, 1.2f, 1.4f};

constexpr int kUpdateEvery = 4;      // the filters' coefficients are recomputed every 4 samples (tan is the expensive part)
constexpr float kSmoothing = 0.002f; // per sample: about 10 ms at 48 kHz

float GlideMs(float v) { return ParamDisplay::Exp(v, 5.0f, 800.0f); }
float SharpFactor(float v) { return ParamDisplay::Exp(v, 0.4f, 2.5f); } // 1 at 50%
float LevelDb(float v) { return ParamDisplay::Lin(v, -12.0f, 12.0f); }
} // namespace

static const auto s_metaData = [] {
    std::array<ParameterMetaData, VowelModule::PARAM_COUNT> params{};

    params[VowelModule::VOWEL] = {
        name : "Vowel",
        valueType : ParameterValueType::Binned,
        valueBinCount : VowelModule::VOWEL_COUNT,
        valueBinNames : s_vowelNames,
        defaultValue : {.uint_value = 0}, // A
        knobMapping : 0,
        midiCCMapping : -1
    };

    params[VowelModule::HOLD_VOWEL] = {
        name : "Hold Vowel",
        valueType : ParameterValueType::Binned,
        valueBinCount : VowelModule::VOWEL_COUNT,
        valueBinNames : s_vowelNames,
        defaultValue : {.uint_value = 2}, // I
        knobMapping : 1,
        midiCCMapping : -1
    };

    params[VowelModule::GLIDE] = {
        name : "Glide",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.4f},
        knobMapping : 2,
        midiCCMapping : -1,
        unit : " ms",
        decimals : 0,
        displayFn : [](float v) { return GlideMs(v); }
    };

    params[VowelModule::SHARP] = {
        name : "Sharp",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 3,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    };

    params[VowelModule::MIX] = {
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

    params[VowelModule::LEVEL] = {
        name : "Level",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f}, // 0 dB
        knobMapping : 5,
        midiCCMapping : -1,
        unit : " dB",
        decimals : 1,
        showSign : true,
        displayFn : [](float v) { return LevelDb(v); }
    };

    params[VowelModule::VOICE] = {
        name : "Voice",
        valueType : ParameterValueType::Binned,
        valueBinCount : VowelModule::VOICE_COUNT,
        valueBinNames : s_voiceNames,
        defaultValue : {.uint_value = 0}, // Male
        knobMapping : -1,
        midiCCMapping : -1
    };

    return params;
}();

VowelModule::VowelModule()
    : MonoEffectModule(), m_sampleRate(48000.0f), m_dirty(true), m_hold(false), m_vowel(0), m_holdVowel(2), m_voice(0), m_glide(0.01f), m_sharp(1.0f), m_mix(1.0f),
      m_outTarget(1.0f), m_out(1.0f), m_lastHold(false), m_count(0) {
    m_name = "Vowel";
    m_paramMetaData = s_metaData.data();
    this->InitParams(static_cast<int>(s_metaData.size()));
    for (int i = 0; i < kFormants; i++) {
        m_f[i] = m_fTarget[i] = 500.0f;
        m_bw[i] = m_bwTarget[i] = 100.0f;
        m_g[i] = m_gTarget[i] = 0.0f;
        m_a1[i] = 1.0f;
        m_a2[i] = m_a3[i] = 0.0f;
        m_k[i] = 0.2f;
        m_ic1[i] = m_ic2[i] = 0.0f;
    }
}

VowelModule::~VowelModule() {}

void VowelModule::Init(float sample_rate) {
    BaseEffectModule::Init(sample_rate);
    m_sampleRate = sample_rate;
    m_hold = false;
    m_lastHold = false;
    m_count = 0;
    for (int i = 0; i < kFormants; i++) m_ic1[i] = m_ic2[i] = 0.0f;
    Recompute();
    m_dirty = false;
    for (int i = 0; i < kFormants; i++) { // start at the vowel, no glide from somewhere else
        m_f[i] = m_fTarget[i];
        m_bw[i] = m_bwTarget[i];
        m_g[i] = m_gTarget[i];
    }
    m_out = m_outTarget;
    UpdateFilters();
    Settle();
}

void VowelModule::ParameterChanged(int /*parameter_id*/) { m_dirty = true; }

void VowelModule::SetEnabled(bool isEnabled) {
    BaseEffectModule::SetEnabled(isEnabled);
    if (!isEnabled) m_hold = false; // the release is only delivered while the effect is on
}

void VowelModule::AlternateFootswitchPressed() { m_hold = true; }

void VowelModule::AlternateFootswitchReleased() { m_hold = false; }

float VowelModule::GetBrightnessForLED(int led_id) const {
    float value = BaseEffectModule::GetBrightnessForLED(led_id);
    if (led_id == 1) return value * (m_hold ? 1.0f : 0.2f); // the left LED is bright while the second vowel is held
    return value;
}

void VowelModule::Recompute() {
    m_vowel = std::min(std::max(GetParameterAsBinnedValue(VOWEL) - 1, 0), static_cast<int>(VOWEL_COUNT) - 1);
    m_holdVowel = std::min(std::max(GetParameterAsBinnedValue(HOLD_VOWEL) - 1, 0), static_cast<int>(VOWEL_COUNT) - 1);
    m_voice = std::min(std::max(GetParameterAsBinnedValue(VOICE) - 1, 0), static_cast<int>(VOICE_COUNT) - 1);
    m_glide = 1.0f - std::exp(-1.0f / (0.001f * GlideMs(GetParameterAsFloat(GLIDE)) * m_sampleRate));
    m_sharp = SharpFactor(GetParameterAsFloat(SHARP));
    m_mix = GetParameterAsFloat(MIX);
    m_outTarget = dbToGain(LevelDb(GetParameterAsFloat(LEVEL)));
    m_lastHold = m_hold;
    SetTargets();
}

// The formants the filter is moving to: those of the Vowel, or of the Hold Vowel while the second footswitch is held
void VowelModule::SetTargets() {
    const VowelData &v = kVowel[m_hold ? m_holdVowel : m_vowel];
    for (int i = 0; i < kFormants; i++) {
        m_fTarget[i] = std::min(v.f[i] * kVoiceScale[m_voice], 0.42f * m_sampleRate);
        m_bwTarget[i] = v.bw[i] * kVoiceScale[m_voice] / m_sharp;
        m_gTarget[i] = dbToGain(v.db[i] + v.makeupDb);
    }
}

void VowelModule::UpdateFilters() {
    for (int i = 0; i < kFormants; i++) {
        const float g = std::tan(kPi * m_f[i] / m_sampleRate);
        m_k[i] = std::max(m_bw[i] / m_f[i], 0.004f); // 1 / Q
        m_a1[i] = 1.0f / (1.0f + g * (g + m_k[i]));
        m_a2[i] = g * m_a1[i];
        m_a3[i] = g * m_a2[i];
    }
}

void VowelModule::Process(const float *in, float *out, size_t n) {
    if (m_dirty || m_hold != m_lastHold) {
        const bool all = m_dirty;
        m_dirty = false;
        if (all) {
            Recompute();
        } else {
            m_lastHold = m_hold;
            SetTargets();
        }
    }

    for (size_t i = 0; i < n; i++) {
        m_out += (m_outTarget - m_out) * kSmoothing;
        for (int j = 0; j < kFormants; j++) { // glide to the vowel
            m_f[j] += (m_fTarget[j] - m_f[j]) * m_glide;
            m_bw[j] += (m_bwTarget[j] - m_bw[j]) * m_glide;
            m_g[j] += (m_gTarget[j] - m_g[j]) * m_glide;
        }
        if (m_count == 0) UpdateFilters();
        m_count = (m_count + 1) % kUpdateEvery;

        const float x = in[i];
        float wet = 0.0f;
        for (int j = 0; j < kFormants; j++) {
            const float v3 = x - m_ic2[j];
            const float v1 = m_a1[j] * m_ic1[j] + m_a2[j] * v3; // band-pass
            const float v2 = m_ic2[j] + m_a2[j] * m_ic1[j] + m_a3[j] * v3;
            m_ic1[j] = 2.0f * v1 - m_ic1[j];
            m_ic2[j] = 2.0f * v2 - m_ic2[j];
            wet += m_g[j] * m_k[j] * v1; // k * band-pass: unity gain at the centre
        }
        float y = x + m_mix * (wet * m_out - x);
        out[i] = SoftClipAdaa::f(y * (1.0f / 1.5f)); // a ceiling at full scale
    }
}
