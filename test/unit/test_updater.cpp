// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "fake/fake_mga.hpp"
#include "image/image.hpp"
#include "mga/mga.hpp"
#include "updater/updater.hpp"
#include "y8960/config_rom.hpp"
#include "y8960/system_controller.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <sstream>
#include <string>
#include <vector>

using y8960upd::image::Image;
using y8960upd::image::make_version;
using y8960upd::mga::Mga;
using y8960upd::test::FakeMga;
using y8960upd::updater::Request;
using y8960upd::y8960::ConfigRom;
using y8960upd::y8960::PowerOnWait;
using y8960upd::y8960::SystemController;

namespace {

using namespace std::chrono_literals;
using namespace std::string_view_literals;

struct Bench {
    FakeMga fake;
    Mga mga{fake};
    SystemController sc{mga, PowerOnWait{20, 0ms, 300}};
    std::ostringstream out;

    // 書き込む前のカートリッジ：Config・バージョン・BIOS がどれも書いてある
    Bench()
    {
        auto& f = fake.cart.flash;
        std::fill(f.begin(), f.begin() + 0x7D0000, std::uint8_t{0xC5});
        std::fill(f.begin() + 0x7D0000, f.begin() + 0x7E0000, std::uint8_t{0x3A});
        put(0x7DFF00, "Y8960_v0.9.3\0\0\0\0"sv);
        put(0x7DFF10, "MSXSSE_v0.5.1\0\0\0"sv);
        std::fill(f.begin() + 0x7E0000, f.end(), std::uint8_t{0x5C});
        sc.open();
    }

    void put(std::uint32_t at, std::string_view s) { std::copy(s.begin(), s.end(), fake.cart.flash.begin() + at); }

    std::string flash_string(std::uint32_t at, std::size_t n) const
    {
        return {fake.cart.flash.begin() + at, fake.cart.flash.begin() + at + n};
    }

    bool flash_all(std::uint32_t begin, std::uint32_t end, std::uint8_t v) const
    {
        return std::all_of(fake.cart.flash.begin() + begin, fake.cart.flash.begin() + end, [v](auto b) { return b == v; });
    }

    bool flash_equals(std::uint32_t at, const std::vector<std::uint8_t>& want) const
    {
        return std::equal(want.begin(), want.end(), fake.cart.flash.begin() + at);
    }
};

std::vector<std::uint8_t> body_of(std::size_t n, unsigned seed)
{
    std::vector<std::uint8_t> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = static_cast<std::uint8_t>(i * 13 + seed + (i >> 9));
    // 消去の後のままにするバイトも混ぜる
    for (std::size_t i = 5; i < n; i += 97) v[i] = 0xFF;
    return v;
}

Image image_of(std::vector<std::uint8_t> body, std::string_view version)
{
    Image img;
    img.version = make_version(version);
    img.body = std::move(body);
    return img;
}

}  // namespace

TEST_CASE("versions are read from 7DFF00h")
{
    Bench b;
    b.put(0x7DFF10, "MSXSSE\x01v0.5.1\0\0\0"sv);
    ConfigRom rom(b.mga);
    CHECK(y8960upd::updater::show_versions(rom, b.out));
    CHECK(b.out.str() == "Cfg. Ver: Y8960_v0.9.3\nBIOS Ver: MSXSSE?v0.5.1\n");
}

TEST_CASE("a BIOS update rewrites the BIOS area and only the BIOS version")
{
    Bench b;
    const auto body = body_of(0x20000, 1);
    ConfigRom rom(b.mga);
    Request req;
    req.bios = image_of(body, "MSXSSE_v0.5.4");
    CHECK(y8960upd::updater::update(rom, req, b.out));

    CHECK(b.flash_equals(0x7E0000, body));
    CHECK(b.flash_all(0x000000, 0x7D0000, 0xC5));
    CHECK(b.flash_string(0x7DFF00, 32) == std::string("Y8960_v0.9.3\0\0\0\0MSXSSE_v0.5.4\0\0\0", 32));
    // バージョンのブロックは、2 つのバージョンのほかは消えたまま（原本と同じ）
    CHECK(b.flash_all(0x7D0000, 0x7DFF00, 0xFF));
    CHECK(b.flash_all(0x7DFF20, 0x7E0000, 0xFF));
    CHECK(b.fake.cart.erased_blocks == std::vector<std::uint32_t>{0x7E0000, 0x7F0000, 0x7D0000});
    CHECK(b.out.str() ==
          "\rErase   : 7E0000h\rErase   : 7F0000h OK\n"
          "\rWrite   : 7E0000h\rWrite   : 7E4000h\rWrite   : 7E8000h\rWrite   : 7EC000h"
          "\rWrite   : 7F0000h\rWrite   : 7F4000h\rWrite   : 7F8000h\rWrite   : 7FC000h"
          "\rWrite   : 800000h OK\n"
          "Version update\n"
          "\rErase   : 7D0000h OK\n"
          " OK\n"
          "\nBIOS data has been updated.\n"
          "Reset the MSX to load the new BIOS.\n");
}

TEST_CASE("a Config update keeps the BIOS and its version")
{
    Bench b;
    const auto body = body_of(0x10001, 2);
    ConfigRom rom(b.mga);
    Request req;
    req.config = image_of(body, "Y8960_v0.9.5");
    CHECK(y8960upd::updater::update(rom, req, b.out));

    CHECK(b.flash_equals(0x000000, body));
    CHECK(b.flash_all(0x010001, 0x020000, 0xFF));
    CHECK(b.flash_all(0x020000, 0x7D0000, 0xC5));
    CHECK(b.flash_all(0x7E0000, 0x800000, 0x5C));
    CHECK(b.flash_string(0x7DFF00, 32) == std::string("Y8960_v0.9.5\0\0\0\0MSXSSE_v0.5.1\0\0\0", 32));
    CHECK(b.fake.cart.erased_blocks == std::vector<std::uint32_t>{0x000000, 0x010000, 0x7D0000});
    CHECK(b.out.str().ends_with("\rWrite   : 010001h OK\nVersion update\n\rErase   : 7D0000h OK\n OK\n\nConfig data has been updated.\n"));
}

TEST_CASE("Config and BIOS together write the version block once, after both")
{
    Bench b;
    const auto config = body_of(0x8000, 3);
    const auto bios = body_of(0x20000, 4);
    ConfigRom rom(b.mga);
    Request req;
    req.config = image_of(config, "Y8960_v0.9.5");
    req.bios = image_of(bios, "");
    CHECK(y8960upd::updater::update(rom, req, b.out));

    CHECK(b.flash_equals(0x000000, config));
    CHECK(b.flash_equals(0x7E0000, bios));
    CHECK(b.flash_string(0x7DFF00, 32) == std::string("Y8960_v0.9.5\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 32));
    CHECK(b.fake.cart.erased_blocks == std::vector<std::uint32_t>{0x000000, 0x7E0000, 0x7F0000, 0x7D0000});
    CHECK(b.out.str().ends_with("\nConfig data has been updated.\n\nBIOS data has been updated.\nReset the MSX to load the new BIOS.\n"));
}

TEST_CASE("a verify error stops before the BIOS and the version block")
{
    Bench b;
    b.fake.cart.broken_address = 0x000123;
    b.put(0x000123, "\xFF"sv);
    auto config = body_of(0x8000, 5);
    config[0x123] = 0x42;
    ConfigRom rom(b.mga);
    Request req;
    req.config = image_of(config, "Y8960_v0.9.5");
    req.bios = image_of(body_of(0x20000, 6), "");
    CHECK_FALSE(y8960upd::updater::update(rom, req, b.out));

    CHECK(b.out.str().ends_with("\rWrite   : 000000h\nVerify error at 000123h: expected 42h, read FFh (last SR1=00h).\n"));
    CHECK(b.fake.cart.erased_blocks == std::vector<std::uint32_t>{0x000000});
    CHECK(b.flash_all(0x7E0000, 0x800000, 0x5C));
    CHECK(b.flash_string(0x7DFF00, 16) == std::string("Y8960_v0.9.3\0\0\0\0", 16));
}

TEST_CASE("an erase check failure stops before writing")
{
    Bench b;
    b.fake.cart.broken_address = 0x7F0080;
    ConfigRom rom(b.mga);
    Request req;
    req.bios = image_of(body_of(0x20000, 7), "");
    CHECK_FALSE(y8960upd::updater::update(rom, req, b.out));

    CHECK(b.out.str() == "\rErase   : 7E0000h\rErase   : 7F0000h\nErase check failed at 7F0080h: read 5Ch (last SR1=00h).\n");
    CHECK(b.fake.cart.writes == 0);
}

TEST_CASE("an erase without BUSY is reported as the original tool does")
{
    Bench b;
    b.fake.cart.ignore_erase = true;
    ConfigRom rom(b.mga);
    Request req;
    req.bios = image_of(body_of(0x20000, 8), "");
    CHECK_FALSE(y8960upd::updater::update(rom, req, b.out));

    CHECK(b.out.str() == "\rErase   : 7E0000h\nErase was not accepted (BUSY never set, SR1=00h).\nThe ConfigROM may be write protected.\n");
    CHECK(b.fake.cart.writes == 0);
}

TEST_CASE("a write that never finishes is a write error")
{
    Bench b;
    b.fake.cart.busy_reads_after_write = 1'000'000'000;
    ConfigRom rom(b.mga);
    Request req;
    req.bios = image_of(body_of(0x20000, 9), "");
    CHECK_FALSE(y8960upd::updater::update(rom, req, b.out));

    CHECK(b.out.str().ends_with(" OK\n\rWrite   : 7E0000h\nWrite error.\n"));
    CHECK(b.fake.cart.writes == 1);
}
