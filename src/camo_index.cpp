#include "camo_index.h"

#include "common/mem.h"
#include "game_mgs3.h"

namespace qcamo {
namespace {

bool state_bit(uintptr_t base, int bit)
{
    auto word = mem::read<uint32_t>(base + mgs3::kPlayerState + (bit / 32) * 4);
    return (word >> (bit % 32)) & 1;
}

// Surface material ids are sparse, so the game maps them to dense terrain
// indices through a small table, taking an entry with a zero key as the
// fallback. This is 0xA88F0 minus its miss path, which calls back into the
// game; a miss here just means no camouflage data, which is what the caller
// wants anyway.
int terrain_of(uintptr_t base, int material)
{
    int count = mem::read<int>(base + mgs3::kTerrainMapCount);
    int fallback = -1;
    for (int i = 0; i < count; ++i) {
        auto entry = base + mgs3::kTerrainMap + i * mgs3::kTerrainMapStride;
        int key = mem::read<int>(entry);
        if (key == material) return mem::read<uint16_t>(entry + 4);
        if (key == 0) fallback = mem::read<uint16_t>(entry + 4);
    }
    return fallback;
}

} // namespace

int camo_slot(uintptr_t base)
{
    // Hanging off a wall reads the wall's material and has its own two
    // postures; otherwise it is the ground underfoot, standing, crouched or
    // prone. Same order of tests as 0xA8070.
    if (state_bit(base, mgs3::kStateOnWall)) {
        int material = mem::read<int>(base + mgs3::kWallMaterial);
        int terrain = material < 0 ? -1 : terrain_of(base, material);
        if (terrain < 0) return -1;
        return terrain * mgs3::kCamoPostures + (state_bit(base, mgs3::kStateCrouch) ? 4 : 3);
    }
    int terrain = terrain_of(base, mem::read<int>(base + mgs3::kGroundMaterial));
    if (terrain < 0) return -1;
    int posture = state_bit(base, mgs3::kStateProne) && !state_bit(base, mgs3::kStateProneOverride)
                      ? 2
                  : state_bit(base, mgs3::kStateCrouch) ? 1
                                                        : 0;
    return terrain * mgs3::kCamoPostures + posture;
}

int camo_value(uintptr_t base, int slot, uint8_t uniform)
{
    if (slot < 0 || slot >= mgs3::kCamoValues) return 0;
    auto record = base + mgs3::kUniformCamo + uniform * mgs3::kCamoRecordStride;
    auto values = mem::read<uintptr_t>(record + mgs3::kCamoRecordValues);
    if (!values || !mem::range_readable(values + slot, 1)) return 0;
    return mem::read<int8_t>(values + slot) * 10;
}

int face_value(uintptr_t base, int slot, uint8_t face)
{
    if (slot < 0) return 0;
    int terrain = slot / mgs3::kCamoPostures;
    auto record = base + mgs3::kFaceCamo + face * mgs3::kCamoRecordStride;
    auto values = mem::read<uintptr_t>(record + mgs3::kCamoRecordValues);
    if (!values || !mem::range_readable(values + terrain, 1)) return 0;
    return mem::read<int8_t>(values + terrain) * 10;
}

int camo_index(uintptr_t base)
{
    return mem::read<int>(base + mgs3::kCamoIndex);
}

} // namespace qcamo
