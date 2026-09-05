#include "ctxr.h"

#include <windows.h>
#include <d3d11.h>

#include <cstring>
#include <fstream>

#include "common/log.h"

namespace qcamo {
namespace {

std::vector<uint8_t> read_file(const std::filesystem::path& path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return {};
    auto size = static_cast<std::streamsize>(file.tellg());
    std::vector<uint8_t> data(static_cast<size_t>(size));
    file.seekg(0);
    file.read(reinterpret_cast<char*>(data.data()), size);
    return file ? data : std::vector<uint8_t>{};
}

uint32_t read_be32(const uint8_t* at)
{
    return (uint32_t(at[0]) << 24) | (uint32_t(at[1]) << 16) | (uint32_t(at[2]) << 8) | at[3];
}

} // namespace

const std::filesystem::path& game_dir()
{
    static std::filesystem::path dir = [] {
        wchar_t buffer[MAX_PATH]{};
        GetModuleFileNameW(GetModuleHandleW(nullptr), buffer, MAX_PATH);
        return std::filesystem::path(buffer).parent_path();
    }();
    return dir;
}

// CTXR: "TXTR" magic, big-endian dimensions at 0x08, then a big-endian byte
// count followed by the top mip level as BGRA. Later mip levels use a different
// framing and are not read.
Image load_ctxr(const std::filesystem::path& relative)
{
    constexpr size_t kMip0 = 0x80;
    auto path = game_dir() / relative;
    auto file = read_file(path);
    if (file.size() < kMip0 + 4 || std::memcmp(file.data(), "TXTR", 4) != 0) {
        LOG_WARN("not a texture: %s", path.string().c_str());
        return {};
    }
    Image image{(file[8] << 8) | file[9], (file[10] << 8) | file[11], {}};
    uint32_t bytes = read_be32(file.data() + kMip0);
    if (image.width <= 0 || image.height <= 0 ||
        bytes != uint32_t(image.width) * image.height * 4 || file.size() < kMip0 + 4 + bytes) {
        LOG_WARN("unreadable texture: %s", path.string().c_str());
        return {};
    }
    auto* first = file.data() + kMip0 + 4;
    image.pixels.assign(first, first + bytes);
    return image;
}

ID3D11ShaderResourceView* create_texture(ID3D11Device* device, int width, int height,
                                         const std::vector<uint8_t>& bgra)
{
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width);
    desc.Height = static_cast<UINT>(height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA initial{bgra.data(), static_cast<UINT>(width * 4), 0};
    ID3D11Texture2D* texture{};
    if (FAILED(device->CreateTexture2D(&desc, &initial, &texture))) return nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC view{};
    view.Format = desc.Format;
    view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    view.Texture2D.MipLevels = 1;
    ID3D11ShaderResourceView* result{};
    device->CreateShaderResourceView(texture, &view, &result);
    texture->Release();
    return result;
}

} // namespace qcamo
