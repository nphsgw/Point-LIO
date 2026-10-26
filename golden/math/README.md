# 数学と状態の表現のゴールデン出力(L2)

最初に移す範囲「数学と状態の表現」の部品を、固定の入力で動かした C++ の出力。Rust(C)は同じ入力を読み、出力を比べる(事前登録の J4。(a, r) は範囲ごとに付録へ登録してから使う)。定義は [state-definition.md](../../docs/state-definition.md)。

## 出所と更新の規則

- 出所: `A-v1`(`3dc49bc`)と同じ C++ のソース。この範囲に関わる B の修正は台帳にないので、この範囲では B と同じ。B の数学の部分を変える修正が入ったら作り直す。
- 生成: `tests/math/math_golden.cpp` を、固定したイメージ(`point-lio-noetic:pinned`、全パッケージが `docker/packages.lock` と一致)の既定のビルド(`-O3 -g`、事前登録の V0)で実行した。生成は 2026-10-26(人の承認による。作成と同時に照合し、通過した)。
- 更新: 人の承認を得てから、`tests/math/run_math_golden.sh write` で書き、理由と実行条件を添えて専用のコミットにする(agents/rules/testing.md)。
- 照合: `tests/math/run_math_golden.sh check`。`SHA256SUMS` を確かめた後、同じ入力で計算し直し、ファイルとバイト単位で比べる。
- 含めないもの: 回転角が π − 1e-3 より大きい入力(SO3 の Log が誤った値や NaN を返す。[台帳](../../docs/bug-ledger.md) BL-008)。決定の後に足す。

## ファイル形式

各 `<名前>.bin` は1つの部品の全記録。リトルエンディアン。

| 位置 | 型 | 内容 |
| --- | --- | --- |
| 0 | `u8[8]` | マジック `PLGOLD01` |
| 8 | `u32` | 版。1 |
| 12 | `u32` | 記録の数 N |
| 16 | `u32` | 入力の長さ I(f64 の個数) |
| 20 | `u32` | 出力の長さ O(f64 の個数) |
| 24 | 記録の列 | 各記録は f64 × I(入力)の後に f64 × O(出力) |

行列はすべて行優先。状態は 42 個の f64: `pos` 3、`rot` 9、`offset_R_L_I` 9、`offset_T_L_I` 3、`vel` 3、`omg` 3、`acc` 3、`gravity` 3、`bg` 3、`ba` 3。

## 部品

| 名前 | 入力 | 出力 | 対象(C++) |
| --- | --- | --- | --- |
| `so3_exp` | v(3) | R(9) | `MTK::SO3<double>::exp(v)` |
| `so3_log` | R(9) | w(3) | `MTK::SO3<double>::log(R)` |
| `so3_boxplus` | R(9)、δ(3) | R ⊞ δ(9) | `SO3::boxplus` |
| `so3_boxminus` | R₁(9)、R₂(9) | R₁ ⊟ R₂(3) | `SO3::boxminus` |
| `a_matrix` | v(3) | A(v)(9) | `MTK::A_matrix` |
| `so3math_exp3` | v(3) | R(9) | so3_math.h の `Exp(v1, v2, v3)`(閾値 1e-5) |
| `quat_from_matrix` | R(9) | q(4、x, y, z, w) | `Eigen::Quaterniond(R).coeffs()`(出力の姿勢) |
| `state_boxplus` | x(42)、δ(30) | x ⊞ δ(42) | `state_output::boxplus` |
| `state_boxminus` | x₁(42)、x₂(42) | x₁ ⊟ x₂(30) | `state_output::boxminus` |
| `f_output` | x(42) | f(30) | `get_f_output` |
| `df_dx_output` | x(42) | ∂f/∂x(900) | `df_dx_output` |
| `predict_state` | x(42)、dt(1) | x(42) | `esekf::predict(dt, 0, …, true, false)` |
| `predict_cov` | x(42)、P(900)、Q(900)、dt(1) | P(900) | `esekf::predict(dt, Q, …, false, true)` |

入力の回転角には、閾値の境界(Exp 1e-7、Log 0.001、A 行列 1e-11、`Exp(v1, v2, v3)` 1e-5)の前後を含める。
