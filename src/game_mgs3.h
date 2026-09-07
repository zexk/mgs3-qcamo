#pragma once

#include <cstdint>

namespace qcamo::mgs3 {

// PC Master Collection build 0x6980B92F.
inline constexpr uint32_t kExpectedTimestamp = 0x6980B92F;
inline constexpr uint32_t kStatsSlot = 0xACDE98;
inline constexpr uint32_t kMessageDispatch = 0x10EDC0;
// Runs queued actor jobs, then returns to the main loop. Camouflage changes
// must run after it: changing a model from inside one actor callback leaves
// later jobs walking the old composite nodes.
inline constexpr uint32_t kTaskDispatch = 0x10EE10;
inline constexpr uint32_t kPlayerSlot = 0x1E16CD0;

inline constexpr uint32_t kEquippedUniform = 0x67E;
inline constexpr uint32_t kEquippedFace = 0x67F;
inline constexpr uint32_t kBeginCamoChange = 0x1A0001;
inline constexpr uint32_t kLoadCamo = 0x1A0002;
inline constexpr uint32_t kBeginFaceChange = 0x1A000F;
inline constexpr uint32_t kRefreshCamo = 0x1A0014;

inline constexpr uint32_t kAssetBusy = 0xE1970;
inline constexpr uint32_t kAssetRequest = 0xE1650;
inline constexpr uint32_t kAssetRequestMode = 0xE1660;
inline constexpr uint32_t kAssetRequestId = 0xE17C0;
inline constexpr uint32_t kAssetFinish = 0xE1680;
inline constexpr uint32_t kUniformAssetId = 0x9BFA0;
inline constexpr uint32_t kFinalizeAsset = 0x304050;
// Sets the allocator's default heap index at kAllocHeapSlot; 0x113F90 reads
// that global whenever a caller asks for heap -1. The Viewer wraps its own
// dispatches as set(0) ... set(1) because the Viewer screen runs with the
// ambient heap already at 1; during gameplay it is 0, so copying the literal
// pair leaves every later allocation in the wrong arena and the next area
// load never finishes its wait at 0x9BF40. Always save and restore instead.
inline constexpr uint32_t kSetAllocHeap = 0x1143F0;
inline constexpr uint32_t kAllocHeapSlot = 0x1D7A550;
// Task-context yield/schedule, not a task pump, and 0x106 is not a mask.
// It looks up the current context in the table at 0x103BC60 (stride 0x90) via
// the global at 0x1044C60: on context 1 it runs the scheduler and ignores the
// argument, otherwise it treats the argument as a duration and yields. Calling
// it from inside a dispatch therefore re-enters the scheduler.
inline constexpr uint32_t kPumpTasks = 0x725CD0;
inline constexpr uint32_t kFindAsset = 0x113B00;
inline constexpr uint32_t kPrepareFace = 0x2F8D0;
inline constexpr uint32_t kApplyFace = 0xC3600;
inline constexpr uint32_t kRefreshEquipment = 0x2FDB00;

inline constexpr uint32_t kUniformAssetType = 0x602F5702;
inline constexpr uint32_t kUniformAssetSlot = 0x0D413AA8;
inline constexpr uint32_t kFaceAssetType = 0x609B53C5;
inline constexpr uint32_t kFaceAsset = 0x6903A157;
inline constexpr uint32_t kFaceAssetSlot = 0x00413AA8;
inline constexpr uint32_t kFaceAssetId = 0x0003A157;

// Stats-block area code (bbtracker: 7-char stage string, s*/v* are gameplay).
inline constexpr uint32_t kAreaCode = 0x24;
inline constexpr uint32_t kAreaSize = 7;

// Survival Viewer builds its disabled-entry mask at 0x311400. When bit 10 at
// stats + 0x680 is clear it disables CAMOUFLAGE, BACKPACK, and CURE (mask
// 0x0B). The game clears this while Snake has no backpack: before its first
// pickup and after torture until his equipment is recovered.
inline constexpr uint32_t kSurvivalFlags = 0x680;
inline constexpr uint32_t kHasBackpack = 1 << 10;

// Survival Viewer context slot: live pointer only while the Viewer screen exists.
inline constexpr uint32_t kViewerSlot = 0x1E14AE0;

// Camouflage. The player record starts at kPlayerSlot and is 0x80 bytes; the
// state bitset 0x359020 queries sits at +0x28.
inline constexpr uint32_t kPlayerState = 0x1E16CF8;
inline constexpr int kStateCrouch = 2;
inline constexpr int kStateProne = 3;
inline constexpr int kStateOnWall = 0x3B;
inline constexpr int kStateProneOverride = 0xA9;

// One 0x18-byte record per uniform and per face paint, indexed by the equipped
// byte. +0x00 is the internal name, +0x08 the value table: 27 terrains of 5
// postures, signed bytes, ten times the percentage each contributes.
inline constexpr uint32_t kUniformCamo = 0x1E216E0;
inline constexpr uint32_t kFaceCamo = 0x1E214A0;
inline constexpr uint32_t kCamoRecordStride = 0x18;
inline constexpr uint32_t kCamoRecordValues = 0x08;
inline constexpr int kCamoPostures = 5;
inline constexpr int kCamoValues = 27 * kCamoPostures;

// Surface materials under Snake, republished by 0xA8070 every frame, and the
// table that maps them to terrain indices.
inline constexpr uint32_t kGroundMaterial = 0x1D38AF8;
inline constexpr uint32_t kWallMaterial = 0x1D38AFC;
inline constexpr uint32_t kTerrainMap = 0x1D38B10;
inline constexpr uint32_t kTerrainMapCount = 0x1D38F10;
inline constexpr uint32_t kTerrainMapStride = 8;

// UI sound. 0x9ECC0 takes a cue id in ecx and nothing else: it masks the id to
// eleven bits, tags it 0x43 and hands it to the mixer. Every menu in the game
// uses the same three cues, and the Survival Viewer's camouflage list is no
// exception -- cursor at 0x302628, decide at 0x302656, back at 0x302710.
inline constexpr uint32_t kPlaySound = 0x9ECC0;
// The weapon and item wheels open with 0x1A008: both wheel modules play it
// immediately after taking the same pause bit this menu takes, at 0x32DA49 and
// 0x32E6C6. Closing uses the Viewer's back cue rather than the wheels' own
// 0x1A009, which sits at 0x32D361 and 0x32E034 if it is ever wanted.
inline constexpr uint32_t kSoundOpen = 0x1A008;
// The game's "not permitted" sound, played when an equip is refused.
inline constexpr uint32_t kSoundDenied = 0x300F;
inline constexpr uint32_t kSoundCursor = 0x1A00B;
inline constexpr uint32_t kSoundDecide = 0x1A00C;
inline constexpr uint32_t kSoundCancel = 0x1A00D;

// GV_PauseLevel. Weapon and item wheels set bit 2; GV_ExecActor skips actors
// whose pause mask intersects it, while wheel UI and audio keep updating.
inline constexpr uint32_t kPauseLevel = 0x1D78F6C;
inline constexpr uint32_t kWheelPause = 1 << 2;

// Global player-state flags, tested as a pair against a mask all over the
// game. The wheel-style popups that take the wheel pause bit guard on exactly
// this before opening: 0x6CA3EA and 0x6E074D with 0xFE000200, 0x6DA4EB with
// 0x86000200. Bit 9 is the one that separates a cutscene from gameplay --
// observed live as 0x200 throughout a cutscene and clear during play -- and
// the top bits cover the other states a popup must not interrupt. The
// stricter of the two masks is the one to copy.
inline constexpr uint32_t kPlayerFlagsA = 0x1E21AB0;
inline constexpr uint32_t kPlayerFlagsB = 0x1E21AB4;
inline constexpr uint32_t kNoPopupMask = 0xFE000200;

} // namespace qcamo::mgs3
