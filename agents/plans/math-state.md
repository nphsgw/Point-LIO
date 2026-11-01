# プラン: 数学と状態の表現(C の最初の範囲)

状態: 承認済み(2026-10-31、人の判断。別セッションのレビュー2回を経た)。派生イメージ・自前の固定長行列型・sincos FFI の3点は人が承認済み。sincos FFI は、最初の承認の理由(「Rust は sin・cos を別々に呼ぶ」)が別セッションのレビューで誤りと分かったため、書き直した理由(3.3)で再承認した(2026-10-31)。レビューの指摘は末尾の「別セッションのレビューの反映」に記録した。
元資料: [計画書](../../docs/rust-reimplementation-verification-plan.md) §6・フェーズ3・決定事項(2026-10-30 の3、2026-10-31)、[状態の定義](../../docs/state-definition.md)、[事前登録の付録](../../docs/metrics-preregistration.md#付録-範囲ごとの基準)、[基準の根拠](../../docs/math-tolerance-proposal.md)、[注入による確認](../../docs/math-injection-validation.md)、[ゴールデン出力](../../golden/math/README.md)、[台帳](../../docs/bug-ledger.md) BL-008・BL-009

「実測」は 2026-10-31 に固定イメージ(`point-lio-noetic:pinned`、ID `sha256:4ffccd04…`)の中で確かめたもの。ただし「実測(ホストの rustc)」は、ホストの rustc 1.99.0(LLVM 23.1.1)で生成した命令列を見たもので、数値は比べていない。「要確認」は未決定か未検証。

## 1. 概要

`golden/math` の13部品(SO3 の Exp・Log・boxplus・boxminus、A 行列、so3_math.h の `Exp(v1, v2, v3)`、回転行列からクォータニオン、状態の boxplus・boxminus、`f_output`・`df_dx_output`、予測の状態と共分散)を Rust に移し、付録に登録済みの (a, r) で B のゴールデン出力と一致させる。計画書フェーズ3の最初の範囲。完了すると §6 の条件を満たした最初の C の範囲になり、後の範囲(前処理、推定器)が使う型(回転、状態、共分散)ができる。

## 2. 背景

- §6 の5条件はこの範囲で満たした: 状態の定義の文書、台帳(BL-008・BL-009 は B で修正済み)、L1 テストとゴールデン出力、固定イメージ、注入による確認(`0ed3d08`、2026-10-31)。
- 決定済み(2026-10-30): 置き場所は `rust/`、ツールチェーンは 1.99.0 を `rust-toolchain.toml` で固定し `Cargo.lock` をコミット、ビルドと実行は C++ と同じイメージを土台にする。
- (a, r) は登録済み。`so3_log`・`so3_boxminus`・`state_boxminus`・`a_matrix` はビット一致、`predict_cov` は r = 1e-10、ほかは r = 64u。閾値の例外は0件。
- 照合器 `tests/math/compare_math_golden.py` は注入で確かめ済み。C の出力にもこれを使い、Rust 側に別の照合器を作らない(作ると、その照合器の注入による確認が要る)。

## 3. 詳細設計

### 3.1 置き場所とビルド環境

```
rust/
  rust-toolchain.toml        channel = "1.99.0"、profile = "minimal"、components = rustfmt・clippy
  Cargo.toml                 ワークスペース。members = ["pointlio-math"]。edition 2024
  Cargo.lock
  run.sh                     Docker の中で cargo を動かす(引数はそのまま cargo へ)
  run_math_golden.sh         Docker の中でゴールデン出力の入力から C の出力を作り、照合器で比べる
  pointlio-math/
    src/lib.rs
    src/libm.rs              glibc の sincos の呼び出し(unsafe を隔離する唯一のモジュール)
    src/linalg.rs            Vec3・Mat3・Vec30・Mat30(固定長の配列)
    src/so3.rs               exp、log、boxplus、boxminus、a_matrix、exp3、quat_from_matrix
    src/state.rs             StateOutput、boxplus、boxminus、oplus
    src/process.rs           f_output、df_dx_output
    src/esekf.rs             predict(状態、共分散)
    src/golden.rs            PLGOLD01 の読み書き
    src/bin/math_golden.rs   `math_golden <ゴールデンのディレクトリ> <出力ディレクトリ>`
    tests/                   L1 テスト(性質・解析解・数値微分)
docker/
  Dockerfile.rust            FROM point-lio-noetic:pinned に rustup と 1.99.0 を足す
  build_rust_image.sh        土台のイメージ ID を確かめてから point-lio-rust を作る
```

- **Docker の土台**: `docker/Dockerfile` に足さず、`point-lio-noetic:pinned` から派生したイメージ `point-lio-rust` を別に作る。元のイメージを作り直さないので、apt の版が配布元から消えるリスク(計画書のリスク)を負わず、glibc 2.31(`2.31-0ubuntu9.18`、2026-10-31 に `dpkg-query -W libc6` で確認)の libm が B と同じになる。タグは書き換えられるので、`build_rust_image.sh` が土台のイメージ ID を `sha256:4ffccd04…` と照合してから作る。
- **ツールチェーンの固定**: rustup-init は版(1.29.1、ホストと同じ)と SHA256 で固定し、イメージを作るときに 1.99.0 を、`rust-toolchain.toml` と同じ profile と components(rustfmt・clippy)で入れる。足りない component があると、実行時に rustup がダウンロードしようとして失敗する。実行時は `--network none` で動かし、ダウンロードが起きないことを保証する。
- **作業ディレクトリ**: C++ のスクリプトと同じく、リポジトリは読み取り専用で mount する。`CARGO_TARGET_DIR` はコンテナ内(`--rm` で消える)に置く。`Cargo.lock` は手順1で1回だけ、`rust/` を書き込み可能に mount して `cargo generate-lockfile --offline` で作ってコミットし、以降は `--locked` で使う。
- **照合器の版**: 照合器はホストの `python3` で動かす(注入による確認と同じ)。`run_math_golden.sh` は `python3 --version` を出力ディレクトリに記録する(§6 の条件4)。
- **依存クレート**: なし(3.2)。`Cargo.lock` はワークスペースの crate だけになる。オフラインでビルドできる。
- **ビルドの設定**: `RUSTFLAGS`・`target-cpu`・`.cargo/config.toml` の rustflags を設定しない(x86-64 の基本命令だけ。FMA なし。B の V0 と同じ)。Rust(LLVM)は `mul_add` を書かない限り積和を結合せず、`-ffast-math` 相当もない。照合は `--release`(opt-level 3)で行い、debug でも出力がバイト単位で同じことを確かめる(5 節)。
- **ホストで動かさない**: ホストにも 1.99.0 があるが、ホストの glibc の libm が混ざると B→C の差に libm の版の違いが入る(2026-10-30 の決定)。

### 3.2 数値ライブラリ: 自前の小さな行列型にする

nalgebra を使わず、固定長の配列の型を自前で書く。各演算の和の順を、Eigen 3.3.7 の実際の順に合わせて明示的に書く。

理由(実測): Eigen の小さな行列の演算は、式の形と行によって和の順が変わる。ビット一致を求める部品(`so3_log`、`so3_boxminus`、`state_boxminus`、`a_matrix`)では、これを1つずつ再現する必要がある。nalgebra の積やノルムの和の順は文書化されていない実装の詳細で、行ごとに順を変えることもできない。

| Eigen の式(B での使用箇所) | 実測した順(20万件でビット一致) |
| --- | --- |
| `Matrix3d::trace()`(Log の θ と c) | d₀ + (d₁ + d₂)。左からの順 (d₀ + d₁) + d₂ とは 22% で異なる(割合は入力の分布による。成分が一様乱数の行列では 7%) |
| `Vector3d::norm()`(Exp の ‖θ‖、Log の ‖K‖) | sqrt((x₀² + x₁²) + x₂²) |
| `s * A * B`(3×3。Exp と A 行列の `(1 − cos) * K * K`) | スカラーは左の行列に先に掛かる(s·A_ik)·B_kj。0・1 行目は左からの順 (a₀ + a₁) + a₂、**2 行目は a₀ + (a₁ + a₂)**(SSE2 のパケットで2行、残りの1行をスカラーの縮約で計算するため) |

**手順2の測定対象(2026-10-31 に測定済み。結果は下表)**: `A.transpose() * B`(boxminus。ビット一致が要る)、スカラーなしの `A * B`(boxplus の R·Exp(δ))、`A * v`(`rot * acc`、予測の A 行列 × 列)、`-A * B`(`df_dx_output` の −rot·[acc]×)、`a.dot(K)`(Log の π の近くの分岐)、`S.col(i).normalized()`(Eigen 3.3.7 の `Dot.h` 124〜132 行では、z = squaredNorm が z > 0 のときだけ v / sqrt(z) の成分ごとの割り算、そうでなければ v をそのまま返す)、`maxCoeff(&i)`(同値なら最初の添字)。要素ごとの和(`I + s₁K + X`、`0.5 * (R + Rᵀ) − c·I`、`F += f_x_final * dt`)も測定対象とした。

参考(別セッションのレビューでの実測、各20万件。`noinline` の単体の関数で測ったもので、B の関数に inline された形ではない。手順2の実測の代わりにしない): `A.transpose() * B` は**全行が**左からの順。スカラーなしの `A * B` と `A * v` は `s * A * B` と同じく2行目だけ a₀ + (a₁ + a₂)。`S.col(i).normalized()` の squaredNorm と `a.dot(K)` は左からの順。`I + s₁K + s₂·K·K` は ((I + s₁K) + X)。零ベクトルの `normalized()` は零を返す。したがって boxminus の `Rᵀ·R` に、通常の積の関数を流用してはいけない(2行目の順が違う)。

**手順2の測定結果(2026-10-31)**: `tests/math/eigen_order_probe.cpp` を固定イメージ内で `g++ -std=c++14 -O3 -g -pthread -fexceptions -fopenmp -Wall -Wextra -Werror -I/usr/include/eigen3` によりコンパイルした。gcc 9.4.0、Eigen 3.3.7、`-march`・`-ffast-math` なし、`OMP_NUM_THREADS=1`。seed = 20261031。乱数は `mt19937_64` の上位53ビットを 2⁵³ で割り、別の乱数ビットで符号を付ける。ビット比較には `memcmp` を使う。

| 式 | 観測した順・動作 | 検査数 / 不一致数 |
| --- | --- | --- |
| `A.transpose() * B` | 全行が (a₀ + a₁) + a₂ | 20万行列、180万成分 / 0 |
| `A * B`、`s * A * B`、`-A * B` | 0・1行目は左結合、2行目は a₀ + (a₁ + a₂)。スカラー・負号は左の行列に先に適用 | 各20万行列、180万成分 / 0 |
| `A * v` | 0・1行目は左結合、2行目は右結合 | 20万ベクトル、60万成分 / 0 |
| `a.dot(K)` | (a₀K₀ + a₁K₁) + a₂K₂ | 20万件 / 0 |
| `trace()`、`norm()` | trace は d₀ + (d₁ + d₂)、norm は sqrt((x₀² + x₁²) + x₂²) | 各20万件 / 0 |
| `S.col(i).normalized()` | 左結合の squaredNorm の平方根で各成分を割る。逆数を先に作って掛ける形とは異なる | 20万列、60万成分 / 0。零列は3成分 / 0 |
| `maxCoeff(&i)` | 最大の対角成分の先頭添字。同値と符号付き零のケースも先頭 | 乱数20万件 + 同値7件 / 0 |
| `I + s₁K + s₂·K·K` | (I + s₁K) + X。X の積は上記のスカラー付き積 | 20万行列、180万成分 / 0 |
| `0.5 * (R + Rᵀ) − c·I` | 全成分で減算。非対角でも c·0 を引く | 20万行列、180万成分 / 0。符号付き零2成分 / 0 |
| `F += J * dt` (30×30) | 各成分で Fᵢⱼ + (Jᵢⱼ·dt) | 2000行列、180万成分 / 0 |

別の結合順・積の後のスカラー倍・正規化での逆数倍も比べ、それぞれ不一致が生じることを確認した(道具は代替候補の不一致が0なら失敗する)。例: 転置積を右結合にした候補は559450/1800000成分、通常の積を全行左結合にした候補は187347/1800000成分、正規化の逆数倍は155514/600000成分で異なった。

再実行(リポジトリ直下):

```bash
docker run --rm --network none -e OMP_NUM_THREADS=1 -v "$PWD":/repo:ro point-lio-noetic:pinned bash -c 'g++ -std=c++14 -O3 -g -pthread -fexceptions -fopenmp -Wall -Wextra -Werror -I/usr/include/eigen3 /repo/tests/math/eigen_order_probe.cpp -o /tmp/eigen_order_probe && /tmp/eigen_order_probe'
```

この道具の関数境界は `noinline`。参考の記録を流用せず新しい seed と検査で測定したが、B の関数内に inline された形の順を証明するものではない。実際の部品で同じ順になるかは、手順5の L2 で確かめる。ビット一致が説明できず落ちた場合の停止条件は変えない。

**避けられない違い(一覧に載せる)**: `predict_cov` の 30×30 の積 `F * P * Fᵀ`。Eigen はブロック化した GEMM で計算し、和の順はカーネルの実装に依存する。C は (F·P)·Fᵀ の結合を保ち、各要素を k の昇順で足す。登録済みの r = 1e-10 はこの違いを見込んだもの(math-tolerance-proposal.md 3 節)。

後の範囲(LiDAR 更新の 6×6・30×30 の逆行列など)で行列の分解が要るときは、その範囲のプランで、自前で Eigen の手順を移すか、ライブラリを入れるかを決める。

### 3.3 libm: B が呼ぶ関数をそのまま呼ぶ

実測: B の `math_golden` と `pointlio_offline` では、GCC が同じ引数の `std::sin` と `std::cos` を glibc の `sincos` 1回にまとめている。対象は `MTK::SO3::exp`、`MTK::A_matrix`、so3_math.h の `Exp(v1, v2, v3)`。BL-011 (`4558dea`) 後の `SO3::log` は `acos`・`atan2` を呼び、通常の返値には `sin` を使わない。以下の B の逆アセンブルの実測は BL-011 前の記録であり、手順6では修正後 B と C を改めて確認する。別セッションのレビューで、両バイナリの逆アセンブルを関数ごとに見て、`sincos` の呼び出しはこの3関数の本体だけにあり、inline された箇所で `sin`・`cos` を別々に呼ぶものがないことを確かめた。

glibc 2.31 の `sincos` は、別々の `sin`・`cos` と結果が異なることがある(実測: 回転角 0.001〜π の範囲で、100万件あたり 36〜1829 件が 1 ulp 程度違う。1e-3 未満では0件)。ビット一致を求める `a_matrix` で別々に呼ぶと、入力の 0.1〜0.2% で落ちる見込みになる。

Rust の `f64::sin`・`f64::cos` がどちらを呼ぶかは、ビルドで変わる(実測(ホストの rustc)): opt-level 3 では、LLVM が同じ引数の `sin` と `cos` を `sincos` 1回にまとめる。opt-level 0 では別々に呼ぶ。まとめるかどうかは、2つが同じ基本ブロックにあるかにも依存する。`f64` のままでは、release と debug で呼ぶ関数が変わり、コードの形の小さな変更でも変わりうる。

そのため C は、B が `sincos` を呼ぶ3か所で glibc の `sincos` を FFI で呼び、どのビルドでも B と同じ関数を呼ぶようにする。最初の FFI の承認は「Rust は別々に呼ぶ」という誤った理由に基づいていたので、この理由で再承認した(2026-10-31、計画書の決定事項 2026-10-31 の9)。`f64` のまま opt-level 3 の LLVM のまとめに頼る案は、unsafe は要らないが、debug との一致と、まとめが起きることの保証を失うので採らない。

- `unsafe` は `src/libm.rs` の1モジュールに隔離する(`unsafe extern "C" { fn sincos(x: f64, s: *mut f64, c: *mut f64); }` と、1つの `unsafe` 呼び出しブロックを包む安全な関数)。宣言1件と呼び出しブロック1件を別々に数え、行数も記録する。前提条件(2つのポインタは呼び出し側のスタック上の有効な `f64` を指し、glibc は書くだけで保持しない)をコメントに書く。crate 全体は `#![deny(unsafe_code)]` とし、このモジュールだけ許す。リンクの先を std に頼らないよう、宣言に `#[link(name = "m")]` を付ける。
- FFI の箇所なので、ルールどおりメモリ安全レビュー担当の確認を受ける。
- 単独で必要な `acos`・`atan2` は Rust の標準ライブラリから呼び、`sincos` とともに glibc の同名の関数に解決されることをビルド後の `nm -D` で確かめる(5 節)。`sin`・`cos` は BL-011 後の既定版では単独に呼ぶ必要がなく、シンボルの存在を必須にしない。`sqrt` は両方とも命令(`sqrtsd`)で、正しく丸められる。
- `nm -D` はシンボルがあることしか示さず、FFI の `sincos` と LLVM がまとめた `sincos` を区別できない。そこで release と debug の両方の `math_golden` を逆アセンブルし(`objdump -d -C`)、呼び出し先をたどって確かめる。命令の個数は数えない(debug では共通の安全な関数を経由し、release ではその関数が inline されうるので、直接の `call` の数はビルドで変わる)。合格の条件:
  - `so3::exp`・`so3::a_matrix`・`so3::exp3` の3つの経路それぞれが、`libm.rs` の共通の安全な関数か、その inline 展開を経て `sincos` を呼ぶ。経路の中で `sin`・`cos` を呼ばない。
  - `so3::log` の経路は `acos`・`atan2` だけを呼び、`sin`・`cos`・`sincos` を呼ばない (BL-011 後)。
  - それ以外の経路は三角関数を呼ばない。
  - inline で関数の境界が消えた箇所は、デバッグ情報(`objdump -dl` などの行情報)でソースの関数に対応づける。**対応づけられない呼び出しが1つでも残る間は、この検査を未完了とする**(記録だけで完了扱いにしない)。
- `sincos` を外す比較用の版は、opt-level 3 では LLVM がまた `sincos` にまとめるので、何も比べられない。片方の呼び出しの**引数を** `std::hint::black_box` に通して(`let s = x.sin(); let c = black_box(x).cos();`)まとめを防ぎ、逆アセンブルで別々に呼ぶことを確かめてから照合する。2つの呼び出しの間に `black_box(())` を置くだけでは、opt-level 3 でまた `sincos` にまとめられる(別セッションのレビューでの実測(ホストの rustc))。
- glibc の libm は CPU に応じて実装を選ぶ(ifunc)。B と C は同じ機械で比べ、CPU を記録する(既存の方針どおり)。

### 3.4 型と演算

| C++ の対象 | Rust の対象 | 差異・注意 |
| --- | --- | --- |
| `MTK::SO3<double>::exp`(`include/IKFoM/IKFoM_toolkit/mtk/types/SOn.hpp`) | `so3::exp(v, scale)` | ang = v·scale。‖ang‖ > 1e-7 のときだけロドリゲス、それ以外はちょうど I。r_axis = ang / ‖ang‖(割り算)。`(I + sin·K) + ((1 − cos)·K)·K`。sin・cos は `sincos` |
| `SO3::log`(BL-008・BL-011 の修正後) | `so3::log` | θ の閾値(tr > 3 − 1e-6)、c = 0.5·(tr − 1) < −1 + 5e-8 の分岐(対称な部分、`maxCoeff`、`normalized`、K との内積で向き、`atan2(0.5·‖K‖, c)`)、\|θ\| < 0.001 の 0.5·K、それ以外は **k_norm = ‖K‖、angle = atan2(0.5·k_norm, c)、(angle/k_norm)·K** の順 (BL-011)。小角判定用の旧 θ は残す。トレースは式に現れる回数だけ計算し直す(値は同じ)。対称な部分 S = 0.5·(R + Rᵀ) − c·I は、**非対角でも c·0 を引く**(c < 0 なので −0 を引き、−0 の和が +0 になる。照合器はビット一致の部品で生のバイトを比べるので、符号付き零の違いも不一致になる)。`normalized` は z = squaredNorm が z > 0 のときだけ割り、そうでなければ列をそのまま返す。この分岐の中では、実数の演算で tr(S) = tr(R) − 3c = 1 − c > 2 − 5e-8 なので(π − 1e-4 の回転で tr(S) = 1.999999995、別セッションのレビューでの実測)、有限な回転行列の入力では対角の最大は正で、選んだ列の squaredNorm も正になる(NaN の入力を除く)。零の場合は到達しないのでテストを足さないが、式は Eigen と同じ形で写す |
| `SO3::boxplus`・`oplus` | `so3::boxplus` | R·Exp(s·δ)(右から) |
| `SO3::boxminus` | `so3::boxminus` | Log(R₂ᵀ·R₁)。積の順は手順2で実測 |
| `MTK::A_matrix`(`mtk/src/mtkmath.hpp`) | `so3::a_matrix` | ノルムは `sqrt((v₀v₀ + v₁v₁) + v₂v₂)` と明示の式。‖v‖ < 1e-11 なら I。`(I + ((1 − cos)/sq)·[v]×) + (((1 − sin/n)/sq)·[v]×)·[v]×`。sin・cos は `sincos` |
| `Exp(v1, v2, v3)`(`include/so3_math.h`) | `so3::exp3` | 閾値 1e-5。ノルムは明示の式。sin・cos は `sincos` |
| `Eigen::Quaterniond(R)`(Eigen 3.3.7 `quaternionbase_assign_impl<Other,3,3>`) | `so3::quat_from_matrix` → `[x, y, z, w]` | state-definition.md 6 節の手順。正規化しない |
| `state_output`(`include/common_lib.h`) | `state::StateOutput` | 成分の並び・30 次元の添字は state-definition.md 4 節。回転は `Mat3`(回転行列。クォータニオンで持たない)。正規化しない |
| `state_output::boxplus`・`boxminus`・`oplus` | `StateOutput::boxplus`・`boxminus`・`oplus` | 宣言順に成分ごと。vect は `v + s·δ`(s = 1 でも掛け算を残す)。SO3 は上の boxplus |
| `get_f_output`・`df_dx_output`(`src/Estimator.cpp`) | `process::f_output`・`df_dx_output` | 入力 `input_ikfom` を受けない(範囲内では参照されない) |
| `esekf::predict`(`include/IKFoM/IKFoM_toolkit/esekfom/esekfom.hpp` 137〜183 行) | `esekf::predict(x, p, q, dt, predict_state, prop_cov)` | 状態: x ⊕ (f·dt)。共分散: f_x_final の行の並べ替え、SO3 の行ブロックは A(−f·dt)·(列ごとの 3×1)、F の SO3 の対角は Exp(−f·dt)、`F += f_x_final·dt`、`(F·P)·Fᵀ + Q·(dt·dt)`。対称化しない。状態の型が固定なので、MTK の `vect_state`・`SO3_state` の表は持たず、添字を直接書く。2つのフラグがともに true のときは、C++ と同じく、状態を進めた**後の** x で f と f_x を計算し直してから共分散を進める。この組み合わせはゴールデン出力に含まれず、イベントループも使わない(`src/laserMapping.cpp` 615〜748 行は、どちらか一方だけを立てる)ので、範囲内では確かめない |

- 行列の持ち方は `[[f64; 3]; 3]` の行優先(`m[r][c]`)。Eigen は列優先だが、この範囲では配列として受け渡さないので、演算の順だけを合わせる。PLGOLD01 は行優先なので、読み書きは添字の対応だけで済む。
- 範囲外の分岐(`kf_input`、S2)は型に入れない。

### 3.5 公開 API

手順4の前に [公開 API 案](math-state-api.md)を提示し、2026-10-31 に人の承認を得た。配列の型エイリアス、`StateOutput` の全フィールド、C++ に対応する更新メソッドと関数を公開し、内部の行列演算は公開しない。数値の本体は手順5で実装する。

## 4. テスト影響範囲

- C++(B)のソース、ゴールデン出力、登録済みの (a, r)、照合器は変えない。
- 既存のイメージ `point-lio-noetic:pinned` は変えない。`point-lio-rust` は別のタグ。
- 追加するのは `rust/`、`docker/Dockerfile.rust`・`docker/build_rust_image.sh`、手順2の実測の道具(`tests/math/eigen_order_probe.cpp`。B のビルドには組み込まない)。
- CLAUDE.md の構成の表と「Rust ビルド・テスト」の行を、作ったコマンドに合わせて更新する(この草案と同時に、予定として記入した)。

## 5. 新規テストケース

数学と状態の表現の L1 は14件。`math_tests.cpp` の全16件から範囲外の `SetInit.*`・`IVoxClosest.*` の2件を除く。表では関連するテストを1行にまとめているが、移植時には元の14件と対応づけて件数を確認する。

| 台帳 ID／要件 | 前提・入力 | 操作 | 期待結果と許容誤差 | 出典 | 種別 |
| --- | --- | --- | --- | --- | --- |
| J4(付録) | `golden/math` の13ファイル(SHA256 一致)の入力 | `rust/run_math_golden.sh <出力>` で C の出力を作り、`compare_math_golden.py golden/math <出力>` | 終了コード0。付録の (a, r): ビット一致4部品、64u の8部品、`predict_cov` 1e-10。分岐の違いは例外にしない | B のゴールデン出力(golden/math/README.md) | 結合 |
| Red の確認 | 同上 | 実装前に、全部品が NaN を返す仮の実装で同じ操作 | 終了コード2(不正なデータ)。全部品が 0 を返す仮の実装で終了コード1、かつ13部品すべてが `FAIL` の行になる(行の数を確かめる) | 照合器の契約(math-injection-validation.md) | 結合 |
| BL-008 の修正を戻すと落ちる | 実装後の C | `so3::log` から c < −1 + 5e-8 の分岐を外した版(A の式)を一時的に作り、L1 と照合を実行する | `So3Log.FiniteNearPi`・`So3Log.AccurateAroundNearPiBranch` に対応する L1 が落ち、照合で `so3_log` が `FAIL`(π の近くの記録で不一致、または NaN で終了コード2)。確かめたら版を捨て、コミットしない | testing.md(修正を戻すとテストが落ちること)、台帳 BL-008 | 単体・結合 |
| 最適化の影響 | 同上 | `--release` と debug の両方で C の出力を作る | 13ファイルがバイト単位で一致(どちらも照合器を通る) | Rust の浮動小数点の意味論(fast-math なし)からの予想 | 性質 |
| 3.3 libm | `--release` の `math_golden` | `nm -D` | `acos`・`atan2`・`sincos` が `GLIBC_*` の未定義シンボルとして現れる。BL-011 後の既定版では単独の `sin`・`cos` は必須にしない | 3.3 の設計 (BL-011 後) | 性質 |
| 3.3 libm の呼び出し箇所 | `--release` と debug の `math_golden` | `objdump -d -C`(inline された箇所は行情報で対応づける)で、三角関数の呼び出し先をたどる | 3.3 の合格の条件をすべて満たす。ソースの関数に対応づけられない呼び出しが残れば未完了(通過扱いにしない) | 3.3 の設計、B の逆アセンブル | 性質 |
| 3.3 の根拠の記録(合否にしない) | 同上 | `sincos` を、片方の引数を `black_box` に通した別々の `sin`・`cos` に替えた版で照合。先に逆アセンブルで別々に呼ぶことを確かめる | 落ちる部品と件数を記録する(落ちなくても失敗としない。0.1〜0.2% なので記録の数によっては落ちない) | 3.3 の実測 | 記録 |
| L1: Exp | 回転角 2e-7〜3.14 の 120 件 | 別の形のロドリゲス(cos·I + sin·[a]× + (1 − cos)·aaᵀ)と比べる | 成分の最大差 ≤ 8e-15。件数 120 を確かめる | math_tests.cpp `So3Exp.MatchesAxisAngle`(同じ許容誤差) | 単体 |
| L1: Exp の閾値 | 回転角 1e-9〜1e-7 | `exp` | ちょうど I、真値との差 ≤ ‖θ‖·1.0001 | 同 `BelowThresholdReturnsIdentityWithinAngle` | 単体 |
| L1: 回転 | 回転角 1e-6〜π の 200 件 | `exp` | ‖RᵀR − I‖_max ≤ 8e-15、\|det R − 1\| ≤ 8e-15 | 同 `IsRotation` | 性質 |
| L1: Log | 1e-3〜π − 1e-3 の 200 件、1e-6〜0.999e-3 の 100 件、π − d の各 20 件 | 別の形の Exp で作った R の `log` | ‖w − v‖ ≤ 1e-12(大きい角)、≤ θ³/6·1.01 + 1e-18(小さい角)、π の近くは有限かつ Exp の差 ≤ 1e-6、分岐の内側 ≤ 1e-12・外側 ≤ 10ε/d² | 同 `So3Log.*`(BL-008 の回帰を含む) | 単体 |
| L1: BL-011 (B の承認済み修正に伴う追加) | B の回帰テストと同じ R・v の固定ビット列1件 | `log(R)` と v を比べる | 有限、‖w − v‖ ≤ 1e-12。旧通常式で失敗し、新Bの式で通過 | `4558dea` の `So3Log.InvertsRoundedRotationBL011`。新たな許容誤差は決めない | 単体 |
| L1: boxplus・boxminus | 回転と状態の乱数 | 右からの摂動、boxminus(boxplus(x, δ), x) | 右積との差 ≤ 8e-15 かつ左積との差 > 1e-6、逆元の差 ≤ 1e-12 | 同 `So3Boxplus.*`・`So3Boxminus.*`・`StateBoxminus.*` | 単体 |
| L1: A 行列 | 回転角 1e-3〜2.5 の 50 件 | 中心差分(h = 1e-6)の左ヤコビアン | 成分の最大差 ≤ 1e-8 | 同 `AMatrix.IsLeftJacobianOfExp` | 単体 |
| L1: 過程モデル | 状態の乱数 20 件 | f の中心差分 | ∂f/∂x との最大差 ≤ 1e-7 | 同 `ProcessModel.JacobianMatchesNumeric` | 単体 |
| L1: 予測の共分散 | dt 0.005・0.1、各 10 件 | 状態の伝播の中心差分で F を作り F·P·Fᵀ + Q·dt² | 最大差 ≤ 1e-7·max\|expected\|。共分散だけの呼び出しで状態が変わらない | 同 `Predict.CovarianceMatchesNumericJacobian` | 単体 |
| L1: 予測の共分散の性質 | 20 件 | `predict` | 非対称 ≤ 1e-13·m、最小固有値 ≥ −1e-12·m | 同 `Predict.CovarianceStaysSymmetricPositive` | 性質 |

- L1 の参照のうち、別の形のロドリゲス(cos·I + sin·[a]× + (1 − cos)·aaᵀ)、別の Log、最小固有値は、テスト側に実装と独立に書く。C++ のテストが使う Eigen の `AngleAxisd` の代わりに、Log の参照はクォータニオン経由の別の式にする。最小固有値は、テスト側に Jacobi 法を書いて求める。
- 数値微分のテスト(`ProcessModel.JacobianMatchesNumeric`、`Predict.CovarianceMatchesNumericJacobian`)は、C++ と同じく、摂動と差に**実装の** `StateOutput::boxplus`・`boxminus`・`oplus` と `process::f_output` を使う。テスト側に書き直すと、その参照を別に確かめる手段が要るため。したがって、この2件が確かめるのは「Jacobian・共分散の伝播が、実装の状態の演算と整合すること」であり、boxplus・boxminus・Exp 自体の正しさは、上の Exp・Log・boxplus・boxminus のテストが独立の参照で確かめる。
- L1 の許容誤差は math_tests.cpp と同じ値にし、新たに決めない。乱数は C++ の `std::mt19937_64` と同じ列を再現しない(L1 は解析解との比較なので、入力が同じである必要はない)。
- ループの検査は件数も確かめる(testing.md)。
- 範囲外の L1(`SetInit.*`、`IVoxClosest.*`)は移さない。

## 6. 実装順

1. **ビルド環境**: `docker/Dockerfile.rust`・`docker/build_rust_image.sh`、`rust/` のワークスペースと空の crate、`rust/run.sh`。`--network none` で `cargo build --release --locked --offline` が通り、`rustc -vV` が 1.99.0 であることを確かめる。`curl` は土台に導入済み(`7.68.0-1ubuntu2.25`、2026-10-31 に確認)で、追加の apt インストールは不要。rustup-init 1.29.1 の SHA256 は実ファイルと公式のチェックサムの一致を確認済み(末尾の「手順1の実施記録」)。
2. **Eigen の演算順の実測**: `tests/math/eigen_order_probe.cpp` で 3.2 の要確認の式を実測し、表を 3.2 に追記する。実測は固定イメージで、B と同じフラグ(`-O3`、`-march` なし)で行う。B の実際の関数に inline された形で順が変わらないことは、手順5のビット一致で確かめる。
3. **PLGOLD01 の読み書きと Red**: `golden.rs`、`bin/math_golden.rs`、`run_math_golden.sh`。全部品を仮の実装(NaN、0)にして、照合器が終了コード2・1を返すことを確かめる。
4. **L1 テストを先に書く**(5 節)。実装前に失敗することを確かめる。
5. **実装**: `linalg` → `libm` → `so3`(exp、log、boxplus、boxminus、a_matrix、exp3、quat)→ `state` → `process` → `esekf`。部品ごとに照合器を通し、ビット一致の部品で落ちたら、和の順を手順2の表と照らす。表で説明できない不一致は、基準を広げずに停止して報告する(math-tolerance-proposal.md 4 節の選択肢)。
6. **残りの検査**: debug と release のバイト一致、`nm -D` と関数ごとの逆アセンブル、`sincos` を外した版の記録、BL-008 の修正を戻した版で落ちること、`cargo clippy`、`unsafe` の数え上げ。
7. **CLAUDE.md の更新**: 作ったコマンドで「Rust ビルド・テスト」の行を確定し、未作成の注記を外す。
8. **別セッションでの差分レビュー**(agents/README.md の手順4)と、FFI のメモリ安全レビュー。

手順1と2は独立なので並べてよい。ほかは順に行う。

## 7. コミット前テスト実行

| 検証 | 実行場所 | コマンド | 前提条件 |
| --- | --- | --- | --- |
| 単体テスト(L1) | リポジトリ直下(Docker) | `rust/run.sh test --release --locked --offline` | `point-lio-rust`(手順1で作る) |
| ゴールデン出力との比較 | リポジトリ直下(Docker、照合はホストの `python3`) | `rust/run_math_golden.sh <新しい出力ディレクトリ>`(中で `compare_math_golden.py golden/math <出力>` を呼ぶ) | `golden/math/SHA256SUMS` の一致、`point-lio-rust` |
| 性質テスト | リポジトリ直下(Docker) | 回転・共分散などの性質は上の単体テストに含む | `point-lio-rust` |
| 手順6の検査 | リポジトリ直下(Docker、保存した ELF の読解はホスト) | `bash rust/check_math_step6.sh <リポジトリ外の新しい出力先>`。debug/release のバイト比較、動的シンボル・逆アセンブルの採取、比較版の検査。終了0だけでは関数ごとの逆アセンブルの確認を通過とみなさない | `point-lio-rust`、`golden/math/SHA256SUMS` の一致、ホストの `python3` と Rust v0 の名前を復元できる binutils (2.42 で確認) |
| 照合器の契約テスト(変えていないことの確認) | リポジトリ直下 | `python3 -B tests/math/test_compare_math_golden.py` | — |
| clippy | リポジトリ直下(Docker) | `rust/run.sh clippy --release --locked --offline --all-targets -- -D warnings` | `point-lio-rust` |
| fmt | リポジトリ直下(Docker) | `rust/run.sh fmt --all -- --check` | `point-lio-rust` |

完了時に実行結果と未検証の範囲を追記する。実行予定を、実行済みの結果として書かない。

## 8. スコープ外

- ゴールデン出力の13部品にない関数: so3_math.h の `Exp(ang)`・`Exp(ang_vel, dt)`・`Log`・`RotMtoEuler`、`SO3ToEuler`、`Jacob_right`・`Jacob_right_inv`、`process_noise_cov_output`(設定に依存する。推定器の範囲で移す)、観測モデル、更新、`Set_init`(初期化の範囲)。
- S2、`kf_input`(24 次元)、`use_imu_as_input: true` の経路。
- 回転の正規化、クォータニオンでの保持、並列化、SIMD、`mul_add`(D の範囲)。
- バグの修正(B で先に行う)。この範囲で B の誤りが疑われたら、台帳に起票して止まる。
- 性能の計測(フェーズ0の項目6が先)。
- 後の範囲の行列の分解(3.2 の末尾)。

## 完了時に記録するもの

- `unsafe` の箇所数と行数(予定: `src/libm.rs` の1モジュール、unsafe extern 宣言1件と unsafe 呼び出しブロック1件)
- 許容誤差内に収まったか。収まらなかった場合は差分の大きさと推定原因
- 避けられない違いの一覧(予定: `predict_cov` の GEMM の和の順)
- イメージ ID(`point-lio-rust` と土台)、`rustc -vV`、CPU、`Cargo.lock` のコミット
- 未検証の範囲(予定: 実データでの L3、f32 の中間計算、ほかの CPU での libm の実装の選択)

## 承認前の確認記録(2026-10-31)

- 人の判断: `point-lio-rust` の派生イメージ、自前の固定長行列型、glibc `sincos` の FFI を承認。プラン全体の承認とは区別する。
- このセッションでの点検: L1 の14件の対応を明記、実測した libc6 の版を訂正、curl の導入済みを確認、単独の cos を必須シンボルから除外、unsafe の宣言と呼び出しブロックを分けて記録するよう明記した。
- プラン全体の別セッションレビュー、rustup-init の SHA256、Eigen の残りの演算順、Rust 実装の検証は未完了。これは承認前の時点の記録。SHA256 と Eigen の演算順の確認は、後述の「手順1の実施記録」「手順2の実施記録」で完了した。

## 別セッションのレビューの反映(2026-10-31)

- 3.3: 「LLVM は sincos にまとめない」は誤りだった(ホストの rustc 1.99.0 で、opt-level 3 ではまとめ、0 ではまとめない)。理由を書き直し、その理由で FFI を再承認した(人の判断)。比較用の版を `black_box` で作り、関数ごとの逆アセンブルの検査を足した。
- 3.4: Log の `((0.5·θ)/sin θ)·K` の結合、S の非対角で c·0 を引くこと、`normalized` の z > 0 の分岐(到達しないことの理由も)を書いた。predict の両フラグの動作を書いた。
- 5 節: BL-008 の修正を戻すと落ちることの確認、零を返す仮の実装で13部品すべてが FAIL になることの確認を足した。数値微分のテストが実装の状態の演算を使うことを明記した。
- 3.1・3.3: component の導入、読み取り専用の mount と `CARGO_TARGET_DIR`、`Cargo.lock` の作り方、`#[link(name = "m")]`、照合器の Python の版の記録を足した。
- 3.2: レビューでの参考の実測(`noinline` の関数で測ったもの)を追記した。手順2の実測は残る。
- 2回目の別セッションのレビュー(`54b3753` が対象。判定は「修正すれば承認してよい」)の指摘を反映した: 逆アセンブルの検査を、命令の個数ではなく呼び出し先をたどる形にし、対応づけられない呼び出しが残る間は未完了とした。`black_box` は片方の引数に通す形に決めた。`normalized` の零の分岐に到達しない理由の下界を tr(S) > 2 − 5e-8 に訂正した(結論は変わらない)。
- 3点の人の承認を、計画書の決定事項(2026-10-31)の7〜9に記録した。sincos FFI は書き直した理由で再承認した。
- 既存スクリプトを実行: `bash tests/math/run_math_tests.sh` は全16件通過、`bash tests/math/run_math_golden.sh check` は全13部品通過。どちらも終了コード0。latest と pinned は同じ ID `sha256:4ffccd043d071a088d7b09e80eb78cbef3b61d777e2b4f146768bac3def2dead` だった。livox のビルド失敗は再現しなかったため、今回は台帳に起票しない。別の失敗ログがある場合は、その実行条件との差を調べる。ログ: `/tmp/pointlio-plan-l1-20261031.log`、`/tmp/pointlio-plan-golden-20261031.log`。

## 手順1の実施記録(2026-10-31)

- **手順1完了**: `docker/Dockerfile.rust`、`docker/build_rust_image.sh`、`rust/` のワークスペース、空の `pointlio-math` crate、`rust/run.sh`、コンテナで生成した `Cargo.lock` を追加した。数学の実装・L1 テスト・ゴールデン出力の読み書きは未着手。
- rustup-init: [公式チェックサム](https://static.rust-lang.org/rustup/archive/1.29.1/x86_64-unknown-linux-gnu/rustup-init.sha256)とダウンロードした x86_64-unknown-linux-gnu 用の実ファイルが一致。SHA256 は `dda7234360b7f578ca8b0ddcb80145646fa61a67c1720a5abc7051b35c9fcb71`。Dockerfile も同じ値で照合してから実行する。
- 土台の ID: `sha256:4ffccd043d071a088d7b09e80eb78cbef3b61d777e2b4f146768bac3def2dead`。作成した `point-lio-rust` の ID: `sha256:1aec8ce20e45bf85982dc8fd4cbd90e45ee8c58f65d61b6a07f5b6813d79ddb5`。土台・派生とも `docker/check_packages.sh` を通過し、OS パッケージと livox は固定版のまま。
- `rustc -vV`: rustc 1.99.0 (`b940084d7eb6a299eb4bfeb8e34901bc051e7ac4`, 2026-09-28)、host `x86_64-unknown-linux-gnu`、LLVM 23.1.1。cargo 1.99.0 (`5f94df478`, 2026-08-27)。rustfmt・clippy の導入を確認した。
- CPU: AMD Ryzen 7 7700X 8-Core Processor。libc6 は `2.31-0ubuntu9.18`。
- `rust/run.sh build --release --locked --offline` が、`--network none`、リポジトリの読み取り専用 mount、ホストと同じ UID/GID、コンテナ内の `CARGO_TARGET_DIR` で通過。`fmt --all -- --check` と `clippy --release --locked --offline -- -D warnings` も通過。これらは空の crate とビルド環境の検証であり、数学の L1・L2 を通過したとは扱わない。
- 記録先: `/tmp/pointlio-rust-step12-20261031`。イメージのビルドログ、環境情報、公式チェックサムと実ファイルを置いた。`/tmp` は永続保管先ではない。
- `unsafe`: 現時点で宣言0件・呼び出しブロック0件(FFI は手順5)。`Cargo.lock` とビルド環境は手順1のコミットに含めた。別セッションの差分レビューは未実施。

## 手順2の実施記録(2026-10-31)

- **手順2完了**: `tests/math/eigen_order_probe.cpp` と 3.2 の実測結果・再実行手順を手順2のコミットに含めた。B のビルドとゴールデン出力は変えていない。
- 記録先: `/tmp/pointlio-rust-step12-20261031/eigen-order.log` と同ディレクトリの実測バイナリ。検証時のソースの SHA256、コンパイラ・Eigen の版、コンパイルコマンド、全件数・不一致数をログに記録した。
- 別セッションの差分レビューは未実施。次は手順3(入出力と Red)。

## 手順3の実施記録(2026-10-31)

- **手順3完了**: `golden.rs`、`src/bin/math_golden.rs`、`rust/run_math_golden.sh` を追加した。PLGOLD01 の仕様は `golden/math/README.md` に従い、13部品の寸法・版・マジック・記録数・ファイル長を検査する。リトルエンディアンで入出力し、符号付き零を含む入力のビット列を保持する。転送形式の読み書きは NaN/Inf の出力も保存でき、数値の合否は既存の Python 照合器に委ねる。
- 数値計算は全13部品で未実装。生成器は基準の出力値を候補の計算に使わず、入力を読み、新しい出力を作る。既定は NaN の仮出力。`POINTLIO_MATH_STUB=zero` を指定すると零を返す。これは手順5で本実装に置き換えるための Red 用の仮実装であり、合格する代替計算ではない。
- 読み書きの仕様テストを先に6件追加した。未実装で4件失敗・2件通過(終了コード101)、実装後は6件すべて通過。独立に組み立てたバイト列での読解・書き出し、符号付き零、不正なヘッダ・空・切断・余分なバイト、寸法不一致、NaN のペイロードと Inf の保持、13部品の形を検査した。数学の L1 テストは手順4で追加する。
- `rust/run_math_golden.sh` は SHA256 を照合してから、リポジトリ読み取り専用・`--network none`・`OMP_NUM_THREADS=1`・ホストと同じ UID/GID で生成する。出力先は新規ディレクトリを要求し、`golden/` と既存出力先を拒否する。生成器も既存の `.bin` を上書きしない。
- Red: 最終版で `rust/run_math_golden.sh /tmp/pointlio-rust-step3-20261031/nan-final` は照合器の終了コード2(NaN/Inf)、`POINTLIO_MATH_STUB=zero rust/run_math_golden.sh /tmp/pointlio-rust-step3-20261031/zero-final` は終了コード1。零出力では13件の FAIL 行の部品名の集合が13部品すべてと一致した。NaN では照合器は最初の部品で停止するため、全13部品が FAIL 行になるとは扱わない。
- 両出力を別途バイト単位で検査し、全13ファイル・1205記録のヘッダと入力が B と一致し、全出力が指定した仮値(NaN または正の零)であることを確認した。
- `rust/run.sh test --release --locked --offline` (入出力6件)、`rust/run.sh clippy --release --locked --offline --all-targets -- -D warnings`、`rust/run.sh fmt --all -- --check`、`bash -n rust/run_math_golden.sh`、既存照合器の契約テスト8件が通過。`golden/` 内と既存の出力先への実行が終了コード2で拒否されることも確認した。
- 各出力ディレクトリに Python の版、Rust の版、イメージ ID、stub の種類、ソースのコミット・差分・Rust 関連ファイルの SHA256、生成・ビルド・照合ログ、照合の終了コードを保存する。今回の親コミットは `24ea238`、イメージは手順1と同じ `sha256:1aec8ce20e45bf85982dc8fd4cbd90e45ee8c58f65d61b6a07f5b6813d79ddb5`。入出力テストの Red/Green と静的検査のログは `/tmp/pointlio-rust-step3-20261031/` に保存した。`/tmp` は永続保管先ではない。
- AGENTS.md (`CLAUDE.md` のリンク先)の未作成の注記を、入出力のみ検証済み・数値は Red の状態に更新した。`Cargo.lock`、B のソース、ゴールデン出力、照合器、登録済みの許容誤差は変更していない。
- `unsafe` は引き続き0件。数学の L1・L2 の通過、debug/release の一致、libm のシンボル・呼び出し箇所の検査は未実施。この時点のスクリプトは release のみを実行し、手順6の検査はまだ含めていない。手順3の入出力と Red の成果は、手順1・2とは別のコミットに含めた。別セッションの差分レビューは未実施。次は手順4(L1 のテストを先に書く)。

### 手順3のホスト依存の修正(2026-10-31)

- 再実行したユーザーのホストでは `rg` はシェル関数のみで、スクリプトから見える本体がなく、ソース一覧の取得で終了コード127になった。これは Red の確認ではなく、生成前の実行環境の不具合。上の最初の実行記録は `rg` 本体が PATH にある環境の結果であり、そのままではユーザーの端末で動作しなかった。
- ソース一覧を `git ls-files -co --exclude-standard -z -- rust` に変更した。シェルの `case` で `.rs`・`.toml`・`.lock`・`.sh` に絞り、NUL 区切りのまま重複を除いて SHA256 を計算する。追跡済み・未追跡の両方を含み、ripgrep とシェル関数には依存しない。
- `env -i PATH=/usr/bin:/bin /bin/bash --noprofile --norc` で `rg` が見つからないことを確認し、代替コマンドなしで再実行した。NaN は終了コード2、零は終了コード1・13部品すべて FAIL。照合ログと `comparison-exit.txt` を確認し、単なる非零終了を Red と扱っていない。
- 両出力の13ファイル・1205記録をバイト単位で再確認した。入力とヘッダは B と一致し、出力は全て指定どおりの NaN または正の零だった。ソースの SHA256 一覧は対象10ファイルを重複なく含み、未追跡の新規 Rust ファイルも列挙された。
- 再実行の保存先: `/tmp/pointlio-rust-step3-no-rg-nan`、`/tmp/pointlio-rust-step3-no-rg-zero`。今回の修正はホスト側スクリプトと記録のみで、Rust のコード・テスト・照合器・許容誤差・ゴールデン出力は変更していない。`bash -n` と差分の空白検査も通過した。

## 手順4の実施記録(2026-10-31)

- **手順4完了**: 公開 API を事前提示して承認を得た後、`linalg` の4型、`so3`・`state`・`process`・`esekf` の型・シグネチャと `todo!` の未実装スタブを追加した。数学の本実装・libm FFI はまだ書いていない。
- `tests/math_l1.rs` に C++ の範囲内14件を移した。対応は下表のとおり。全ケースで固定シードを使い、完了件数も検査する。数値微分の列数も検査する。許容誤差は C++ と同じ。

| C++ のテスト | Rust (`tests/math_l1.rs`) | 実装後に完了を要求する件数 |
| --- | --- | --- |
| `So3Exp.MatchesAxisAngle` | `so3_exp_matches_axis_angle` | 120 |
| `So3Exp.BelowThresholdReturnsIdentityWithinAngle` | `so3_exp_below_threshold_returns_identity_within_angle` | 20 |
| `So3Exp.IsRotation` | `so3_exp_is_rotation` | 200 |
| `So3Log.InvertsExp` | `so3_log_inverts_exp` | 200 |
| `So3Log.SmallAngleBranchErrorBound` | `so3_log_small_angle_branch_error_bound` | 100 |
| `So3Log.FiniteNearPi` | `so3_log_finite_near_pi` | 80 |
| `So3Log.AccurateAroundNearPiBranch` | `so3_log_accurate_around_near_pi_branch` | 160 |
| `So3Boxplus.IsRightPerturbation` | `so3_boxplus_is_right_perturbation` | 50 |
| `So3Boxminus.InvertsBoxplus` | `so3_boxminus_inverts_boxplus` | 100 |
| `StateBoxminus.InvertsBoxplusForAllComponents` | `state_boxminus_inverts_boxplus_for_all_components` | 50 |
| `AMatrix.IsLeftJacobianOfExp` | `a_matrix_is_left_jacobian_of_exp` | 50 (微分150列) |
| `ProcessModel.JacobianMatchesNumeric` | `process_model_jacobian_matches_numeric` | 20 (微分600列) |
| `Predict.CovarianceMatchesNumericJacobian` | `predict_covariance_matches_numeric_jacobian` | 20 (微分600列) |
| `Predict.CovarianceStaysSymmetricPositive` | `predict_covariance_stays_symmetric_positive` | 20 |

- Red: `rust/run.sh test --release --locked --offline --no-run` で全ターゲットがコンパイルできた。`rust/run.sh test --release --locked --offline --test math_l1 -- --test-threads=1` は終了コード101、0件通過・14件失敗・0件無視。14件すべての名前と `not yet implemented` の14件のパニックをログで確認した。コンパイル失敗や別の参照計算の失敗を Red と取り違えていない。
- 各テストは最初の未実装関数で止まるため、表の全ケースの数値判定・完了件数が実行されたとは扱わない。数値アサーションの検出力、全ケースでの通過、BL-008 の修正を戻した版での失敗は手順5以降に確認する。
- `tests/support/mod.rs` に、別の Rodrigues、クォータニオン経由の Log、配列での行列演算、対称行列の Jacobi 法を独立に書いた。数値微分の摂動・差・状態伝播は、C++ とプランどおり実装側の状態メソッドと `f_output` を使う。
- L1 の乱数は SplitMix64 と Box-Muller を使い、シードは元の各 C++ テストと同じ番号で固定する。C++ の `mt19937_64` と同じ列ではない(承認済みプランの方針)。
- 参照の検証 `tests/reference_math.rs` の3件が通過。既知の軸回転、既知の固有値を持つ零・対角・ブロック・密な30次元行列、NaN/Inf を見逃さない検査を確認した。Jacobi の内部停止基準は機械イプシロンと対角の尺度、上限100 sweep。未収束は失敗させる。これは参照ソルバの内部条件で、L1 の許容誤差を追加・変更したものではない。
- 既存の入出力テスト6件が通過。`rust/run.sh clippy --release --locked --offline --all-targets -- -D warnings` と fmt の検査も通過。C++ の入力角 `3.14` を PI に変えないための `approx_constant`、Jacobi の対称な行・列の更新を添字で記述するための `needless_range_loop` だけを該当関数で許可し、全体の警告検査は維持した。
- 記録先: `/tmp/pointlio-rust-step4-20261031/`。コンパイル、L1 Red と終了コード、入出力・参照、clippy のログ。`/tmp` は永続保管先ではない。イメージは手順1と同じ `point-lio-rust` を使い、ホストでは数値を動かしていない。
- 手順1〜3と前提となる計画・承認記録を含む `5d033fd` までを、`origin/docs/rust-verification-plan` にプッシュした(2026-10-31)。API と手順4の変更は手順4の独立したコミットに含めた(未プッシュ)。別セッションの差分レビューは未実施。
- `unsafe` は引き続き0件。C++、ゴールデン出力、照合器、許容誤差、`Cargo.lock`、手順3の仮出力バイナリは変更していない。次は手順5の実装。L1・L2 の通過、libm の検査、debug/release の出力一致は未確認。

## 手順5の途中経過と停止(2026-10-31)

- 手順4の API・スタブ・L1・参照計算・文書を `f074322` に独立してコミットした(未プッシュ)。
- `linalg` に、Exp と Log が使う縮約・3×3 の積を、手順2の順で実装した。未使用の後続用の演算はまだ入れない。
- `src/libm.rs` だけで unsafe を許可し、`#[link(name = "m")]` 付きの sincos 宣言1件・安全な共通関数内の呼び出しブロック1件を追加した。安全性の前提は直前のコメントに記載した。別担当による FFI のメモリ安全レビューは未実施。
- SO3 Exp を実装し、L1 3件とゴールデン124記録が通過した。
- SO3 Log を実装し、ゴールデン124記録はビット一致で通過。L1 は `FiniteNearPi`・`AccurateAroundNearPiBranch`・`SmallAngleBranchErrorBound` に対応する3件は通過したが、`InvertsExp` が1件失敗した。
- 失敗した200入力を同じビット列で B に与えて切り分けた。B と C の出力は全200件でビット一致し、B も record 70 の1件で誤差 `2.6231821973276624e-12`、上限 `1e-12` を超えた。[BL-011](../../docs/math-log-l1-reproduction.md) に記録した。**テスト・乱数列・許容誤差・B を変えず、ここで手順5を停止する**。
- 生成器を実際の関数に配線し、既定モードを `real` にした。`POINTLIO_MATH_STUB=nan|zero` は Red の再現用に残す。`POINTLIO_MATH_COMPONENT=so3_exp` などで単一部品の部分生成・照合を行える。他の部品は NaN にして評価せず、`component.txt` とログの `PARTIAL` 表示で範囲を明記する。既存照合器の `compare_component` を呼び、判定式は変更しない。全体の照合器・基準も変更しない。
- 実行: `rust/run.sh test --release --locked --offline --test math_l1 so3_exp` は3件通過。`... so3_log` は3件通過・1件失敗で終了101。`POINTLIO_MATH_COMPONENT=so3_exp rust/run_math_golden.sh /tmp/pointlio-rust-step5-20261031/so3_exp` と、同様の `so3_log` は終了0(各部品だけの通過で、13部品全体の通過ではない)。
- 記録は `/tmp/pointlio-rust-step5-20261031`。再現の詳細は BL-011 の文書を参照。停止時点で fmt と clippy (全ターゲット、`-D warnings`) を確認した。
- 後続の boxplus・boxminus・A 行列・exp3・クォータニオン・状態の演算・過程モデル・予測は未実装。手順6以降の検査、BL-008 を戻す試験、debug/release の比較、libm 呼び出し先の逆アセンブル・別担当レビューは未実施。手順5の差分は未コミット・未プッシュ。

## BL-011 の L1 基準の根拠の調査(2026-10-31)

- 人の指示で L1 基準の根拠を先に調べ、[調査記録](../../docs/math-log-l1-basis-investigation.md)を追加した。実装・テスト・乱数列・許容誤差は変更していない。
- 1e-12 の出所は元の `b2ca192` の L1。確認した資料には対象の全角度でこの定数を導く具体的な式がなかった。既存の小角・近 π テストは別の条件依存の上限を使っている。
- BL-011 の R の生成誤差は最大成分約3.56e-16。50桁・100桁の高精度参照で同じ通常の Log の式を評価しても誤差約2.603e-12が残り、π近傍の入力誤差の増幅を確認した。B の内部の丸めだけを直して解決する問題ではない。
- θ=1e-3 の固定軸の入力では、近似分岐による誤差約1.667e-10も確認した。seed 4 の C++ と同じ200件の再現器は上限内だが、seed 1〜100の20000件では117件が上限を超えた。元の gtest スクリプト自体を今回再実行したとは扱わない。
- これらは、新たな許容誤差の採用や、L1 を必ず B に適合させる決定ではない。一律1e-12を精度要求として維持するか、条件依存の数値的上限を根拠から導き直すかの判断が残る。L2 のビット一致と登録済み (a,r) は変更しない。
- 固定 C++ イメージで実行した作業用のソース・出力は `/tmp/pointlio-bl011-analysis-20261031`。手順5の停止は継続し、後続の実装は行っていない。

## BL-011 の通常分岐の B 修正(2026-10-31)

- 人が BL-011 を B で直すと決定し、通常分岐の角度を `atan2(‖K‖/2,c)`、出力を `(θ/‖K‖)K` に変更することを承認した。下端の近似分岐と既存テストは変更せず、既知の制約として注記することも承認した。ゴールデン更新の承認はまだ得ていない。
- B の小角の判定には、従来の trace と acos の計算を残した。通常分岐の出力だけを新しい式に変え、BL-008 の π 近傍の分岐は変更していない。
- 固定ビット列の回帰テストを追加し、A と修正前Bで失敗、新Bで通過を確認した。修正後の C++ L1 は全17件が通過。元の Rust L1 の200入力も新Bで全件が1e-12以内になった。ただし下端 θ=1e-3 の約1.667e-10の誤差は残っている。
- [検証記録](../../docs/bl011-b-fix-validation.md)に結果とゴールデン候補の差分を記載した。入力・ヘッダは全件同じで、出力が変わるのは `so3_log`・`so3_boxminus`・`state_boxminus` の3ファイル。ほかの10ファイルはバイト一致した。リポジトリのゴールデンは未更新。
- **新Bへの移植で更新する箇所**: 3.4 の通常 Log の出力は旧式 `((0.5·θ)/sinθ)K` から、上記 atan2 と K ノルムの式に変わる。小角判定用の旧 theta は残す。3.3 の新Bの Log の呼び出しは acos・atan2 であり、通常の返値に sin は使わない。手順6の Log の逆アセンブルの期待も、この新Bに合わせる。承認済みの L2 のビット一致と (a,r) は変えない。
- ゴールデン更新の承認を得て専用コミットで更新し、生成器の照合・注入と開発用2本の比較を行ってから Rust の手順5を再開する。今回は Rust の続きは変更していない。


## ゴールデン更新と手順5の再開・実装結果 (2026-10-31)

- 人がゴールデン更新を承認した。B の専用コミット `4558dea` のソースから固定イメージ・`OMP_NUM_THREADS=1` で再生成し、3ファイル・SHA256SUMS・README を専用コミット `88b1c83` に保存した。1,205記録のヘッダ・入力は不変、他の10ファイルはバイト一致。生成器の自己照合、SHA256、共通照合器の契約テスト8件、未改変 B と6種類の注入の再確認も通過した。
- `88b1c83` のクリーンなチェックアウトから開発用 Avia・Ouster を実行し、修正前 B (`fff663e`) の odom.bin・cloud.bin・cloud_frames.txt・traj.tum・params.txt が、2本ともバイト一致した。487・1985姿勢で、入力 SHA256 と実行条件も確認した。封印済みの評価入力は使用していない。詳細は [B修正の検証](../../docs/bl011-b-fix-validation.md)。
- 以上の後、Rust の手順5を再開した。B の `InvertsRoundedRotationBL011` の固定ビット列と上限1e-12をそのまま隣に追加し、旧 Rust の通常式でこの1件が数値アサーションにより失敗することを確認した。新Bの通常式へ合わせた後、回帰テストと元の `InvertsExp` を含む Log の5件が通過した。既存の14件の入力・乱数列・許容誤差は変更していない。
- 承認済み API と3.2の演算順に従って、残りの SO3、状態、過程モデル、予測を順に実装した。部品ごとに共通照合器で部分照合し、ビット一致が必要な4部品を含めて全件通過。説明できない不一致による再停止はなかった。予測の2フラグの実装は C++ と同じく、両方 true なら状態更新後に f と df/dx を再計算、両方 false なら何もしない。L1・L2 の数値検証はプランどおり片方ずつのフラグを対象とし、全組合せの動的確認とは扱わない。
- `rust/run.sh test --release --locked --offline` は L1 全15件、入出力6件、参照計算3件が通過した。`rust/run_math_golden.sh /tmp/pointlio-rust-step5-bl011/full` は全13部品で終了0。別途、生バイトも確認し、この1,205記録では predict_cov を含む全出力がビット一致した。30×30の実装は依然として k 昇順の積であり、Eigen の GEMM との一般的な差異や登録済み r=1e-10 を取り消すものではない。
- fmt と clippy (`--release --locked --offline --all-targets -- -D warnings`) が通過。C++ の `(-1 * f) * dt` の式を保つため、その1行だけ `clippy::neg_multiply` を許可した。数値比較は固定 `point-lio-rust` の中だけで実行した。
- unsafe は src/libm.rs の extern 宣言1件 (3行) と呼び出しブロック1件 (3行)、計2箇所・6行。安全なラッパーを共有し、他のモジュールでは deny を維持した。FFI のメモリ安全レビューは未実施。
- 記録: `/tmp/pointlio-rust-step5-bl011` の各部品・全体の出力と比較ログ、`/tmp/pointlio-rust-step5-bl011-tests.log`、`/tmp/pointlio-rust-step5-bl011-clippy-final.log`、`/tmp/pointlio-bl011-rust-regression-red.log`。いずれも `/tmp` は永続保管先ではない。
- 手順5の実装と進捗文書は未コミット・未プッシュ。手順6 (debug/release のバイト一致、nm・関数ごとの逆アセンブル、sincos を外す比較、BL-008 を戻す検査)、手順7の確定、別セッションの差分・FFI レビューは未実施。下端 θ=1e-3 の制約は未修正。これらが残るため、この範囲を完了済みの C とは扱わない。

## 手順6の実施記録 (2026-11-01)

- **手順6完了**。対象は手順5を保存した `30e5d51`。[検証の詳細](../../docs/math-step6-validation.md)にビルド条件、関数ごとの呼び出し位置、比較版の式、失敗した数値アサーションを記録した。
- debug/release はともに共通照合器で全13部品・1,205記録が通過し、13ファイル全体がバイト一致した。この入力集合では B ともバイト一致した。許容誤差は変更していない。
- `nm -D`・動的シンボル・GOT・関数ごとの逆アセンブルと実行時 binding で、Exp・A行列・Exp3 は共通ラッパーまたはその inline 展開を経て glibc の `sincos`、Log は `acos`・`atan2` だけを呼ぶことを確認した。release の boxplus の inline 展開も行情報で対応づけ、未対応の呼び出しは残らなかった。
- 通常 release の行情報はないため、診断用の行情報付き release を別に作った。リンク配置の違いで `.text` はバイト一致しないが、SO3 全8関数でアドレスを除く命令・シンボルによる呼び出し先の列が同じことを確認して対応づけた。数値判定は通常の release を使った。古いコンテナ binutils が復元できない Rust v0 の関数名は、ホスト binutils 2.42 で保存した ELF を読んだ。ホストで数値計算はしていない。
- 新Bの `math_golden`・`pointlio_offline` も再ビルドし、対象の3関数だけが `sincos` を呼ぶこと、新Bの Log は `acos`・`atan2` であることを確認した。B のゴールデン再照合も全13部品通過。全ターゲットのビルドは Livox SDK のヘッダ不足で失敗し、既存文書の対象限定ビルドで成功した。B の L1 や実データでの offline 実行は今回行っていない。
- `let sin = x.sin(); let cos = black_box(x).cos();` のコピーを作り、別々の glibc の `sin`・`cos` 呼び出しであることを確認後、共通照合器で照合した。不一致は全部品0件 (0/1,205)。生の出力バイトの差も0件。この比較は記録用であり、常に同じ関数値になることや FFI が不要であることの証明にはしない。
- BL-008 の領域だけを A の `(0.5*theta/sin(theta))*K` に戻したコピーでは、コンパイル成功後、L1 は13件通過・2件失敗・無視0件 (終了101)。`FiniteNearPi` 対応は π−1e-7、`AccurateAroundNearPiBranch` 対応は π−0.00031 の数値アサーションで失敗した。BL-011 の通常式と回帰は維持した。L2 は `so3_log` の record 108 の NaN/Inf を検出して終了2となり、数値の誤りを検出できた。
- 既定版の release L1 全15件、clippy (全ターゲット、`-D warnings`)、fmt が通過。検査スクリプト `rust/check_math_step6.sh` を追加し、最終版の実行と `bash -n` を確認した。スクリプトの終了0だけでは逆アセンブルの手動レビューを通過扱いにしない。
- unsafe は `libm.rs` の extern 宣言1件・呼び出しブロック1件、各3行、**2箇所・6行**。他の `.rs` に unsafe の実コードはない。FFI のメモリ安全レビューは未実施。
- 記録先は `~/data/slamrun-runs/c-step6-30e5d51/rust`、B は同じディレクトリの `b-targets`・`b-full-build` (実行時の `/tmp` からコピーし、一致を確認済み)。手順1〜5と BL-011 の検証で記した `/tmp` の記録は、2026-11-01 の時点で残っていない。判定と根拠は各文書の記述だけが残る。比較版はこの記録先に隔離し、本体には適用していない。ゴールデン・照合器・許容誤差・数学の実装・既存テストは変更していない。
- 手順7の確定と別セッションの差分・FFI レビューは未完了。θ=1e-3 の既知の制約も維持しており、モジュール全体を完了した C とはまだ扱わない。

## 手順7の実施記録 (2026-11-01)

- **手順7完了**。人が選んだ (a) に従い、AGENTS.md の前提条件欄には手順8の別セッションの差分・FFI レビュー未完了の注記を1行だけ残した。手順6のコマンドは実装済みの `check_math_step6.sh` に合わせ、`run_math_golden.sh` へ検査を組み込む変更はしていない。
- 変更したのは AGENTS.md の概要表の `rust/` 行と主要コマンド表の「Rust ビルド・テスト」行、本プラン7節の検証表とこの実施記録だけ。`CLAUDE.md` は AGENTS.md への既存のシンボリックリンクなので同じ内容を参照する。
- 概要表は現状とプランへのリンクに整理した。コマンド欄に clippy・fmt・手順6の検査・照合器の契約テストを加え、`POINTLIO_MATH_COMPONENT`・`POINTLIO_MATH_STUB` の説明もここへ移した。日付付きの進捗と手順7の未完了注記は外し、前提条件・制約と手順8の未完了注記を残した。「使わないもの」の欄は変更していない。
- 本プラン7節は性質テストと手順6の採取・検査を分け、clippy を `--all-targets` 付きの実行コマンドに揃え、fmt の行を加えた。スクリプトの終了0だけでは関数ごとの逆アセンブルの確認を通過扱いにしないことを、両方の表に記載した。
- 親コミット `01f1069` で編集前に確認し、上の行を書き直した後、新しい出力先で同じコマンドをもう一度、各々順に実行した。2回とも次の結果だった:

| コマンド | 結果 (編集前・編集後とも同じ) |
| --- | --- |
| `docker/build_rust_image.sh` の土台IDの照合部分のみ | 終了0。土台 `sha256:4ffccd043d071a088d7b09e80eb78cbef3b61d777e2b4f146768bac3def2dead` と一致 |
| `rust/run.sh build --release --locked --offline` | 終了0 |
| `rust/run.sh test --release --locked --offline` | 終了0。L1 15件・入出力6件・参照計算3件通過、無視0件 |
| `rust/run_math_golden.sh <新しい出力ディレクトリ>` | 終了0。実計算で全13部品・1,205記録通過 |
| `rust/run.sh clippy --release --locked --offline --all-targets -- -D warnings` | 終了0。`run.sh` 経由で実行可能 |
| `rust/run.sh fmt --all -- --check` | 終了0。`run.sh` 経由で実行可能 |
| `bash rust/check_math_step6.sh <リポジトリ外の新しい出力先>` | 終了0。13ファイルの debug/release バイト一致と両方の照合を確認。BL-008 比較版は L1 の対応する2件が失敗 (終了101)、照合は NaN/Inf を検出 (終了2) |
| `python3 -B tests/math/test_compare_math_golden.py` | 終了0。契約テスト8件通過 |

- イメージ作成は人の指示どおり土台IDの確認までに限定した。元スクリプトの先頭から `check_packages.sh` 呼び出し直前までを抽出し、元のパスを `$0` に渡して実行した。パッケージ照合・イメージの再作成は今回実行していない。前後の `docker image inspect` で Rust イメージの ID は既存の `sha256:1aec8ce20e45bf85982dc8fd4cbd90e45ee8c58f65d61b6a07f5b6813d79ddb5` のままだった。
- 各コマンドの引数・ログ・終了コードと出力は `/tmp/pointlio-step7-20261101/before`・`after` に記録した。具体的な照合出力先はそれぞれの `golden-output`、手順6は `step6-output`。`commands.json` に実際に渡した引数 (土台ID確認の抽出コマンドも含む) がある。`/tmp` は永続保管先ではない。
- 今回はコマンドの再実行と記録の採取を確認した。手順6の全逆アセンブルの手動レビューを改めて実施したとは扱わず、手順6の判定根拠は既存の[検証記録](../../docs/math-step6-validation.md)を参照する。イメージの新規作成、実データでの C の評価、手順8の差分・FFI レビューは今回未実施。
- `git diff --check` が通過。実装・スクリプト・テスト・ゴールデン・許容誤差は変更していない。手順8が残るため、モジュール全体を完了した C とは扱わない。手順7の文書だけを独立したコミットにし、プッシュはしない。
