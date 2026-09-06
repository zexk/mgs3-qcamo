#pragma once

#include <atomic>
#include <cstdint>

namespace qcamo {

// Render-thread menu state, consumed by the pause state machine.
extern std::atomic_bool menu_open;

using QueueUniform = bool (*)(uint8_t uniform);

bool start_overlay(uintptr_t image_base, QueueUniform queue_uniform);

} // namespace qcamo
