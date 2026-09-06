#pragma once

#include <atomic>
#include <cstdint>

namespace qcamo {

// Render-thread menu state, consumed by the pause state machine.
extern std::atomic_bool menu_open;

// Equips are always a pair: the menu offers a uniform together with the face
// paint that scores best where Snake stands.
using QueueUniform = bool (*)(uint8_t uniform, uint8_t face);

bool start_overlay(uintptr_t image_base, QueueUniform queue_uniform);

} // namespace qcamo
