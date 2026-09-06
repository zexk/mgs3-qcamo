#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace qcamo {

// Master Collection texture: BGRA pixels of the top mip level.
struct Image {
    int width{};
    int height{};
    std::vector<uint8_t> pixels;

    explicit operator bool() const { return width > 0; }
    const uint8_t* at(int x, int y) const { return pixels.data() + (size_t(y) * width + x) * 4; }
};

Image load_ctxr(const std::filesystem::path& relative);

ID3D11ShaderResourceView* create_texture(ID3D11Device* device, int width, int height,
                                         const std::vector<uint8_t>& bgra);

} // namespace qcamo
