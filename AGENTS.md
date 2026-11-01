# プロジェクトの地図

## 概要

Point-LIO(LiDAR-慣性オドメトリ。本リポジトリは grid-map 付きの fork)を Rust で再実装し、精度・性能・安全性がどれだけ変わるかを測る検証プロジェクト。全体計画は [docs/rust-reimplementation-verification-plan.md](docs/rust-reimplementation-verification-plan.md)。

比較は A→B、B→C、C→D で行う。A→B の差がバグ修正の効果、B→C の差が移植の効果(言語・数値ライブラリ・メモリ配置・探索順を含む。言語だけの効果とは言わない)、C→D の差が設計変更・最適化の効果。A→C を直接比べない。

- A: 現状の C++(上流 `4b86a46` + 挙動を変えないハーネス化。タグ `A-v1`)
- B: C++ でバグ修正したもの(台帳で承認された修正だけ。メモリ/UB 系も含む)
- C: Rust 実装(B のアルゴリズム・演算順・スレッド構成を保つ)
- D: C を改良した Rust(C の完成後にだけ作る)

## 技術スタックと構成

C++14 / ROS1(catkin)/ Eigen3 / PCL 1.8 / OpenMP(任意)。READMEの動作確認環境は Ubuntu 20.04 + noetic。地図は ikd-tree ではなく iVox。状態推定は IKFoM(MTK)。

| パス | 責務 |
| --- | --- |
| src/ | C++ 本体(laserMapping, Estimator, IMU_Processing, preprocess, li_initialization)。ROS ノードは laserMapping_node.cpp、ファイル駆動の `pointlio_offline` は offline_main.cpp |
| include/ivox/ | iVox 地図 |
| include/IKFoM/ | 状態推定(多様体 ESEKF) |
| config/, launch/ | センサ別設定(avia, horizon, ouster64, velodyne16) |
| eval/ | 評価用の実行スクリプト、評価 bag 向けの起動・設定の上書き、正準設定(eval/config/canonical) |
| tests/harness/ | 実行ハーネスのテスト(T1〜T14) |
| docs/ | 計画・仕様 |
| agents/ | 開発ルール・手順・移植ごとのプラン |
| rust/ | Rust 実装(C)。Cargo のワークスペースで、範囲ごとに crate を置く。最初は数学と状態の表現の `rust/pointlio-math`(2026-10-31: 数学・状態・過程モデル・予測を実装。BL-011 の新Bへ合わせ、L1 全15件と L2 全13部品が通過。手順6以降と別セッションのレビューは未完了。[agents/plans/math-state.md](agents/plans/math-state.md)) |

bag は `~/data/slamrun`、ハーネスの入力ファイルは `~/data/slamrun-input`、実行結果は `~/data/slamrun-runs`(いずれもリポジトリ外。[docs/evaluation-data.md](docs/evaluation-data.md))。ゴールデン出力は golden/(数学と状態の表現は golden/math)。

## 主要コマンド

| 操作 | 実行場所 | コマンド・参照先 | 前提条件 | 使わないもの(理由) |
| --- | --- | --- | --- | --- |
| ビルド(C++) | リポジトリ直下(Docker) | 初回のみ `docker build -t point-lio-noetic -f docker/Dockerfile docker` と `docker/check_packages.sh`(全パッケージの版を `docker/packages.lock` と照合)。ビルドは `docker run --rm -v "$PWD":/ws/src/point_lio:ro point-lio-noetic bash -c '. /opt/ros/noetic/setup.sh && . /ws/devel/setup.sh && cd /ws && catkin_make'` | Docker。イメージに ROS noetic・glog・livox_ros_driver 入り | ホスト直での `catkin_make`(ホストは noetic 環境ではない)。成果物は `--rm` で消える |
| 実行(C++) | リポジトリ直下 | `eval/run_cpp.sh <avia-quick-shack\|ouster-nc-quad-easy> <出力ディレクトリ>`(ビルド・bag 再生・記録・TUM 変換まで。出力は `traj.tum`、`cloud.bag`、`meta.txt`、各ログ。決定性の確認用に環境変数 `PLAY_RATE`・`RVIZ=1`) | Docker イメージ `point-lio-noetic:latest`、bag が `~/data/slamrun`(SHA256 不一致なら中止) | `mapping_<sensor>.launch` の直接実行(rviz が起動する。Ouster は `scan_line` が 64 のままで OS0-128 に合わない)。実時間再生なので性能の数値には使わない |
| 入力ファイルの作成 | リポジトリ直下(Docker) | `docker run --rm -v "$PWD":/ws/src/point_lio:ro -v ~/data/slamrun:/data:ro -v ~/data/slamrun-input:/input point-lio-noetic bash -c '. /opt/ros/noetic/setup.sh && . /ws/devel/setup.sh && python3 /ws/src/point_lio/eval/extract_input.py /data/<bag> /ws/src/point_lio/eval/config/canonical/<ID>.yaml /input/<ID>.plin'` | bag の SHA256 一致。作成後に SHA256 を docs/evaluation-data.md と照合 | — |
| 実行(ファイル駆動) | リポジトリ直下 | `eval/run_offline.sh <ID> <出力ディレクトリ>`(ビルドと `pointlio_offline` の実行。出力は `traj.tum`、`odom.bin`、`cloud.bin` など。形式は src/offline_output.h) | 入力ファイルが `~/data/slamrun-input`(SHA256 不一致なら中止) | — |
| ハーネスのテスト | リポジトリ直下 | 抽出 `tests/harness/run_extract_tests.sh`(T1〜T4)、C++ `tests/harness/run_cpp_tests.sh`(T5・T6・T13)、設定 `tests/harness/run_t11.sh <ID> <run_offline の出力>`、結合 `~/clone/slamrun/.venv/bin/python -I tests/harness/check_runs.py <t7\|t8\|t9\|t10\|t12\|t14> ...`(引数はファイル先頭の説明) | 入力ファイル、bag、作業用 venv(evo 1.37.1・rosbags・scipy) | — |
| 数学の L1 テスト | リポジトリ直下(Docker) | `tests/math/run_math_tests.sh`(gtest の引数を渡せる。無効にしたテストは `--gtest_also_run_disabled_tests`) | Docker イメージ(libgtest-dev 入り) | — |
| サニタイザ付きビルド(ASan+UBSan) | リポジトリ直下(Docker) | 上記ビルドの `catkin_make` を `catkin_make --cmake-args -DCATKIN_WHITELIST_PACKAGES="" -DCMAKE_CXX_FLAGS_DEBUG="-g -fsanitize=address,undefined -fno-omit-frame-pointer" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"` に替える | 同上。ビルド成立のみ確認済みで、実行は未検証 | `CMAKE_CXX_FLAGS` への指定(CMakeLists.txt が `-O3` で上書きする)。MSan・TSan は未試行 |
| ゴールデン出力の検証 | リポジトリ直下(Docker) | 数学と状態の表現: `tests/math/run_math_golden.sh check`(形式と出所は golden/math/README.md) | Docker イメージ。`golden/math/SHA256SUMS` の一致 | `write`(人の承認と専用コミットが要る) |
| Rust ビルド・テスト | リポジトリ直下(Docker) | 初回のみ `docker/build_rust_image.sh`(土台 `point-lio-noetic:pinned` のイメージ ID を確かめてから、`docker/Dockerfile.rust` で `point-lio-rust` を作る)。ビルド `rust/run.sh build --release --locked --offline`。テスト `rust/run.sh test --release --locked --offline`(引数はそのまま `cargo` へ。`--network none`、成果物は `--rm` で消える)。ゴールデン出力との照合 `rust/run_math_golden.sh <新しい出力ディレクトリ>`(C の出力を作り、`tests/math/compare_math_golden.py` で比べる) | **2026-10-31: release で L1 全15件・入出力6件・参照計算3件・L2 全13部品が通過。手順6の debug/release 比較・libm の検査・BL-008 を戻す検査と、別セッションの差分・FFI レビューは未実施。照合の既定は実計算。単一部品は `POINTLIO_MATH_COMPONENT=<名前>` (部分照合と明記)。Red の再現には `POINTLIO_MATH_STUB=nan` (終了2)または `zero` (終了1)を指定。** Docker イメージ `point-lio-rust`(Rust 1.99.0 を `rust/rust-toolchain.toml` で固定。依存クレートなし) | ホストの `cargo`(ホストの glibc の libm が混ざり、B→C の差に libm の版の違いが入る)。`RUSTFLAGS`・`target-cpu` の指定(FMA が入り、B の V0 と演算が変わる) |

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
