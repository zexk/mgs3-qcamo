#pragma once

#include <cstdint>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace qcamo {

// Swatch textures are twice as wide as they are tall; draw them at that ratio
// or the camouflage pattern comes out stretched.
inline constexpr float kSwatchAspect = 2.0f;

// Camouflage swatch cut from the uniform texture the game itself loads for that
// uniform. Read once per uniform and cached; returns null when the texture is
// missing or unreadable.
ID3D11ShaderResourceView* camo_swatch(ID3D11Device* device, uint8_t uniform);
ID3D11ShaderResourceView* face_swatch(ID3D11Device* device, uint8_t face);

} // namespace qcamo
