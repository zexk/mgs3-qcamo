#pragma once

#include <cstdint>

namespace qcamo::mgs3 {

// PC Master Collection build 0x6980B92F.
inline constexpr uint32_t kExpectedTimestamp = 0x6980B92F;
inline constexpr uint32_t kStatsSlot = 0xACDE98;
inline constexpr uint32_t kMessageDispatch = 0x10EDC0;
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
inline constexpr uint32_t kLoadingGuard = 0x1143F0;
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

// Per-frame dispatch observed on gameplay thread.
inline constexpr uint32_t kFrameMessage = 0x00000002;
inline constexpr uint32_t kFrameCaller = 0x5BC850;

} // namespace qcamo::mgs3
