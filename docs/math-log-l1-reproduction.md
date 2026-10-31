# BL-011: Log の L1 が B にも適合しない入力の再現

> 以下は `f074322` の BL-011 修正前 B についての記録。後に通常分岐の修正が承認され、[修正後の検証](bl011-b-fix-validation.md)を行った。下端の制約は未修正。
2026-10-31、手順5の `so3::log` 実装時に確認した。分類・対応は未決定。テスト・乱数列・許容誤差・B の式は変更していない。

## 観測

- 手順4の `f074322` にある `so3_log_inverts_exp` は、シード4の SplitMix64・Box-Muller で、回転角 1e-3〜π−1e-3 の200件を作る。C++ の元テストとは乱数列が異なる(承認済みプランの方針)。
- `rust/run.sh test --release --locked --offline --test math_l1 so3_log` は4件のうち3件が通過、`so3_log_inverts_exp` だけが失敗した。最初の不一致は0始まりの record 70。
- 入力の回転ベクトルのノルムは `3.1240330382446562` rad。`‖Log(R)−v‖ = 2.6231821973276624e-12` で、元の L1 の上限 `1e-12` を超える。
- 200件すべての v・R・Rust の Log 出力をバイナリで保存し、同じ R のビット列を、変更していない B の `MTK::SO3<double>::log` に与えた。**B と Rust は200件すべてで出力がビット一致し、B も record 70 の1件で同じ L1 上限を超えた**。
- `c = 0.5·(tr(R)−1) = -0.9998458339158249`。BL-008 の `c < -1 + 5e-8` の分岐の外であり、A と同じ acos・sin の式を使う。元の C++ テストの既定の乱数列も落ちる、と主張しているわけではない。
- `golden/math` の `so3_log` 124件はビット一致で通過。SO3 Exp の L1 3件とゴールデン124件も通過。数学の範囲全体を通過したとは扱わない。

## 固定した入力のビット列

IEEE binary64 のビットパターン。R は行優先。R はテスト側の独立した Rodrigues の参照計算で v から作ったもの。

```text
v:
c0052889165ec0c2 3fe97bf35e541585 bff75a8559c5151b
R:
3fdbbe5539650384 bfdb18dce9eb1e75 3fe974c0a7d7e313
bfdc25aeb5f0ee9f bfebd61caea14362 bfcc95d7b3ad77e5
3fe92b6a9e24735f bfd0320598cac0f6 bfe206874f7eb430
B と Rust の出力:
c0052889165ead3a 3fe97bf35e53fe1e bff75a8559c4ff80
```

## 実行条件と記録

- B のソースは `f074322` の C++ (手順5で変更なし)。対象は `include/IKFoM/IKFoM_toolkit/mtk/types/SOn.hpp` の `log`。
- B の固定イメージ ID は `sha256:4ffccd043d071a088d7b09e80eb78cbef3b61d777e2b4f146768bac3def2dead`。C の派生イメージ ID は `sha256:1aec8ce20e45bf85982dc8fd4cbd90e45ee8c58f65d61b6a07f5b6813d79ddb5`。
- B の再現器は `g++ -std=c++14 -O3 -g -pthread -fexceptions -fopenmp -I/usr/include/eigen3 -I/repo` でコンパイル。C は rustc 1.99.0、release。両方ともコンテナ内で実行した。
- 記録先は `/tmp/pointlio-rust-step5-20261031`。`l1-log.log`、`log-l1-rust.log`、`log-l1-b.log`、200件の `log-l1-inputs.bin` (v 3値、R 9値、C の出力3値の順の f64 little-endian)、`log-l1-b-output.bin`、再現器 `diagnose.rs`・`diagnose.cpp` を保存した。`/tmp` は永続保管先ではない。

## 判断が必要な点

これは B→C のビット不一致ではなく、L1 の入力範囲・一律の上限と B の数値的な性質が合わないケース。B の精度改善を検討するか、L1 の上限の根拠を再検討するかは、人の判断を待つ。テストを通すためのシード変更・入力の除外・許容誤差の拡大は行わない。B の式を変える場合も、台帳の承認とゴールデン出力の更新の承認が必要。

この判断まで、手順5は Exp と Log の時点で停止する。後続の boxplus・boxminus・A 行列・exp3・クォータニオン・状態・過程モデル・予測は未実装。

## L1 基準の根拠の調査

2026-10-31、人の指示で [L1 基準の根拠を調査した](math-log-l1-basis-investigation.md)。入力 R の丸めの増幅と小角近似の切り捨てにより、一律 `1e-12` を現 B の全域の誤差上限とする根拠は不足していると確認した。新しい基準の採用・B の修正は未承認で、停止は継続する。
