// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "serial/serial_port.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace y8960upd::serial {

namespace {

std::string errno_text(const std::string& what)
{
    return what + " failed: " + std::strerror(errno);
}

class PosixSerialPort final : public SerialPort {
public:
    explicit PosixSerialPort(const std::string& name)
    {
        fd_ = ::open(name.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (fd_ < 0) throw SerialError(errno_text("open " + name));

        termios tty{};
        if (tcgetattr(fd_, &tty) != 0) fail("tcgetattr");
        cfmakeraw(&tty);
        cfsetispeed(&tty, B115200);
        cfsetospeed(&tty, B115200);
        tty.c_cflag |= CLOCAL | CREAD;
        tty.c_cflag &= ~(CSTOPB | PARENB | CRTSCTS);
        tty.c_cc[VMIN] = 0;
        tty.c_cc[VTIME] = 0;
        if (tcsetattr(fd_, TCSANOW, &tty) != 0) fail("tcsetattr");

        int bits = TIOCM_DTR | TIOCM_RTS;
        if (ioctl(fd_, TIOCMBIS, &bits) != 0) fail("TIOCMBIS");
        tcflush(fd_, TCIOFLUSH);
    }

    ~PosixSerialPort() override
    {
        if (fd_ >= 0) ::close(fd_);
    }

    PosixSerialPort(const PosixSerialPort&) = delete;
    PosixSerialPort& operator=(const PosixSerialPort&) = delete;

    void write(std::span<const std::uint8_t> data) override
    {
        std::size_t done = 0;
        while (done < data.size()) {
            ssize_t n = ::write(fd_, data.data() + done, data.size() - done);
            if (n > 0) {
                done += static_cast<std::size_t>(n);
                continue;
            }
            if (n < 0 && errno != EAGAIN && errno != EINTR) fail("write");
            pollfd p{fd_, POLLOUT, 0};
            int r = ::poll(&p, 1, 5000);
            if (r == 0) throw SerialError("write timed out");
            if (r < 0 && errno != EINTR) fail("poll");
        }
    }

    std::size_t read(std::span<std::uint8_t> buf, std::chrono::milliseconds timeout) override
    {
        if (buf.empty()) return 0;
        pollfd p{fd_, POLLIN, 0};
        int r = ::poll(&p, 1, static_cast<int>(timeout.count()));
        if (r == 0) return 0;
        if (r < 0) {
            if (errno == EINTR) return 0;
            fail("poll");
        }
        ssize_t n = ::read(fd_, buf.data(), buf.size());
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) return 0;
            fail("read");
        }
        if (n == 0) throw SerialError("device disconnected");
        return static_cast<std::size_t>(n);
    }

private:
    [[noreturn]] void fail(const std::string& what) { throw SerialError(errno_text(what)); }

    int fd_ = -1;
};

std::string read_first_line(const std::filesystem::path& p)
{
    std::ifstream f(p);
    std::string s;
    std::getline(f, s);
    return s;
}

}  // namespace

std::unique_ptr<SerialPort> open_serial_port(const std::string& name)
{
    return std::make_unique<PosixSerialPort>(name);
}

std::vector<PortInfo> find_mga_ports()
{
    namespace fs = std::filesystem;
    std::vector<PortInfo> found;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator("/sys/class/tty", ec)) {
        fs::path device = entry.path() / "device";
        if (!fs::exists(device, ec)) continue;
        // device は USB のインターフェースを指す。VID:PID はその親（USB の機器）にある
        fs::path usb = fs::canonical(device, ec).parent_path();
        if (ec) continue;
        std::string vid = read_first_line(usb / "idVendor");
        std::string pid = read_first_line(usb / "idProduct");
        if (vid.empty() || pid.empty()) continue;
        unsigned long v = 0, d = 0;
        try {
            v = std::stoul(vid, nullptr, 16);
            d = std::stoul(pid, nullptr, 16);
        } catch (const std::exception&) {
            continue;
        }
        if (v != kMgaVendorId || d != kMgaProductId) continue;
        found.push_back({"/dev/" + entry.path().filename().string(), vid + ":" + pid});
    }
    return found;
}

}  // namespace y8960upd::serial
