#include <windows.h>
#include <MinHook.h>

#include <atomic>
#include <cstdint>
#include <filesystem>

#include "common/log.h"
#include "common/mem.h"
#include "game_mgs3.h"

namespace {

using Dispatch = intptr_t(__fastcall*)(void*, uint32_t, void*);
Dispatch original_dispatch;
uintptr_t image_base;
bool applying;
std::atomic_bool pending;
uint64_t settle_until;

template <typename Function>
Function game_function(uint32_t rva)
{
    return reinterpret_cast<Function>(image_base + rva);
}

void send_player(uint32_t message, void* data = nullptr)
{
    auto player = qcamo::mem::read<void*>(image_base + qcamo::mgs3::kPlayerSlot);
    if (!player) {
        return;
    }
    auto guard = game_function<void(__fastcall*)(int)>(qcamo::mgs3::kLoadingGuard);
    guard(0);
    original_dispatch(player, message, data);
    guard(1);
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
    int pumps = 0;
    while (busy() && pumps++ < 10000) {
        pump(0x106);
    }
    if (busy()) {
        return nullptr;
    }
    game_function<void(__fastcall*)(void*, int)>(qcamo::mgs3::kFinalizeAsset)(
        reinterpret_cast<void*>(request), 2);
    return queue;
}

void change_camo()
{
    auto stats = qcamo::mem::read<uintptr_t>(image_base + qcamo::mgs3::kStatsSlot);
    auto player = qcamo::mem::read<void*>(image_base + qcamo::mgs3::kPlayerSlot);
    if (!stats || !player || !qcamo::mem::range_readable(stats, 0x680)) {
        LOG_WARN("F6 ignored: gameplay state unavailable");
        return;
    }

    auto address = stats + qcamo::mgs3::kEquippedUniform;
    uint8_t current = qcamo::mem::read<uint8_t>(address);
    uint8_t next = current == 0 ? 1 : 0; // Olive Drab <-> Tiger Stripe.
    uint8_t face = qcamo::mem::read<uint8_t>(stats + qcamo::mgs3::kEquippedFace);
    int id = game_function<int(__fastcall*)(uint32_t, int)>(
        qcamo::mgs3::kUniformAssetId)(qcamo::mgs3::kUniformAssetType, next);
    LOG_INFO("uniform %u -> %u; loading asset %08X", current, next, id);
    qcamo::mem::write<uint8_t>(address, next);
    send_player(qcamo::mgs3::kBeginCamoChange);
    auto queue = load_asset(qcamo::mgs3::kUniformAssetType, id);
    auto asset = queue ? game_function<void*(__fastcall*)(void*, uint32_t)>(
                             qcamo::mgs3::kAssetFinish)(queue, qcamo::mgs3::kUniformAssetSlot)
                       : nullptr;
    if (!asset) {
        LOG_ERROR("change stopped: loaded asset unavailable");
        return;
    }
    send_player(qcamo::mgs3::kLoadCamo, asset);

    qcamo::mem::write<uint8_t>(stats + qcamo::mgs3::kEquippedFace, 0);
    send_player(qcamo::mgs3::kBeginFaceChange);
    int face_id = game_function<int(__fastcall*)(uint32_t, int)>(
        qcamo::mgs3::kUniformAssetId)(qcamo::mgs3::kFaceAssetType, face);
    if (!load_asset(qcamo::mgs3::kFaceAssetType, face_id)) {
        LOG_ERROR("change stopped: face asset unavailable");
        return;
    }
    qcamo::mem::write<uint8_t>(stats + qcamo::mgs3::kEquippedFace, face);
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
    settle_until = GetTickCount64() + 3000;
    LOG_INFO("uniform %u -> %u applied; settling", current, next);
}

intptr_t __fastcall dispatch_hook(void* target, uint32_t message, void* data)
{
    auto result = original_dispatch(target, message, data);
    auto caller = reinterpret_cast<uintptr_t>(__builtin_return_address(0)) - image_base;
    if (!applying && message == qcamo::mgs3::kFrameMessage && caller == qcamo::mgs3::kFrameCaller) {
        applying = true;
        if (settle_until && GetTickCount64() >= settle_until) {
            send_player(qcamo::mgs3::kRefreshCamo);
            settle_until = 0;
            pending = false;
            LOG_INFO("change complete; F6 ready");
        } else if (settle_until && pending.exchange(false)) {
            LOG_INFO("F6 ignored: change still settling");
        } else if (!settle_until && pending.exchange(false)) {
            change_camo();
        }
        applying = false;
    }
    return result;
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
    LOG_INFO("ready: F6 toggles Olive Drab/Tiger Stripe");
    bool held = false;
    for (;;) {
        bool down = (GetAsyncKeyState(VK_F6) & 0x8000) != 0;
        if (down && !held) {
            pending = true;
            LOG_INFO("F6 queued");
        }
        held = down;
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
