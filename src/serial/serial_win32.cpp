// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "serial/serial_port.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <setupapi.h>

#include <algorithm>
#include <cwctype>

namespace y8960upd::serial {

namespace {

// GUID_DEVINTERFACE_COMPORT。ntddser.h は windows.h が定義済みのマクロを定義し直して警告が出るので、
// 値だけを写した（Windows SDK 10.0.28000.0 の ntddser.h）
constexpr GUID kComPortInterface = {0x86E0D1E0, 0x8089, 0x11D0, {0x9C, 0xE4, 0x08, 0x00, 0x3E, 0x30, 0x1F, 0x73}};

std::string last_error_text(const char* what)
{
    return std::string(what) + " failed (Win32 error " + std::to_string(GetLastError()) + ")";
}

std::wstring widen(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

class Win32SerialPort final : public SerialPort {
public:
    explicit Win32SerialPort(const std::string& name)
    {
        // COM10 以上は \\.\ を付けないと開けない
        std::wstring path = L"\\\\.\\" + widen(name);
        handle_ = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle_ == INVALID_HANDLE_VALUE) throw SerialError(last_error_text(("open " + name).c_str()));

        DCB dcb{};
        dcb.DCBlength = sizeof(dcb);
        if (!GetCommState(handle_, &dcb)) fail("GetCommState");
        dcb.BaudRate = CBR_115200;
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        dcb.fBinary = TRUE;
        dcb.fParity = FALSE;
        dcb.fOutxCtsFlow = FALSE;
        dcb.fOutxDsrFlow = FALSE;
        dcb.fDtrControl = DTR_CONTROL_ENABLE;
        dcb.fRtsControl = RTS_CONTROL_ENABLE;
        dcb.fDsrSensitivity = FALSE;
        dcb.fOutX = FALSE;
        dcb.fInX = FALSE;
        dcb.fErrorChar = FALSE;
        dcb.fNull = FALSE;
        dcb.fAbortOnError = FALSE;
        if (!SetCommState(handle_, &dcb)) fail("SetCommState");
        PurgeComm(handle_, PURGE_RXCLEAR | PURGE_TXCLEAR);
    }

    ~Win32SerialPort() override
    {
        if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_);
    }

    Win32SerialPort(const Win32SerialPort&) = delete;
    Win32SerialPort& operator=(const Win32SerialPort&) = delete;

    void write(std::span<const std::uint8_t> data) override
    {
        set_timeouts(0);
        std::size_t done = 0;
        while (done < data.size()) {
            DWORD chunk = static_cast<DWORD>(std::min<std::size_t>(data.size() - done, 0x10000));
            DWORD n = 0;
            if (!WriteFile(handle_, data.data() + done, chunk, &n, nullptr)) fail("WriteFile");
            if (n == 0) throw SerialError("WriteFile timed out");
            done += n;
        }
    }

    std::size_t read(std::span<std::uint8_t> buf, std::chrono::milliseconds timeout) override
    {
        if (buf.empty()) return 0;
        set_timeouts(static_cast<DWORD>(std::max<long long>(1, timeout.count())));
        DWORD n = 0;
        DWORD want = static_cast<DWORD>(std::min<std::size_t>(buf.size(), 0x10000));
        if (!ReadFile(handle_, buf.data(), want, &n, nullptr)) fail("ReadFile");
        return n;
    }

private:
    [[noreturn]] void fail(const char* what) { throw SerialError(last_error_text(what)); }

    // ReadIntervalTimeout と ReadTotalTimeoutMultiplier を MAXDWORD にすると、ReadFile は
    // 1 バイトでも届けばすぐ戻り、何も届かなければ ReadTotalTimeoutConstant で戻る
    void set_timeouts(DWORD read_ms)
    {
        if (read_ms == 0) read_ms = current_read_ms_ == 0 ? 1 : current_read_ms_;
        if (read_ms == current_read_ms_) return;
        COMMTIMEOUTS t{};
        t.ReadIntervalTimeout = MAXDWORD;
        t.ReadTotalTimeoutMultiplier = MAXDWORD;
        t.ReadTotalTimeoutConstant = read_ms;
        t.WriteTotalTimeoutMultiplier = 0;
        t.WriteTotalTimeoutConstant = 5000;
        if (!SetCommTimeouts(handle_, &t)) fail("SetCommTimeouts");
        current_read_ms_ = read_ms;
    }

    HANDLE handle_ = INVALID_HANDLE_VALUE;
    DWORD current_read_ms_ = 0;
};

bool contains_mga_id(const std::wstring& multi_sz)
{
    wchar_t want[32];
    swprintf(want, 32, L"VID_%04X&PID_%04X", kMgaVendorId, kMgaProductId);
    for (const wchar_t* p = multi_sz.c_str(); *p != L'\0'; p += wcslen(p) + 1) {
        std::wstring id(p);
        std::transform(id.begin(), id.end(), id.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
        if (id.find(want) != std::wstring::npos) return true;
    }
    return false;
}

}  // namespace

std::unique_ptr<SerialPort> open_serial_port(const std::string& name)
{
    return std::make_unique<Win32SerialPort>(name);
}

std::vector<PortInfo> find_mga_ports()
{
    std::vector<PortInfo> found;
    HDEVINFO set = SetupDiGetClassDevsW(&kComPortInterface, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) throw SerialError(last_error_text("SetupDiGetClassDevs"));

    SP_DEVINFO_DATA info{};
    info.cbSize = sizeof(info);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(set, i, &info); ++i) {
        DWORD size = 0;
        SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_HARDWAREID, nullptr, nullptr, 0, &size);
        if (size == 0) continue;
        std::wstring ids(size / sizeof(wchar_t) + 2, L'\0');
        if (!SetupDiGetDeviceRegistryPropertyW(set, &info, SPDRP_HARDWAREID, nullptr,
                                               reinterpret_cast<BYTE*>(ids.data()), size, nullptr)) {
            continue;
        }
        if (!contains_mga_id(ids)) continue;

        HKEY key = SetupDiOpenDevRegKey(set, &info, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (key == INVALID_HANDLE_VALUE) continue;
        wchar_t name[64] = {};
        DWORD name_size = sizeof(name) - sizeof(wchar_t);
        DWORD type = 0;
        LONG rc = RegQueryValueExW(key, L"PortName", nullptr, &type, reinterpret_cast<BYTE*>(name), &name_size);
        RegCloseKey(key);
        if (rc != ERROR_SUCCESS || type != REG_SZ) continue;

        found.push_back({narrow(name), narrow(std::wstring(ids.c_str()))});
    }
    SetupDiDestroyDeviceInfoList(set);
    return found;
}

}  // namespace y8960upd::serial
