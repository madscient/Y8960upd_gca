// SPDX-License-Identifier: MIT
// Copyright (c) 2026 MadScient

#include "image/zx0.hpp"
#include "unit/zx0_vector.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <span>
#include <vector>

using y8960upd::image::zx0::decompress;
using y8960upd::test::kZx0Vector;
using y8960upd::test::kZx0VectorSize;
using y8960upd::test::zx0_vector_output;

namespace {

using Bytes = std::vector<std::uint8_t>;

// 手で組んだストリーム。ビットの並びは「リテラルの長さ 1、'A'、新しいオフセット」の後に
//   kOneLiteral：終わりの印
//   kCopyLast1：オフセット 1 から 2 バイト、新しいオフセットの終わりの印 → "AAA"
//   kCopyLast2：kCopyLast1 のオフセットを 2 にしたもの。出力の先頭より前を指す
const Bytes kOneLiteral = {0xd5, 0x41, 0x55, 0x60};
const Bytes kCopyLast1 = {0xf5, 0x41, 0xff, 0x55, 0x58};
const Bytes kCopyLast2 = {0xf5, 0x41, 0xfd, 0x55, 0x58};

bool run(const Bytes& in, Bytes& out)
{
    return decompress(in, out);
}

}  // namespace

TEST_CASE("the ZX0 vector of the original Windows tool decompresses to its sentence")
{
    Bytes out(kZx0VectorSize);
    CHECK(run(kZx0Vector, out));
    CHECK(out == zx0_vector_output());
}

TEST_CASE("hand-made streams copy literals and earlier bytes")
{
    Bytes one(1);
    CHECK(run(kOneLiteral, one));
    CHECK(one == Bytes{'A'});
    Bytes three(3);
    CHECK(run(kCopyLast1, three));
    CHECK(three == Bytes{'A', 'A', 'A'});
}

TEST_CASE("a stream must fill the output exactly")
{
    Bytes shorter(kZx0VectorSize - 1);
    CHECK_FALSE(run(kZx0Vector, shorter));
    Bytes longer(kZx0VectorSize + 1);
    CHECK_FALSE(run(kZx0Vector, longer));
}

TEST_CASE("a stream must be used up exactly")
{
    Bytes out(kZx0VectorSize);
    Bytes extra = kZx0Vector;
    extra.push_back(0x00);
    CHECK_FALSE(run(extra, out));
    Bytes cut(kZx0Vector.begin(), kZx0Vector.end() - 1);
    CHECK_FALSE(run(cut, out));
}

TEST_CASE("a copy cannot reach before the start of the output")
{
    // 出力の前に別のバイトを置き、範囲を見ずにそれを読む誤りを捕まえる
    Bytes buf = {0x5a, 0x00, 0x00, 0x00};
    std::span<std::uint8_t> out(buf.begin() + 1, buf.end());
    CHECK_FALSE(decompress(kCopyLast2, out));
    CHECK(decompress(kCopyLast1, out));
    CHECK(buf == Bytes{0x5a, 'A', 'A', 'A'});
}

TEST_CASE("a stream never writes past the output")
{
    // 長さの誤りは最後にも断るので、出力の後ろの番兵で、途中で書き過ぎていないかを見る
    Bytes buf = {0x5a, 0x5b, 0x5b, 0x5b};
    CHECK_FALSE(decompress(kOneLiteral, std::span<std::uint8_t>(buf.begin() + 1, 0)));
    CHECK(buf == Bytes{0x5a, 0x5b, 0x5b, 0x5b});
    CHECK_FALSE(decompress(kCopyLast1, std::span<std::uint8_t>(buf.begin() + 1, 1)));
    CHECK(buf == Bytes{0x5a, 'A', 0x5b, 0x5b});
}

TEST_CASE("an overlong length is rejected instead of wrapping around")
{
    // リテラルの長さが 2^32 になるビット列の後に終わりの印。32 ビットで桁あふれすると長さ 0 になり、
    // 空の出力として通ってしまう
    const Bytes wraps = {0, 0, 0, 0, 0, 0, 0, 0, 0xd5, 0x55, 0x60};
    Bytes empty;
    CHECK_FALSE(run(wraps, empty));
}
