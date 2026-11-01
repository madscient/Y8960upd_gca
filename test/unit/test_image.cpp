// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "image/image.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

using y8960upd::image::display_version;
using y8960upd::image::FileContent;
using y8960upd::image::ImageError;
using y8960upd::image::make_version;
using y8960upd::image::parse_bios;
using y8960upd::image::parse_config;
using y8960upd::image::read_file;
using y8960upd::image::Version;
using Reason = y8960upd::image::ImageError::Reason;

namespace {

// 原本（hra1129/Y8960_Cartridge の 5ad5d06）の tool/config/header.bin と tool/bios/header.bin
const std::vector<std::uint8_t> kConfigHeader = {
    0x59, 0x38, 0x39, 0x36, 0x30, 0x5f, 0x54, 0x50, 0x32, 0x35, 0x4b, 0x5f, 0x43, 0x41, 0x52, 0x54,
    0x52, 0x49, 0x44, 0x47, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x59, 0x38, 0x39, 0x36, 0x30, 0x5f, 0x76, 0x30, 0x2e, 0x39, 0x2e, 0x35, 0x00, 0x00, 0x00, 0x00,
};
const std::vector<std::uint8_t> kBiosHeader = {
    0x59, 0x38, 0x39, 0x36, 0x30, 0x5f, 0x42, 0x49, 0x4f, 0x53, 0x5f, 0x49, 0x4d, 0x41, 0x47, 0x45,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x4d, 0x53, 0x58, 0x53, 0x53, 0x45, 0x5f, 0x76, 0x30, 0x2e, 0x35, 0x2e, 0x32, 0x00, 0x00, 0x00,
};

std::vector<std::uint8_t> body_of(std::size_t n)
{
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::uint8_t>(i * 31 + (i >> 8));
    return v;
}

FileContent file_of(const std::vector<std::uint8_t>& head, const std::vector<std::uint8_t>& body)
{
    FileContent f;
    f.bytes = head;
    f.bytes.insert(f.bytes.end(), body.begin(), body.end());
    f.size = f.bytes.size();
    return f;
}

Version version_of(const std::vector<std::uint8_t>& header)
{
    Version v{};
    std::copy(header.begin() + 32, header.begin() + 48, v.begin());
    return v;
}

template <class F>
Reason reason_of(F&& f)
{
    try {
        f();
    } catch (const ImageError& e) {
        return e.reason;
    }
    FAIL("no ImageError");
    return Reason::Open;
}

}  // namespace

TEST_CASE("a Config data image gives its version and body")
{
    const auto body = body_of(895288);
    auto img = parse_config(file_of(kConfigHeader, body));
    CHECK(img.has_header);
    CHECK(img.version == version_of(kConfigHeader));
    CHECK(img.body == body);
}

TEST_CASE("the Config data body is 1 to 7D0000h bytes")
{
    CHECK(reason_of([&] { parse_config(file_of(kConfigHeader, {})); }) == Reason::Size);
    CHECK(parse_config(file_of(kConfigHeader, body_of(1))).body.size() == 1);
    CHECK(parse_config(file_of(kConfigHeader, body_of(0x7D0000))).body.size() == 0x7D0000);
    CHECK(reason_of([&] { parse_config(file_of(kConfigHeader, body_of(0x7D0001))); }) == Reason::Size);

    FileContent huge;
    huge.bytes = kConfigHeader;
    huge.size = 0x1000000;
    CHECK(reason_of([&] { parse_config(huge); }) == Reason::Size);
}

TEST_CASE("a file that is not a Config data image is a format error")
{
    CHECK(reason_of([&] { parse_config(file_of({kConfigHeader.begin(), kConfigHeader.begin() + 47}, {})); }) == Reason::Format);
    CHECK(reason_of([&] { parse_config(file_of(kBiosHeader, body_of(0x20000))); }) == Reason::Format);
    // ヘッダの無い Config は受け付けない
    CHECK(reason_of([&] { parse_config(file_of({}, body_of(895288))); }) == Reason::Format);
    // シグネチャの 32 バイトは、後ろの 00h まで一致しなければならない
    auto bad = kConfigHeader;
    bad[31] = 0x20;
    CHECK(reason_of([&] { parse_config(file_of(bad, body_of(16))); }) == Reason::Format);
    // シグネチャを見るのが大きさより先
    FileContent huge;
    huge.bytes = kBiosHeader;
    huge.size = 0x1000000;
    CHECK(reason_of([&] { parse_config(huge); }) == Reason::Format);
}

TEST_CASE("a BIOS data image gives its version and a 128KB body")
{
    const auto body = body_of(0x20000);
    auto img = parse_bios(file_of(kBiosHeader, body), std::nullopt);
    CHECK(img.has_header);
    CHECK(img.version == version_of(kBiosHeader));
    CHECK(img.body == body);
    CHECK(reason_of([&] { parse_bios(file_of(kBiosHeader, body_of(0x20001)), std::nullopt); }) == Reason::Size);
    CHECK(reason_of([&] { parse_bios(file_of(kBiosHeader, body_of(0x1FFFF)), std::nullopt); }) == Reason::Size);
    CHECK(reason_of([&] { parse_bios(file_of(kBiosHeader, body), std::string("x")); }) == Reason::BiosVersionWithHeader);
}

TEST_CASE("a headerless 128KB BIOS body takes its version from the option")
{
    const auto body = body_of(0x20000);
    auto img = parse_bios(file_of({}, body), std::nullopt);
    CHECK_FALSE(img.has_header);
    CHECK(img.version == Version{});
    CHECK(img.body == body);

    img = parse_bios(file_of({}, body), std::string("MSXSSE_v0.5.4"));
    const Version want = {0x4d, 0x53, 0x58, 0x53, 0x53, 0x45, 0x5f, 0x76, 0x30, 0x2e, 0x35, 0x2e, 0x34, 0x00, 0x00, 0x00};
    CHECK(img.version == want);

    CHECK(reason_of([&] { parse_bios(file_of({}, body_of(0x1FFFF)), std::nullopt); }) == Reason::Format);
    CHECK(reason_of([&] { parse_bios(file_of({}, body_of(0x20001)), std::nullopt); }) == Reason::Format);
    CHECK(reason_of([&] { parse_bios(file_of(kConfigHeader, body_of(0x20000 - 48)), std::nullopt); }) == Reason::Format);
}

TEST_CASE("the version option must fit in 16 printable bytes")
{
    CHECK(make_version("0123456789ABCDEF")[15] == 0x46);
    CHECK(reason_of([] { make_version("0123456789ABCDEFG"); }) == Reason::BiosVersionTooLong);
    CHECK(make_version(" ~")[1] == 0x7E);
    CHECK(reason_of([] { make_version("a\x1F"); }) == Reason::BiosVersionNotPrintable);
    CHECK(reason_of([] { make_version("a\x7F"); }) == Reason::BiosVersionNotPrintable);
    CHECK(reason_of([] { make_version("v\xC3\xA9"); }) == Reason::BiosVersionNotPrintable);
    CHECK(make_version("") == Version{});
}

TEST_CASE("versions are shown as the original tool shows them")
{
    CHECK(display_version(version_of(kBiosHeader)) == "MSXSSE_v0.5.2");
    Version v{};
    CHECK(display_version(v) == "(none)");
    v.fill(0xFF);
    CHECK(display_version(v) == "(none)");
    v = {0x41, 0x01, 0x7F, 0xFF, 0x42, 0x00, 0x43};
    CHECK(display_version(v) == "A???B");
    v.fill(0x5A);
    CHECK(display_version(v) == std::string(16, 'Z'));
}

TEST_CASE("read_file reads the whole file, or only the header of a file of 16MB or more")
{
    const auto dir = std::filesystem::temp_directory_path();
    const auto small = dir / "y8960upd_gca_test_small.bin";
    const auto large = dir / "y8960upd_gca_test_large.bin";
    const auto body = body_of(1000);
    {
        std::ofstream(small, std::ios::binary).write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
        std::ofstream out(large, std::ios::binary);
        out.write(reinterpret_cast<const char*>(kConfigHeader.data()), 48);
        out.seekp(0x1000000 - 1);
        out.put('\0');
    }
    auto f = read_file(small);
    CHECK(f.size == 1000);
    CHECK(f.bytes == body);
    f = read_file(large);
    CHECK(f.size == 0x1000000);
    CHECK(f.bytes == kConfigHeader);
    CHECK(reason_of([&] { parse_config(f); }) == Reason::Size);
    std::filesystem::remove(small);
    std::filesystem::remove(large);
    CHECK(reason_of([&] { read_file(small); }) == Reason::Open);
}
