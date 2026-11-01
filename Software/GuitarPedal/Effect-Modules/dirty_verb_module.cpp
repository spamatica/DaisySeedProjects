#include "dirty_verb_module.h"
#include <algorithm>
#include <array>
#include <cmath>

using namespace bkshepherd;

namespace {
constexpr float kSmoothing = 0.002f;
constexpr size_t kChunk = 64;

float DecayS(float v) { return ParamDisplay::Exp(v, 0.4f, 30.0f); }
float DampHz(float v) { return ParamDisplay::Exp(v, 600.0f, 10000.0f); }
float SizeX(float v) { return ParamDisplay::Exp(v, 0.5f, 2.0f); }
float ModMs(float v) { return ParamDisplay::Lin(v, 0.0f, 2.0f); }
float ToneHz(float v) { return ParamDisplay::Exp(v, 1500.0f, 16000.0f); }
float PreMs(float v) { return ParamDisplay::Lin(v, 0.0f, 500.0f); }
float LowCutHz(float v) { return ParamDisplay::Exp(v, 20.0f, 500.0f); }
float FizzHz(float v) { return ParamDisplay::Exp(v, 100.0f, 1000.0f); }
float DistLevelDb(float v) { return ParamDisplay::Lin(v, -12.0f, 18.0f); }
float ModRateHz(float v) { return ParamDisplay::Exp(v, 0.1f, 4.0f); }

const char *s_placeNames[2] = {"Output", "Input"};
const char *s_typeNames[3] = {"Soft", "Hard", "Fizz"};

float DSY_SDRAM_BSS s_memory[dirtyverb::kMemFloats];
float DSY_SDRAM_BSS s_memoryClean[dirtyverb::kMemFloats];
} // namespace

static const auto s_metaData = [] {
    std::array<ParameterMetaData, DirtyVerbModule::PARAM_COUNT> params{};

    params[DirtyVerbModule::DECAY] = {
        name : "Decay",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.691f},
        knobMapping : 0,
        midiCCMapping : -1,
        unit : " s",
        decimals : 1,
        displayFn : [](float v) { return DecayS(v); }
    }; // the time for the tail to fall by 60 dB (the highs fall faster)

    params[DirtyVerbModule::MIX] = {
        name : "Mix",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.4f},
        knobMapping : 1,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    };

    params[DirtyVerbModule::DAMP] = {
        name : "Damp",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 2,
        midiCCMapping : -1,
        unit : " Hz",
        decimals : 0,
        displayFn : [](float v) { return DampHz(v); }
    }; // the low-pass in the loops: the tail gets darker every time round

    params[DirtyVerbModule::SIZE] = {
        name : "Size",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 1.0f},
        knobMapping : 3,
        midiCCMapping : -1,
        unit : " x",
        decimals : 2,
        displayFn : [](float v) { return SizeX(v); }
    }; // the length of the delay lines

    params[DirtyVerbModule::MOD] = {
        name : "Mod",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.4f},
        knobMapping : 4,
        midiCCMapping : -1,
        unit : " ms",
        decimals : 2,
        displayFn : [](float v) { return ModMs(v); }
    }; // the depth of the modulation of the delay lines

    params[DirtyVerbModule::TONE] = {
        name : "Tone",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.6f},
        knobMapping : 5,
        midiCCMapping : -1,
        unit : " Hz",
        decimals : 0,
        displayFn : [](float v) { return ToneHz(v); }
    }; // the low-pass on the output of the reverb

    params[DirtyVerbModule::PREDELAY] = {
        name : "PreDelay",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.04f},
        knobMapping : -1,
        midiCCMapping : -1,
        unit : " ms",
        decimals : 0,
        displayFn : [](float v) { return PreMs(v); }
    };

    params[DirtyVerbModule::LOWCUT] = {
        name : "Low Cut",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.43f},
        knobMapping : -1,
        midiCCMapping : -1,
        unit : " Hz",
        decimals : 0,
        displayFn : [](float v) { return LowCutHz(v); }
    }; // a high-pass on the input of the reverb: keeps the tail clear of the low notes

    params[DirtyVerbModule::MODRATE] = {
        name : "Mod Rate",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.49f},
        knobMapping : -1,
        midiCCMapping : -1,
        unit : " Hz",
        decimals : 2,
        displayFn : [](float v) { return ModRateHz(v); }
    };

    params[DirtyVerbModule::DIFFUSION] = {
        name : "Diffusion",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.7f},
        knobMapping : -1,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    }; // how much the input is smeared before it reaches the tail

    params[DirtyVerbModule::DIST] = {
        name : "Dist",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.05f},
        knobMapping : -1,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    }; // a soft-clipping distortion on the reverb only, the dry signal stays clean

    params[DirtyVerbModule::DISTPLACE] = {
        name : "Dist Place",
        valueType : ParameterValueType::Binned,
        valueBinCount : 2,
        valueBinNames : s_placeNames,
        defaultValue : {.uint_value = 0},
        knobMapping : -1,
        midiCCMapping : -1
    }; // where Dist works: on the output of the reverb, or on its input (a clearer sound)

    params[DirtyVerbModule::LINEDIST] = {
        name : "Feedback Dist",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.64f},
        knobMapping : -1,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    }; // a separate distortion of the delay lines of the tail (the lines themselves stay linear): the tail keeps its grit as it fades

    params[DirtyVerbModule::DISTTYPE] = {
        name : "Dist Type",
        valueType : ParameterValueType::Binned,
        valueBinCount : 3,
        valueBinNames : s_typeNames,
        defaultValue : {.uint_value = 1},
        knobMapping : -1,
        midiCCMapping : -1
    }; // Soft: a soft clip of the whole band. Hard: a hard clip of the whole band. Fizz: only the highs are clipped, the lows stay clean

    params[DirtyVerbModule::FIZZFREQ] = {
        name : "Fizz Freq",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.477f},
        knobMapping : -1,
        midiCCMapping : -1,
        unit : " Hz",
        decimals : 0,
        displayFn : [](float v) { return FizzHz(v); }
    }; // with Dist Type Fizz: only what is above this frequency is clipped

    params[DirtyVerbModule::DISTLEVEL] = {
        name : "Dist Level",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.6f},
        knobMapping : -1,
        midiCCMapping : -1,
        unit : " dB",
        decimals : 1,
        showSign : true,
        displayFn : [](float v) { return DistLevelDb(v); }
    }; // the level of the clipped highs with Dist Type Fizz (the clean lows stay as they are)

    params[DirtyVerbModule::LINECOMP] = {
        name : "Line Comp",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 1.0f},
        knobMapping : -1,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    }; // how much the drive of Feedback Dist follows the level of the tail: 0% a fixed drive (the grit clears up as the tail fades), 100% the same grit all the way

    params[DirtyVerbModule::CLEANBLEND] = {
        name : "Clean Blend",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : -1,
        midiCCMapping : -1,
        unit : " %",
        decimals : 0,
        displayFn : [](float v) { return ParamDisplay::Percent(v); }
    }; // the level of a second, clean reverb (the same settings, no distortion) against the main reverb: 0% only the main reverb, 100% only the clean one

    return params;
}();

DirtyVerbModule::DirtyVerbModule()
    : BaseEffectModule(), m_dirty(true), m_mixTarget(0.4f), m_mix(0.4f), m_blendTarget(0.0f), m_blend(0.0f), m_gatePos(0), m_runMain(true), m_runClean(false) {
    m_name = "Dirty Verb";
    m_paramMetaData = s_metaData.data();
    this->InitParams(static_cast<int>(s_metaData.size()));
}

DirtyVerbModule::~DirtyVerbModule() {}

void DirtyVerbModule::Init(float sample_rate) {
    BaseEffectModule::Init(sample_rate);
    Recompute(); // the targets are set before Init() of the reverb, which starts from them
    m_reverb.Init(sample_rate, s_memory);
    m_clean.Init(sample_rate, s_memoryClean);
    m_gatePos = 0;
    m_dirty = false;
    m_mix = m_mixTarget;
    m_blend = m_blendTarget;
}

void DirtyVerbModule::ParameterChanged(int /*parameter_id*/) { m_dirty = true; }

void DirtyVerbModule::Recompute() {
    m_mixTarget = GetParameterAsFloat(MIX);
    m_blendTarget = GetParameterAsFloat(CLEANBLEND);
    for (dirtyverb::Reverb *r : {&m_reverb, &m_clean}) { // the same settings; the clean reverb has no distortion
        r->SetDecaySeconds(DecayS(GetParameterAsFloat(DECAY)));
        r->SetDampHz(DampHz(GetParameterAsFloat(DAMP)));
        r->SetSize(SizeX(GetParameterAsFloat(SIZE)));
        r->SetModDepthMs(ModMs(GetParameterAsFloat(MOD)));
        r->SetToneHz(ToneHz(GetParameterAsFloat(TONE)));
        r->SetPreDelayMs(PreMs(GetParameterAsFloat(PREDELAY)));
        r->SetLowCutHz(LowCutHz(GetParameterAsFloat(LOWCUT)));
        r->SetModRateHz(ModRateHz(GetParameterAsFloat(MODRATE)));
        r->SetDiffusion(GetParameterAsFloat(DIFFUSION));
        r->SetDistortionType(std::min(std::max(GetParameterAsBinnedValue(DISTTYPE) - 1, 0), 2));
        r->SetDistLevelDb(DistLevelDb(GetParameterAsFloat(DISTLEVEL)));
        r->SetFizzHz(FizzHz(GetParameterAsFloat(FIZZFREQ)));
        r->SetLineComp(GetParameterAsFloat(LINECOMP));
        r->SetDistortionPlace(std::min(std::max(GetParameterAsBinnedValue(DISTPLACE) - 1, 0), 1));
    }
    m_reverb.SetDistortion(GetParameterAsFloat(DIST));
    m_reverb.SetLineDistortion(GetParameterAsFloat(LINEDIST));
    m_clean.SetDistortion(0.0f);
    m_clean.SetLineDistortion(0.0f);
}

void DirtyVerbModule::Block(const float *inL, const float *inR, float *outL, float *outR, size_t n) {
    if (m_dirty) {
        m_dirty = false;
        Recompute();
    }
    float mainL[kChunk], mainR[kChunk], cleanL[kChunk], cleanR[kChunk];
    size_t o = 0;
    while (o < n) {
        // A reverb that is not heard is not run (it is cleared instead, a bit at a time, so that it starts from silence when it is needed again). This is decided at
        // every 16th sample of the stream, from the level that it has now and the one that it is moving to.
        if (m_gatePos == 0) {
            m_runMain = m_blendTarget < 1.0f || m_blend < 0.9995f;
            m_runClean = m_blendTarget > 0.0f || m_blend > 0.0005f;
        }
        const size_t len = std::min(std::min(kChunk, n - o), kGate - m_gatePos);
        if (m_runMain) {
            m_reverb.Process(inL + o, inR + o, mainL, mainR, len);
        } else {
            m_reverb.Idle(len);
            std::fill(mainL, mainL + len, 0.0f);
            std::fill(mainR, mainR + len, 0.0f);
        }
        if (m_runClean) {
            m_clean.Process(inL + o, inR + o, cleanL, cleanR, len);
        } else {
            m_clean.Idle(len);
            std::fill(cleanL, cleanL + len, 0.0f);
            std::fill(cleanR, cleanR + len, 0.0f);
        }
        for (size_t i = 0; i < len; i++) {
            m_mix += (m_mixTarget - m_mix) * kSmoothing;
            m_blend += (m_blendTarget - m_blend) * kSmoothing;
            if (std::fabs(m_blendTarget - m_blend) < 1e-6f) m_blend = m_blendTarget; // (no denormals)
            const float wl = mainL[i] + (cleanL[i] - mainL[i]) * m_blend;
            const float wr = mainR[i] + (cleanR[i] - mainR[i]) * m_blend;
            const float l = inL[o + i], r = inR[o + i]; // (read before writing: in-place is safe)
            outL[o + i] = l + (wl - l) * m_mix;
            outR[o + i] = r + (wr - r) * m_mix;
        }
        m_gatePos = (m_gatePos + len) % kGate;
        o += len;
    }
}

void DirtyVerbModule::ProcessMono(float in) {
    float l, r;
    Block(&in, &in, &l, &r, 1);
    m_audioLeft = l;
    m_audioRight = r;
}

void DirtyVerbModule::ProcessStereo(float inL, float inR) {
    float l, r;
    Block(&inL, &inR, &l, &r, 1);
    m_audioLeft = l;
    m_audioRight = r;
}

void DirtyVerbModule::ProcessMonoBlock(const float *in, float *outL, float *outR, size_t n) {
    Block(in, in, outL, outR, n);
    m_audioLeft = outL[n - 1];
    m_audioRight = outR[n - 1];
}

void DirtyVerbModule::ProcessStereoBlock(const float *inL, const float *inR, float *outL, float *outR, size_t n) {
    Block(inL, inR, outL, outR, n);
    m_audioLeft = outL[n - 1];
    m_audioRight = outR[n - 1];
}
