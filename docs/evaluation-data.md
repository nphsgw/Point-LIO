# 評価データ

フェーズ0で固定する評価 bag。bag 本体はリポジトリに含めない(置き場所: `~/data/slamrun`)。

| ID | センサ | `lidar_type` | パス | サイズ | 長さ | SHA256 | 正解軌跡 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| avia-quick-shack | Livox Avia | 1 | `avia/2020-09-16-quick-shack.bag` | 227,829,516 B | 50 s | `05a56e75898f952766f136d1e5db64a35d202e990e3b1052558369d8384d7ffe` | なし |
| ouster-nc-quad-easy | Ouster OS0-128 | 3 | `newer_college/2021-07-01-10-37-38-quad-easy-002.bag` | 10,775,549,922 B | 198 s | `64e4d2cdaffc652c4c536e9718a24999d0dd24ab71d255738a449d9683fc4215` | `newer_college/collection1/ground_truth/gt-nc-quad-easy.csv` |

## 出所

- avia-quick-shack: FAST-LIO 公開の Avia bag(Google Drive `1YqxHuDKzWUcda80QKBV61lXI86TXsGjP`)。取得日 2026-10-21。
- ouster-nc-quad-easy: Newer College Dataset(collection 1, quad-easy)。取得元・取得日は未記録(`.sha256` は同ディレクトリに保存済み)。

## 注意

- SHA256 は取得後に私たちが計算した値。avia-quick-shack には配布元のチェックサムがない。
- avia-quick-shack は正解軌跡がないため、ATE/RPE は出せない。動作確認と A/B/C の出力一致の比較に使う。
- Avia の IMU は g 単位(`acc_norm: 1.0`)。Ouster の IMU は m/s²。
- 点群の時刻フィールド単位と設定の整合は、実行して確認するまで未検証。
- 動的環境の bag は含めない(2026-10-21 に決定。理由は計画書のフェーズ0を参照)。
