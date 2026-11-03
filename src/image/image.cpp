// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "image/image.hpp"

#include "image/zx0.hpp"

#include <algorithm>
#include <fstream>

namespace y8960upd::image {

namespace {

using Reason = ImageError::Reason;
using Signature = std::array<std::uint8_t, kSignatureSize>;

// シグネチャの最後のバイトは形式の印で、照合しない
constexpr std::size_t kMarkerOffset = kSignatureSize - 1;
constexpr std::uint8_t kMarkerRaw = 0x00;
constexpr std::uint8_t kMarkerZx0 = 0x01;
constexpr std::size_t kSubheaderSize = 6;
constexpr std::size_t kMaxBlock = 0x4000;
constexpr std::uint32_t kStoredFrame = 0x8000;

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
           std::equal(sig.begin(), sig.begin() + kMarkerOffset, file.bytes.begin());
}

std::uint32_t le16(std::span<const std::uint8_t> p, std::size_t at)
{
    return static_cast<std::uint32_t>(p[at] | p[at + 1] << 8);
}

std::uint32_t le32(std::span<const std::uint8_t> p, std::size_t at)
{
    return le16(p, at) | le16(p, at + 2) << 16;
}

// p は +48 以降。誤りの分け方と見る順は原本の asm に揃え、asm が見ない壊れ方も断る
std::vector<std::uint8_t> unpack(std::span<const std::uint8_t> p, std::uint64_t min_body, std::uint64_t max_body)
{
    if (p.size() < kSubheaderSize) throw ImageError(Reason::Read, "the compressed header is cut short");
    std::uint32_t original = le32(p, 0);
    std::size_t block = le16(p, 4);
    if (original > 0xFFFFFF) throw ImageError(Reason::Size, "the original size does not fit in 24 bits");
    if (block == 0 || block > kMaxBlock) throw ImageError(Reason::Format, "block size out of range");
    if (original < min_body || original > max_body) throw ImageError(Reason::Size, "original size out of range");

    std::vector<std::uint8_t> body(original);
    std::size_t pos = kSubheaderSize;
    for (std::size_t done = 0; done < body.size();) {
        std::size_t want = std::min(block, body.size() - done);
        if (p.size() - pos < 2) throw ImageError(Reason::Read, "a frame is cut short");
        std::uint32_t word = le16(p, pos);
        pos += 2;
        bool stored = (word & kStoredFrame) != 0;
        std::size_t len = word & ~kStoredFrame;
        if (stored ? len != want : (len == 0 || len > kMaxBlock)) throw ImageError(Reason::Format, "bad frame length");
        if (p.size() - pos < len) throw ImageError(Reason::Read, "a frame is cut short");
        auto frame = p.subspan(pos, len);
        auto dst = std::span(body).subspan(done, want);
        if (stored) {
            std::copy(frame.begin(), frame.end(), dst.begin());
        } else if (!zx0::decompress(frame, dst)) {
            throw ImageError(Reason::Decompress, "a ZX0 block is broken");
        }
        pos += len;
        done += want;
    }
    if (pos != p.size()) throw ImageError(Reason::Format, "bytes after the last frame");
    return body;
}

// 原本と同じく、形式の印を見てから大きさを見る
Image headered(const FileContent& file, std::uint64_t min_body, std::uint64_t max_body)
{
    std::uint8_t marker = file.bytes[kMarkerOffset];
    if (marker != kMarkerRaw && marker != kMarkerZx0) throw ImageError(Reason::Format, "unknown format marker");
    if (file.size >= kFileSizeLimit) throw ImageError(Reason::Size, "file too large");
    Image img;
    std::copy_n(file.bytes.begin() + kSignatureSize, kVersionSize, img.version.begin());
    auto rest = std::span(file.bytes).subspan(kHeaderSize);
    if (marker == kMarkerZx0) {
        img.body = unpack(rest, min_body, max_body);
    } else {
        if (rest.size() < min_body || rest.size() > max_body) throw ImageError(Reason::Size, "data size out of range");
        img.body.assign(rest.begin(), rest.end());
    }
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
    return headered(file, 1, kConfigMaxBody);
}

Image parse_bios(const FileContent& file, const std::optional<std::string>& bios_version)
{
    if (has_signature(file, kBiosSignature)) {
        Image img = headered(file, kBiosBody, kBiosBody);
        if (bios_version) throw ImageError(ImageError::Reason::BiosVersionWithHeader, "--bios-version given for a BIOS data image with a header");
        return img;
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
