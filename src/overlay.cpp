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
#include <cstring>
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

// The pad reaches this process only through Steam Input: the game imports no
// input API, and XInput, winmm and DirectInput all enumerate nothing while
// Steam holds the device. The game drives Steam Input through the C++
// interface, so its action names are the contract here; they were read back
// with GetStringForDigitalActionName off the handles it polls.
struct Pad {
    bool present;
    bool shoulder; // L1
    bool open_button; // triangle
    bool equip; // cross
    bool up;
    bool down;
};

// GetDigitalActionData returns two bytes, state then active, packed into the
// flat wrapper's return value.
using DigitalDataFn = uint16_t (*)(void*, uint64_t, uint64_t);
using ActionHandleFn = uint64_t (*)(void*, const char*);
using ConnectedFn = int (*)(void*, uint64_t*);

struct SteamPad {
    void* self;
    DigitalDataFn data;
    uint64_t controller;
    uint64_t triangle, cross, shoulder, up, down;
};

SteamPad steam_pad;

bool open_pad()
{
    if (steam_pad.controller) return true;
    static uint64_t retry_at;
    uint64_t now = GetTickCount64();
    if (now < retry_at) return false;
    retry_at = now + 2000;

    HMODULE steam = GetModuleHandleW(L"steam_api64.dll");
    if (!steam) return false;
    auto accessor = reinterpret_cast<void* (*)()>(
        GetProcAddress(steam, "SteamAPI_SteamInput_v006"));
    auto connected = reinterpret_cast<ConnectedFn>(
        GetProcAddress(steam, "SteamAPI_ISteamInput_GetConnectedControllers"));
    auto name_of = reinterpret_cast<const char* (*)(void*, uint64_t)>(
        GetProcAddress(steam, "SteamAPI_ISteamInput_GetStringForDigitalActionName"));
    steam_pad.data = reinterpret_cast<DigitalDataFn>(
        GetProcAddress(steam, "SteamAPI_ISteamInput_GetDigitalActionData"));
    steam_pad.self = accessor ? accessor() : nullptr;
    if (!steam_pad.self || !connected || !name_of || !steam_pad.data) return false;

    uint64_t handles[16]{};
    if (connected(steam_pad.self, handles) < 1) return false;
    uint64_t controller = handles[0];

    // GetDigitalActionHandle wants the name from the game's action manifest,
    // which is not on disk here. The handles themselves are small integers, and
    // each one can report the button it stands for, so walk them and match.
    const struct {
        const char* name;
        uint64_t* target;
    } wanted[] = {
        {"L1 Button", &steam_pad.shoulder}, {"Y Button", &steam_pad.triangle},
        {"A Button", &steam_pad.cross},     {"Arrow Up", &steam_pad.up},
        {"Arrow Down", &steam_pad.down},
    };
    for (uint64_t action = 1; action <= 64; ++action) {
        const char* name = name_of(steam_pad.self, action);
        if (!name || !*name) continue;
        for (const auto& entry : wanted) {
            if (std::strcmp(name, entry.name) == 0) *entry.target = action;
        }
    }
    if (!steam_pad.shoulder || !steam_pad.triangle || !steam_pad.cross) {
        LOG_WARN("steam input actions not found");
        return false;
    }
    steam_pad.controller = controller;
    LOG_INFO("pad ready through steam input: L1=%llu triangle=%llu cross=%llu up=%llu down=%llu",
             static_cast<unsigned long long>(steam_pad.shoulder),
             static_cast<unsigned long long>(steam_pad.triangle),
             static_cast<unsigned long long>(steam_pad.cross),
             static_cast<unsigned long long>(steam_pad.up),
             static_cast<unsigned long long>(steam_pad.down));
    return true;
}

Pad read_pad()
{
    Pad pad{};
    if (!open_pad()) return pad;
    // Low byte is the pressed state, high byte is whether the action belongs to
    // the action set the game currently has active. A dead high byte means the
    // handle is fine but the set is wrong, which no amount of pressing fixes.
    struct Read {
        uint64_t action;
        bool* target;
    };
    const Read reads[] = {
        {steam_pad.shoulder, &pad.shoulder}, {steam_pad.triangle, &pad.open_button},
        {steam_pad.cross, &pad.equip},       {steam_pad.up, &pad.up},
        {steam_pad.down, &pad.down},
    };
    uint32_t snapshot = 0;
    for (int i = 0; i < 5; ++i) {
        uint16_t value = reads[i].action
                             ? steam_pad.data(steam_pad.self, steam_pad.controller,
                                              reads[i].action)
                             : 0;
        *reads[i].target = (value & 0xFF) != 0;
        snapshot |= static_cast<uint32_t>(value & 0x0101) << (i * 2);
    }
    pad.present = true;
    static uint32_t traced = ~0u;
    if (snapshot != traced) {
        traced = snapshot;
        LOG_DEBUG("pad state/active L1=%d/%d tri=%d/%d cross=%d/%d up=%d/%d down=%d/%d",
                  pad.shoulder, (snapshot >> 1) & 1, pad.open_button, (snapshot >> 3) & 1,
                  pad.equip, (snapshot >> 5) & 1, pad.up, (snapshot >> 7) & 1, pad.down,
                  (snapshot >> 9) & 1);
    }
    return pad;
}

// Both keys are polled rather than short-circuited so that neither one's held
// state goes stale.
bool pressed_any(int first, int second)
{
    bool a = pressed(first);
    bool b = pressed(second);
    return a || b;
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

// G stands in for holding L1 on a pad. Every other key near the movement hand
// is taken: both keyboard layouts use E, N, O and Space, layout A adds F, H, M
// and U, layout B adds C, Q, R and V. Enter stands in for cross, which is what
// the game's own keyboard prompts show for it. W/S join the arrow keys for the
// D-pad; they collide with movement until the menu freezes the game, but they
// are the natural reach for a hand already on the movement keys.
constexpr int kHoldKey = 'G';

void poll_menu(const std::vector<uint8_t>& uniforms)
{
    Pad pad = read_pad();
    static Pad previous;
    auto fresh = [&](bool Pad::*button) { return pad.*button && !(previous.*button); };
    bool pad_up = fresh(&Pad::up);
    bool pad_down = fresh(&Pad::down);
    bool pad_equip = fresh(&Pad::equip);
    previous = pad;

    static bool pad_seen;
    if (pad.present != pad_seen) {
        pad_seen = pad.present;
        LOG_INFO("pad %s", pad.present ? "detected" : "disconnected");
    }

    // Triangle plus L1 opens the menu and L1 alone keeps it up, because on a
    // pad every button is already spoken for. G is the keyboard stand-in.
    // Both are read as levels rather than edges so the chord opens whichever
    // way round it is pressed.
    bool keyboard = (GetAsyncKeyState(kHoldKey) & 0x8000) != 0;
    bool held = keyboard || (pad.shoulder && (open || pad.open_button));
    if (held != open) {
        open = held;
        menu_open = open;
        LOG_INFO("menu %s by %s", open ? "opened" : "closed", keyboard ? "keyboard" : "pad");
        if (open) {
            auto it = std::find(uniforms.begin(), uniforms.end(), equipped_uniform());
            selected = it == uniforms.end() ? 0 : static_cast<int>(it - uniforms.begin());
        }
    }
    if (!open || uniforms.empty()) return;
    int count = static_cast<int>(uniforms.size());
    bool up = pressed_any(VK_UP, 'W');
    bool down = pressed_any(VK_DOWN, 'S');
    if (up || pad_up) {
        selected = wrapped(selected, count, -1);
        LOG_INFO("menu selected uniform %u", uniforms[selected]);
    }
    if (down || pad_down) {
        selected = wrapped(selected, count, 1);
        LOG_INFO("menu selected uniform %u", uniforms[selected]);
    }
    // Equipping leaves the menu up; releasing the hold is what closes it.
    if (pressed(VK_RETURN) || pad_equip) queue_uniform(uniforms[selected]);
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
        text({inner, hint_y}, hint_height, kHint, "W/S SELECT   ENTER EQUIP   G HOLD");
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
    label("HOLD");
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

std::atomic_bool menu_open;

bool start_overlay(uintptr_t image_base, QueueUniform callback)
{
    base = image_base;
    queue_uniform = callback;
    inventory = find_inventory();
    LOG_INFO("inventory table %s", inventory ? "found" : "not found; using POC uniforms");
    return install_hooks();
}

} // namespace qcamo
