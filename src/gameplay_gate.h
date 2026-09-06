// Gameplay gate: the quick menu opens only during playable gameplay.
// No menus, no cutscenes, no Survival Viewer, no foreign pause states.
//
// Signals, all read-only and fail-closed:
// - Area code at stats + kAreaCode (7-char stage string). bbtracker treats
//   s*/v* as gameplay ("s001a", "v000a") and anything else ("title") as out.
//   This alone excludes title screens and other non-stage states.
// - Survival Viewer context slot: non-null pointer only while the Viewer
//   screen exists.
// - GV_PauseLevel: must carry no bits outside our own wheel bit. A set wheel
//   bit we did not set means a game wheel (or another pauser) owns it.
// - Player state flags: the same test the game's own wheel popups make before
//   they open. Cutscenes keep the stage's area code, so this is what actually
//   separates a scripted sequence from playable gameplay.

#pragma once

#include <cstdint>

#include "common/mem.h"
#include "game_mgs3.h"

namespace qcamo {

enum class GateBlock {
    None,
    Stats,
    Area,
    Viewer,
    Cutscene,
    Pause,
};

inline const char* gate_name(GateBlock block)
{
    switch (block) {
    case GateBlock::None: return "gameplay";
    case GateBlock::Stats: return "stats unavailable";
    case GateBlock::Area: return "non-gameplay area";
    case GateBlock::Viewer: return "survival viewer open";
    case GateBlock::Cutscene: return "cutscene or uncontrollable state";
    case GateBlock::Pause: return "game paused";
    }
    return "unknown";
}

// When allow_wheel is false the wheel bit itself blocks, which is what the
// opening edge wants: the game must be fully unpaused before the menu takes
// that bit. When true the menu's own wheel pause is tolerated, for the
// stay-open check, equips, and the gameplay-thread apply.
inline GateBlock gate_state(uintptr_t base, bool allow_wheel)
{
    if (!base || !mem::range_readable(base + mgs3::kStatsSlot, sizeof(uintptr_t))) {
        return GateBlock::Stats;
    }
    uintptr_t stats = mem::read<uintptr_t>(base + mgs3::kStatsSlot);
    if (!stats || !mem::range_readable(stats + mgs3::kAreaCode, mgs3::kAreaSize)) {
        return GateBlock::Stats;
    }
    char first = mem::read<char>(stats + mgs3::kAreaCode);
    if (first != 's' && first != 'v') {
        return GateBlock::Area;
    }
    if (!mem::range_readable(base + mgs3::kViewerSlot, sizeof(uintptr_t))) {
        return GateBlock::Viewer;
    }
    if (mem::read<uintptr_t>(base + mgs3::kViewerSlot) != 0) {
        return GateBlock::Viewer;
    }
    if (!mem::range_readable(base + mgs3::kPlayerFlagsA, sizeof(uint32_t)) ||
        !mem::range_readable(base + mgs3::kPlayerFlagsB, sizeof(uint32_t))) {
        return GateBlock::Cutscene;
    }
    uint32_t flags = mem::read<uint32_t>(base + mgs3::kPlayerFlagsA) |
                     mem::read<uint32_t>(base + mgs3::kPlayerFlagsB);
    if ((flags & mgs3::kNoPopupMask) != 0) {
        return GateBlock::Cutscene;
    }
    if (!mem::range_readable(base + mgs3::kPauseLevel, sizeof(uint32_t))) {
        return GateBlock::Pause;
    }
    uint32_t pause = mem::read<uint32_t>(base + mgs3::kPauseLevel);
    if ((pause & ~mgs3::kWheelPause) != 0) {
        return GateBlock::Pause;
    }
    if ((pause & mgs3::kWheelPause) != 0 && !allow_wheel) {
        return GateBlock::Pause;
    }
    return GateBlock::None;
}

inline bool can_open_menu(uintptr_t base)
{
    return gate_state(base, false) == GateBlock::None;
}

inline bool stay_open(uintptr_t base)
{
    return gate_state(base, true) == GateBlock::None;
}

} // namespace qcamo
