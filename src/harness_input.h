#pragma once
// ハーネスの入力ファイル(docs/harness-input-format.md)を先頭から1件ずつ読む。
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>
#include <boost/shared_ptr.hpp>
#include <livox_ros_driver/CustomMsg.h>
#include <sensor_msgs/Imu.h>
#include <sensor_msgs/PointCloud2.h>

struct InputRecord
{
    enum Kind : uint8_t { IMU = 1, POINTCLOUD2 = 2, LIVOX = 3 };
    uint8_t kind = 0;
    uint32_t bag_sec = 0, bag_nsec = 0;     // bag の記録時刻
    uint32_t stamp_sec = 0, stamp_nsec = 0; // header.stamp の写し
    std::vector<uint8_t> payload;           // ROS1 シリアライズ
};

class InputReader
{
public:
    explicit InputReader(const std::string &path); // 開けない・マジックや版が違うときは例外
    bool next(InputRecord &rec);                   // 末尾で false。途中で切れたレコードは例外

private:
    std::ifstream in_;
};

// ペイロードを復元する。余りや不足があれば例外。
boost::shared_ptr<sensor_msgs::Imu> to_imu(const InputRecord &rec);
boost::shared_ptr<sensor_msgs::PointCloud2> to_pointcloud2(const InputRecord &rec);
boost::shared_ptr<livox_ros_driver::CustomMsg> to_livox(const InputRecord &rec);
