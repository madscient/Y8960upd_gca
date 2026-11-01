// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#pragma once

#include <cstdint>

// System Controller の I/O（原本の system_controller.md と y8960upd.asm の定数）
namespace y8960upd::y8960 {

inline constexpr std::uint16_t kEnabler = 0x40;
inline constexpr std::uint16_t kDeviceSelect = 0x41;
inline constexpr std::uint16_t kFunctionSelect = 0x42;
inline constexpr std::uint16_t kAddressLow = 0x43;
inline constexpr std::uint16_t kAddressMid = 0x44;
inline constexpr std::uint16_t kAddressHigh = 0x45;
inline constexpr std::uint16_t kCommand = 0x46;
inline constexpr std::uint16_t kData = 0x47;

inline constexpr std::uint8_t kEnable = 0x80;
inline constexpr std::uint8_t kDisable = 0x00;
inline constexpr std::uint8_t kEnablerReady = 0x7F;
inline constexpr std::uint8_t kDeviceY8960 = 0x02;
inline constexpr std::uint8_t kFunctionConfigRom = 0x01;

inline constexpr std::uint8_t kCmdRead = 0x00;
inline constexpr std::uint8_t kCmdWrite = 0x01;
inline constexpr std::uint8_t kCmdEraseBlock = 0x02;
inline constexpr std::uint8_t kCmdStatus = 0x04;

inline constexpr std::uint8_t kStatusBusy = 0x01;

}  // namespace y8960upd::y8960
