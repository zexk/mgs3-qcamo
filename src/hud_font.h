#pragma once

#include <imgui.h>

struct ID3D11Device;
struct ID3D11ShaderResourceView;

namespace qcamo {

// PlayStation face buttons, from the prompt art the game swaps per controller
// type. The unqualified textures are the original PS2 art; only the circle has
// to come from an override set, because western builds never show it.
enum class HudButton { Cross, Triangle, Circle, Square };

ID3D11ShaderResourceView* hud_button(ID3D11Device* device, HudButton button);

// MGS3 ships no art for a shoulder-button prompt other than R1, and none at all
// for the D-pad, so these two are drawn in the style of that R1 tile.
float hud_tile_width(const char* label, float height);
void hud_tile(ImDrawList* draw, ImVec2 position, float height, const char* label);
void hud_dpad(ImDrawList* draw, ImVec2 position, float height, ImU32 color);

// The game's own HUD typeface, read from its bitmap atlas. Text is drawn one
// glyph quad at a time; `height` is the cell height in pixels, which is what
// the atlas is authored against.
bool hud_font_ready(ID3D11Device* device);
float hud_text_width(const char* text, float height);
void hud_text(ImDrawList* draw, ImVec2 position, float height, ImU32 color, const char* text);

} // namespace qcamo
