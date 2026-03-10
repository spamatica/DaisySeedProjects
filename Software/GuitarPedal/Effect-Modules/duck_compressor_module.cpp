#include "duck_compressor_module.h"

using namespace bkshepherd;

static const int s_paramCount = 5;
static const ParameterMetaData s_metaData[s_paramCount] = {
    {
        name : "Level",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 0,
        midiCCMapping : -1
    },
    {
        name : "Ratio",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 1,
        midiCCMapping : -1
    },
    {
        name : "Thres",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 2,
        midiCCMapping : -1
    },
    {
        name : "Attack",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 3,
        midiCCMapping : -1
    },
    {
        name : "Release",
        valueType : ParameterValueType::Float,
        valueBinCount : 0,
        defaultValue : {.float_value = 0.5f},
        knobMapping : 4,
        midiCCMapping : -1
    },
};

// Default Constructor
DuckCompressorModule::DuckCompressorModule() : BaseEffectModule() 
{
    // Set the name of the effect
    m_name = "DuckComp";

    // Setup the meta data reference for this Effect
    m_paramMetaData = s_metaData;

    // Initialize Parameters for this Effect
    this->InitParams(s_paramCount);
}

// Destructor
DuckCompressorModule::~DuckCompressorModule() {
    // No Code Needed
}

void DuckCompressorModule::Init(float sample_rate)
{
    BaseEffectModule::Init(sample_rate);

    m_simpleComp.initRuntime();
    m_simpleComp.setSampleRate(sample_rate);
}

void DuckCompressorModule::ParameterChanged(int parameter_id)
{
    switch (parameter_id) {
    case 1: {
        const float ratioMin = 1.0f;
        const float ratioMax = 40.0f;
        float ratio = ratioMin + (GetParameterAsFloat(1) * (ratioMax - ratioMin));
        m_simpleComp.setRatio(ratio);
        break;
    }
    case 2: {
        const float thresholdMin = 0.0f;
        const float thresholdMax = 80.0f;
        float threshold = thresholdMin + (GetParameterAsFloat(2) * (thresholdMax - thresholdMin));
        // This is in dB so it is supposed to be 0dB to -80dB
        m_simpleComp.setThresh(-threshold);
        break;
    }
    case 3: {
        const float attackMin = 0.001f;
        const float attackMax = 10.0f;
        float attack = attackMin + (GetParameterAsFloat(3) * (attackMax - attackMin));
        m_simpleComp.setAttack(attack);
        break;
    }
    case 4: {
        const float releaseMin = 0.001f;
        const float releaseMax = 10.0f;
        float release = releaseMin + (GetParameterAsFloat(4) * (releaseMax - releaseMin));
        m_simpleComp.setRelease(release);
        break;
    }
    }
}

void DuckCompressorModule::ProcessMono(float in)
{
    float dummy = 0.0f;
    m_simpleComp.process(in, dummy);
//    const float compressor_out = m_compressor.Process(in);

    const float level = m_levelMin + (GetParameterAsFloat(0) * (m_levelMax - m_levelMin));

    m_audioLeft = in * level;
    m_audioRight = m_audioLeft;
}

void DuckCompressorModule::ProcessStereo(float inL, float inR)
{
    // Calculate the mono effect
    this->ProcessMono(inL);
}

float DuckCompressorModule::GetBrightnessForLED(int led_id) const 
{
    float value = BaseEffectModule::GetBrightnessForLED(led_id);

    // TODO: Use gain for the LED
    // if (led_id == 1)
    // {
    //     return value * m_compressor.GetGain();
    // }

    return value;
}