// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "updater/updater.hpp"

#include <algorithm>
#include <format>
#include <stdexcept>
#include <vector>

namespace y8960upd::updater {

namespace {

using y8960::ConfigRom;
using y8960::Region;
using y8960::RomError;

constexpr std::size_t kChunk = 0x4000;
constexpr std::size_t kBlankCheck = 256;
constexpr std::uint32_t kBiosAddress = 0x7E0000;

std::string address(std::uint32_t a) { return std::format("{:06X}h", a); }

void report(std::ostream& out, const RomError& e)
{
    switch (e.kind) {
    case RomError::Kind::ReadTimeout: out << "\nConfigROM read error.\n"; break;
    case RomError::Kind::EraseTimeout: out << "\nErase error.\n"; break;
    case RomError::Kind::EraseNotAccepted:
        out << std::format("\nErase was not accepted (BUSY never set, SR1={:02X}h).\n", e.status)
            << "The ConfigROM may be write protected.\n";
        break;
    case RomError::Kind::WriteTimeout: out << "\nWrite error.\n"; break;
    }
}

bool verify(ConfigRom& rom, std::uint32_t at, std::span<const std::uint8_t> want, std::ostream& out)
{
    auto got = rom.read(at, want.size());
    auto [w, g] = std::mismatch(want.begin(), want.end(), got.begin());
    if (w == want.end()) return true;
    auto off = static_cast<std::uint32_t>(w - want.begin());
    out << std::format("\nVerify error at {}: expected {:02X}h, read {:02X}h (last SR1={:02X}h).\n", address(at + off), *w, *g, rom.last_status());
    return false;
}

// 原本と同じく、消去を確かめるのはブロックの先頭 256 バイトだけ
bool erase_blocks(ConfigRom& rom, Region region, std::uint32_t first, std::size_t count, std::ostream& out)
{
    for (std::size_t k = 0; k < count; ++k) {
        auto block = static_cast<std::uint32_t>(first + k * y8960::kBlockSize);
        out << "\rErase   : " << address(block) << std::flush;
        rom.erase_block(region, block);
        auto head = rom.read(block, kBlankCheck);
        auto it = std::find_if(head.begin(), head.end(), [](auto b) { return b != 0xFF; });
        if (it != head.end()) {
            auto at = static_cast<std::uint32_t>(block + (it - head.begin()));
            out << std::format("\nErase check failed at {}: read {:02X}h (last SR1={:02X}h).\n", address(at), *it, rom.last_status());
            return false;
        }
    }
    out << " OK\n";
    return true;
}

bool program(ConfigRom& rom, Region region, std::uint32_t start, const std::vector<std::uint8_t>& body, std::ostream& out)
{
    std::size_t blocks = (body.size() + y8960::kBlockSize - 1) / y8960::kBlockSize;
    if (!erase_blocks(rom, region, start, blocks, out)) return false;
    for (std::size_t off = 0; off < body.size(); off += kChunk) {
        auto at = static_cast<std::uint32_t>(start + off);
        out << "\rWrite   : " << address(at) << std::flush;
        std::span<const std::uint8_t> chunk(body.data() + off, std::min(kChunk, body.size() - off));
        rom.write(region, at, chunk);
        if (!verify(rom, at, chunk, out)) return false;
    }
    out << "\rWrite   : " << address(static_cast<std::uint32_t>(start + body.size())) << " OK\n";
    return true;
}

// 7D0000h のブロックを消して、2 つのバージョンの 32 バイトだけを書き戻す。書き換えない側は
// Flash から読み戻した値を使う
bool update_version(ConfigRom& rom, const Request& request, std::ostream& out)
{
    out << "Version update\n";
    auto v = rom.read(y8960::kVersionAddress, y8960::kVersionLength * 2);
    if (request.config) std::copy(request.config->version.begin(), request.config->version.end(), v.begin());
    if (request.bios) std::copy(request.bios->version.begin(), request.bios->version.end(), v.begin() + y8960::kVersionLength);
    // 消してから書き戻すまでに止めると、両方のバージョンが消える
    mga::Mga::InterruptHold hold(rom.mga());
    if (!erase_blocks(rom, Region::Version, y8960::range_of(Region::Version).begin, 1, out)) return false;
    rom.write(Region::Version, y8960::kVersionAddress, v);
    if (!verify(rom, y8960::kVersionAddress, v, out)) return false;
    out << " OK\n";
    return true;
}

}  // namespace

bool show_versions(ConfigRom& rom, std::ostream& out)
{
    try {
        auto v = rom.read(y8960::kVersionAddress, y8960::kVersionLength * 2);
        std::span<const std::uint8_t> s(v);
        out << "Cfg. Ver: " << image::display_version(s.first<y8960::kVersionLength>()) << "\n";
        out << "BIOS Ver: " << image::display_version(s.subspan<y8960::kVersionLength, y8960::kVersionLength>()) << "\n";
        return true;
    } catch (const RomError& e) {
        report(out, e);
        return false;
    }
}

bool update(ConfigRom& rom, const Request& request, std::ostream& out)
{
    if (!request.config && !request.bios) throw std::logic_error("nothing to update");
    try {
        if (request.config && !program(rom, Region::Config, 0, request.config->body, out)) return false;
        if (request.bios && !program(rom, Region::Bios, kBiosAddress, request.bios->body, out)) return false;
        if (!update_version(rom, request, out)) return false;
    } catch (const RomError& e) {
        report(out, e);
        return false;
    }
    if (request.config) out << "\nConfig data has been updated.\n";
    if (request.bios) out << "\nBIOS data has been updated.\nReset the MSX to load the new BIOS.\n";
    return true;
}

}  // namespace y8960upd::updater
