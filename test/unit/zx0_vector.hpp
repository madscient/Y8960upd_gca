// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace y8960upd::test {

// 原本（hra1129/Y8960_Cartridge の 805d900）の tool_for_windows/tests/Tests.cs の Zx0Vector。
// kZx0Sentence を 3000 バイトまで繰り返したものを、ZX0 の公式の圧縮（v2.2）で縮めたもの
inline const std::vector<std::uint8_t> kZx0Vector = {
    0x05, 0xa1, 0x59, 0x38, 0x39, 0x36, 0x30, 0x20, 0x5a, 0x58, 0x30, 0x20, 0x64, 0x65, 0x63, 0x6f,
    0x6d, 0x70, 0x72, 0x65, 0x73, 0xe1, 0x69, 0x6f, 0x6e, 0x20, 0x74, 0xf1, 0xa6, 0x74, 0x20, 0x76,
    0x65, 0x63, 0x6f, 0x72, 0x2e, 0xe5, 0xb6, 0x41, 0x04, 0xd5, 0x55, 0x60,
};
inline constexpr std::string_view kZx0Sentence = "Y8960 ZX0 decompression test vector. ";
inline constexpr std::size_t kZx0VectorSize = 3000;

inline std::vector<std::uint8_t> zx0_vector_output()
{
    std::vector<std::uint8_t> v(kZx0VectorSize);
    for (std::size_t i = 0; i < v.size(); ++i) v[i] = static_cast<std::uint8_t>(kZx0Sentence[i % kZx0Sentence.size()]);
    return v;
}

}  // namespace y8960upd::test
