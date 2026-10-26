#pragma once
// laserMapping.cpp の処理本体を、ROS ノード(laserMapping_node.cpp)と pointlio_offline(offline_main.cpp)で共有する。
#include <cstdio>
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
