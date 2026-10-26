#include "offline_output.h"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace
{
FILE *open_or_throw(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        throw std::runtime_error("cannot write " + path);
    return f;
}

void write_or_throw(FILE *f, const void *p, size_t n)
{
    if (n && std::fwrite(p, 1, n, f) != n)
        throw std::runtime_error("write failed");
}
}

FileOutput::FileOutput(const std::string &dir)
{
    tum_ = open_or_throw(dir + "/traj.tum");
    odom_ = open_or_throw(dir + "/odom.bin");
    cloud_ = open_or_throw(dir + "/cloud.bin");
    frames_ = open_or_throw(dir + "/cloud_frames.txt");
}

FileOutput::~FileOutput()
{
    for (FILE *f : {tum_, odom_, cloud_, frames_})
        if (f)
            std::fclose(f);
}

void FileOutput::close()
{
    for (FILE **f : {&tum_, &odom_, &cloud_, &frames_})
    {
        if (*f && std::fclose(*f) != 0)
            throw std::runtime_error("close failed");
        *f = nullptr;
    }
}

void FileOutput::odometry(const nav_msgs::Odometry &msg)
{
    const geometry_msgs::Point &p = msg.pose.pose.position;
    const geometry_msgs::Quaternion &q = msg.pose.pose.orientation;
    if (std::fprintf(tum_, "%u.%09u %.9f %.9f %.9f %.9f %.9f %.9f %.9f\n", msg.header.stamp.sec, msg.header.stamp.nsec,
                     p.x, p.y, p.z, q.x, q.y, q.z, q.w) < 0)
        throw std::runtime_error("write failed");
    const uint32_t t[2] = {msg.header.stamp.sec, msg.header.stamp.nsec};
    const double v[7] = {p.x, p.y, p.z, q.x, q.y, q.z, q.w};
    write_or_throw(odom_, t, sizeof(t)); // x86-64 はリトルエンディアン
    write_or_throw(odom_, v, sizeof(v));
    write_or_throw(odom_, msg.pose.covariance.data(), sizeof(double) * msg.pose.covariance.size());
    n_odom_++;
}

void FileOutput::cloud_registered(const sensor_msgs::PointCloud2 &msg)
{
    std::vector<uint32_t> cols;
    for (const sensor_msgs::PointField &f : msg.fields)
    {
        if (f.datatype != sensor_msgs::PointField::FLOAT32 || f.count != 1)
            throw std::runtime_error("unexpected field " + f.name);
        for (uint32_t k = 0; k < 4; k++)
            cols.push_back(f.offset + k);
    }
    std::sort(cols.begin(), cols.end());
    const size_t n = size_t(msg.width) * msg.height;
    std::vector<uint8_t> buf;
    buf.reserve(n * cols.size());
    for (size_t i = 0; i < n; i++)
        for (uint32_t c : cols)
            buf.push_back(msg.data[i * msg.point_step + c]);
    write_or_throw(cloud_, buf.data(), buf.size());
    if (std::fprintf(frames_, "%u %u %zu\n", msg.header.stamp.sec, msg.header.stamp.nsec, n) < 0)
        throw std::runtime_error("write failed");
    n_cloud_++;
}
