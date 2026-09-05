#pragma once

#include <cstdint>

namespace qcamo {

using QueueUniform = bool (*)(uint8_t uniform);

bool start_overlay(uintptr_t image_base, QueueUniform queue_uniform);

} // namespace qcamo
