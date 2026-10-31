# 数学と状態の表現: 条件5の注入による確認

対象は `golden/math` の13部品。比較基準は [事前登録の付録](metrics-preregistration.md#付録-範囲ごとの基準)に従い、許容誤差とゴールデン出力は変更しない。

2026-10-31 に、人の承認で「f32 での計算」を「各部品の f64 出力を f32 に丸めて f64 に戻す」で代替すると決めた。これは出力精度の低下の検出を確認するもので、中間演算の f32 化による累積誤差・分岐の変化を確認したとは扱わない。`efc461e` はプッシュせず保持すると決めた。

同日、人の判断で次の2点を決めた。実行時の道具はコミット前だったため、再実行はせず、道具の SHA256 を記録に足す(下の「道具の版」)。比較される側の NaN/Inf は、今のまま不正なデータ(終了コード2)として全体の比較を止める。J4 の「NaN/Inf は不通過」は、終了コードが0でないことで満たす。

## 道具と実行方法

```bash
python3 -B tests/math/test_compare_math_golden.py
bash tests/math/run_math_injections.sh /tmp/pointlio-math-injections-20261031
# 後で C の出力にも、同じ照合器を使う
python3 tests/math/compare_math_golden.py golden/math /absolute/path/to/c-output
```

出力先には新しい作業用ディレクトリを指定する。実行スクリプトは `golden/` への出力を拒否する。固定イメージ `point-lio-noetic:pinned` の全パッケージ・livox の版と、基準ファイルの SHA256 を照合してからビルドする。

- `compare_math_golden.py`: 基準側の SHA256、13ファイルの集合、ヘッダ・長さ・件数・有限値を検査する。入力はビット一致を要求する。出力は付録どおり、ビット一致か、各成分の `|c-b| ≤ r·max_block|b| + r·|b|` で比べる。状態は42値の10区画、`f_output` は30値の10区画、Jacobian と共分散は行列全体。クォータニオンは内積が負なら候補の符号を反転する。終了コードは通過0、数値不一致1、不正なデータ2。
- `prepare_math_injections.py`: B の生成器を作業先にコピーし、計算を呼ぶ箇所だけに注入用ラッパーを加える。リポジトリの C++ ファイルは変更しない。入力のシリアライズは変えない。
- `check_math_injections.py`: 未改変 B の全ファイルが既存ゴールデンとバイト一致すること、各注入が対象部品をすべて落とすこと、対象外の部品は通ることを検査する。照合の計算は C 用と共通。

| 注入 | 変更 | 落ちるべき部品 |
| --- | --- | --- |
| 固定摂動 | 記録ごとの最大絶対値成分に `abs(value)·1e-9` を加える。同率なら先頭 | 全13部品 |
| f32 出力丸め | 全出力を `double(float(value))` にする | 全13部品 |
| boxplus の左右 | 右積を左積にする。状態では2つの回転区画 | `so3_boxplus`, `state_boxplus` |
| クォータニオン成分順 | xyzw → wxyz | `quat_from_matrix` |
| Jacobian の1項欠落 | `df[0]/dx[12] = 1` を0にする | `df_dx_output` |
| dt の1つずれ | 同じ部品の次の記録の dt で計算。最後は先頭に循環 | `predict_state`, `predict_cov` |

注入の単位は部品。零出力の記録や、微小角の Exp が単位行列になる記録は、相対摂動・f32 丸め・左右の取り違えでも値が変わらない場合がある。すべての記録が落ちるとは要求せず、各対象部品で不一致が検出されることを要求する。ログには部品ごとの不一致記録数と最初の不一致を残す。

## 検証結果

2026-10-31、B の `efc461e` と今回追加した道具で実施し、通過した。Rust(C)は未着手。

- 記録先: `/tmp/pointlio-math-injections-20261031`。生成器の作業用コピー、7組の出力、`build.log`、`comparison.log`、ソースのコミット・差分、イメージ ID を保存した。`/tmp` のため永続保管先ではない。
- イメージ ID: `sha256:4ffccd043d071a088d7b09e80eb78cbef3b61d777e2b4f146768bac3def2dead`。全パッケージと livox の版は lock と一致。`OMP_NUM_THREADS=1`。コンパイル行で `-O3 -g` を確認し、`-march`・`-ffast-math` の指定はなかった。
- 未改変 B: 13部品すべて通過し、既存ゴールデンとバイト一致。
- 固定摂動・f32 出力丸め: それぞれ全13部品が不通過。
- 左右の取り違え: `so3_boxplus` は107/124件、`state_boxplus` は60/60件が不一致。
- 成分順: `quat_from_matrix` は124/124件が不一致。
- Jacobian 欠落: `df_dx_output` は60/60件が不一致。
- dt のずれ: `predict_state` は60/60件、`predict_cov` は30/30件が不一致。
- 各注入の対象外の部品はすべて通過。
- 照合器の契約テスト8件が通過。許容誤差の境界、クォータニオンの符号、符号付き零のビット比較、区画の独立性、零区画の許容誤差、NaN/Inf、入力のビット不一致、空・切断ファイルを検査した。

### 道具の版

記録先の `source-commit.txt`(`efc461e`)には道具が含まれず、道具は未追跡だったため `source-diff.patch` も空。代わりに、2026-10-31 の作業ツリーの SHA256 を記す。

| ファイル | SHA256 |
| --- | --- |
| `tests/math/compare_math_golden.py` | `7d3a818a4142281cfb02b204112b22947b650ab354ef8928eb21765d74d217d4` |
| `tests/math/check_math_injections.py` | `23d91a842aa200169c7667238f0a2b032dca6c804fda1fa2c5d194c4f763e2d6` |
| `tests/math/prepare_math_injections.py` | `035f7b82181057c8234d94f9ffd72640e9998ec7e9ba98fbc4f99c1a21210b5f` |
| `tests/math/run_math_injections.sh` | `86050602844d7a9009be22007c6675a071e812e75f28b6769a407e4074e81b92` |
| `tests/math/test_compare_math_golden.py` | `00b971a918feb86bbd4161e0dfc6966ebac58b2e9c31fd66608378d3d9a6f48f` |

これらが実行時と同じであることは、次の範囲で確かめた。

- `prepare_math_injections.py`: `tests/math/math_golden.cpp`(`efc461e` から変更なし)から作り直した生成器が、記録先の `math_golden.cpp` とバイト一致した。
- `compare_math_golden.py`・`check_math_injections.py`: 記録先の7組の出力に対して再実行し、終了コード0で、出力が `comparison.log` とバイト一致した。
- `run_math_injections.sh`: 確かめられない。更新時刻(12:30:42)が `comparison.log`(12:30:24)より後で、実行後に変更された可能性がある。

ゴールデン出力・評価データ・登録済みの許容誤差は変更していない。今回の検証範囲は数学と状態の表現の L2 判定だけであり、実データでの L3、Rust との比較、f32 中間演算の累積・分岐は未検証。
