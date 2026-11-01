// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#pragma once

#include "image/image.hpp"
#include "y8960/config_rom.hpp"

#include <optional>
#include <ostream>

namespace y8960upd::updater {

struct Request {
    std::optional<image::Image> config;
    std::optional<image::Image> bios;
};

// 表示は原本（spec の 5 章）の文言。進み具合は CR で同じ行に上書きする。
// ConfigROM の誤りは表示して false を返す。MGA との通信の誤りは例外のまま投げる

// Cfg. Ver と BIOS Ver を表示する
bool show_versions(y8960::ConfigRom& rom, std::ostream& out);

// Config、BIOS の順に、消去 → 書き込みとベリファイ。どちらも通ったときだけ、バージョンの
// ブロックを 1 回書き直す
bool update(y8960::ConfigRom& rom, const Request& request, std::ostream& out);

}  // namespace y8960upd::updater
