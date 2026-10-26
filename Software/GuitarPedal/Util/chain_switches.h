#pragma once
// The "Switches" parameter of an effect chain (EffectChain, read by guitar_pedal.cpp through GetFootswitchSlots): the two footswitches can
// switch two of the chain's slots on and off instead of working as usual (the right one bypassing the whole effect, the left one the effect's
// own alternate function). The right footswitch gets the lower slot. The choice of slots is here so that it can be tested on the PC.
// Any two slots of a chain of any size can be chosen: the modes are Normal and then every pair of slots, named left to right, as the
// footswitches are on the pedal: "3+1" has slot 3 on the left (alternate) footswitch and slot 1 on the right (bypass) one. The pairs are
// ordered by the left slot and then the right one (2+1, 3+1, 3+2, 4+1, 4+2, 4+3, 5+1, ...), so a mode keeps its number when the chain gets
// more slots, and a chain of n slots has 1 + n(n-1)/2 modes.
#include <cstddef>
#include <cstdio>

namespace chainswitches {

enum Mode { NORMAL = 0, SLOTS_2_1, SLOTS_3_1, SLOTS_3_2 }; // the first modes (the others follow in the order above)

/** The number of modes of a chain with slotCount slots: Normal, and one for every pair of slots (a chain of one has only Normal). */
inline int ModeCount(int slotCount) { return slotCount >= 2 ? 1 + slotCount * (slotCount - 1) / 2 : 1; }

/** The slots the two footswitches switch (0-based), or -1 for both when the footswitches work as usual. */
struct Assignment {
    int bypassSlot = -1;    // the right (bypass) footswitch
    int alternateSlot = -1; // the left (alternate) footswitch
    bool Active() const { return bypassSlot >= 0; }
};

/** The assignment for a mode and an effect with slotCount slots (0 for an effect that is not a chain). A mode that needs a slot the effect does
 *  not have, an unknown mode and a plain effect give the usual behaviour. */
inline Assignment Resolve(int mode, int slotCount) {
    Assignment a;
    if (mode < 1 || slotCount < 2) return a;
    int left = 1;   // the left slot of the pairs being counted
    int before = 0; // the pairs with a lower left slot
    while (mode - 1 >= before + left) {
        before += left;
        left++;
    }
    if (left >= slotCount) return a;
    a.bypassSlot = mode - 1 - before;
    a.alternateSlot = left;
    return a;
}

/** The name of a mode for the menu: "Normal", or "3+1" (the left footswitch's slot first, the slots counted from 1). */
inline void ModeName(int mode, char *buffer, size_t size) {
    const Assignment a = Resolve(mode, 1 << 20); // (any number of slots: the pair of the mode)
    if (mode < 1 || !a.Active()) {
        std::snprintf(buffer, size, "Normal");
    } else {
        std::snprintf(buffer, size, "%d+%d", a.alternateSlot + 1, a.bypassSlot + 1);
    }
}

} // namespace chainswitches
