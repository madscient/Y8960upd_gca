// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "fake/fake_mga.hpp"
#include "mga/mga.hpp"
#include "mga/mga_script.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using y8960upd::mga::Mga;
using y8960upd::mga::MgaError;
using y8960upd::mga::MgaScript;
using y8960upd::mga::Response;
using y8960upd::mga::Terminator;
using y8960upd::test::FakeMga;

TEST_CASE("resync discards what the previous run left behind")
{
    FakeMga fake;
    // 前回の BSND の途中で止めたときの形：バイナリの後に改行の無い OK
    const std::string leftover = std::string("\x01\x02\x0A\xFFOK\n", 7) + "0040 : 7f\nOK\n";
    fake.preload(leftover);
    Mga mga(fake);
    CHECK(mga.resync() == leftover.size());
    CHECK(std::count(fake.commands.begin(), fake.commands.end(), "HVER") == 1);
    Response r = mga.command("SCHK");
    CHECK(r.ok());
    CHECK(r.lines == std::vector<std::string>{"0010"});
}

TEST_CASE("responses end with any of the five terminators")
{
    FakeMga fake;
    Mga mga(fake);
    Response r = mga.command("NOSUCH");
    CHECK(r.end == Terminator::UnknownCommand);
    CHECK(r.end_line == "Unknown command: [NOSUCH]");
    CHECK_THROWS_AS(mga.command_ok("NOSUCH"), MgaError);
    CHECK(mga.command("SPON").ok());
}

TEST_CASE("I/O reads and writes reach the System Controller")
{
    FakeMga fake;
    Mga mga(fake);
    mga.command_ok("SPON");
    mga.io_write(0x40, 0x80);
    CHECK(mga.io_read(0x40) == 0x7F);
    mga.io_write(0x41, 0x02);
    CHECK(mga.io_read(0x41) == 0xFD);
    mga.io_write(0x42, 0x01);
    CHECK(mga.io_read(0x42) == 0xFE);
}

TEST_CASE("I/O without slot power fails")
{
    FakeMga fake;
    Mga mga(fake);
    CHECK_THROWS_AS(mga.io_read(0x40), MgaError);
    CHECK_THROWS_AS(mga.io_write(0x40, 0x80), MgaError);
}

TEST_CASE("buffer round trip keeps every byte, including a leading LF")
{
    FakeMga fake;
    Mga mga(fake);
    std::vector<std::uint8_t> data(1000);
    for (std::size_t i = 0; i < data.size(); ++i) data[i] = static_cast<std::uint8_t>(i * 7 + 3);
    data[0] = 0x0A;
    data[1] = 0x0D;
    mga.buffer_receive(0x0100, data);
    CHECK(std::equal(data.begin(), data.end(), fake.buffer.begin() + 0x0100));
    CHECK(mga.buffer_send(0x0100, data.size()) == data);
    // バイナリの後も行の区切りがずれていない
    CHECK(mga.command("SCHK").ok());
}

TEST_CASE("BRCV out of the buffer is refused before anything is sent")
{
    FakeMga fake;
    Mga mga(fake);
    std::vector<std::uint8_t> data(0x100, 0x55);
    CHECK_THROWS_AS(mga.buffer_receive(0xFF00, data), MgaError);
    CHECK(fake.commands.empty());
    std::vector<std::uint8_t> most(0xFFFF, 0x55);
    CHECK_NOTHROW(mga.buffer_receive(0x0000, most));
}

TEST_CASE("a script reads the ConfigROM into the buffer")
{
    FakeMga fake;
    const std::vector<std::uint8_t> want = {0x41, 0x42, 0x5F, 0x41, 0x00, 0x50, 0xFF, 0x12};
    std::copy(want.begin(), want.end(), fake.cart.flash.begin() + 0x7E0000);

    Mga mga(fake);
    mga.command_ok("SPON");
    mga.io_write(0x40, 0x80);
    mga.io_write(0x41, 0x02);
    mga.io_write(0x42, 0x01);

    MgaScript s;
    s.write_io(0x45, 0x7E);
    s.write_io(0x44, 0x00);
    for (std::uint16_t i = 0; i < want.size(); ++i) {
        s.write_io(0x43, static_cast<std::uint8_t>(i));
        s.write_io(0x46, 0x00);
        s.read_io(0x40);
        s.compare(0x7F);
        s.jump_back(2);
        s.read_io(0x47);
        s.push(static_cast<std::uint16_t>(0xC000 + i));
    }
    s.end();
    mga.buffer_receive(0, s.bytes());
    CHECK(mga.run_script());
    CHECK(mga.buffer_send(0xC000, want.size()) == want);
}

TEST_CASE("a failing script is reported as false")
{
    FakeMga fake;
    Mga mga(fake);
    MgaScript s;
    s.abort();
    mga.buffer_receive(0, s.bytes());
    CHECK_FALSE(mga.run_script());
}
