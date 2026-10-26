#pragma once
// pointlio_offline の出力。ROS 経路で記録するトピックと同じ値を、比較しやすい形でファイルに書く。
//
// - traj.tum: /aft_mapped_to_init の姿勢。1行 "秒.ナノ秒 x y z qx qy qz qw"。時刻は header.stamp をそのまま書き、
//   位置と姿勢は %.9f(eval/odom_to_tum.py と同じ桁)
// - odom.bin: 同じ姿勢を丸めずに。1件 = u32 秒, u32 ナノ秒, f64 x y z qx qy qz qw, f64 共分散36個(リトルエンディアン)。
//   eval/hash_run.py が ROS の記録から作るバイト列と同じなので、ファイルの SHA256 がそのまま比べられる
// - cloud.bin: /cloud_registered の各点の fields のバイトだけを、offset の昇順・フレーム順に連結したもの(パディングは書かない)。
//   これも eval/hash_run.py と同じバイト列。全 fields が float32 なので、1点は float32 の列になる
// - cloud_frames.txt: /cloud_registered の1フレーム1行 "秒 ナノ秒 点数"
#include <cstdio>
#include <string>
#include "mapping_core.h"

class FileOutput : public MappingOutput
{
public:
    explicit FileOutput(const std::string &dir); // 書けなければ例外
    ~FileOutput() override;
    void odometry(const nav_msgs::Odometry &msg) override;
    void path(const nav_msgs::Path &) override {}
    void cloud_registered(const sensor_msgs::PointCloud2 &msg) override;
    void cloud_registered_body(const sensor_msgs::PointCloud2 &) override {}
    void laser_map(const sensor_msgs::PointCloud2 &) override {}
    void close(); // 書き込みの失敗は例外
    size_t odometry_count() const { return n_odom_; }
    size_t cloud_frame_count() const { return n_cloud_; }

private:
    FILE *tum_ = nullptr, *odom_ = nullptr, *cloud_ = nullptr, *frames_ = nullptr;
    size_t n_odom_ = 0, n_cloud_ = 0;
};
