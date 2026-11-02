// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "y8960/system_controller.hpp"

#include "y8960/ports.hpp"

#include <thread>

namespace y8960upd::y8960 {

SystemController::SystemController(mga::Mga& mga, PowerOnWait wait) : mga_(mga), wait_(wait) {}

SystemController::~SystemController() { close(); }

void SystemController::open()
{
    mga_.resync();
    // 別の道具が上限を変えたままかもしれないので、初期値の 1000 に戻す
    (void)mga_.command_ok("LSCR");

    // 4 文字の左から 3 文字目が SLOT1。0010 と 0110 で挿さっている（main.c の
    // cmd_slotCassetteCheck）― 未検証（1 スロット機の 0010 だけを実機で見た）
    mga::Response schk = mga_.command_ok("SCHK");
    if (schk.lines.size() != 1 || schk.lines[0].size() != 4 || schk.lines[0][2] != '1') {
        throw NotDetectedError("no cartridge in SLOT1");
    }

    // SPON が途中で切れても電源が入っていることがあるので、送る前に印を立てる
    must_power_off_ = true;
    (void)mga_.command_ok("SPON");
    wait_power_on();
    detect();
}

// MGA は /WAIT を見ないので、起動時の BIOS のコピーの間も I/O が読めてしまう。
// 40h が続けて 7Fh を返すまで待つ
void SystemController::wait_power_on()
{
    int stable = 0;
    for (int i = 0; i < wait_.max_reads; ++i) {
        mga_.io_write(kEnabler, kEnable);
        if (mga_.io_read(kEnabler) == kEnablerReady) {
            if (++stable >= wait_.stable_reads) return;
        } else {
            stable = 0;
        }
        if (wait_.interval.count() > 0) std::this_thread::sleep_for(wait_.interval);
    }
    throw NotDetectedError("System Controller did not settle after power on");
}

void SystemController::detect()
{
    mga_.io_write(kEnabler, kEnable);
    bool ok = mga_.io_read(kEnabler) == kEnablerReady;
    if (ok) {
        mga_.io_write(kDeviceSelect, kDeviceY8960);
        ok = mga_.io_read(kDeviceSelect) == static_cast<std::uint8_t>(~kDeviceY8960);
    }
    if (ok) {
        mga_.io_write(kFunctionSelect, kFunctionConfigRom);
        ok = mga_.io_read(kFunctionSelect) == static_cast<std::uint8_t>(~kFunctionConfigRom);
    }
    if (!ok) throw NotDetectedError("Y8960 Cartridge is not detected");
}

void SystemController::close() noexcept
{
    if (!must_power_off_) return;
    mga::Mga::InterruptHold hold(mga_);
    // 40h が書けなくても電源は切る
    try {
        mga_.io_write(kEnabler, kDisable);
    } catch (...) {
    }
    try {
        (void)mga_.command("SPOFF");
        must_power_off_ = false;
    } catch (...) {
    }
}

}  // namespace y8960upd::y8960
