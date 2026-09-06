#include "camo_swatch.h"

#include <d3d11.h>

#include <array>
#include <string>
#include <vector>

#include "common/log.h"
#include "ctxr.h"

namespace qcamo {
namespace {

constexpr int kWidth = 64;
constexpr int kHeight = 32;
static_assert(kWidth == kHeight * kSwatchAspect);

// Survival Viewer camouflage icons, one per uniform id. They are seamless
// 128x128 tiles under textures/flatlist/_win, named by asset id rather than by
// camouflage, so the ids were matched to uniforms by comparing each tile with
// the body texture its camouflage slot loads. Banana ships blank in the stock
// build, so its icon is fully transparent and falls back to a plain chip.
constexpr std::array kIcons = {
    "00ad439e", // olive drab
    "003e43b2", // tiger stripe
    "000a4397", // leaf
    "005c43b3", // tree bark
    "0026436e", // choco chip
    "002343af", // splitter
    "004043a9", // raindrop
    "004c43af", // squares
    "004b43bd", // water
    "00b71656", // black
    "0007925f", // snow
    "007b439a", // naked
    "005ceaf7", // sneaking suit
    "008043ad", // scientist
    "00da439d", // officer
    "00a54397", // maintenance
    "001b43b4", // tuxedo
    "00054383", // hornet stripe
    "002043af", // spider
    "00e643ae", // moss
    "0049437a", // fire
    "00b79656", // spirit
    "00fe3dac", // cold war
    "00d843ae", // snake
    "003a437d", // ga-ko
    "00184159", // desert tiger
    "0024436b", // dpm
    "00a2437a", // flecktarn
    "00b84391", // auscam
    "00030da5", // animals
    "00ca4367", // fly
    "00040da5", // banana
    "00050da5", // download
};

// Face paint icons, read straight out of the game's own table of them at RVA
// 0x8ED380: twenty-three ids in equipped-face order. They are 128x64 tiles in
// the same flatlist as the uniform icons.
constexpr std::array kFaceIcons = {
    "000a365b", // no paint
    "0042367f", // woodland
    "00e9362a", // black
    "0092367d", // water
    "00113632", // mountain
    "006a366f", // splitter
    "002d366f", // snow
    "0080364d", // kabuki
    "0000368b", // zombie
    "008b3660", // oyama
    "00c93657", // mask
    "00ac363f", // green
    "00ac362b", // brown
    "004c3656", // infinity
    "0011366c", // soviet union
    "008dab1c", // united kingdom
    "00a3363b", // france
    "0010363e", // germany
    "00df3647", // italy
    "005f366f", // spain
    "00433670", // sweden
    "006c364b", // japan
    "00bf3677", // usa
};

std::array<ID3D11ShaderResourceView*, kIcons.size()> views;
std::array<bool, kIcons.size()> resolved;
std::array<ID3D11ShaderResourceView*, kFaceIcons.size()> face_views;
std::array<bool, kFaceIcons.size()> face_resolved;

ID3D11ShaderResourceView* load(ID3D11Device* device, const char* icon, bool tiling)
{
    Image image = load_ctxr(std::filesystem::path("textures") / "flatlist" / "_win" /
                            (std::string(icon) + ".ctxr"));
    if (!image) return nullptr;
    // Uniform icons tile seamlessly, so a centred band of the right shape shows
    // the pattern without stretching it. Face icons are portraits already at
    // the swatch shape, so they are taken whole.
    int band = tiling ? image.height * kHeight / kWidth : image.height;
    int top = (image.height - band) / 2;
    std::vector<uint8_t> swatch(size_t(kWidth) * kHeight * 4);
    bool visible = false;
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const uint8_t* source = image.at(x * image.width / kWidth, top + y * band / kHeight);
            uint8_t* target = swatch.data() + (size_t(y) * kWidth + x) * 4;
            target[0] = source[0];
            target[1] = source[1];
            target[2] = source[2];
            target[3] = 255;
            visible = visible || source[3] != 0;
        }
    }
    if (!visible) {
        LOG_INFO("camouflage icon %s is blank", icon);
        return nullptr;
    }
    return create_texture(device, kWidth, kHeight, swatch);
}

} // namespace

ID3D11ShaderResourceView* camo_swatch(ID3D11Device* device, uint8_t uniform)
{
    if (uniform >= kIcons.size()) return nullptr;
    if (!resolved[uniform]) {
        resolved[uniform] = true;
        views[uniform] = load(device, kIcons[uniform], true);
    }
    return views[uniform];
}

ID3D11ShaderResourceView* face_swatch(ID3D11Device* device, uint8_t face)
{
    if (face >= kFaceIcons.size()) return nullptr;
    if (!face_resolved[face]) {
        face_resolved[face] = true;
        face_views[face] = load(device, kFaceIcons[face], false);
    }
    return face_views[face];
}

} // namespace qcamo
