// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace y8960upd::image {

inline constexpr std::size_t kHeaderSize = 48;
inline constexpr std::size_t kSignatureSize = 32;
inline constexpr std::size_t kVersionSize = 16;
inline constexpr std::size_t kConfigMaxBody = 0x7D0000;
inline constexpr std::size_t kBiosBody = 0x20000;
// 原本は FCB のファイル長の最上位バイトが 0 でなければ大きさの誤りにする
inline constexpr std::uint64_t kFileSizeLimit = 0x1000000;

using Version = std::array<std::uint8_t, kVersionSize>;

class ImageError : public std::runtime_error {
public:
    enum class Reason {
        Open,                 // Cannot open the file.
        Read,                 // File read error.
        Format,               // This file is not a valid image for this option.
        Size,                 // Invalid data size.
        Decompress,           // Decompress error.
        BiosVersionTooLong,
        BiosVersionNotPrintable,
        BiosVersionWithHeader,  // ヘッダ付きの BIOS data に --bios-version を付けた
    };
    ImageError(Reason reason, const std::string& what) : std::runtime_error(what), reason(reason) {}

    Reason reason;
};

// ファイルの中身。大きさが kFileSizeLimit 以上なら、bytes は先頭の kHeaderSize バイトまでしか持たない
struct FileContent {
    std::vector<std::uint8_t> bytes;
    std::uint64_t size = 0;
};

FileContent read_file(const std::filesystem::path& path);

struct Image {
    bool has_header = true;
    Version version{};
    std::vector<std::uint8_t> body;
};

// ヘッダ付きの Config data だけを受け付ける。圧縮していれば展開した本体を返す
Image parse_config(const FileContent& file);

// ヘッダ付きの BIOS data（圧縮していれば展開する）か、ヘッダの無い 128KB の本体。bios_version は
// ヘッダの無い本体のときだけ受け付け、省略したら 16 バイトの 00h にする
Image parse_bios(const FileContent& file, const std::optional<std::string>& bios_version);

// 16 バイトに収まり、20h〜7Eh だけからなる文字列を、残りを 00h で埋めた 16 バイトにする
Version make_version(std::string_view text);

// 原本の put_version と同じ。先頭が FFh か 00h なら (none)、表示できない文字は ?、00h で終わる
std::string display_version(std::span<const std::uint8_t, kVersionSize> v);

}  // namespace y8960upd::image
