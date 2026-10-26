// An effect chain's "Switches" parameter: which slots the footswitches get (Util/chain_switches.h), the parameter itself, and the slot
// interface of EffectChain that guitar_pedal.cpp uses (GetFootswitchSlots, GetSlotCount, IsSlotOn, SetSlotOn, GetSlotLevel).
#include "../../Util/chain_switches.h"
#include "effect_chain.h"
#include "screamer_module.h"
#include "tape_sat_module.h"
#include "wavefolder_module.h"
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
using namespace bkshepherd;
using namespace chainswitches;

static int fails = 0;
static void check(bool ok, const char *what) {
    fails += !ok;
    std::printf("  %-100s %s\n", what, ok ? "OK" : "FAILED");
}

static bool is(Assignment a, int bypass, int alternate) { return a.bypassSlot == bypass && a.alternateSlot == alternate; }

int main() {
    std::printf("Which slots the footswitches get:\n");
    check(!Resolve(NORMAL, 3).Active(), "Normal: the footswitches work as usual");
    check(is(Resolve(SLOTS_2_1, 2), 0, 1) && is(Resolve(SLOTS_2_1, 3), 0, 1), "2+1 (left, right): right = slot 1, left = slot 2 (chains of two and three)");
    check(is(Resolve(SLOTS_3_1, 3), 0, 2), "3+1: right = slot 1, left = slot 3");
    check(is(Resolve(SLOTS_3_2, 3), 1, 2), "3+2: right = slot 2, left = slot 3");
    check(!Resolve(SLOTS_3_1, 2).Active() && !Resolve(SLOTS_3_2, 2).Active(), "3+1 and 3+2 on a chain of two: as usual");
    check(!Resolve(SLOTS_2_1, 0).Active() && !Resolve(SLOTS_2_1, 1).Active(), "a plain effect (no slots) or a chain of one: as usual");
    check(!Resolve(-1, 3).Active() && !Resolve(ModeCount(3), 3).Active() && !Resolve(1000, 3).Active(), "an unknown mode (corrupt settings): as usual");

    std::printf("\nAny pair of slots of a chain of any size:\n");
    check(ModeCount(0) == 1 && ModeCount(1) == 1 && ModeCount(2) == 2 && ModeCount(3) == 4 && ModeCount(4) == 7 && ModeCount(5) == 11 && ModeCount(8) == 29,
          "a chain of n slots has 1 + n(n-1)/2 modes (1, 1, 2, 4, 7, 11, ... 29 for eight)");
    {
        // every pair of slots of chains of 2 to 8 slots exactly once: the right slot below the left one, both inside the chain, in the order of the names
        bool ok = true, ordered = true;
        for (int n = 2; n <= 8; n++) {
            std::vector<int> seen(n * n, 0);
            int lastLeft = 0, lastRight = -1;
            for (int mode = 1; mode < ModeCount(n); mode++) {
                const Assignment a = Resolve(mode, n);
                ok = ok && a.Active() && a.bypassSlot >= 0 && a.bypassSlot < a.alternateSlot && a.alternateSlot < n && ++seen[a.alternateSlot * n + a.bypassSlot] == 1;
                ordered = ordered && (a.alternateSlot > lastLeft || (a.alternateSlot == lastLeft && a.bypassSlot > lastRight));
                lastLeft = a.alternateSlot;
                lastRight = a.bypassSlot;
            }
            ok = ok && !Resolve(ModeCount(n), n).Active();
        }
        check(ok, "chains of 2 to 8 slots: every mode is a different pair, the right slot below the left one, and the next number is not a mode");
        check(ordered, "the modes are ordered by the left slot and then the right slot (a mode keeps its number when slots are added)");
        check(is(Resolve(4, 4), 0, 3) && is(Resolve(5, 4), 1, 3) && is(Resolve(6, 4), 2, 3) && !Resolve(4, 3).Active(), "4+1, 4+2 and 4+3 are slot 4 on the left with slot 1, 2 or 3 on the right (not in a chain of three)");
        char name[24];
        std::string names;
        for (int mode = 0; mode < ModeCount(5); mode++) {
            ModeName(mode, name, sizeof name);
            names += std::string(mode ? " " : "") + name;
        }
        check(names == "Normal 2+1 3+1 3+2 4+1 4+2 4+3 5+1 5+2 5+3 5+4", "the names of a chain of five, left to right");
    }

    std::printf("\nThe slots of an effect chain:\n");
    EffectChain chain("Test", {{"Sc", new ScreamerModule()}, {"Wf", new WavefolderModule()}, {"Ts", new TapeSatModule()}}, {});
    chain.Init(48000.0f);
    chain.SetEnabled(true);
    ScreamerModule plain;
    plain.Init(48000.0f);
    check(chain.GetSlotCount() == 3 && plain.GetSlotCount() == 0, "a chain reports its slots, a plain effect none");
    check(chain.IsSlotOn(0) && chain.IsSlotOn(1) && chain.IsSlotOn(2), "every slot starts on");

    auto run = [&](float seconds) {
        std::vector<float> in(48), l(48), r(48);
        for (int b = 0; b < int(seconds * 1000); b++) chain.ProcessStereoBlock(in.data(), in.data(), l.data(), r.data(), 48);
    };
    run(0.01f);
    check(chain.GetSlotLevel(1) == 1.0f, "an on slot is fully faded in (LED full)");
    chain.SetSlotOn(1, false);
    check(!chain.IsSlotOn(1) && chain.GetParameterAsBool(1) == false && chain.IsSlotOn(0) && chain.IsSlotOn(2),
          "switching a slot off changes only that slot, through its own On parameter (saved and shown in the menu)");
    run(0.05f);
    const float mid = chain.GetSlotLevel(1);
    run(0.1f);
    std::printf("    slot 2 level 50 ms after switching off: %.2f, 150 ms after: %.2f\n", mid, chain.GetSlotLevel(1));
    check(mid > 0.0f && mid < 1.0f && chain.GetSlotLevel(1) == 0.0f, "the slot crossfades out (about 0.1 s), and its LED level follows");
    chain.SetSlotOn(1, true);
    run(0.15f);
    check(chain.IsSlotOn(1) && chain.GetSlotLevel(1) == 1.0f, "and switches on again");
    chain.SetSlotOn(5, false);
    chain.SetSlotOn(-1, false);
    check(chain.IsSlotOn(0) && chain.IsSlotOn(1) && chain.IsSlotOn(2) && !chain.IsSlotOn(7) && chain.GetSlotLevel(7) == 0.0f,
          "a slot number the chain does not have is ignored");

    std::printf("\nThe Switches parameter of a chain:\n");
    {
        const int sw = 3; // after the three On parameters
        check(std::string(chain.GetParameterName(sw)) == "Switches" && chain.GetParameterType(sw) == ParameterValueType::Binned &&
                  chain.GetParameterBinCount(sw) == 4 && std::string(chain.GetParameterBinNames(sw)[1]) == "2+1" && std::string(chain.GetParameterBinNames(sw)[2]) == "3+1" &&
                  std::string(chain.GetParameterBinNames(sw)[3]) == "3+2",
              "a chain of three has Switches after its On parameters, with Normal, 2+1, 3+1 and 3+2 (left switch first)");
        check(!chain.GetFootswitchSlots().Active() && !plain.GetFootswitchSlots().Active(), "Switches starts at Normal; a plain effect never has footswitch slots");
        chain.SetParameterRaw(sw, SLOTS_3_2);
        check(is(chain.GetFootswitchSlots(), 1, 2), "Switches 3+2: right = slot 2, left = slot 3");
        chain.SetParameterRaw(sw, SLOTS_3_1);
        check(is(chain.GetFootswitchSlots(), 0, 2), "Switches 3+1: right = slot 1, left = slot 3");
        check(std::string(chain.GetParameterName(sw + 1)).rfind("Sc ", 0) == 0, "the children's parameters follow it (the first is the Screamer's, tagged Sc)");

        EffectChain two("Two", {{"Sc", new ScreamerModule()}, {"Ts", new TapeSatModule()}}, {});
        two.Init(48000.0f);
        check(two.GetParameterBinCount(2) == 2 && std::string(two.GetParameterBinNames(2)[1]) == "2+1", "a chain of two only offers Normal and 2+1");
        two.SetParameterRaw(2, SLOTS_2_1);
        check(is(two.GetFootswitchSlots(), 0, 1), "and 2+1 gives right = slot 1, left = slot 2");

        EffectChain four("Four", {{"A", new ScreamerModule()}, {"B", new WavefolderModule()}, {"C", new TapeSatModule()}, {"D", new ScreamerModule()}}, {});
        four.Init(48000.0f);
        const int sw4 = 4;
        check(std::string(four.GetParameterName(sw4)) == "Switches" && four.GetParameterBinCount(sw4) == 7 && std::string(four.GetParameterBinNames(sw4)[4]) == "4+1" &&
                  std::string(four.GetParameterBinNames(sw4)[6]) == "4+3",
              "a chain of four has seven Switches choices (Normal, 2+1, 3+1, 3+2, 4+1, 4+2, 4+3)");
        four.SetParameterRaw(sw4, 5);
        check(is(four.GetFootswitchSlots(), 1, 3), "Switches 4+2: right = slot 2, left = slot 4");
        four.SetSlotOn(3, false);
        check(!four.IsSlotOn(3) && four.IsSlotOn(0), "and the fourth slot is switched through its own On parameter");
        check(std::string(four.GetParameterName(sw4 + 1)).rfind("A ", 0) == 0, "the children's parameters follow Switches");

        EffectChain one("One", {{"Sc", new ScreamerModule()}}, {});
        one.Init(48000.0f);
        check(std::string(one.GetParameterName(1)).rfind("Sc ", 0) == 0 && !one.GetFootswitchSlots().Active(), "a chain of one has no Switches");

        // A child parameter changed through the chain still reaches the child (the ids after Switches are shifted by one)
        auto *tapeSat = new TapeSatModule(); // owned by the chain
        EffectChain probe("Probe", {{"Sc", new ScreamerModule()}, {"Ts", tapeSat}}, {});
        probe.Init(48000.0f);
        const int firstTapeSat = 2 + 1 + ScreamerModule::PARAM_COUNT; // On x2, Switches, the Screamer's
        probe.SetParameterAsFloat(firstTapeSat + TapeSatModule::DRIVE, 0.25f);
        check(std::string(probe.GetParameterName(firstTapeSat)) == "Ts Drive" && tapeSat->GetParameterAsFloat(TapeSatModule::DRIVE) == 0.25f,
              "a child parameter set through the chain reaches that child (Ts Drive)");
    }

    std::printf("\n%s\n", fails ? "CHAIN SWITCHES TESTS FAILED" : "ALL CHAIN SWITCHES TESTS PASSED");
    return fails ? 1 : 0;
}
