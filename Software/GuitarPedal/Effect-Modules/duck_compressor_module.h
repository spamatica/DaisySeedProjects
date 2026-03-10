#pragma once
#ifndef DUCKCOMPRESSOR_MODULE_H
#define DUCKCOMPRESSOR_MODULE_H

#include <stdint.h>

#include "base_effect_module.h"
#include "daisysp-lgpl.h"
#include "SimpleCompressor/SimpleComp.h"

#ifdef __cplusplus

/** @file duck_compressor_module.h */

using namespace daisysp;

namespace bkshepherd {

class DuckCompressorModule : public BaseEffectModule {
  public:
    DuckCompressorModule();
    ~DuckCompressorModule();

    void Init(float sample_rate) override;
    void ParameterChanged(int parameter_id) override;
    void ProcessMono(float in) override;
    void ProcessStereo(float inL, float inR) override;
    float GetBrightnessForLED(int led_id) const override;

  private:
    float m_levelMin = 0.0f;
    float m_levelMax = 1.0f;

    //Compressor m_compressor;
    chunkware_simple::SimpleComp m_simpleComp;

};
} // namespace bkshepherd
#endif
#endif
