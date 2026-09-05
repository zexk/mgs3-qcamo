#include "overlay.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <MinHook.h>
#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

#include "camo_swatch.h"
#include "common/log.h"
#include "hud_font.h"
#include "common/mem.h"
#include "game_mgs3.h"

namespace qcamo {
namespace {

using Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using ResizeBuffers = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT, UINT,
                                                  DXGI_FORMAT, UINT);

Present original_present;
ResizeBuffers original_resize;
ID3D11Device* device;
ID3D11DeviceContext* context;
ID3D11RenderTargetView* render_target;
uintptr_t base;
uintptr_t inventory;
QueueUniform queue_uniform;
bool ready;
bool open;
int selected;

// Sampled from the game's own Survival Viewer and equipment HUD.
constexpr ImU32 kPanel = IM_COL32(10, 10, 7, 205);
constexpr ImU32 kFrame = IM_COL32(67, 67, 53, 220);
constexpr ImU32 kHeaderText = IM_COL32(149, 149, 123, 255);
constexpr ImU32 kRow = IM_COL32(67, 67, 53, 235);
constexpr ImU32 kRowSelected = IM_COL32(168, 168, 140, 250);
constexpr ImU32 kRowWorn = IM_COL32(10, 10, 7, 235);
constexpr ImU32 kRowText = IM_COL32(20, 20, 15, 255);
constexpr ImU32 kRowWornText = IM_COL32(103, 103, 82, 255);
constexpr ImU32 kHint = IM_COL32(110, 110, 94, 235);

constexpr std::array kUniformNames = {
    "OLIVE DRAB", "TIGER STRIPE", "LEAF", "TREE BARK", "CHOCO CHIP",
    "SPLITTER", "RAINDROP", "SQUARES", "WATER", "BLACK", "SNOW", "NAKED",
    "SNEAKING SUIT", "SCIENTIST", "OFFICER", "MAINTENANCE", "TUXEDO",
    "HORNET STRIPE", "SPIDER", "MOSS", "FIRE", "SPIRIT", "COLD WAR", "SNAKE",
    "GA-KO", "DESERT TIGER", "DPM", "FLECKTARN", "AUSCAM", "ANIMALS", "FLY",
    "BANANA", "DOWNLOADED",
};

bool pressed(int key)
{
    static bool held[256]{};
    bool down = (GetAsyncKeyState(key) & 0x8000) != 0;
    bool result = down && !held[key];
    held[key] = down;
    return result;
}

constexpr int wrapped(int selected, int count, int step)
{
    return (selected + count + step) % count;
}

static_assert(wrapped(0, 3, -1) == 2 && wrapped(2, 3, 1) == 0);

uintptr_t find_inventory()
{
    constexpr uint8_t signature[] = {0x00, 0x00, 0xDA, 0x5A, 0x2B, 0x00};
    uintptr_t address = base + 0x1C00000;
    uintptr_t end = address + 0x300000;
    uintptr_t found = 0;
    while (address < end) {
        MEMORY_BASIC_INFORMATION region{};
        if (!VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region))) break;
        uintptr_t region_end = std::min(end, reinterpret_cast<uintptr_t>(region.BaseAddress) +
                                                region.RegionSize);
        if (mem::range_readable(address, region_end - address)) {
            auto first = reinterpret_cast<const uint8_t*>(address);
            auto last = reinterpret_cast<const uint8_t*>(region_end);
            for (auto hit = std::search(first, last, std::begin(signature), std::end(signature));
                 hit != last;
                 hit = std::search(hit + 1, last, std::begin(signature), std::end(signature))) {
                if (found) return 0;
                found = reinterpret_cast<uintptr_t>(hit + sizeof(signature) + 12);
            }
        }
        address = region_end;
    }
    return found;
}

std::vector<uint8_t> owned_uniforms()
{
    static uint64_t retry_at;
    uint64_t now = GetTickCount64();
    if (!inventory && now >= retry_at) {
        inventory = find_inventory();
        retry_at = now + 2000;
        if (inventory) LOG_INFO("inventory table found after startup");
    }
    std::vector<uint8_t> result;
    if (inventory) {
        for (uint8_t id = 0; id < kUniformNames.size(); ++id) {
            auto entry = inventory + (40 + id) * 80;
            if (mem::range_readable(entry, sizeof(int16_t)) && mem::read<int16_t>(entry) >= 1) {
                result.push_back(id);
            }
        }
    }
    if (result.empty()) result = {0, 1};
    return result;
}

uint8_t equipped_uniform()
{
    auto stats = mem::read<uintptr_t>(base + mgs3::kStatsSlot);
    return stats ? mem::read<uint8_t>(stats + mgs3::kEquippedUniform) : 0;
}

void poll_menu(const std::vector<uint8_t>& uniforms)
{
    if (pressed(VK_F7)) {
        open = !open;
        LOG_INFO("menu %s", open ? "opened" : "closed");
        if (open) {
            auto it = std::find(uniforms.begin(), uniforms.end(), equipped_uniform());
            selected = it == uniforms.end() ? 0 : static_cast<int>(it - uniforms.begin());
        }
    }
    if (!open || uniforms.empty()) return;
    if (pressed(VK_ESCAPE)) {
        open = false;
        return;
    }
    int count = static_cast<int>(uniforms.size());
    if (pressed(VK_UP)) {
        selected = wrapped(selected, count, -1);
        LOG_INFO("menu selected uniform %u", uniforms[selected]);
    }
    if (pressed(VK_DOWN)) {
        selected = wrapped(selected, count, 1);
        LOG_INFO("menu selected uniform %u", uniforms[selected]);
    }
    if (pressed(VK_RETURN)) {
        if (queue_uniform(uniforms[selected])) open = false;
    }
}

void draw_menu(const std::vector<uint8_t>& uniforms)
{
    if (!open) return;
    ImGuiIO& io = ImGui::GetIO();
    float scale = std::max(0.75f, std::min(io.DisplaySize.x / 1920.0f,
                                          io.DisplaySize.y / 1080.0f));
    ImDrawList* draw = ImGui::GetForegroundDrawList();
    bool hud = hud_font_ready(device);
    auto text = [draw, hud](ImVec2 position, float height, ImU32 color, const char* value) {
        if (hud) {
            hud_text(draw, position, height, color, value);
        } else {
            draw->AddText(nullptr, height, position, color, value);
        }
    };

    float pad = 6 * scale;
    float header_height = 20 * scale;
    float hint_height = 13 * scale;
    float row_height = 34 * scale;
    float width = 330 * scale;
    int count = static_cast<int>(uniforms.size());
    int visible = std::min(count, 8);
    float height = 4 * pad + header_height + visible * row_height + hint_height;
    float x = io.DisplaySize.x - width - 46 * scale;
    float y = (io.DisplaySize.y - height) * 0.5f;
    float inner = x + pad;
    float inner_width = width - 2 * pad;

    draw->AddRectFilled({x, y}, {x + width, y + height}, kPanel);
    draw->AddRect({x, y}, {x + width, y + height}, kFrame, 0.0f, 0, 2 * scale);
    text({inner, y + pad}, header_height, kHeaderText, "CAMOUFLAGE");

    float top = y + 2 * pad + header_height;
    int first = std::clamp(selected - 3, 0, count - visible);
    uint8_t equipped = equipped_uniform();
    for (int row = 0; row < visible; ++row) {
        int index = first + row;
        uint8_t id = uniforms[index];
        bool active = index == selected;
        // The HUD inverts the row Snake is wearing: olive on black rather than
        // black on olive, dim normally and bright under the cursor.
        bool worn = id == equipped;
        ImU32 worn_ink = active ? kRowSelected : kRowWornText;
        ImVec2 row_min{inner, top + row * row_height};
        ImVec2 row_max{inner + inner_width, row_min.y + row_height - 3 * scale};
        draw->AddRectFilled(row_min, row_max,
                            worn ? kRowWorn : active ? kRowSelected : kRow);
        if (worn) draw->AddRect(row_min, row_max, worn_ink, 0.0f, 0, scale);
        float patch_height = row_max.y - row_min.y - 8 * scale;
        ImVec2 patch_min{row_min.x + 5 * scale, row_min.y + 4 * scale};
        ImVec2 patch_max{patch_min.x + patch_height * kSwatchAspect,
                         patch_min.y + patch_height};
        if (auto* texture = camo_swatch(device, id)) {
            draw->AddImage(reinterpret_cast<ImTextureID>(texture), patch_min, patch_max);
        } else {
            draw->AddRectFilled(patch_min, patch_max,
                                IM_COL32(70 + (id * 37) % 100, 66 + (id * 19) % 85,
                                         42 + (id * 29) % 70, 255));
        }
        float label_height = 17 * scale;
        text({patch_max.x + 10 * scale,
              row_min.y + (row_max.y - row_min.y - label_height) * 0.5f},
             label_height, worn ? worn_ink : kRowText, kUniformNames[id]);
    }
    float hint_y = top + visible * row_height + pad;
    if (!hud) {
        text({inner, hint_y}, hint_height, kHint, "UP/DOWN SELECT   ENTER EQUIP   ESC CLOSE");
        return;
    }
    float cursor = inner;
    auto label = [&](const char* value) {
        text({cursor, hint_y}, hint_height, kHint, value);
        cursor += hud_text_width(value, hint_height) + 11 * scale;
    };
    hud_dpad(draw, {cursor, hint_y}, hint_height, kHint);
    cursor += hint_height + 4 * scale;
    label("SELECT");
    if (auto* icon = hud_button(device, HudButton::Cross)) {
        // The button art carries its own padding, so it reads right a little
        // larger than the cap height beside it.
        float glyph = hint_height * 1.8f;
        float lift = (glyph - hint_height) * 0.5f;
        draw->AddImage(reinterpret_cast<ImTextureID>(icon), {cursor, hint_y - lift},
                       {cursor + glyph, hint_y + hint_height + lift}, {0, 0}, {1, 1}, kHint);
        cursor += glyph + 2 * scale;
    }
    label("EQUIP");
    hud_tile(draw, {cursor, hint_y}, hint_height, "L1");
    cursor += hud_tile_width("L1", hint_height) + 4 * scale;
    label("CLOSE");
}

bool create_render_target(IDXGISwapChain* swap_chain)
{
    ID3D11Texture2D* buffer{};
    if (FAILED(swap_chain->GetBuffer(0, IID_PPV_ARGS(&buffer)))) return false;
    HRESULT result = device->CreateRenderTargetView(buffer, nullptr, &render_target);
    buffer->Release();
    return SUCCEEDED(result);
}

bool init_renderer(IDXGISwapChain* swap_chain)
{
    if (FAILED(swap_chain->GetDevice(IID_PPV_ARGS(&device)))) return false;
    device->GetImmediateContext(&context);
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(swap_chain->GetDesc(&desc)) || !create_render_target(swap_chain)) return false;
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse | ImGuiConfigFlags_NoMouseCursorChange;
    ImGui_ImplWin32_Init(desc.OutputWindow);
    ImGui_ImplDX11_Init(device, context);
    ready = true;
    LOG_INFO("quick menu renderer ready");
    return true;
}

HRESULT STDMETHODCALLTYPE present_hook(IDXGISwapChain* swap_chain, UINT interval, UINT flags)
{
    if (!ready && !init_renderer(swap_chain)) return original_present(swap_chain, interval, flags);
    auto uniforms = owned_uniforms();
    poll_menu(uniforms);
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    draw_menu(uniforms);
    ImGui::Render();
    context->OMSetRenderTargets(1, &render_target, nullptr);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    return original_present(swap_chain, interval, flags);
}

HRESULT STDMETHODCALLTYPE resize_hook(IDXGISwapChain* swap_chain, UINT count, UINT width,
                                     UINT height, DXGI_FORMAT format, UINT flags)
{
    if (ready) {
        ImGui_ImplDX11_InvalidateDeviceObjects();
        if (render_target) render_target->Release();
        render_target = nullptr;
    }
    HRESULT result = original_resize(swap_chain, count, width, height, format, flags);
    if (SUCCEEDED(result) && ready) create_render_target(swap_chain);
    return result;
}

bool install_hooks()
{
    WNDCLASSEXA window_class{sizeof(window_class)};
    window_class.lpfnWndProc = DefWindowProcA;
    window_class.hInstance = GetModuleHandleW(nullptr);
    window_class.lpszClassName = "QCamoDummyWindow";
    if (!RegisterClassExA(&window_class)) return false;
    HWND window = CreateWindowExA(0, window_class.lpszClassName, "", WS_OVERLAPPEDWINDOW,
                                  0, 0, 100, 100, nullptr, nullptr,
                                  window_class.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = window;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* swap_chain{};
    ID3D11Device* dummy_device{};
    ID3D11DeviceContext* dummy_context{};
    D3D_FEATURE_LEVEL level{};
    HRESULT result = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION,
        &desc, &swap_chain, &dummy_device, &level, &dummy_context);
    bool ok = SUCCEEDED(result);
    if (ok) {
        void** vtable = *reinterpret_cast<void***>(swap_chain);
        ok = MH_CreateHook(vtable[8], reinterpret_cast<void*>(&present_hook),
                           reinterpret_cast<void**>(&original_present)) == MH_OK &&
             MH_CreateHook(vtable[13], reinterpret_cast<void*>(&resize_hook),
                           reinterpret_cast<void**>(&original_resize)) == MH_OK &&
             MH_EnableHook(vtable[8]) == MH_OK && MH_EnableHook(vtable[13]) == MH_OK;
    }
    if (swap_chain) swap_chain->Release();
    if (dummy_device) dummy_device->Release();
    if (dummy_context) dummy_context->Release();
    if (window) DestroyWindow(window);
    UnregisterClassA(window_class.lpszClassName, window_class.hInstance);
    return ok;
}

} // namespace

bool start_overlay(uintptr_t image_base, QueueUniform callback)
{
    base = image_base;
    queue_uniform = callback;
    inventory = find_inventory();
    LOG_INFO("inventory table %s", inventory ? "found" : "not found; using POC uniforms");
    return install_hooks();
}

} // namespace qcamo
