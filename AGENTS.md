# プロジェクトの地図

## 概要

Point-LIO(LiDAR-慣性オドメトリ。本リポジトリは grid-map 付きの fork)を Rust で再実装し、精度・性能・安全性がどれだけ変わるかを測る検証プロジェクト。全体計画は [docs/rust-reimplementation-verification-plan.md](docs/rust-reimplementation-verification-plan.md)。

比較は3系統で行う。A→B の差がバグ修正の効果、B→C の差が言語・実装の効果。A→C を直接比べない。

- A: 現状の C++(as-is)
- B: C++ でバグ修正したもの
- C: Rust 実装

## 技術スタックと構成

C++14 / ROS1(catkin)/ Eigen3 / PCL 1.8 / OpenMP(任意)。READMEの動作確認環境は Ubuntu 20.04 + noetic。地図は ikd-tree ではなく iVox。状態推定は IKFoM(MTK)。

| パス | 責務 |
| --- | --- |
| src/ | C++ 本体(laserMapping, Estimator, IMU_Processing, preprocess, li_initialization) |
| include/ivox/ | iVox 地図 |
| include/IKFoM/ | 状態推定(多様体 ESEKF) |
| config/, launch/ | センサ別設定(avia, horizon, ouster64, velodyne16) |
| docs/ | 計画・仕様 |
| agents/ | 開発ルール・手順・移植ごとのプラン |

テストコードは未整備。Rust 実装・ゴールデン出力・評価データの置き場所は、作成時に追記する。

## 主要コマンド

| 操作 | 実行場所 | コマンド・参照先 | 前提条件 | 使わないもの(理由) |
| --- | --- | --- | --- | --- |
| ビルド(C++) | catkin ワークスペース | `catkin_make`([README](README.md) の Build 節) | ROS noetic、livox_ros_driver を source 済み | 【記入】 |
| 実行(C++) | 同上 | `roslaunch point_lio mapping_<sensor>.launch` | bag 再生またはドライバ | 【記入】 |
| サニタイザ付きビルド | 【記入】 | 【記入】 | 【記入】 | 【記入】 |
| ゴールデン出力の検証 | 【記入】 | 【記入】 | 【記入】 | 【記入】 |
| Rust ビルド・テスト | 【記入】 | 【記入】 | 【記入】 | 【記入】 |

未記入のコマンドは推測して実行しない。必要な操作について確認する。

## 作業前に読むもの

[agents/README.md](agents/README.md) で対象に合うルールを選ぶ。複数該当したらすべて読む。

- テストの追加・ゴールデン出力・許容誤差: [agents/rules/testing.md](agents/rules/testing.md)
- Rust への移植・数値の等価性・unsafe: [agents/rules/rust-port.md](agents/rules/rust-port.md)

矛盾する指示や未確定の仕様は、該当箇所を示して確認する。

## 依頼の範囲

- 頼まれた範囲だけを変更する。気づいた改善や別の不具合は手を付けず、報告に挙げる(バグは台帳への起票候補として報告する)
- 取り消せる細かな判断は妥当な仮定を置いて進める。次は必ず確認する: 許容誤差の変更、ゴールデン出力・評価データの更新、バグを「直す／仕様として固定」のどちらにするかの決定
- 返事がないことを承認とみなさない

## ユーザーへの応答

結論から端的に答える。判断に必要な根拠と未確認の事項は省かない。

## プランと作業の完了

Rust への移植はモジュール単位で [agents/plans/template.md](agents/plans/template.md) をもとにプランを作る。小さな修正までプランを要求しない。

変更内容、実行した検証と結果、未検証の範囲を報告する。実行していないテストや走らせていない実データでの確認を、通過扱いにしない。
