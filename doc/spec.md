# 原本のツールの仕様

**この文書は、原本の更新ツール `Y8960UPD.COM` から読み取った仕様と、それを MGA 経由で
実現するために必要なアダプタ側の仕様を持つ。** 読者は、このリポジトリを実装する開発者と AI。
作業の計画と経緯は `doc/plan.md` にある。

## 出所

- 原本：[`hra1129/Y8960_Cartridge`](https://github.com/hra1129/Y8960_Cartridge) の `5ad5d06`
  - `tool/y8960upd.asm`（ソース）、`tool/y8960upd.md`（原本の仕様書）
  - `tool_for_windows/`（USB 経由で書く Windows 版。ファイル形式の参考）
- アダプタ：[`v9938/MSXPLAYer_GameAdapter`](https://github.com/v9938/MSXPLAYer_GameAdapter) の `bb58f7b`
  - `com_command.md`（シリアルコマンドの仕様書）、`FIRMWARE/main.c`（実装）
- 兄弟：`mgadump` の `7734878` の `doc/porting-notes.md`（MGA を PC から操作するときの知見）

## 確度

各項目に確度を添える。

- **確認済み** — 動かして確かめた。何で確かめたかを添える
- **未検証** — ソースや文書を読んで分かったことで、動かしてはいない
- **推測** — 根拠を一行添える

## 1. ConfigROM の配置

## 2. ファイル形式

## 3. 動作

## 4. ConfigROM へのアクセス手順

## 5. エラーの判定と表示

## 6. MGA 経由で実現するときの対応
