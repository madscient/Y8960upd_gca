// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "fake/fake_mga.hpp"
#include "mga/mga.hpp"
#include "y8960/config_rom.hpp"
#include "y8960/system_controller.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <vector>

using y8960upd::mga::Mga;
using y8960upd::test::FakeMga;
using y8960upd::y8960::ConfigRom;
using y8960upd::y8960::NotDetectedError;
using y8960upd::y8960::PowerOnWait;
using y8960upd::y8960::Region;
using y8960upd::y8960::RomError;
using y8960upd::y8960::SystemController;

namespace {

using namespace std::chrono_literals;

constexpr PowerOnWait kNoSleep{20, 0ms, 300};

std::vector<std::uint8_t> pattern(std::size_t n, unsigned seed)
{
    std::vector<std::uint8_t> v(n);
    unsigned x = seed;
    for (auto& b : v) {
        x = x * 1103515245u + 12345u;
        b = static_cast<std::uint8_t>(x >> 16);
    }
    return v;
}

long count_of(const FakeMga& fake, const std::string& cmd)
{
    return static_cast<long>(std::count(fake.commands.begin(), fake.commands.end(), cmd));
}

// 範囲の外の Flash が全部 FFh か
bool blank_outside(const FakeMga& fake, std::uint32_t begin, std::uint32_t end)
{
    const auto& f = fake.cart.flash;
    return std::all_of(f.begin(), f.begin() + begin, [](auto b) { return b == 0xFF; }) &&
           std::all_of(f.begin() + end, f.end(), [](auto b) { return b == 0xFF; });
}

}  // namespace

TEST_CASE("open powers the slot, waits for the enabler, and selects the ConfigROM")
{
    FakeMga fake;
    fake.cart.unready_40h_reads_after_power_on = 30;
    Mga mga(fake);
    {
        SystemController sc(mga, kNoSleep);
        sc.open();
        CHECK(fake.powered);
        // 7Fh でない 30 回の後に、7Fh が 20 回続くまで読む
        CHECK(count_of(fake, "IORD") >= 30 + 20);
        CHECK(count_of(fake, "LSCR") == 1);
        CHECK(mga.io_read(0x42) == 0xFE);
    }
    CHECK_FALSE(fake.powered);
    CHECK(count_of(fake, "SPOFF") == 1);
}

TEST_CASE("open refuses an empty slot without powering it")
{
    FakeMga fake;
    fake.cartridge_inserted = false;
    Mga mga(fake);
    SystemController sc(mga, kNoSleep);
    CHECK_THROWS_AS(sc.open(), NotDetectedError);
    CHECK(count_of(fake, "SPON") == 0);
    sc.close();
    CHECK(count_of(fake, "SPOFF") == 0);
}

TEST_CASE("an enabler that never settles is not detected, and the slot is powered off")
{
    FakeMga fake;
    fake.cart.unready_40h_reads_after_power_on = 1000;
    Mga mga(fake);
    {
        SystemController sc(mga, kNoSleep);
        CHECK_THROWS_AS(sc.open(), NotDetectedError);
    }
    CHECK_FALSE(fake.powered);
}

TEST_CASE("another device on the System Controller is not detected")
{
    FakeMga fake;
    fake.cart.device_id = 0x03;
    Mga mga(fake);
    {
        SystemController sc(mga, kNoSleep);
        CHECK_THROWS_AS(sc.open(), NotDetectedError);
    }
    CHECK_FALSE(fake.powered);
}

namespace {

struct Opened {
    FakeMga fake;
    Mga mga{fake};
    SystemController sc{mga, kNoSleep};
    Opened() { sc.open(); }
};

}  // namespace

TEST_CASE("read crosses 1024-byte chunks and the version block boundary")
{
    Opened o;
    const std::uint32_t at = 0x7DFF00;
    const auto want = pattern(0x100 + 3000, 1);
    std::copy(want.begin(), want.end(), o.fake.cart.flash.begin() + at);
    ConfigRom rom(o.mga);
    std::vector<std::uint32_t> progress;
    CHECK(rom.read(at, want.size(), [&](std::uint32_t a) { progress.push_back(a); }) == want);
    CHECK(progress == std::vector<std::uint32_t>{0x7DFF00, 0x7E0300, 0x7E0700, 0x7E0B00});
}

TEST_CASE("erase clears exactly one 64KB block")
{
    Opened o;
    auto& flash = o.fake.cart.flash;
    std::fill(flash.begin() + 0x7D0000, flash.end(), std::uint8_t{0x5A});
    ConfigRom rom(o.mga);
    rom.erase_block(Region::Bios, 0x7E1234);
    CHECK(o.fake.cart.erased_blocks == std::vector<std::uint32_t>{0x7E0000});
    CHECK(std::all_of(flash.begin() + 0x7E0000, flash.begin() + 0x7F0000, [](auto b) { return b == 0xFF; }));
    CHECK(std::all_of(flash.begin() + 0x7D0000, flash.begin() + 0x7E0000, [](auto b) { return b == 0x5A; }));
    CHECK(std::all_of(flash.begin() + 0x7F0000, flash.end(), [](auto b) { return b == 0x5A; }));
    CHECK(rom.last_status() == 0x00);
}

TEST_CASE("erase outside the named region sends nothing")
{
    Opened o;
    ConfigRom rom(o.mga);
    const auto before = o.fake.commands.size();
    CHECK_THROWS_AS(rom.erase_block(Region::Bios, 0x7DFFFF), std::out_of_range);
    CHECK_THROWS_AS(rom.erase_block(Region::Config, 0x7D0000), std::out_of_range);
    CHECK_THROWS_AS(rom.erase_block(Region::Version, 0x7E0000), std::out_of_range);
    CHECK(o.fake.commands.size() == before);
    CHECK(o.fake.cart.erases == 0);
}

TEST_CASE("an erase that never shows BUSY is not accepted")
{
    Opened o;
    o.fake.cart.ignore_erase = true;
    ConfigRom rom(o.mga);
    try {
        rom.erase_block(Region::Version, 0x7D0000);
        FAIL("no exception");
    } catch (const RomError& e) {
        CHECK(e.kind == RomError::Kind::EraseNotAccepted);
        CHECK(e.status == 0x00);
    }
}

TEST_CASE("an erase whose BUSY never clears times out")
{
    Opened o;
    o.fake.cart.busy_reads_after_erase = 1'000'000'000;
    ConfigRom rom(o.mga, 20ms);
    try {
        rom.erase_block(Region::Config, 0x000000);
        FAIL("no exception");
    } catch (const RomError& e) {
        CHECK(e.kind == RomError::Kind::EraseTimeout);
        CHECK(e.status == 0x03);
    }
}

TEST_CASE("write skips FFh, waits for BUSY between bytes, and stays in its region")
{
    Opened o;
    auto data = pattern(3000, 2);
    for (std::size_t i = 0; i < data.size(); i += 7) data[i] = 0xFF;
    const auto non_ff = std::count_if(data.begin(), data.end(), [](auto b) { return b != 0xFF; });
    o.fake.cart.busy_reads_after_write = 3;
    ConfigRom rom(o.mga);
    std::vector<std::uint32_t> progress;
    rom.write(Region::Bios, 0x7E0100, data, [&](std::uint32_t a) { progress.push_back(a); });

    CHECK(std::equal(data.begin(), data.end(), o.fake.cart.flash.begin() + 0x7E0100));
    CHECK(blank_outside(o.fake, 0x7E0100, 0x7E0100 + static_cast<std::uint32_t>(data.size())));
    CHECK(o.fake.cart.writes == non_ff);
    CHECK(o.fake.cart.writes_while_busy == 0);
    // 1 本のスクリプトに入りきらない長さ
    CHECK(o.fake.scripts_run >= 2);
    CHECK(progress.size() == static_cast<std::size_t>(o.fake.scripts_run));
    CHECK(progress.front() == 0x7E0100);
}

TEST_CASE("write outside the named region sends nothing")
{
    Opened o;
    ConfigRom rom(o.mga);
    const std::vector<std::uint8_t> data(0x20, 0x00);
    const auto before = o.fake.commands.size();
    CHECK_THROWS_AS(rom.write(Region::Bios, 0x7DFFF0, data), std::out_of_range);
    CHECK_THROWS_AS(rom.write(Region::Config, 0x7CFFF0, data), std::out_of_range);
    CHECK_THROWS_AS(rom.write(Region::Version, 0x7DFFF0, data), std::out_of_range);
    CHECK_THROWS_AS(rom.write(Region::Bios, 0x7FFFF0, data), std::out_of_range);
    CHECK(o.fake.commands.size() == before);
    CHECK(o.fake.cart.writes == 0);
}

TEST_CASE("a write whose BUSY never clears stops at the script wait limit")
{
    Opened o;
    o.fake.cart.busy_reads_after_write = 1'000'000'000;
    ConfigRom rom(o.mga);
    const std::vector<std::uint8_t> data(4, 0x00);
    try {
        rom.write(Region::Version, 0x7DFF00, data);
        FAIL("no exception");
    } catch (const RomError& e) {
        CHECK(e.kind == RomError::Kind::WriteTimeout);
        CHECK(e.status == 0x03);
    }
    CHECK(o.fake.cart.writes == 1);
}
