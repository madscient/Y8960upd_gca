# CLAUDE.md

このリポジトリは GitHub で公開する。この文書も公開される。**手元の環境に固有のこと**
（ローカルのパス、WSL の配布名、実機の COM 番号や BUSID）は、追跡の外にある
`CLAUDE.local.md` にだけ書く。ほかの文書・コード・コミットメッセージには書かない。

## 最初に読むもの

1. `doc/plan.md` — 決めたこと（理由と前提）、見送った案、未決事項、残りの作業、経緯。
   経緯はこの文書にだけ書く
2. `doc/spec.md` — 原本のツールとアダプタから読み取った仕様

チャットで決まったことは、その都度 `doc/plan.md` に書く。

## 文書の役割

出所：`doc/initial_prompt.txt`（利用者による最初の依頼文。AI への指示として扱う）

| 文書 | 内容 | 読者 |
|---|---|---|
| `doc/plan.md` | 作業計画・進捗・経緯 | AI |
| `doc/spec.md` | 原本のツールから読み取った仕様 | AI・開発者 |
| `doc/manual.md` | 詳細なリファレンスマニュアル・使用例 | 開発者・エンドユーザー |
| `README.md` | 概要・ビルド手順・使い方 | 開発者・エンドユーザー |
| `CLAUDE.md` | このプロジェクト固有の約束事・手順 | AI |

`README.md` と `doc/manual.md` には、経緯や開発用文書への参照、コードの中にしか無い
シンボルを書かない。

## 参照先のリポジトリ

隣に置いたクローンを読む。**どれも読むだけで、書かない。** 読む前に `git fetch` して
`HEAD..origin/main` を見る。`doc/spec.md` に書くときは、読んだコミットを出所に書く。

- `../Y8960_Cartridge` — 原本。`tool/y8960upd.asm` が一次の資料で、`tool/y8960upd.md` は
  原本の仕様書。食い違えば asm を正とし、食い違いを `doc/spec.md` に書く
- `../MSXPLAYer_GameAdapter` — アダプタ。シリアルコマンドの仕様書 `com_command.md` は
  実装と食い違う箇所がある。応答の形は `FIRMWARE/main.c` で確かめる
- `../mgadump` — 同じアダプタを Linux から使う兄弟のツール。`doc/porting-notes.md` に
  通信の作法（前回の残りの読み捨て、応答の終端の種類など）がある

## 確度

`doc/spec.md` とコードコメントで、事実の主張に確度（確認済み／未検証／推測）を添える。
定義は `doc/spec.md` の冒頭にある。

## ビルドと試験

```sh
cmake --preset windows-vs2026
cmake --build --preset windows-vs2026
ctest --preset windows-vs2026
```

Linux は `linux-system`（vcpkg なし）か `linux-vcpkg`。ビルド先は `build/<プリセット名>/`。
Linux での確かめ方は `CLAUDE.local.md` にある。

試験を足したら、変更前のコードにも当てて落ちることを見る。

## git

- 改行は LF（`.gitattributes`）

## 実機

- **実機で走らせる前・ポートに何かを送る前に、利用者に一声かける**
- **break を送らない。** MGA の試作機が応答しなくなり、USB の抜き差しが要った
  （`../mgadump/doc/porting-notes.md`）
- MGA 以外のシリアルポートには送らない
- `test/hw/probe.ps1` は、Windows から手で走らせる読み出しだけの確認
  （`-Port COMn -LogPath <記録の置き場>`）。検出、バージョン、BIOS と Config の先頭を読む。
  ctest には入れない
