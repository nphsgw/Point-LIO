#pragma once
// laserMapping.cpp の処理本体を、ROS ノード(laserMapping_node.cpp)と pointlio_offline(offline_main.cpp)で共有する。
#include <cstdio>
#include <Eigen/Core>
#include <nav_msgs/Odometry.h>
#include <nav_msgs/Path.h>
#include <sensor_msgs/PointCloud2.h>

// 処理結果の送り先。ROS ノードはトピックへ発行し、pointlio_offline はファイルへ書く。
// 呼ばれる時点・回数・渡す値は、改造前の各 publish と同じ。
class MappingOutput
{
public:
    virtual ~MappingOutput() {}
    virtual void odometry(const nav_msgs::Odometry &msg) = 0;              // /aft_mapped_to_init と tf
    virtual void path(const nav_msgs::Path &msg) = 0;                      // /path
    virtual void cloud_registered(const sensor_msgs::PointCloud2 &msg) = 0;      // /cloud_registered
    virtual void cloud_registered_body(const sensor_msgs::PointCloud2 &msg) = 0; // /cloud_registered_body
    virtual void laser_map(const sensor_msgs::PointCloud2 &msg) = 0;            // /Laser_map(初期地図)
};

extern bool flg_exit;
void SigHandle(int sig);

void mapping_init();                       // readParameters の後に1回呼ぶ
void open_debug_logs();                    // Log/ への書き出しを開く(ROS ノードのみ)
void process_measures(MappingOutput &out); // sync_packages(Measures) が true を返すたびに1回呼ぶ
void mapping_finish();                     // 終了時に1回呼ぶ

// S1b: 最初のフレームの準備
void first_frame_init();
// S2b: フレームの前処理
void downsample_frame();
// S3b: 初期姿勢
void init_attitude();
// S3c: 初期地図
void init_map_frame(MappingOutput &out);
// S0 の前半: 最初の組の準備(is_first_frame の処理)。呼ぶ前に呼び出し側が time_current を計算する
void group_begin(bool &imu_upda_cov);
// S0 の後半: IMU の待ち行列の送り。呼び出し側が imu_en && !imu_deque.empty() のときだけ呼ぶ
void advance_imu_queue();
// S4i: IMU の時刻まで状態を予測
void predict_state_to_imu();
// S5: IMU の時刻まで共分散を予測。予測したときに true を返す
bool predict_cov_to_imu();
// S4g: 組の時刻まで状態を予測。prop_at_freq_of_imu が偽なら共分散も予測(正準設定では通らない)
void predict_state_to_group();
// S7: 更新後の組の点を変換
void transform_group_points();
// 予測の Q と初期共分散 P を読み出す
const Eigen::Matrix<double, 30, 30> &get_Q_output();
const Eigen::Matrix<double, 30, 30> &get_P_init_output();
