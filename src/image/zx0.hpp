// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021, Einar Saukas
// Copyright (c) 2026 MadScient
// ZX0 の参照の展開（Einar Saukas の dzx0.c）を移したもの。ライセンスの全文は LICENSE-zx0.txt

#pragma once

#include <cstdint>
#include <span>

namespace y8960upd::image::zx0 {

// 独立した ZX0 のストリーム（標準の形、v2）を 1 本展開する。in をちょうど使い切って out をちょうど
// 埋めたときだけ true。out の外に書くことも、out の先頭より前を読むこともない
bool decompress(std::span<const std::uint8_t> in, std::span<std::uint8_t> out);

}  // namespace y8960upd::image::zx0
