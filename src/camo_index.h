#pragma once

#include <cstdint>

namespace qcamo {

// Camouflage as the game computes it. The HUD index is a sum of terms: the
// uniform's value for the surface Snake is on, the face paint's value for the
// same surface, a movement penalty and a light term. Only the first changes
// with the uniform, so ranking uniforms and differencing them is exact even
// though the other terms are not modelled here.

// Index into a uniform's value table: terrain * 5 + posture. Negative when the
// surface underfoot has no camouflage data, which is also when the HUD shows
// no index.
int camo_slot(uintptr_t base);

// That uniform's own contribution at `slot`, in tenths of a percent, the same
// units the HUD index uses.
int camo_value(uintptr_t base, int slot, uint8_t uniform);

// The face paint's own contribution, in the same units. Face tables carry one
// value per terrain rather than one per posture, so they take the terrain the
// slot names rather than the slot itself.
int face_value(uintptr_t base, int slot, uint8_t face);

} // namespace qcamo
