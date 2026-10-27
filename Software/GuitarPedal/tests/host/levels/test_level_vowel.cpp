// Level test for the Vowel. A formant filter keeps only a few narrow bands of the guitar, so its gain depends on the vowel and on the pitch:
// the makeup of each vowel (VowelData::makeupDb) is calibrated on a guitar-like signal at the reference level so that every vowel gives about
// +3 dB (like the other level-neutral effects), never attenuating, never reaching full scale.
#include "level_common.h"
#include "vowel_module.h"
#include <memory>

using namespace bkshepherd;
using namespace leveltest;

static std::unique_ptr<VowelModule> make(int vowel) {
    auto m = std::make_unique<VowelModule>();
    m->Init(kSampleRate); // every parameter at its default (Hold Vowel I, Glide 40%, Sharp 50%, Mix 100%, Level 0 dB, Male)
    m->SetParameterRaw(VowelModule::VOWEL_1, vowel);
    m->SetEnabled(true);
    return m;
}

int main() {
    const char *names[] = {"A", "E", "I", "O", "U"};
    bool ok = true;
    for (int v = 0; v < VowelModule::VOWEL_COUNT; v++) {
        char title[64];
        std::snprintf(title, sizeof title, "Vowel, %s", names[v]);
        ok &= report(title, [v] { return make(v); }, {0.0, 8.0, 3.0, 1.5}, Signal::Guitar);
    }
    std::printf("\n%s\n", ok ? "ALL IN WINDOW" : "SOME CONFIGURATIONS OUT OF WINDOW");
    return ok ? 0 : 1;
}
