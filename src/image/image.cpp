// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "image/image.hpp"

#include <algorithm>
#include <fstream>

namespace y8960upd::image {

namespace {

using Signature = std::array<std::uint8_t, kSignatureSize>;

constexpr Signature signature_of(std::string_view s)
{
    Signature sig{};
    for (std::size_t i = 0; i < s.size(); ++i) sig[i] = static_cast<std::uint8_t>(s[i]);
    return sig;
}

constexpr Signature kConfigSignature = signature_of("Y8960_TP25K_CARTRIDGE");
constexpr Signature kBiosSignature = signature_of("Y8960_BIOS_IMAGE");

bool has_signature(const FileContent& file, const Signature& sig)
{
    return file.size >= kHeaderSize && file.bytes.size() >= kHeaderSize &&
           std::equal(sig.begin(), sig.end(), file.bytes.begin());
}

Image headered(const FileContent& file)
{
    Image img;
    std::copy_n(file.bytes.begin() + kSignatureSize, kVersionSize, img.version.begin());
    img.body.assign(file.bytes.begin() + kHeaderSize, file.bytes.end());
    return img;
}

}  // namespace

FileContent read_file(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) throw ImageError(ImageError::Reason::Open, "cannot open " + path.string());
    std::error_code ec;
    FileContent file;
    file.size = std::filesystem::file_size(path, ec);
    if (ec) throw ImageError(ImageError::Reason::Open, "cannot open " + path.string());

    std::uint64_t want = file.size >= kFileSizeLimit ? std::min<std::uint64_t>(file.size, kHeaderSize) : file.size;
    file.bytes.resize(static_cast<std::size_t>(want));
    in.read(reinterpret_cast<char*>(file.bytes.data()), static_cast<std::streamsize>(want));
    if (static_cast<std::uint64_t>(in.gcount()) != want) throw ImageError(ImageError::Reason::Read, "cannot read " + path.string());
    return file;
}

// 原本と同じく、シグネチャを先に見て、次に大きさを見る
Image parse_config(const FileContent& file)
{
    if (!has_signature(file, kConfigSignature)) throw ImageError(ImageError::Reason::Format, "not a Config data image");
    if (file.size >= kFileSizeLimit) throw ImageError(ImageError::Reason::Size, "file too large");
    std::uint64_t body = file.size - kHeaderSize;
    if (body == 0 || body > kConfigMaxBody) throw ImageError(ImageError::Reason::Size, "Config data size out of range");
    return headered(file);
}

Image parse_bios(const FileContent& file, const std::optional<std::string>& bios_version)
{
    if (has_signature(file, kBiosSignature)) {
        if (file.size >= kFileSizeLimit || file.size - kHeaderSize != kBiosBody) {
            throw ImageError(ImageError::Reason::Size, "BIOS data size is not 128KB");
        }
        if (bios_version) throw ImageError(ImageError::Reason::BiosVersionWithHeader, "--bios-version given for a BIOS data image with a header");
        return headered(file);
    }
    if (file.size != kBiosBody || has_signature(file, kConfigSignature)) {
        throw ImageError(ImageError::Reason::Format, "neither a BIOS data image nor a 128KB BIOS body");
    }
    Image img;
    img.has_header = false;
    if (bios_version) img.version = make_version(*bios_version);
    img.body = file.bytes;
    return img;
}

Version make_version(std::string_view text)
{
    if (text.size() > kVersionSize) throw ImageError(ImageError::Reason::BiosVersionTooLong, "version string longer than 16 bytes");
    Version v{};
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x20 || c >= 0x7F) throw ImageError(ImageError::Reason::BiosVersionNotPrintable, "version string has a character that cannot be shown");
        v[i] = c;
    }
    return v;
}

std::string display_version(std::span<const std::uint8_t, kVersionSize> v)
{
    if (v[0] == 0xFF || v[0] == 0x00) return "(none)";
    std::string s;
    for (auto c : v) {
        if (c == 0x00) break;
        s.push_back(c < 0x20 || c >= 0x7F ? '?' : static_cast<char>(c));
    }
    return s;
}

}  // namespace y8960upd::image
