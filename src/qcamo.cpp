#include <windows.h>
#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <filesystem>

#include "common/log.h"
#include "common/mem.h"
#include "game_mgs3.h"
#include "gameplay_gate.h"
#include "overlay.h"

namespace {

using Dispatch = intptr_t(__fastcall*)(void*, uint32_t, void*);
Dispatch original_dispatch;
uintptr_t image_base;
bool applying;
// Frames to wait before the settle refresh, and so how long the input gate
// holds. This replaces a 2.5s wall-clock wait copied from the gap between a
// native change's 1A000F and its 1A0014 -- but that gap is the Viewer screen
// closing at 0x3030F0, not anything a change waits for, and neither native
// change path has a timer at all. Both of theirs wait on Viewer flags through
// 0x2FF400, which reads null during gameplay, so there is no game-side
// predicate to borrow.
//
// Nothing of ours is left pending when change_camo returns: both assets are
// pumped until 0xE1970 clears and all three dispatches have gone out. What is
// left is the player consuming them on its own tick, which is a frame, not
// seconds. This is the one number to raise if a fast second swap misbehaves.
constexpr int kSettleFrames = 1;

std::atomic_int pending_uniform{-1};
std::atomic_int pending_face{-1};
// Gameplay thread only: set by change_camo, counted down by the frame hook.
int settle_frames;
uint64_t change_started;
std::atomic_bool change_busy;
bool menu_paused;

template <typename Function>
Function game_function(uint32_t rva)
{
    return reinterpret_cast<Function>(image_base + rva);
}

// The game routes allocations that ask for heap -1 through the index in
// kAllocHeapSlot. Message handlers allocate, and native selects heap 0 around
// its dispatches, so do the same -- but restore whatever gameplay had rather
// than the literal 1 the Viewer restores, which is only right inside the
// Viewer screen.
class AllocHeap {
public:
    explicit AllocHeap(int heap)
        : set_(game_function<void(__fastcall*)(int)>(qcamo::mgs3::kSetAllocHeap)),
          previous_(qcamo::mem::read<int>(image_base + qcamo::mgs3::kAllocHeapSlot))
    {
        set_(heap);
    }
    ~AllocHeap() { set_(previous_); }
    AllocHeap(const AllocHeap&) = delete;
    AllocHeap& operator=(const AllocHeap&) = delete;

private:
    void(__fastcall* set_)(int);
    int previous_;
};

void send_player(uint32_t message, void* data = nullptr)
{
    auto player = qcamo::mem::read<void*>(image_base + qcamo::mgs3::kPlayerSlot);
    if (!player) {
        return;
    }
    // Our sends go straight to original_dispatch, so the dispatch hook never
    // sees them; log them here to keep the sequence visible in the log.
    LOG_INFO("send msg=%08X target=%llX data=%llX", message,
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(player)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(data)));
    AllocHeap heap(0);
    original_dispatch(player, message, data);
}

// Second 1A0014 target (Snake actor beside the player controller), latched
// whenever native code refreshes it. Handles carry a per-session prefix, so
// the latch is only used while the prefix still matches the live player.
uintptr_t latched_actor;
uint32_t latched_prefix;

void send_refresh()
{
    send_player(qcamo::mgs3::kRefreshCamo);
    auto player = qcamo::mem::read<uintptr_t>(image_base + qcamo::mgs3::kPlayerSlot);
    if (latched_actor && player && (player & 0xFFFF0000u) == latched_prefix) {
        LOG_INFO("send msg=%08X target=%llX data=0 (actor)", qcamo::mgs3::kRefreshCamo,
                 static_cast<unsigned long long>(latched_actor));
        {
            AllocHeap heap(0);
            original_dispatch(reinterpret_cast<void*>(latched_actor),
                              qcamo::mgs3::kRefreshCamo, nullptr);
        }
        LOG_INFO("refresh sent to player and actor %llX",
                 static_cast<unsigned long long>(latched_actor));
    } else {
        LOG_INFO("refresh sent to player only (actor %s)", latched_actor ? "stale" : "unknown");
    }
}

void* load_asset(uint32_t type, int id)
{
    auto pool = game_function<void*(__fastcall*)(uint32_t)>(qcamo::mgs3::kAssetRequest)(type);
    if (!pool) {
        return nullptr;
    }
    auto request = reinterpret_cast<uintptr_t>(pool) + 0x10;
    auto queue = qcamo::mem::read<void*>(request + 8);
    if (!queue) {
        return nullptr;
    }
    game_function<void(__fastcall*)(void*, int)>(qcamo::mgs3::kAssetRequestMode)(queue, 2);
    qcamo::mem::write<int>(request, id);
    game_function<void(__fastcall*)(void*, int)>(qcamo::mgs3::kAssetRequestId)(queue, id);

    auto busy = game_function<int(__fastcall*)()>(qcamo::mgs3::kAssetBusy);
    auto pump = game_function<void(__fastcall*)(int)>(qcamo::mgs3::kPumpTasks);
    // The game's own area loader waits the same way at 0x9BF40, so re-entering
    // the scheduler from here is the sanctioned pattern rather than a hack.
    int pumps = 0;
    while (busy() && pumps++ < 10000) {
        pump(0x106);
    }
    LOG_INFO("asset %08X: %d pumps%s", id, pumps, busy() ? " (still busy)" : "");
    if (busy()) {
        return nullptr;
    }
    game_function<void(__fastcall*)(void*, int)>(qcamo::mgs3::kFinalizeAsset)(
        reinterpret_cast<void*>(request), 2);
    return queue;
}

void change_camo(uint8_t next, uint8_t next_face)
{
    auto stats = qcamo::mem::read<uintptr_t>(image_base + qcamo::mgs3::kStatsSlot);
    auto player = qcamo::mem::read<void*>(image_base + qcamo::mgs3::kPlayerSlot);
    if (!stats || !player || !qcamo::mem::range_readable(stats, 0x680)) {
        change_busy = false;
        LOG_WARN("uniform change ignored: gameplay state unavailable");
        return;
    }
    // Re-check on the gameplay thread: the menu may have been open when the
    // change was queued but gameplay left since. Our own wheel pause is
    // tolerated here; anything else aborts.
    if (qcamo::GateBlock block = qcamo::gate_state(image_base, true);
        block != qcamo::GateBlock::None) {
        change_busy = false;
        LOG_WARN("uniform change ignored: %s", qcamo::gate_name(block));
        return;
    }

    auto address = stats + qcamo::mgs3::kEquippedUniform;
    uint8_t current = qcamo::mem::read<uint8_t>(address);
    uint8_t current_face = qcamo::mem::read<uint8_t>(stats + qcamo::mgs3::kEquippedFace);
    if (current == next && current_face == next_face) {
        LOG_INFO("uniform %u and face %u already equipped", current, current_face);
        change_busy = false;
        return;
    }
    settle_frames = kSettleFrames;
    change_started = GetTickCount64();
    int id = game_function<int(__fastcall*)(uint32_t, int)>(
        qcamo::mgs3::kUniformAssetId)(qcamo::mgs3::kUniformAssetType, next);
    LOG_INFO("uniform %u -> %u, face %u -> %u; loading asset %08X", current, next,
             current_face, next_face, id);
    // Load before committing. 1A0001 starts a change the game expects to be
    // finished by 1A0002 with a real asset; sending the begin and then failing
    // to load leaves the player mid-change and wedges the next attempt. An
    // area transition is exactly when the load fails, so ordering it this way
    // is the difference between a refused change and a frozen game.
    auto queue = load_asset(qcamo::mgs3::kUniformAssetType, id);
    auto asset = queue ? game_function<void*(__fastcall*)(void*, uint32_t)>(
                             qcamo::mgs3::kAssetFinish)(queue, qcamo::mgs3::kUniformAssetSlot)
                       : nullptr;
    if (!asset) {
        settle_frames = 0;
        change_busy = false;
        LOG_ERROR("change stopped: asset unavailable; nothing dispatched");
        return;
    }
    qcamo::mem::write<uint8_t>(address, next);
    send_player(qcamo::mgs3::kBeginCamoChange);
    send_player(qcamo::mgs3::kLoadCamo, asset);

    qcamo::mem::write<uint8_t>(stats + qcamo::mgs3::kEquippedFace, 0);
    send_player(qcamo::mgs3::kBeginFaceChange);
    int face_id = game_function<int(__fastcall*)(uint32_t, int)>(
        qcamo::mgs3::kUniformAssetId)(qcamo::mgs3::kFaceAssetType, next_face);
    if (!load_asset(qcamo::mgs3::kFaceAssetType, face_id)) {
        LOG_ERROR("change stopped: face asset unavailable");
        return;
    }
    qcamo::mem::write<uint8_t>(stats + qcamo::mgs3::kEquippedFace, next_face);
    game_function<void(__fastcall*)()>(qcamo::mgs3::kRefreshEquipment)();
    auto face_asset = game_function<void*(__fastcall*)(uint32_t)>(
        qcamo::mgs3::kFindAsset)(qcamo::mgs3::kFaceAsset);
    if (face_asset) {
        auto prepared = game_function<void*(__fastcall*)(void*)>(
            qcamo::mgs3::kPrepareFace)(face_asset);
        game_function<void(__fastcall*)(uint32_t, uint32_t, void*)>(
            qcamo::mgs3::kApplyFace)(qcamo::mgs3::kFaceAssetSlot,
                                     qcamo::mgs3::kFaceAssetId, prepared);
    }
    LOG_INFO("uniform %u -> %u, face %u -> %u applied; settling", current, next,
             current_face, next_face);
}

intptr_t __fastcall dispatch_hook(void* target, uint32_t message, void* data)
{
    auto result = original_dispatch(target, message, data);
    auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - image_base;
    if (((message >> 16) & 0xFFu) == 0x1Au) {
        // Latch the Snake-actor refresh target whenever native code sends
        // one anywhere but the player slot.
        if (message == qcamo::mgs3::kRefreshCamo) {
            auto player =
                qcamo::mem::read<uintptr_t>(image_base + qcamo::mgs3::kPlayerSlot);
            auto handle = reinterpret_cast<uintptr_t>(target);
            if (player && handle && handle != player) {
                latched_actor = handle;
                latched_prefix = static_cast<uint32_t>(player & 0xFFFF0000u);
                LOG_INFO("actor latch: %llX (prefix %04X)",
                         static_cast<unsigned long long>(handle), latched_prefix >> 16);
            }
        }
    }
    if (!applying && message == qcamo::mgs3::kFrameMessage && caller == qcamo::mgs3::kFrameCaller) {
        applying = true;
        if (int cue = qcamo::pending_sound.exchange(0); cue) {
            game_function<void(__fastcall*)(uint32_t)>(qcamo::mgs3::kPlaySound)(
                static_cast<uint32_t>(cue));
        }
        if (settle_frames > 0 && --settle_frames == 0) {
            send_refresh();
            pending_uniform = -1;
            change_busy = false;
            LOG_INFO("change complete in %llums; input ready",
                     static_cast<unsigned long long>(GetTickCount64() - change_started));
        } else if (settle_frames > 0 && pending_uniform.exchange(-1) >= 0) {
            LOG_INFO("uniform change ignored: still settling");
        } else if (settle_frames == 0) {
            int requested = pending_uniform.exchange(-1);
            if (requested >= 0) {
                change_camo(static_cast<uint8_t>(requested),
                            static_cast<uint8_t>(pending_face.exchange(-1)));
            }
        }
        applying = false;
    }
    return result;
}

void set_menu_pause(bool pause_menu)
{
    if (pause_menu == menu_paused) {
        return;
    }
    auto& pause = *reinterpret_cast<uint32_t*>(image_base + qcamo::mgs3::kPauseLevel);
    std::atomic_ref pause_level(pause);
    if (pause_menu) {
        pause_level.fetch_or(qcamo::mgs3::kWheelPause);
    } else {
        pause_level.fetch_and(~qcamo::mgs3::kWheelPause);
    }
    menu_paused = pause_menu;
    LOG_INFO("wheel pause %s", pause_menu ? "set" : "cleared");
}

bool queue_uniform(uint8_t uniform, uint8_t face)
{
    // Equips come from the open menu, which holds our own wheel pause; F6
    // comes with the menu closed and must find the game fully unpaused so a
    // change never stacks onto a game wheel or another pauser.
    bool from_menu = qcamo::menu_open.load();
    if (qcamo::GateBlock block = qcamo::gate_state(image_base, from_menu);
        block != qcamo::GateBlock::None) {
        LOG_INFO("uniform %u face %u ignored: %s", uniform, face, qcamo::gate_name(block));
        return false;
    }
    if (change_busy.exchange(true)) {
        LOG_INFO("uniform %u face %u ignored: change gate active", uniform, face);
        return false;
    }
    pending_face = face;
    pending_uniform = uniform;
    LOG_INFO("uniform %u face %u queued", uniform, face);
    return true;
}

uint8_t toggle_target()
{
    auto stats = qcamo::mem::read<uintptr_t>(image_base + qcamo::mgs3::kStatsSlot);
    uint8_t current =
        stats ? qcamo::mem::read<uint8_t>(stats + qcamo::mgs3::kEquippedUniform) : 0;
    return current == 0 ? 1 : 0;
}

uint8_t equipped_face()
{
    auto stats = qcamo::mem::read<uintptr_t>(image_base + qcamo::mgs3::kStatsSlot);
    return stats ? qcamo::mem::read<uint8_t>(stats + qcamo::mgs3::kEquippedFace) : 0;
}

std::filesystem::path own_dir()
{
    HMODULE self{};
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&own_dir), &self);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(self, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

DWORD WINAPI init(LPVOID)
{
    auto game = GetModuleHandleW(L"METAL GEAR SOLID3.exe");
    if (!game || !qcamo::log_init((own_dir() / L"qcamo.log").string().c_str())) {
        return 0;
    }
    image_base = reinterpret_cast<uintptr_t>(game);
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image_base + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.TimeDateStamp != qcamo::mgs3::kExpectedTimestamp) {
        LOG_ERROR("unsupported executable");
        return 0;
    }

    void* target = reinterpret_cast<void*>(image_base + qcamo::mgs3::kMessageDispatch);
    if (MH_Initialize() != MH_OK ||
        MH_CreateHook(target, reinterpret_cast<void*>(&dispatch_hook),
                      reinterpret_cast<void**>(&original_dispatch)) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        LOG_ERROR("message hook failed");
        return 0;
    }
    if (!qcamo::start_overlay(image_base, queue_uniform)) {
        LOG_ERROR("quick menu hook failed");
    }
    LOG_INFO("ready: G or pad opens the menu, F6 toggles uniform 0/1");
    bool held = false;
    for (;;) {
        bool down = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
        if (down && !held) {
            queue_uniform(toggle_target(), equipped_face());
        }
        held = down;
        // Area watcher. This thread keeps running when the gameplay thread
        // wedges, so a transition that starts and never finishes shows up here
        // as a first line with no second one. Reads memory only: calling game
        // functions from here while the game thread is inside them is not safe.
        {
            auto stats = qcamo::mem::read<uintptr_t>(image_base + qcamo::mgs3::kStatsSlot);
            static char area[qcamo::mgs3::kAreaSize + 1];
            if (stats && qcamo::mem::range_readable(stats + qcamo::mgs3::kAreaCode,
                                                    qcamo::mgs3::kAreaSize)) {
                char now[qcamo::mgs3::kAreaSize + 1]{};
                for (uint32_t i = 0; i < qcamo::mgs3::kAreaSize; ++i) {
                    now[i] = qcamo::mem::read<char>(stats + qcamo::mgs3::kAreaCode + i);
                }
                if (__builtin_memcmp(now, area, qcamo::mgs3::kAreaSize) != 0) {
                    LOG_INFO("area %s -> %s", area[0] ? area : "(none)", now);
                    __builtin_memcpy(area, now, sizeof(now));
                }
            }
        }
        // Watchdog: Present may stall across loads and cutscene cuts, so drop
        // our pause promptly when gameplay goes away under an open menu. The
        // render thread closes its side on the next frame.
        if (qcamo::menu_open.load()) {
            if (qcamo::GateBlock block = qcamo::gate_state(image_base, true);
                block != qcamo::GateBlock::None) {
                qcamo::menu_open = false;
                LOG_INFO("menu closed: %s", qcamo::gate_name(block));
            }
        }
        set_menu_pause(qcamo::menu_open.load());
        Sleep(10);
    }
}

} // namespace

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        if (HANDLE thread = CreateThread(nullptr, 0, init, nullptr, 0, nullptr)) {
            CloseHandle(thread);
        }
    }
    return TRUE;
}
