#pragma once

#include <atomic>
#include <cstdint>

namespace qcamo {

// Render-thread menu state, consumed by the pause state machine.
extern std::atomic_bool menu_open;

// UI sound cue the menu wants played, set on the render thread and drained by
// the gameplay thread, which is the only one that may call into the game. One
// slot is enough: a frame never owes more than one menu sound.
extern std::atomic_int pending_sound;

// Equips are always a pair: the menu offers a uniform together with the face
// paint that scores best where Snake stands.
using QueueUniform = bool (*)(uint8_t uniform, uint8_t face);

// Retry inventory discovery from the worker thread, never the render hook.
void refresh_inventory();
bool start_overlay(uintptr_t image_base, QueueUniform queue_uniform);

} // namespace qcamo
