# ハーネス入力ファイルの形式(v1)

評価 bag から LiDAR と IMU のメッセージを取り出し、記録順に並べた1本のバイナリファイル。C++(`pointlio_offline`)と Rust の双方がこれを読む。書き出しは `eval/extract_input.py`。

設計の要点:

- ペイロードは、bag に記録されている ROS1 のシリアライズ形式のバイト列をそのまま持つ。値の変換をしないので、bag の値とビット単位で同じになる。
- 並びは bag の記録時刻(受信時刻)の順。種別をまたいだ順序も保つ。ヘッダ時刻では並べ替えない。
- 時刻の逆転したメッセージも除かない(受信処理側で捨てる)。

## 全体

すべてリトルエンディアン。

| 位置 | 型 | 内容 |
| --- | --- | --- |
| 0 | `u8[8]` | マジック `PLIOIN01`(ASCII) |
| 8 | `u32` | 版。1 |
| 12 | `u32` | 予約。0 |
| 16 | レコードの列 | ファイル末尾まで |

## レコード

| 位置(レコード先頭から) | 型 | 内容 |
| --- | --- | --- |
| 0 | `u8` | 種別。1 = `sensor_msgs/Imu`、2 = `sensor_msgs/PointCloud2`、3 = `livox_ros_driver/CustomMsg` |
| 1 | `u8[3]` | 予約。0 |
| 4 | `u32`, `u32` | bag の記録時刻(秒、ナノ秒) |
| 12 | `u32`, `u32` | ヘッダ時刻 `header.stamp`(秒、ナノ秒)。ペイロード内の値の写し |
| 20 | `u32` | ペイロード長 L |
| 24 | `u8[L]` | ペイロード(ROS1 シリアライズ) |

ファイルの途中で終わるレコード(ヘッダやペイロードが途中で切れる)は不正とし、読み手は失敗させる。

## ペイロード(ROS1 シリアライズ)

ROS1 の規則: 数値は固定長のリトルエンディアン。`string` は `u32` の長さ + バイト列(終端 NUL なし)。可変長配列は `u32` の要素数 + 要素。固定長配列は要素だけ。`time` は `u32` 秒 + `u32` ナノ秒。

3種別とも先頭は `std_msgs/Header`: `u32 seq`、`time stamp`、`string frame_id`。

メッセージ定義の MD5(イメージ `point-lio-noetic` の `rosmsg md5` で確認):

| 種別 | 型 | MD5 |
| --- | --- | --- |
| 1 | `sensor_msgs/Imu` | `6a62c6daae103f4ff57a132d6f95cec2` |
| 2 | `sensor_msgs/PointCloud2` | `1158d486dd51d683ce2f1be655c3c181` |
| 3 | `livox_ros_driver/CustomMsg` | `e4d6829bdfe657cb6c21a746c86b21a6` |

### 1: `sensor_msgs/Imu`

`Header`、`orientation`(`f64` x, y, z, w)、`f64[9] orientation_covariance`、`angular_velocity`(`f64` x, y, z)、`f64[9] angular_velocity_covariance`、`linear_acceleration`(`f64` x, y, z)、`f64[9] linear_acceleration_covariance`。

Point-LIO が使うのは `header.stamp`、`angular_velocity`、`linear_acceleration` だけ。

### 2: `sensor_msgs/PointCloud2`

`Header`、`u32 height`、`u32 width`、`fields`(要素数 + 各要素 `string name`、`u32 offset`、`u8 datatype`、`u32 count`)、`u8 is_bigendian`(bool)、`u32 point_step`、`u32 row_step`、`u8[] data`(要素数 + バイト列)、`u8 is_dense`(bool)。

`data` は前処理前の生データ。点の解釈(フィールド名と offset、型)は `fields` に従う。

### 3: `livox_ros_driver/CustomMsg`

`Header`、`u64 timebase`、`u32 point_num`、`u8 lidar_id`、`u8[3] rsvd`、`points`(要素数 + 各要素 `u32 offset_time`、`f32 x`、`f32 y`、`f32 z`、`u8 reflectivity`、`u8 tag`、`u8 line`。1点 19 バイト、詰め物なし)。

## 対象の bag

| ID | LiDAR の種別 | LiDAR トピック | IMU トピック |
| --- | --- | --- | --- |
| avia-quick-shack | 3 | `/livox/lidar` | `/livox/imu` |
| ouster-nc-quad-easy | 2 | 正準設定 `common/lid_topic` | 正準設定 `common/imu_topic` |

トピック名は正準設定(`eval/config/canonical/<ID>.yaml`)から読む。
