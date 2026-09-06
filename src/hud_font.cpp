#include "hud_font.h"

#include <d3d11.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

#include "common/log.h"
#include "ctxr.h"

namespace qcamo {
namespace {

constexpr int kColumns = 32;
constexpr int kRows = 5;
constexpr char kFirstGlyph = ' ';

// Advances, as a fraction of the cell height: the gap left after each glyph and
// the width of a space, both eyeballed against the game's own HUD spacing.
constexpr float kGap = 0.07f;
constexpr float kSpace = 0.26f;

// Sampled from the game's own R1 prompt tile.
constexpr ImU32 kTile = IM_COL32(77, 77, 67, 255);
constexpr ImU32 kTileInk = IM_COL32(160, 159, 138, 255);

struct Glyph {
    float u0, v0, u1, v1;
    float width; // in cell heights, zero for blank cells
};

std::array<Glyph, kColumns * kRows> glyphs;
ID3D11ShaderResourceView* atlas;
bool attempted;

// Glyph shapes live in alpha at the PS2 half scale where 0x80 is opaque.
// Flatten to white so a draw colour tints cleanly.
void whiten(Image& image)
{
    for (size_t i = 0; i < image.pixels.size(); i += 4) {
        image.pixels[i] = image.pixels[i + 1] = image.pixels[i + 2] = 255;
        image.pixels[i + 3] = static_cast<uint8_t>(std::min(255, image.pixels[i + 3] * 2));
    }
}

const Glyph& glyph_for(char value)
{
    int index = static_cast<unsigned char>(value) - kFirstGlyph;
    if (index < 0 || index >= static_cast<int>(glyphs.size())) index = 0;
    return glyphs[index];
}

} // namespace

bool hud_font_ready(ID3D11Device* device)
{
    if (attempted) return atlas != nullptr;
    attempted = true;
    Image image = load_ctxr("Misc/Layoutfont/_win/layoutfont.ctxr");
    if (!image) return false;
    float cell_width = static_cast<float>(image.width) / kColumns;
    float cell_height = static_cast<float>(image.height) / kRows;
    for (int row = 0; row < kRows; ++row) {
        for (int column = 0; column < kColumns; ++column) {
            int left = static_cast<int>(column * cell_width);
            int right = static_cast<int>((column + 1) * cell_width);
            int top = static_cast<int>(row * cell_height);
            int bottom = static_cast<int>((row + 1) * cell_height);
            int ink_left = right;
            int ink_right = left - 1;
            for (int y = top; y < bottom; ++y) {
                for (int x = left; x < right; ++x) {
                    if (image.at(x, y)[3] <= 16) continue;
                    ink_left = std::min(ink_left, x);
                    ink_right = std::max(ink_right, x);
                }
            }
            Glyph& glyph = glyphs[row * kColumns + column];
            if (ink_right < ink_left) continue;
            glyph.u0 = ink_left / static_cast<float>(image.width);
            glyph.u1 = (ink_right + 1) / static_cast<float>(image.width);
            glyph.v0 = top / static_cast<float>(image.height);
            glyph.v1 = bottom / static_cast<float>(image.height);
            glyph.width = (ink_right + 1 - ink_left) / cell_height;
        }
    }
    whiten(image);
    atlas = create_texture(device, image.width, image.height, image.pixels);
    LOG_INFO("hud font %s", atlas ? "ready" : "failed");
    return atlas != nullptr;
}

ID3D11ShaderResourceView* hud_button(ID3D11Device* device, HudButton button)
{
    static const std::array<std::filesystem::path, 4> kPaths{
        std::filesystem::path("textures/flatlist/_win/004a230b.ctxr"),
        std::filesystem::path("textures/flatlist/_win/0053d847.ctxr"),
        std::filesystem::path("textures/flatlist/ovr_stm/ctrltype_ps4/ovr_jp/_win/00ddd547.ctxr"),
        std::filesystem::path("textures/flatlist/_win/00e9d866.ctxr"),
    };
    static std::array<ID3D11ShaderResourceView*, 4> views;
    static std::array<bool, 4> resolved;
    auto index = static_cast<size_t>(button);
    if (!resolved[index]) {
        resolved[index] = true;
        Image image = load_ctxr(kPaths[index]);
        if (image) {
            whiten(image);
            views[index] = create_texture(device, image.width, image.height, image.pixels);
        }
    }
    return views[index];
}

float hud_text_width(const char* text, float height)
{
    float width = 0;
    for (; *text; ++text) {
        const Glyph& glyph = glyph_for(*text);
        width += glyph.width > 0 ? glyph.width * height + kGap * height : kSpace * height;
    }
    return width;
}

float hud_tile_width(const char* label, float height)
{
    return hud_text_width(label, height) + height;
}

void hud_tile(ImDrawList* draw, ImVec2 position, float height, const char* label)
{
    float lift = height * 0.3f;
    draw->AddRectFilled({position.x, position.y - lift},
                        {position.x + hud_tile_width(label, height), position.y + height + lift},
                        kTile);
    hud_text(draw, {position.x + height * 0.5f, position.y}, height, kTileInk, label);
}

void hud_dpad(ImDrawList* draw, ImVec2 position, float height, ImU32 color)
{
    float arm = height / 3.0f;
    ImU32 idle = (color & 0x00FFFFFF) | 0x66000000;
    draw->AddRectFilled({position.x, position.y + arm},
                        {position.x + height, position.y + 2 * arm}, idle);
    draw->AddRectFilled({position.x + arm, position.y + arm},
                        {position.x + 2 * arm, position.y + 2 * arm}, idle);
    draw->AddRectFilled({position.x + arm, position.y},
                        {position.x + 2 * arm, position.y + arm}, color);
    draw->AddRectFilled({position.x + arm, position.y + 2 * arm},
                        {position.x + 2 * arm, position.y + height}, color);
}

void hud_text(ImDrawList* draw, ImVec2 position, float height, ImU32 color, const char* text)
{
    for (; *text; ++text) {
        const Glyph& glyph = glyph_for(*text);
        if (glyph.width <= 0) {
            position.x += kSpace * height;
            continue;
        }
        float width = glyph.width * height;
        ImVec2 min{std::round(position.x), std::round(position.y)};
        ImVec2 max{std::round(position.x + width), std::round(position.y + height)};
        draw->AddImage(reinterpret_cast<ImTextureID>(atlas), min, max,
                       {glyph.u0, glyph.v0}, {glyph.u1, glyph.v1}, color);
        position.x += width + kGap * height;
    }
}

} // namespace qcamo
