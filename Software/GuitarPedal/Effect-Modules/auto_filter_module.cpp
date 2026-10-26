#include "auto_filter_module.h"
#include <algorithm>
#include <array>
#include <cmath>

using namespace bkshepherd;
using namespace distdsp;

namespace {
const char *s_typeNames[AutoFilterModule::TYPE_COUNT] = {"Dynamic", "Mutron", "Q-Tron", "Auto Wah", "Bass"};
const char *s_filterNames[AutoFilterModule::FILTER_COUNT] = {"Low Pass", "Band Pass", "High Pass"};
const char *s_directionNames[2] = {"Up", "Down"};

struct TypeData {
    float baseLo, baseHi;   // Hz: Range 0% and 100% (where the sweep starts)
    float octaves;          // how far up the playing sweeps
    float qLo, qHi;         // Peak 0% and 100%
    float attackMs;         // how fast the filter follows a pick
    float decayLo, decayHi; // ms: Decay 0% and 100%
    float drive;            // the input stage saturates at 1 / drive (1: not at all)
    float curve;            // 0..1: how much slower the start of the sweep is than linear (the optical coupling of the Mu-Tron)
    float lowKeepHz;        // above 0: the dry signal below this frequency is added back
    float makeupDb;         // calibrated so that the default settings are about +3 dB at normal playing level (the level test)
};
constexpr TypeData kType[AutoFilterModule::TYPE_COUNT] = {
    //  baseLo baseHi oct  qLo  qHi  att  decLo decHi drive curve lowKeep makeup
    {150.0f, 1200.0f, 3.5f, 0.7f, 8.0f, 2.0f, 40.0f, 900.0f, 1.0f, 0.0f, 0.0f, 8.4f},   // Dynamic
    {100.0f, 800.0f, 4.0f, 1.5f, 14.0f, 8.0f, 80.0f, 1200.0f, 3.0f, 0.6f, 0.0f, 2.9f},   // Mutron
    {120.0f, 1000.0f, 4.5f, 0.8f, 16.0f, 3.0f, 30.0f, 700.0f, 1.5f, 0.0f, 0.0f, 5.3f},   // Q-Tron
    {350.0f, 800.0f, 2.6f, 1.5f, 6.0f, 1.5f, 30.0f, 300.0f, 1.0f, 0.0f, 0.0f, 8.7f},     // Auto Wah
    {50.0f, 300.0f, 3.2f, 0.7f, 8.0f, 4.0f, 80.0f, 1000.0f, 1.5f, 0.0f, 90.0f, 4.7f}};  // Bass

constexpr float kSensMaxDb = 48.0f;
constexpr float kSmoothing = 0.002f; // per sample: about 10 ms at 48 kHz
constexpr int kUpdateEvery = 4;      // the filter's coefficients are recomputed every 4 samples (tan is the expensive part)

float SensDb(float v) { return ParamDisplay::Lin(v, 0.0f, kSensMaxDb); }
float LevelDb(float v) { return ParamDisplay::Lin(v, -12.0f, 12.0f); }
} // namespace

static const auto s_metaData = [] {
    std::array<ParameterMetaData, AutoFilterModule::PARAM_COUNT> params{};

    params[AutoFilterModule::SENS] = {
        name : "Sens",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.625f}, // 30 dB
        knobMapping : 0,
        midiCCMapping : -1,
        unit : " dB",
        decimals : 0,
        displayFn : [](float v) { return SensDb(v); }
    };

    params[AutoFilterModule::RANGE] = {
        name : "Range",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.3f},
        knobMapping : 1,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    };

    params[AutoFilterModule::PEAK] = {
        name : "Peak",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 2,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    };

    params[AutoFilterModule::DECAY] = {
        name : "Decay",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.4f},
        knobMapping : 3,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    };

    params[AutoFilterModule::TYPE] = {
        name : "Type",
        valueType : ParameterValueType::Binned,
        valueBinCount : AutoFilterModule::TYPE_COUNT,
        valueBinNames : s_typeNames,
        defaultValue : {.uint_value = 0},
        knobMapping : 4,
        midiCCMapping : -1
    };

    params[AutoFilterModule::LEVEL] = {
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

    params[AutoFilterModule::DIRECTION] = {
        name : "Direction",
        valueType : ParameterValueType::Binned,
        valueBinCount : 2,
        valueBinNames : s_directionNames,
        defaultValue : {.uint_value = 0},
        knobMapping : -1,
        midiCCMapping : -1
    };

    params[AutoFilterModule::FILTER] = {
        name : "Filter",
        valueType : ParameterValueType::Binned,
        valueBinCount : AutoFilterModule::FILTER_COUNT,
        valueBinNames : s_filterNames,
        defaultValue : {.uint_value = 1}, // Band Pass
        knobMapping : -1,
        midiCCMapping : -1
    };

    params[AutoFilterModule::MIX] = {
        name : "Mix",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 1.0f},
        knobMapping : -1,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    };

    return params;
}();

AutoFilterModule::AutoFilterModule()
    : MonoEffectModule(), m_sampleRate(48000.0f), m_dirty(true), m_sens(1.0f), m_base(200.0f), m_octaves(3.5f), m_k(1.0f), m_bpScale(1.0f), m_attack(0.01f),
      m_release(0.001f), m_curve(0.0f), m_drive(1.0f), m_up(true), m_mode(BANDPASS), m_mix(1.0f), m_lowKeep(0.0f), m_outTarget(1.0f), m_out(1.0f), m_env(0.0f),
      m_g(0.01f), m_a1(1.0f), m_a2(0.0f), m_a3(0.0f), m_count(0), m_ic1(0.0f), m_ic2(0.0f), m_led(0.0f) {
    m_name = "Auto Filter";
    m_paramMetaData = s_metaData.data();
    this->InitParams(static_cast<int>(s_metaData.size()));
}

AutoFilterModule::~AutoFilterModule() {}

void AutoFilterModule::Init(float sample_rate) {
    BaseEffectModule::Init(sample_rate);
    m_sampleRate = sample_rate;
    m_env = 0.0f;
    m_ic1 = m_ic2 = 0.0f;
    m_count = 0;
    m_led = 0.0f;
    m_low.Reset();
    Recompute();
    m_dirty = false;
    m_out = m_outTarget; // start at the knob position
    Settle();
}

void AutoFilterModule::ParameterChanged(int /*parameter_id*/) { m_dirty = true; }

void AutoFilterModule::Recompute() {
    const int type = std::min(std::max(GetParameterAsBinnedValue(TYPE) - 1, 0), static_cast<int>(TYPE_COUNT) - 1);
    const TypeData &t = kType[type];
    const float range = GetParameterAsFloat(RANGE), peak = GetParameterAsFloat(PEAK), decay = GetParameterAsFloat(DECAY);

    m_sens = dbToGain(SensDb(GetParameterAsFloat(SENS)));
    m_base = ParamDisplay::Exp(range, t.baseLo, t.baseHi);
    m_octaves = t.octaves;
    const float q = ParamDisplay::Exp(peak, t.qLo, t.qHi);
    m_k = 1.0f / q;
    m_bpScale = std::sqrt(m_k); // the band-pass peak is sqrt(Q) rather than 1 (a unity peak is much quieter than the input)
    m_attack = 1.0f - std::exp(-1.0f / (0.001f * t.attackMs * m_sampleRate));
    m_release = 1.0f - std::exp(-1.0f / (0.001f * ParamDisplay::Exp(decay, t.decayLo, t.decayHi) * m_sampleRate));
    m_curve = t.curve;
    m_drive = t.drive;
    m_up = GetParameterAsBinnedValue(DIRECTION) != 2;
    m_mode = std::min(std::max(GetParameterAsBinnedValue(FILTER) - 1, 0), static_cast<int>(FILTER_COUNT) - 1);
    m_mix = GetParameterAsFloat(MIX);
    m_lowKeep = (t.lowKeepHz > 0.0f && m_mode != LOWPASS) ? 0.7f : 0.0f;
    if (t.lowKeepHz > 0.0f) m_low.SetCutoff(t.lowKeepHz, m_sampleRate);
    m_outTarget = dbToGain(LevelDb(GetParameterAsFloat(LEVEL)) + t.makeupDb);
}

// pos 0..1: where in the sweep the filter is; sets the coefficients of the state variable filter (Zavalishin's topology-preserving transform)
void AutoFilterModule::SetFrequency(float pos) {
    const float fc = std::min(m_base * std::exp2(pos * m_octaves), 0.42f * m_sampleRate);
    m_g = std::tan(kPi * fc / m_sampleRate);
    m_a1 = 1.0f / (1.0f + m_g * (m_g + m_k));
    m_a2 = m_g * m_a1;
    m_a3 = m_g * m_a2;
}

void AutoFilterModule::Process(const float *in, float *out, size_t n) {
    if (m_dirty) {
        m_dirty = false;
        Recompute();
    }

    for (size_t i = 0; i < n; i++) {
        m_out += (m_outTarget - m_out) * kSmoothing;
        const float x = in[i];

        // The envelope of the playing: a rectifier with a fast attack and the Decay as release
        const float r = std::fabs(x) * m_sens;
        m_env += (r - m_env) * (r > m_env ? m_attack : m_release);
        float pos = m_env / (1.0f + m_env); // 0..1, saturating: a hard pick does not sweep any further than the top of the range
        pos += m_curve * (pos * pos - pos);
        m_led = pos;
        if (!m_up) pos = 1.0f - pos;
        if (m_count == 0) SetFrequency(pos);
        m_count = (m_count + 1) % kUpdateEvery;

        // The input stage: unity gain for small signals, saturating at 1 / drive
        float v = x;
        if (m_drive > 1.0f) v = SoftClipAdaa::f(x * m_drive * (1.0f / 1.5f)) * (1.5f / m_drive);

        const float v3 = v - m_ic2;
        const float v1 = m_a1 * m_ic1 + m_a2 * v3; // band-pass
        const float v2 = m_ic2 + m_a2 * m_ic1 + m_a3 * v3; // low-pass
        m_ic1 = 2.0f * v1 - m_ic1;
        m_ic2 = 2.0f * v2 - m_ic2;
        float wet;
        switch (m_mode) {
        case LOWPASS: wet = v2; break;
        case HIGHPASS: wet = v - m_k * v1 - v2; break;
        default: wet = v1 * m_bpScale; break;
        }
        if (m_lowKeep > 0.0f) wet += m_lowKeep * m_low.LP(x);

        float y = x + m_mix * (wet * m_out - x);
        y = SoftClipAdaa::f(y * (1.0f / 1.5f)); // a ceiling at full scale: a resonant filter can get very loud
        out[i] = y;
    }
}

float AutoFilterModule::GetBrightnessForLED(int led_id) const {
    float value = BaseEffectModule::GetBrightnessForLED(led_id);
    if (led_id == 1) return value * m_led; // the left LED shows where the filter is in its sweep
    return value;
}
