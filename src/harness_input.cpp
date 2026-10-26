#include "harness_input.h"

#include <cstring>
#include <stdexcept>
#include <ros/serialization.h>

namespace
{
const char MAGIC[8] = {'P', 'L', 'I', 'O', 'I', 'N', '0', '1'};

uint32_t u32(const uint8_t *p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

template <typename M>
boost::shared_ptr<M> deserialize(const InputRecord &rec, uint8_t want)
{
    if (rec.kind != want)
        throw std::runtime_error("record kind " + std::to_string(rec.kind) + ", expected " + std::to_string(want));
    boost::shared_ptr<M> msg(new M);
    // IStream は書き換えないが、const を受けない API のため外す
    ros::serialization::IStream s(const_cast<uint8_t *>(rec.payload.data()), rec.payload.size());
    ros::serialization::deserialize(s, *msg);
    if (s.getLength() != 0)
        throw std::runtime_error("payload has " + std::to_string(s.getLength()) + " trailing bytes");
    if (msg->header.stamp.sec != rec.stamp_sec || msg->header.stamp.nsec != rec.stamp_nsec)
        throw std::runtime_error("record stamp differs from payload header");
    return msg;
}
}

InputReader::InputReader(const std::string &path) : in_(path, std::ios::binary)
{
    if (!in_)
        throw std::runtime_error("cannot open " + path);
    uint8_t head[16];
    if (!in_.read(reinterpret_cast<char *>(head), sizeof(head)) || std::memcmp(head, MAGIC, 8) != 0)
        throw std::runtime_error("bad magic: " + path);
    if (u32(head + 8) != 1 || u32(head + 12) != 0)
        throw std::runtime_error("unsupported version: " + path);
}

bool InputReader::next(InputRecord &rec)
{
    uint8_t h[24];
    in_.read(reinterpret_cast<char *>(h), sizeof(h));
    if (in_.gcount() == 0 && in_.eof())
        return false;
    if (in_.gcount() != sizeof(h))
        throw std::runtime_error("truncated record header");
    rec.kind = h[0];
    if (rec.kind < InputRecord::IMU || rec.kind > InputRecord::LIVOX || h[1] || h[2] || h[3])
        throw std::runtime_error("bad record kind");
    rec.bag_sec = u32(h + 4);
    rec.bag_nsec = u32(h + 8);
    rec.stamp_sec = u32(h + 12);
    rec.stamp_nsec = u32(h + 16);
    rec.payload.resize(u32(h + 20));
    in_.read(reinterpret_cast<char *>(rec.payload.data()), rec.payload.size());
    if (static_cast<size_t>(in_.gcount()) != rec.payload.size())
        throw std::runtime_error("truncated payload");
    return true;
}

boost::shared_ptr<sensor_msgs::Imu> to_imu(const InputRecord &rec)
{
    return deserialize<sensor_msgs::Imu>(rec, InputRecord::IMU);
}

boost::shared_ptr<sensor_msgs::PointCloud2> to_pointcloud2(const InputRecord &rec)
{
    return deserialize<sensor_msgs::PointCloud2>(rec, InputRecord::POINTCLOUD2);
}

boost::shared_ptr<livox_ros_driver::CustomMsg> to_livox(const InputRecord &rec)
{
    return deserialize<livox_ros_driver::CustomMsg>(rec, InputRecord::LIVOX);
}
