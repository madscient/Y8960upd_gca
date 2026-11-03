// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021, Einar Saukas
// Copyright (c) 2026 MadScient
// ZX0 の参照の展開（Einar Saukas の dzx0.c）を移したもの。ライセンスの全文は LICENSE-zx0.txt

#include "image/zx0.hpp"

#include <cstddef>

namespace y8960upd::image::zx0 {

namespace {

struct Malformed {};

// 正しいストリームの値は 16384 を超えない。上限が無いと、値が桁あふれで小さな値に戻り、壊れた
// ストリームが通ることがある
constexpr std::uint32_t kGammaLimit = 0x100000;

class Decoder {
public:
    Decoder(std::span<const std::uint8_t> in, std::span<std::uint8_t> out) : in_(in), out_(out) {}

    void run()
    {
        std::size_t last_offset = 1;
        for (;;) {
            for (std::uint32_t n = gamma(false); n > 0; --n) put(byte());
            if (!bit()) {
                copy(last_offset, gamma(false));
                if (!bit()) continue;
            }
            for (;;) {
                std::uint32_t msb = gamma(true);
                if (msb == 256) {
                    if (in_pos_ != in_.size() || out_pos_ != out_.size()) throw Malformed{};
                    return;
                }
                last_offset = std::size_t{msb} * 128 - (byte() >> 1);
                // オフセットの下位のバイトの bit0 が、続く長さの最初のビットを兼ねる
                backtrack_ = true;
                copy(last_offset, gamma(false) + 1);
                if (!bit()) break;
            }
        }
    }

private:
    std::uint8_t byte()
    {
        if (in_pos_ >= in_.size()) throw Malformed{};
        last_byte_ = in_[in_pos_++];
        return last_byte_;
    }

    bool bit()
    {
        if (backtrack_) {
            backtrack_ = false;
            return (last_byte_ & 1) != 0;
        }
        bit_mask_ >>= 1;
        if (bit_mask_ == 0) {
            bit_mask_ = 0x80;
            bit_value_ = byte();
        }
        return (bit_value_ & bit_mask_) != 0;
    }

    // v2 は、オフセットの上位だけ値のビットを反転して持つ
    std::uint32_t gamma(bool inverted)
    {
        std::uint32_t v = 1;
        while (!bit()) {
            v = (v << 1) | (bit() != inverted ? 1u : 0u);
            if (v > kGammaLimit) throw Malformed{};
        }
        return v;
    }

    void put(std::uint8_t b)
    {
        if (out_pos_ >= out_.size()) throw Malformed{};
        out_[out_pos_++] = b;
    }

    void copy(std::size_t offset, std::uint32_t n)
    {
        if (offset == 0 || offset > out_pos_) throw Malformed{};
        for (; n > 0; --n) put(out_[out_pos_ - offset]);
    }

    std::span<const std::uint8_t> in_;
    std::span<std::uint8_t> out_;
    std::size_t in_pos_ = 0;
    std::size_t out_pos_ = 0;
    std::uint8_t bit_mask_ = 0;
    std::uint8_t bit_value_ = 0;
    std::uint8_t last_byte_ = 0;
    bool backtrack_ = false;
};

}  // namespace

bool decompress(std::span<const std::uint8_t> in, std::span<std::uint8_t> out)
{
    try {
        Decoder(in, out).run();
        return true;
    } catch (const Malformed&) {
        return false;
    }
}

}  // namespace y8960upd::image::zx0
