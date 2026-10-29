# 評価データ

フェーズ0で固定する評価 bag。bag 本体はリポジトリに含めない(置き場所: `~/data/slamrun`)。

| ID | センサ | `lidar_type` | パス | サイズ | 長さ | SHA256 | 正解軌跡 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| avia-quick-shack | Livox Avia | 1 | `avia/2020-09-16-quick-shack.bag` | 227,829,516 B | 50 s | `05a56e75898f952766f136d1e5db64a35d202e990e3b1052558369d8384d7ffe` | なし |
| ouster-nc-quad-easy | Ouster OS0-128 | 3 | `newer_college/2021-07-01-10-37-38-quad-easy-002.bag` | 10,775,549,922 B | 198 s | `64e4d2cdaffc652c4c536e9718a24999d0dd24ab71d255738a449d9683fc4215` | `newer_college/collection1/ground_truth/gt-nc-quad-easy.csv` |

## ハーネスの入力ファイル

bag から `eval/extract_input.py`(`add3cf1`)で作る、ファイル駆動の実行(`pointlio_offline`、将来の Rust)の入力。形式は [harness-input-format.md](harness-input-format.md)。置き場所は `~/data/slamrun-input`(リポジトリに含めない)。`eval/run_offline.sh` は SHA256 が一致しなければ実行しない。

| ID | ファイル | サイズ | SHA256 |
| --- | --- | --- | --- |
| avia-quick-shack | `avia-quick-shack.plin` | 227,384,568 B | `cf90642183710ed725a3402296796eceec53088d77b362da2c38b2b1a404ac29` |
| ouster-nc-quad-easy | `ouster-nc-quad-easy.plin` | 12,533,559,299 B | `571d5063f5121b7e745f1f8843a92a01a735734a1be7eafaa641638db0cd5a59` |

## 出所

- avia-quick-shack: FAST-LIO 公開の Avia bag(Google Drive `1YqxHuDKzWUcda80QKBV61lXI86TXsGjP`)。取得日 2026-10-21。
- ouster-nc-quad-easy: Newer College Dataset(collection 1, quad-easy)。取得元・取得日は未記録(`.sha256` は同ディレクトリに保存済み)。

## 注意

- SHA256 は取得後に私たちが計算した値。avia-quick-shack には配布元のチェックサムがない。
- avia-quick-shack は正解軌跡がないため、ATE/RPE は出せない。動作確認と A/B/C の出力一致の比較に使う。
- Avia の IMU は g 単位(`acc_norm: 1.0`)。Ouster の IMU は m/s²。
- 点群の時刻フィールド単位と設定の整合は、実行して確認するまで未検証。
- 動的環境の bag は含めない(2026-10-21 に決定。理由は計画書のフェーズ0を参照)。

## 最終評価用(封印)

計画書 §3 の最終評価用のデータ。2026-10-26 に人が選んだ。**C が完成するまで実行しない**(抽出・`rosbag info` 以外の中身の確認・推定器の実行・正解の閲覧をしない)。D は封印を解いた後に作るので、D の報告ではこのデータへの合わせ込みを防げていないと書く。

| ID | 条件 | センサ | 状態 | SHA256 | 正解 |
| --- | --- | --- | --- | --- | --- |
| ncd-maths-hard | 激しい運動(最速の歩行、速い回転と揺れ、無特徴の面)。243 s | Ouster OS0-128(開発用の ouster-nc-quad-easy と同じ機材) | 取得済み(下の節)。CC BY-NC-SA 4.0 | 下の節 | `gt_math_hard.csv`(TUM 形式)と `gt_state_hard.csv`。zip のまま置き、中身は見ていない |
| glim-flatwall-01〜08 | 完全な退化(平らな壁だけを見る) | Livox Avia | 取得済み(下の節) | 下の節 | `gt.tar.gz`(AprilTag を見るカメラから求めた軌跡)。中身は見ていない |

選定の経緯:

- 激しい運動には、初め Newer College の quad-hard(手元にある)を選んだ。しかし別プロジェクト slamrun に、Point-LIO の別の版・別の設定で quad_hard・quad_medium・stairs を走らせた ATE の記録があり、2026-10-26 にそれを見た。そのため quad-hard を外し、slamrun で使われていない maths-hard に差し替えた。
- stairs(退化に近い)と park(長時間)も候補だったが、人の判断で選ばなかった(stairs は、その後に slamrun での使用歴も分かった)。長時間の条件は最終評価用にない。
- maths-hard と Flatwall の使用歴は、slamrun のリポジトリ(`results/` と記録の文書)に見当たらなかった(ファイル名と本文の検索による)。

### maths-hard の取得記録

Newer College Dataset(collection 3 - maths institute)の配布ページのフォームから申し込み、Google Drive から 2026-10-30 に取得した。配布元のチェックサムは見当たらないので、下の SHA256 は取得後に私たちが計算した値。置き場所は `~/data/slamrun/newer_college`(リポジトリに含めない)。各 bag の横に `.sha256`、`collection3/` に `SHA256SUMS` を置いた。

bag は2本に分かれている。1本目の終わりと2本目の始まりは同じ時刻(1617800538.98)で、合計 243 s。現在の `eval/extract_input.py` と `eval/run_cpp.sh` は bag を1本しか受け付けないので、封印を解くときに、2本をつなぐか、複数の bag を受け付けるかを決める必要がある。

| ファイル | サイズ | 長さ | SHA256 |
| --- | --- | --- | --- |
| `2021-04-07-13-58-54_0-math-hard-001.bag` | 10,183,601,903 B | 192 s | `d3c21b6e76e136ca8b9df2c8e1b5c8c5dc81a4a9b01ba78871f1a00874583ceb` |
| `2021-04-07-14-02-18_1-math-hard-004.bag` | 2,465,598,156 B | 51 s | `03ffd4028ea4f6dfc5e9bc57e9cfabeb9aa0c2fe3fd17c1b67382d5908801405` |
| `collection3/collection 3 - maths institute-20261029T224732Z-1-010.zip`(`gt_math_hard.csv` と、評価に使わないカメラの較正1件) | 8,334,722 B | — | `08840f87f79a12895541e1bb2411b82ebd8861f2e2f9db5af1d51b2c41d56d33` |
| `collection3/collection 3 - maths institute-20261029T224732Z-1-014.zip`(`gt_state_hard.csv`) | 121,723 B | — | `892b907e014877fa6871121194483f8966fe93f02879c8ec2cb2d887b9c82012` |

取得時に行った確認は、bag への `rosbag info`、zip のファイル一覧の表示と CRC 検査(`unzip -t`)だけ。正解は展開していない。`rosbag info` で確かめたこと: どちらの bag もチャンクが揃っていて(lz4)、開発用の正準設定が使うトピック `/os_cloud_node/points`(1930+510 フレーム)と `/os_cloud_node/imu`(19296+5102 メッセージ)がある。

同じときに math-easy と math-medium の bag、地図(`maths-institute.ply`)、カメラの較正も取得したが、ここには置いていない(`~/Downloads` に残っている)。同じ場所のデータなので、開発に使えば、このデータへの合わせ込みになりうる。

### Flatwall の取得記録

Zenodo record 7641866("LiDAR Degeneration Test Dataset (Flatwall experiment)"、CC BY 4.0)から 2026-10-26 に取得し、配布元の MD5 と全ファイル一致を確かめた。置き場所は `~/data/slamrun/flatwall`(リポジトリに含めない)。`flatwall_04.bag` は1回目の転送が途中で切れたので取り直した。中身(`rosbag info` を含む)は見ていない。

| ファイル | サイズ | SHA256 |
| --- | --- | --- |
| `gt.tar.gz` | 213,710 B | `b2c457f95fdae05af07732a60294762c55567e21e6548977b235ebf415d6a9d9` |
| `flatwall_01.bag` | 184,597,802 B | `4721e667166e11b948aea83b221f99ca0fec21fabdce5b292662a4b442efbab6` |
| `flatwall_02.bag` | 135,975,445 B | `1786d134dc30112f9b52b9a20a6456d0d6a0307bc747eee5cae2b60561e9040c` |
| `flatwall_03.bag` | 143,913,789 B | `f2aacfd0b59e4f95b4567f70bde3c3789479143d9399c729ab40be7d3fdae714` |
| `flatwall_04.bag` | 131,013,599 B | `fa0a26541bb61a1bb041add42ba66c83d53f853a4cf3748f5aee4705d8ac70e5` |
| `flatwall_05.bag` | 139,946,903 B | `ac668922813bb523a56309c7a0b585958eba81e0897dea711e9a04a25b4ddf8e` |
| `flatwall_06.bag` | 88,340,047 B | `933afb11da1d03e1e68f24498f15cfb1654b55ff02dac8f2251c1bf4e004aa67` |
| `flatwall_07.bag` | 89,331,959 B | `66a1d7b1d8417c9883e7a8d3227310647492c5b32c0b373d1a22616b9b758308` |
| `flatwall_08.bag` | 121,088,764 B | `da765f673da9d050c0a3a64d33a47669c5d0d47d4b171ca64fb2c9f57beff751` |
