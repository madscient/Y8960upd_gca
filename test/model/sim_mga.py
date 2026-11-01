# SPDX-License-Identifier: MIT
# Copyright (c) 2026 MadScient

"""Model of MGA executeCommands (FIRMWARE/main.c, bb58f7b) with a Y8960 System Controller and Flash."""
import glob
import sys

BUF = 64 * 1024


class Cart:
    def __init__(self, busy_reads):
        self.flash = bytearray([0xFF]) * 0x800000
        self.addr = [0, 0, 0]
        self.data_in = 0
        self.data_out = 0
        self.busy = 0
        self.busy_reads = busy_reads
        self.writes = 0
        self.ignored = 0
        self.log = []

    def a(self):
        return self.addr[0] | (self.addr[1] << 8) | (self.addr[2] << 16)

    def io_write(self, port, v):
        if port in (0x43, 0x44, 0x45):
            self.addr[port - 0x43] = v
        elif port == 0x47:
            self.data_in = v
        elif port == 0x46:
            if v == 0x00:
                self.data_out = self.flash[self.a()]
            elif v == 0x01:
                if self.busy:
                    self.ignored += 1
                else:
                    self.flash[self.a()] &= self.data_in
                    self.writes += 1
                    self.busy = self.busy_reads
            elif v == 0x04:
                self.data_out = 0x01 if self.busy else 0x00
                if self.busy:
                    self.busy -= 1
            else:
                raise RuntimeError(f"unexpected command {v:02X}")
        elif port in (0x40, 0x41, 0x42):
            pass
        else:
            raise RuntimeError(f"write to I/O {port:02X}")

    def io_read(self, port):
        if port == 0x40:
            return 0x7F
        if port == 0x47:
            return self.data_out
        raise RuntimeError(f"read from I/O {port:02X}")


def execute(mem, cart, start=0, loop_max=1000):
    """Mirrors executeCommands: returns (ok, executed)."""
    buf_size = len(mem) - start
    i = start
    loop = 0
    last = 0
    pc = 0
    while i < buf_size:
        cmd = mem[i]
        addr = (mem[i + 1] << 8) | mem[i + 2] if i + 3 < buf_size else 0
        data = mem[i + 3] if i + 3 < buf_size else 0
        if cmd == 0x00:
            pass
        elif cmd == 0x03:
            last = cart.io_read(addr)
        elif cmd == 0x04:
            cart.io_write(addr, data)
            last = data
        elif cmd == 0x05:
            pass
        elif cmd in (0x06, 0x07, 0x08, 0x09):
            loop += 1
            r = {6: last == data, 7: (last & data) == 0, 8: (last | data) == 0, 9: (last ^ data) == 0}[cmd]
            if r:
                loop = 0
                i += 4
            if loop == loop_max:
                return False, pc
        elif cmd == 0x0A:
            if data < 0x80:
                i += data * 4
            else:
                j = (data - 256) * 4
                if i + j < 0:
                    return False, pc
                i += j
            i -= 4
        elif cmd == 0x0B:
            mem[addr] = last
        elif cmd == 0xFE:
            return False, pc
        elif cmd == 0xFF:
            return True, pc
        else:
            return False, pc
        i += 4
        pc += 1
    return True, pc


def main(script_dir, data_path, n_write, busy_reads):
    want = open(data_path, "rb").read()[:0x10000]
    cart = Cart(busy_reads)
    files = sorted(glob.glob(script_dir + "/s*.bin"))
    readback = bytearray()
    for k, f in enumerate(files):
        s = open(f, "rb").read()
        mem = bytearray(BUF)
        mem[: len(s)] = s
        ok, pc = execute(mem, cart)
        if not ok:
            print(f"script {k} FAILED after {pc} instructions")
            return 1
        if k >= n_write:
            n = min(1024, 0x10000 - len(readback))
            readback += mem[0xC000 : 0xC000 + n]
    flash = bytes(cart.flash[0x7E0000:0x7F0000])
    print(f"busy_reads={busy_reads}: scripts={len(files)} writes={cart.writes} ignored={cart.ignored}")
    print("  flash == want:", flash == want, " readback == want:", bytes(readback) == want)
    outside = sum(1 for i, b in enumerate(cart.flash) if b != 0xFF and not (0x7E0000 <= i < 0x7F0000))
    print("  non-FF bytes outside 7E0000h block:", outside)
    return 0 if flash == want and bytes(readback) == want and outside == 0 and cart.ignored == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4])))
