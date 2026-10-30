# 状態・誤差状態と数学の定義

計画書 §6 の条件1(モード、座標系、単位、状態・誤差状態の定義)の文書。最初に移す範囲「数学と状態の表現」の基準になる。対象は C++ の A(`A-v1`、上流 `4b86a46` と同じ推定器)。

2026-10-26 にコードを読んで書き、同日の別セッションのレビューの指摘を反映した。「確認済み」はコードを読んだ範囲で、実行で確かめたものは個別に書く。数学の演算(5 節)は L1 テスト(`tests/math/math_tests.cpp`)で確かめた。行番号は `A-v1` のもの。

## 1. モード

| 設定 | 値(両正準設定) | この文書での扱い |
| --- | --- | --- |
| `use_imu_as_input` | `false` | `kf_output`(`esekf<state_output, 30, input_ikfom>`)だけを対象にする。`kf_input`(24次元)は範囲外 |
| `prop_at_freq_of_imu` | `true` | 範囲内 |
| `check_satu` | `true` | 範囲内 |
| `extrinsic_est_en` | `false` | LiDAR→IMU の外部パラメータは設定値(`Lidar_R_wrt_IMU`・`Lidar_T_wrt_IMU`)を使う。状態の `offset_R_L_I`・`offset_T_L_I` は観測に使われない(`src/Estimator.cpp` 375〜395 行) |
| `imu_en` | `true` | 範囲内 |

## 2. 座標系

| 記号 | 座標系 | 定義 |
| --- | --- | --- |
| W | 世界 | 初期化で決まる。呼び出し側(`src/laserMapping.cpp` 471 行)が IMU の加速度の平均から重力の向き `tmp_gravity = −mean_acc/‖mean_acc‖·G_m_s2` を求め、`ImuProcess::Set_init`(`src/IMU_Processing.cpp` 40〜67 行)が、それを設定の重力 `gravity_` に合わせる回転を初期姿勢 `rot` にする。`gravity_` は設定の `gravity`(`src/parameters.cpp` 146 行)。回転は so3_math.h の `Exp(v1, v2, v3)`(閾値 1e-5、5.4 節) |
| I | IMU(機体) | 状態の `pos`・`rot` は W における I の姿勢: p_W = rot·p_I + pos |
| L | LiDAR | p_I = R_LI·p_L + t_LI。範囲内では R_LI = `Lidar_R_wrt_IMU`、t_LI = `Lidar_T_wrt_IMU`(設定 `extrinsic_R`・`extrinsic_T`)。点の W への変換は p_W = rot·(R_LI·p_L + t_LI) + pos(`pointBodyToWorld`) |

## 3. 単位

| 量 | 単位 | 根拠 |
| --- | --- | --- |
| 時刻 | 秒(`double`)。ROS 時刻 `header.stamp.toSec()` | — |
| 角速度(入力・状態 `omg`・`bg`) | rad/s | IMU の値をそのまま使う |
| 加速度の入力(生) | 設定 `acc_norm` に依存: Avia は g(`acc_norm: 1.0`)、Ouster は m/s²(`acc_norm: 9.81`) | [evaluation-data.md](evaluation-data.md) |
| 加速度(状態 `acc`・`ba`・`gravity`、IMU 観測の残差) | m/s²。範囲内では、IMU 観測の残差で生の値に `G_m_s2 / acc_norm` を掛ける(`src/Estimator.cpp` 328 行)だけ。予測は入力を使わない(`get_f_output` は `in` を参照しない)。`src/laserMapping.cpp` 802・812・922・945 行の換算は範囲外の `kf_input` の経路 | — |
| `G_m_s2` | m/s²。既定 9.81。最初のフレームで必ず、設定 `gravity` のノルムで上書きされる(`src/laserMapping.cpp` 436 行、`flg_first_scan` の中で `if(imu_en)` の外)。正準設定は `gravity: [0, 0, -9.81]` なので 9.81 のままの見込み(実行では未確認) | — |
| 飽和の閾値 | 生の単位のまま比べる: `fabs(angvel_avr(i)) >= 0.99 * satu_gyro`、`fabs(acc_avr(i)) >= 0.99 * satu_acc`(`src/Estimator.cpp` 330〜364 行)。`satu_acc` は Avia 3.0(g)、Ouster 30.0(m/s²) | — |
| 点の時刻 | 前処理の範囲で定義する(`timestamp_unit`: Avia 1、Ouster 3)。この文書の対象外 | — |
| IMU の時刻の丸め | 受信時に `fromSec(toSec() − timediff_imu_wrt_lidar − time_lag_IMU_wtr_lidar)` で往復する(`src/li_initialization.cpp` 190 行。範囲内では引く値は 0)。1.6e9 秒台の double の刻みは約 2.4e-7 秒なので、ナノ秒の値が変わりうる。C は同じ丸めを再現する(J1 の時刻の完全一致に効く)。実データで値が変わるかは未確認 | — |

## 4. 状態 `state_output`

定義は `include/common_lib.h` 36〜47 行の `MTK_BUILD_MANIFOLD(state_output, ...)`。並びは宣言順で、誤差状態(30次元)の添字も同じ順に割り当てられる。SO3 の DIM と DOF はどちらも 3 で、S2 は状態に含まれないので、各成分の DIM 上の位置と DOF 上の位置は一致する。

| 添字(誤差状態) | 名前 | 型 | 意味 | 単位 |
| --- | --- | --- | --- | --- |
| 0〜2 | `pos` | vect3 | W における I の位置 | m |
| 3〜5 | `rot` | SO3 | W に対する I の姿勢 | — |
| 6〜8 | `offset_R_L_I` | SO3 | LiDAR→IMU の回転(範囲内では観測に使わない) | — |
| 9〜11 | `offset_T_L_I` | vect3 | LiDAR→IMU の並進(同上) | m |
| 12〜14 | `vel` | vect3 | W における速度 | m/s |
| 15〜17 | `omg` | vect3 | I の角速度(バイアス除去後) | rad/s |
| 18〜20 | `acc` | vect3 | I の比力(バイアス除去後) | m/s² |
| 21〜23 | `gravity` | vect3 | W における重力。S2 ではなく3次元のベクトル | m/s² |
| 24〜26 | `bg` | vect3 | ジャイロのバイアス | rad/s |
| 27〜29 | `ba` | vect3 | 加速度のバイアス | m/s² |

共分散 `P_` は 30×30 で、行と列の並びは上の添字と同じ。

### 4.1 過程モデル(`get_f_output`、`df_dx_output`、`src/Estimator.cpp` 68〜110 行)

- f(x) は 30 次元: f[0:3] = vel、f[3:6] = omg、f[12:15] = rot·acc + gravity、ほかは 0。
- ∂f/∂x の非零ブロック: (0, 12) = I、(12, 3) = −rot·[acc]×、(12, 18) = rot、(12, 21) = I、(3, 15) = I。
- 過程雑音 Q(`process_noise_cov_output`、同 41〜51 行)は対角: (12) `vel_cov`、(15) `gyr_cov_output`、(18) `acc_cov_output`、(24) `b_gyr_cov`、(27) `b_acc_cov`。各 3×3。

### 4.2 予測(`esekf::predict`、`include/IKFoM/IKFoM_toolkit/esekfom/esekfom.hpp` 137〜183 行)

- 状態: x ← x ⊕ (f·dt)。vect は加算、SO3 は 5 節の oplus。
- 共分散: F = I + F′·dt。F′ は ∂f/∂x の行を並べ直したもの。ただし SO3 の行ブロック(`rot`、`offset_R_L_I`)は A(−f_SO3·dt)·(∂f/∂x の該当行)に置き換え、F の SO3 の対角ブロックは Exp(−f_SO3·dt) に置き換える。P ← F·P·Fᵀ + Q·dt²。
- `predict_state` と `prop_cov` のフラグで、状態と共分散を別々に進める呼び出しがある(イベントループの範囲で定義する)。

### 4.3 IMU 観測(`h_model_IMU_output`、`src/Estimator.cpp` 324〜366 行)

- 残差 z_IMU(6次元) = [angvel_avr − omg − bg; acc_avr·G_m_s2/acc_norm − acc − ba]。
- 飽和した軸は残差を 0 にし、`satu_check[i]` を立てる。
- 観測雑音は対角: `imu_meas_omg_cov` ×3、`imu_meas_acc_cov` ×3。

## 5. 多様体の演算(`include/IKFoM/IKFoM_toolkit/mtk/`)

### 5.1 SO3(`types/SOn.hpp` 185〜346 行)

- **表現**: `MTK::SO3<double>` は `Eigen::Matrix<double, 3, 3>` を継承した**回転行列**である。ファイル内のコメントは「Quaternion で表す」としているが、実装は行列で、正規化(直交化)はしない。
- **boxplus・oplus**: R ⊞ δ = R·Exp(s·δ)(右から掛ける)。s は引数の `scale`。
- **boxminus**: R₁ ⊟ R₂ = Log(R₂ᵀ·R₁)。
- **Exp**(ロドリゲスの公式): θ = s·δ。‖θ‖ > 1e-7 なら I + sin‖θ‖·K + (1 − cos‖θ‖)·K²(K は単位軸の歪対称行列)。‖θ‖ ≤ 1e-7 なら**ちょうど I**。
- **Log**: tr(R) > 3 − 1e-6 なら θ = 0、そうでなければ θ = acos((tr(R) − 1)/2)。k = (R₂₁ − R₁₂, R₀₂ − R₂₀, R₁₀ − R₀₁)。|θ| < 0.001 なら 0.5·k、そうでなければ 0.5·θ/sin θ·k。
  - tr(R) が丸めで −1 を下回ると acos の引数が −1 未満になり、NaN になりうる(θ = π の近く)。L1 のテストで扱う。
  - θ = π の近くでは k が 0 に近づき、Log の条件が悪い。L1 のテストで扱う。
- **A 行列**(共分散の伝播。`src/mtkmath.hpp` 236〜247 行): ‖v‖ < 1e-11 なら I、そうでなければ I + (1 − cos‖v‖)/‖v‖²·[v]× + (1 − sin‖v‖/‖v‖)/‖v‖²·[v]×²。
- `MTK::tolerance<double>()` は 1e-11。Exp の 1e-7、Log の 1e-6 と 0.001 とは別の閾値である。

### 5.2 vect(`types/vect.hpp`)

- boxplus・oplus: v ← v + s·δ。boxminus: v₁ − v₂。

### 5.3 状態全体の boxplus・oplus・boxminus(`mtk/build_manifold.hpp` 199〜208 行)

- 成分ごとに宣言順で適用する。成分どうしは独立なので、順序は結果に効かない。
- boxplus は DOF の並び、予測の oplus は DIM の並びのベクトルを受ける。この状態ではどちらも30で、同じ並び(4 節の添字)。
- SO3 は `exp(vec·scale)` を右から掛け、vect は `+= scale·vec`。

### 5.4 so3_math.h の関数(`include/so3_math.h`)

MTK とは別に、初期化などで次を使う。閾値が MTK と異なるものがある。

| 関数 | 閾値 | 閾値の内側 | 主な使用箇所 |
| --- | --- | --- | --- |
| `Exp(ang)`(20〜36 行) | ‖ang‖ > 1e-7 | I | — |
| `Exp(ang_vel, dt)`(38〜58 行) | ‖ang_vel‖ > 1e-7 | I | — |
| `Exp(v1, v2, v3)`(60〜78 行) | ‖v‖ > **1e-5** | I | `Set_init`(`src/IMU_Processing.cpp` 66 行) |
| `Log(R)`(83〜89 行) | MTK の Log と同じ式(1e-6、0.001) | — | — |

### 5.5 浮動小数点の演算の形

L2 のゴールデン出力をビット一致で比べるので、式の形も定義に含める。

- Exp: ノルムは Eigen の `.norm()`。結果は `I + sin‖θ‖·K + (1 − cos‖θ‖)·K·K`(K·K は行列積)。
- A 行列: ノルムは `sqrt(v0·v0 + v1·v1 + v2·v2)`(`mtkmath.hpp` 238 行)。
- `Exp(v1, v2, v3)`: ノルムは `sqrt(v1·v1 + v2·v2 + v3·v3)`。
- 予測の共分散: `F_x1 * P_ * F_x1.transpose() + Q * (dt * dt)`。対称化しない。

### 5.6 S2

- `typedef MTK::S2<double, 98090, 10000, 1> S2`(`include/common_lib.h` 21 行)は定義されているが、範囲内の状態には使われない。移植の範囲外とする。

### 5.7 L1 テストで分かった性質

- Log は |θ| < 0.001 で 0.5·k(= sin θ·軸)を返すので、真値との差は θ³/6 まである(最大 約 1.7e-10)。
- Exp は ‖θ‖ ≤ 1e-7 でちょうど I を返すので、真値との差は ‖θ‖ まである。
- Log は π の近く(π − 1e-7 など)で誤った値を、π − 1e-10 で NaN を返すことがある([台帳](bug-ledger.md) BL-008)。範囲内の推定器は SO3 の Log を呼ばない。

## 6. 出力の姿勢

- `set_posestamp`(`src/laserMapping.cpp` 246〜268 行)は `Eigen::Quaterniond q(kf_output.x_.rot)` で回転行列をクォータニオンにし、`q.coeffs()` の順(x, y, z, w)で `orientation` に入れる。TUM 形式の列も `qx qy qz qw`。
- 変換の手順(Eigen 3.3.7 の `quaternionbase_assign_impl<Other,3,3>`、Shoemake の方法)。行列は直交化されないので、結果は単位長とは限らない。正規化もしない。
  - tr(R) > 0: t = sqrt(tr + 1)、w = 0.5·t、t = 0.5/t、x = (R₂₁ − R₁₂)·t、y = (R₀₂ − R₂₀)·t、z = (R₁₀ − R₀₁)·t。
  - それ以外: 対角の最大の添字 i(R₁₁ > R₀₀ なら 1、さらに R₂₂ > R_ii なら 2)、j = (i+1) mod 3、k = (j+1) mod 3。t = sqrt(R_ii − R_jj − R_kk + 1)、q_i = 0.5·t、t = 0.5/t、w = (R_kj − R_jk)·t、q_j = (R_ji + R_ij)·t、q_k = (R_ki + R_ik)·t。
- 符号はこの手順で決まる(w ≥ 0 とは限らない)。C は同じ手順を再現する。判定(事前登録の J4)では、回転は boxminus で比べる。出力(S9)の値の比べ方はトレースの仕様で決める。
- 出力の共分散は常に0([台帳](bug-ledger.md) BL-004)。

## 7. Rust への移植で守ること

- 状態の並び(4 節の添字)と、共分散のブロックの位置を同じにする。
- SO3 は回転行列で持つ。クォータニオンで持つと、Exp・Log・合成の丸めが変わり、J4 の基準の外に出うる。持ち方を変える場合は、避けられない違いの一覧に載せ、L1 と J4 で確かめる。
- 摂動は右から(R·Exp(δ))。
- Exp・Log・A 行列の閾値(1e-7、1e-6、0.001、1e-11)と、閾値の内側で返す値(ちょうど I、0.5·k)を同じにする。
- 正規化をしない(C++ がしないため)。正規化を足すのは D の範囲。

## 8. 未確認の点

- 予測の `predict_state`・`prop_cov` の呼び出しの組み合わせと dt の決め方(イベントループの範囲)。
- 推定器の範囲で追記する定義: LiDAR 観測モデル(`h_model_output`。点・法線・`pd2` を f32 で計算する)、LiDAR 更新の2つの分岐(有効点が30以上か未満か。`esekfom.hpp` 213〜237 行)、`maximum_iter = 1`(反復は1回で、打ち切り条件はない)、IMU 更新の式(飽和した軸の行と列を 0 にする。同 242〜281 行)、6×6 と 30×30 の逆行列、共分散の初期値(`P_init_output`、`src/parameters.cpp` 233〜239 行)と `acc` の初期値(`src/laserMapping.cpp` 483 行)。

## 9. 起票候補(台帳)

- `ImuProcess::Set_init`(`src/IMU_Processing.cpp` 52〜61 行)は、測った重力と設定の重力がちょうど逆向き(`align_norm < 1e-6` かつ `align_cos ≤ 1e-6`)のとき `rot = -Eye3d` を代入する。行列式が −1 で、回転ではない(鏡映)。台帳の BL-009 に起票し、B で直すと決めた(2026-10-30)。
