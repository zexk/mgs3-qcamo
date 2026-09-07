#include <windows.h>
#include <dbghelp.h>
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
Dispatch dispatch;
using TaskDispatch = void(__fastcall*)();
TaskDispatch original_task_dispatch;
uintptr_t image_base;
uintptr_t image_size;
bool applying;
std::atomic_int pending_uniform{-1};
std::atomic_int pending_face{-1};
// Gameplay thread only. Two native ticks separate uniform loading, face
// loading, and the final refresh; doing both loads in one tick leaves the
// composite model without a node and crashes at game RVA 0xC8187.
int settle_frames;
uint8_t change_face;
uint64_t change_started;
std::atomic_bool change_busy;
std::atomic_bool menu_paused;
std::atomic_flag dumping = ATOMIC_FLAG_INIT;
std::filesystem::path crash_dump_path;
// Crash tracking. A change that reaches the same end state by a different route
// than the Viewer's can leave state that only faults later -- a following
// change, an area transition, a cutscene -- so the handler records every fatal
// exception, not only ones raised while a change is in flight. These say what
// qcamo last did, so a report can tell our damage from the game's own.
std::atomic<const char*> change_phase{"idle"};
std::atomic_int change_uniform{-1};
std::atomic_int change_face_logged{-1};
std::atomic_int crashes_logged;

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
    LOG_INFO("send msg=%08X target=%llX data=%llX", message,
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(player)),
             static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(data)));
    AllocHeap heap(0);
    dispatch(player, message, data);
}

void* load_asset(uint32_t type, int id)
{
    auto busy = game_function<int(__fastcall*)()>(qcamo::mgs3::kAssetBusy);
    auto pump = game_function<void(__fastcall*)(int)>(qcamo::mgs3::kPumpTasks);
    int pumps = 0;
    auto wait = [&] {
        while (busy()) {
            pump(0x106);
            if (++pumps == 20000) {
                LOG_WARN("asset %08X: still busy after %d pumps", id, pumps);
            }
        }
    };

    // Both Viewer state machines wait for the shared asset system before
    // touching a request slot. Otherwise a gameplay load can be overwritten.
    wait();
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

    // The game's own area loader waits the same way at 0x9BF40. There is no
    // safe cancellation path: abandoning a still-busy queue strands the asset
    // system and crashes later, so wait to completion exactly as native does.
    wait();
    LOG_INFO("asset %08X: %d pumps", id, pumps);
    game_function<void(__fastcall*)(void*, int)>(qcamo::mgs3::kFinalizeAsset)(
        reinterpret_cast<void*>(request), 2);
    return queue;
}

void change_camo(uint8_t next, uint8_t next_face)
{
    auto stats = qcamo::mem::read<uintptr_t>(image_base + qcamo::mgs3::kStatsSlot);
    auto player = qcamo::mem::read<uintptr_t>(image_base + qcamo::mgs3::kPlayerSlot);
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
    // Native uniform item flag bit 11 means face paint is unavailable. Tuxedo
    // is the only uniform carrying it in this build.
    if (next == qcamo::mgs3::kTuxedoUniform) {
        next_face = qcamo::mgs3::kNoFacePaint;
    }
    if (current == next && current_face == next_face) {
        LOG_INFO("uniform %u and face %u already equipped", current, current_face);
        change_busy = false;
        return;
    }
    change_started = GetTickCount64();
    change_phase = "uniform";
    change_uniform = next;
    change_face_logged = next_face;
    change_face = next_face;
    if (current == next) {
        // Native writes the replacement before BeginFace only when removing
        // Mask; the handler uses the current face id to select its model path.
        if (current_face == qcamo::mgs3::kMaskFacePaint) {
            qcamo::mem::write<uint8_t>(stats + qcamo::mgs3::kEquippedFace, next_face);
        }
        change_phase = "face-begin";
        send_player(qcamo::mgs3::kBeginFaceChange);
        settle_frames = 2;
        LOG_INFO("face-only change %u -> %u pending", current_face, next_face);
        return;
    }
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
        change_phase = "idle";
        LOG_ERROR("change stopped: asset unavailable; nothing dispatched");
        return;
    }
    qcamo::mem::write<uint8_t>(address, next);
    game_function<void(__fastcall*)()>(qcamo::mgs3::kRefreshEquipment)();
    send_player(qcamo::mgs3::kBeginCamoChange);
    send_player(qcamo::mgs3::kLoadCamo, asset);

    qcamo::mem::write<uint8_t>(stats + qcamo::mgs3::kEquippedFace,
                               qcamo::mgs3::kNoFacePaint);
    send_player(qcamo::mgs3::kBeginFaceChange);
    // Native Tuxedo path ends after removing face paint. Other uniforms need
    // the face queue rebuilt before their final refresh.
    settle_frames = next == qcamo::mgs3::kTuxedoUniform ? 1 : 2;
    LOG_INFO("uniform phase applied; face %u pending", next_face);
}

bool finish_face_change()
{
    change_phase = "face";
    auto stats = qcamo::mem::read<uintptr_t>(image_base + qcamo::mgs3::kStatsSlot);
    if (!stats || !qcamo::mem::range_readable(stats, 0x680)) {
        LOG_ERROR("change stopped: gameplay state unavailable before face phase");
        return false;
    }
    int face_id = game_function<int(__fastcall*)(uint32_t, int)>(
        qcamo::mgs3::kUniformAssetId)(qcamo::mgs3::kFaceAssetType, change_face);
    if (!load_asset(qcamo::mgs3::kFaceAssetType, face_id)) {
        LOG_ERROR("change stopped: face asset unavailable");
        return false;
    }
    qcamo::mem::write<uint8_t>(stats + qcamo::mgs3::kEquippedFace, change_face);
    game_function<void(__fastcall*)()>(qcamo::mgs3::kRefreshEquipment)();
    // Mask is a model node toggled by the final 1A0014 handler, not a face
    // texture. Native skips this whole apply path for face id 10.
    if (change_face != qcamo::mgs3::kMaskFacePaint) {
        auto face_asset = game_function<void*(__fastcall*)(uint32_t)>(
            qcamo::mgs3::kFindAsset)(qcamo::mgs3::kFaceAsset);
        if (face_asset) {
            auto prepared = game_function<void*(__fastcall*)(void*)>(
                qcamo::mgs3::kPrepareFace)(face_asset);
            game_function<void(__fastcall*)(uint32_t, uint32_t, void*)>(
                qcamo::mgs3::kApplyFace)(qcamo::mgs3::kFaceAssetSlot,
                                         qcamo::mgs3::kFaceAssetId, prepared);
        }
    }
    LOG_INFO("face %u applied; settling", change_face);
    return true;
}

void set_menu_pause(bool pause_menu)
{
    if (pause_menu == menu_paused.exchange(pause_menu)) {
        return;
    }
    auto& pause = *reinterpret_cast<uint32_t*>(image_base + qcamo::mgs3::kPauseLevel);
    std::atomic_ref pause_level(pause);
    if (pause_menu) {
        pause_level.fetch_or(qcamo::mgs3::kWheelPause);
    } else {
        pause_level.fetch_and(~qcamo::mgs3::kWheelPause);
    }
    LOG_INFO("wheel pause %s", pause_menu ? "set" : "cleared");
}

void __fastcall task_dispatch_hook()
{
    original_task_dispatch();
    if (applying) return;

    // Whole native actor queue has finished. Running from a player dispatch
    // callback changes composite-model nodes while later actors still use the
    // old list and crashes at game RVA 0xC8187.
    applying = true;
    if (int cue = qcamo::pending_sound.exchange(0); cue) {
        game_function<void(__fastcall*)(uint32_t)>(qcamo::mgs3::kPlaySound)(
            static_cast<uint32_t>(cue));
    }
    if (settle_frames == 2) {
        if (finish_face_change()) {
            settle_frames = 1;
        } else {
            settle_frames = 0;
            change_busy = false;
            change_phase = "idle";
        }
    } else if (settle_frames == 1) {
        settle_frames = 0;
        change_phase = "refresh";
        // Player handler relays this message to the Snake actor.
        send_player(qcamo::mgs3::kRefreshCamo);
        pending_uniform = -1;
        change_busy = false;
        change_phase = "idle";
        LOG_INFO("change complete in %llums; input ready",
                 static_cast<unsigned long long>(GetTickCount64() - change_started));
    } else {
        int requested = pending_uniform.exchange(-1);
        if (requested >= 0) {
            // Menu can close immediately after accepting an equip. Own the
            // pause before any model mutation and retain it through refresh.
            set_menu_pause(true);
            change_camo(static_cast<uint8_t>(requested),
                        static_cast<uint8_t>(pending_face.exchange(-1)));
        }
    }
    applying = false;
}

bool queue_uniform(uint8_t uniform, uint8_t face)
{
    // Equips come from the open menu, which holds our own wheel pause, so that
    // one bit is tolerated here; any other pauser still refuses the change.
    if (qcamo::GateBlock block = qcamo::gate_state(image_base, true);
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

// Return addresses left on the stack. Without symbols a minidump is awkward to
// read on the machine this is built on, and the fault RVA alone rarely names
// the caller that passed the bad pointer; the RVAs below are what a disassembly
// of the running image can be walked against directly.
void log_stack_rvas(const CONTEXT* context)
{
    if (!context) {
        return;
    }
    auto stack = static_cast<uintptr_t>(context->Rsp);
    size_t span = 0x800;
    while (span >= sizeof(uintptr_t) && !qcamo::mem::range_readable(stack, span)) {
        span /= 2;
    }
    int found = 0;
    for (size_t offset = 0; offset + sizeof(uintptr_t) <= span && found < 16;
         offset += sizeof(uintptr_t)) {
        auto value = qcamo::mem::read<uintptr_t>(stack + offset);
        if (value - image_base >= image_size) {
            continue;
        }
        LOG_ERROR("  stack +%03zX rva %llX", offset,
                  static_cast<unsigned long long>(value - image_base));
        ++found;
    }
}

LONG WINAPI record_change_exception(EXCEPTION_POINTERS* pointers)
{
    if (!pointers || !pointers->ExceptionRecord) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    auto* record = pointers->ExceptionRecord;
    switch (record->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION:
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
    case EXCEPTION_ILLEGAL_INSTRUCTION:
    case EXCEPTION_IN_PAGE_ERROR:
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
    case EXCEPTION_PRIV_INSTRUCTION:
    case EXCEPTION_STACK_OVERFLOW:
    case 0xC0000374L: // heap corruption
    case 0xC0000409L: // stack buffer overrun / fail-fast
        break;
    default:
        return EXCEPTION_CONTINUE_SEARCH;
    }
    // Only the first few, in case the game faults in a loop rather than dying.
    if (crashes_logged.fetch_add(1) >= 4) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    auto address = reinterpret_cast<uintptr_t>(record->ExceptionAddress);
    MEMORY_BASIC_INFORMATION memory{};
    VirtualQuery(record->ExceptionAddress, &memory, sizeof(memory));
    auto module = reinterpret_cast<uintptr_t>(memory.AllocationBase);
    // First-chance: the game may still handle this itself, so an entry here is
    // not proof of a crash. A run that keeps going after one is the tell.
    LOG_ERROR("exception code=%08lX address=%llX module=%llX offset=%llX thread=%lu",
              record->ExceptionCode, static_cast<unsigned long long>(address),
              static_cast<unsigned long long>(module),
              static_cast<unsigned long long>(address - module), GetCurrentThreadId());
    LOG_ERROR("  qcamo phase=%s uniform=%d face=%d busy=%d %llums since last change",
              change_phase.load(), change_uniform.load(), change_face_logged.load(),
              static_cast<int>(change_busy.load()),
              static_cast<unsigned long long>(change_started ? GetTickCount64() - change_started
                                                             : 0));
    if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION &&
        record->NumberParameters >= 2) {
        LOG_ERROR("  %s address %llX",
                  record->ExceptionInformation[0] == 1 ? "write" : "read",
                  static_cast<unsigned long long>(record->ExceptionInformation[1]));
    }
    if (auto* context = pointers->ContextRecord) {
        LOG_ERROR("  rcx=%llX rdx=%llX r8=%llX r9=%llX rax=%llX rsp=%llX",
                  static_cast<unsigned long long>(context->Rcx),
                  static_cast<unsigned long long>(context->Rdx),
                  static_cast<unsigned long long>(context->R8),
                  static_cast<unsigned long long>(context->R9),
                  static_cast<unsigned long long>(context->Rax),
                  static_cast<unsigned long long>(context->Rsp));
        log_stack_rvas(context);
    }
    if (dumping.test_and_set()) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    HANDLE file = CreateFileW(crash_dump_path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                              nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    BOOL written = FALSE;
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), pointers, FALSE};
        written = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                                    MiniDumpNormal, &info, nullptr, nullptr);
        CloseHandle(file);
    }
    LOG_ERROR("crash dump %s: qcamo-crash.dmp", written ? "written" : "failed");
    return EXCEPTION_CONTINUE_SEARCH;
}

DWORD WINAPI init(LPVOID)
{
    auto game = GetModuleHandleW(L"METAL GEAR SOLID3.exe");
    if (!game || !qcamo::log_init((own_dir() / L"qcamo.log").string().c_str())) {
        return 0;
    }
    image_base = reinterpret_cast<uintptr_t>(game);
    crash_dump_path = own_dir() / L"qcamo-crash.dmp";
    auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(game);
    auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image_base + dos->e_lfanew);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->FileHeader.TimeDateStamp != qcamo::mgs3::kExpectedTimestamp) {
        LOG_ERROR("unsupported executable");
        return 0;
    }
    image_size = nt->OptionalHeader.SizeOfImage;
    if (!AddVectoredExceptionHandler(0, record_change_exception)) {
        LOG_WARN("crash handler unavailable");
    }

    dispatch = game_function<Dispatch>(qcamo::mgs3::kMessageDispatch);
    void* task_target = reinterpret_cast<void*>(image_base + qcamo::mgs3::kTaskDispatch);
    if (MH_Initialize() != MH_OK ||
        MH_CreateHook(task_target, reinterpret_cast<void*>(&task_dispatch_hook),
                      reinterpret_cast<void**>(&original_task_dispatch)) != MH_OK ||
        MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        LOG_ERROR("game hooks failed");
        return 0;
    }
    if (!qcamo::start_overlay(image_base, queue_uniform)) {
        LOG_ERROR("quick menu hook failed");
    }
    LOG_INFO("ready: hold G or the pad chord to open the menu");
    for (;;) {
        qcamo::refresh_inventory();
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
        set_menu_pause(qcamo::menu_open.load() || change_busy.load());
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
