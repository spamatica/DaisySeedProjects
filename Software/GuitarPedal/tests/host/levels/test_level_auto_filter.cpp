// Level test for the Auto Filter. An envelope filter's gain depends on how hard it is played (the filter opens with the level) and on the type,
// so the default makeup of each type is calibrated on a guitar-like signal at the reference level and held there: about +3 dB (like the other
// level-neutral effects), never attenuating, never reaching full scale.
#include "auto_filter_module.h"
#include "level_common.h"
#include <memory>

using namespace bkshepherd;
using namespace leveltest;

static std::unique_ptr<AutoFilterModule> make(int type, int filter) {
    auto m = std::make_unique<AutoFilterModule>();
    m->Init(kSampleRate); // every parameter at its default (Sens 30 dB, Range 30%, Peak 50%, Decay 40%, Band Pass, Up, Mix 100%, Level 0 dB)
    m->SetParameterRaw(AutoFilterModule::TYPE, type);
    m->SetParameterRaw(AutoFilterModule::FILTER, filter);
    return m;
}

int main() {
    const char *names[] = {"Dynamic", "Mutron", "Q-Tron", "Auto Wah", "Bass"};
    bool ok = true;
    for (int t = 0; t < AutoFilterModule::TYPE_COUNT; t++) {
        char title[64];
        std::snprintf(title, sizeof title, "Auto Filter, %s, Band Pass", names[t]);
        ok &= report(title, [t] { return make(t, AutoFilterModule::BANDPASS); }, {0.0, 8.0, 3.0, 1.5}, Signal::Guitar);
    }
    std::printf("\n%s\n", ok ? "ALL IN WINDOW" : "SOME CONFIGURATIONS OUT OF WINDOW");
    return ok ? 0 : 1;
}
