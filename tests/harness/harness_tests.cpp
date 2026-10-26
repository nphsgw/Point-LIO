// ハーネスの C++ テスト(ハーネスのプラン T5・T6・T13)。
// 使い方:
//   harness_tests t5 <入力ファイル> <bag> <正準設定>   入力ファイルを C++ リーダで全件読み、bag の各メッセージと全フィールドを照合
//   harness_tests t6 <入力ファイル> <bag> <正準設定>   全 LiDAR フレームで、bag 経由とファイル経由の Preprocess::process の出力を照合
//   harness_tests t13 <作業ディレクトリ>               軌跡の書き出し(FileOutput)を、既知の姿勢列で検査
// 環境変数 MUTATE=1(t6 のみ): ファイル経由の入力から点の時刻を落とす。テストが落ちることの確認用
// 失敗は終了コード 1。件数が 0 のときも失敗にする。
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <rosbag/bag.h>
#include <rosbag/view.h>
#include "harness_input.h"
#include "offline_output.h"
#include "parameters.h"
#include "yaml_params.h"

namespace
{
int failures = 0;

void check(bool ok, const std::string &what)
{
    if (!ok)
    {
        if (failures < 20)
            std::cerr << "FAIL: " << what << std::endl;
        failures++;
    }
}

std::vector<std::string> topics(const std::string &config)
{
    YamlParamSource p(config);
    std::string lid, imu;
    if (!p.get("common/lid_topic", lid) || !p.get("common/imu_topic", imu))
        throw std::runtime_error("topics missing in " + config);
    return {lid, imu};
}

// 入力ファイルと bag を並べて読み、1件ごとに f(レコード, bag のメッセージ) を呼ぶ。件数の一致も検査する
template <typename F>
size_t zip_records(const std::string &input, const std::string &bag_path, const std::string &config, F f)
{
    rosbag::Bag bag(bag_path);
    rosbag::View view(bag, rosbag::TopicQuery(topics(config)));
    InputReader reader(input);
    InputRecord rec;
    size_t n = 0;
    auto it = view.begin();
    while (true)
    {
        bool has_rec = reader.next(rec), has_bag = it != view.end();
        check(has_rec == has_bag, "record count differs from bag at " + std::to_string(n));
        if (!has_rec || !has_bag)
            break;
        check(rec.bag_sec == it->getTime().sec && rec.bag_nsec == it->getTime().nsec,
              "bag time differs at " + std::to_string(n));
        f(rec, *it, n);
        ++it;
        ++n;
    }
    check(n > 0, "no records");
    return n;
}

int t5(const std::string &input, const std::string &bag, const std::string &config)
{
    size_t counts[4] = {0, 0, 0, 0};
    size_t n = zip_records(input, bag, config, [&](const InputRecord &rec, const rosbag::MessageInstance &m, size_t i) {
        const std::string at = " at " + std::to_string(i);
        counts[rec.kind]++;
        if (rec.kind == InputRecord::IMU)
        {
            auto want = m.instantiate<sensor_msgs::Imu>();
            check(want && *to_imu(rec) == *want, "Imu differs" + at);
        }
        else if (rec.kind == InputRecord::POINTCLOUD2)
        {
            auto want = m.instantiate<sensor_msgs::PointCloud2>();
            check(want && *to_pointcloud2(rec) == *want, "PointCloud2 differs" + at);
        }
        else
        {
            auto want = m.instantiate<livox_ros_driver::CustomMsg>();
            check(want && *to_livox(rec) == *want, "CustomMsg differs" + at);
        }
    });
    std::cout << "t5 records=" << n << " imu=" << counts[1] << " pointcloud2=" << counts[2] << " livox=" << counts[3]
              << std::endl;
    check(counts[1] > 0 && counts[2] + counts[3] > 0, "missing IMU or LiDAR records");
    return failures ? 1 : 0;
}

bool same_points(const PointCloudXYZI &a, const PointCloudXYZI &b)
{
    if (a.size() != b.size())
        return false;
    for (size_t i = 0; i < a.size(); i++)
    {
        const PointType &p = a.points[i], &q = b.points[i];
        const float va[8] = {p.x, p.y, p.z, p.intensity, p.normal_x, p.normal_y, p.normal_z, p.curvature};
        const float vb[8] = {q.x, q.y, q.z, q.intensity, q.normal_x, q.normal_y, q.normal_z, q.curvature};
        if (std::memcmp(va, vb, sizeof(va)) != 0)
            return false;
    }
    return true;
}

int t6(const std::string &input, const std::string &bag, const std::string &config)
{
    YamlParamSource params(config);
    readParameters(params);
    p_pre->lidar_type = lidar_type; // mapping_init と同じ
    const bool mutate = std::getenv("MUTATE") != nullptr;
    size_t frames = 0, points = 0;
    zip_records(input, bag, config, [&](const InputRecord &rec, const rosbag::MessageInstance &m, size_t i) {
        if (rec.kind == InputRecord::IMU)
            return;
        PointCloudXYZI::Ptr from_bag(new PointCloudXYZI()), from_file(new PointCloudXYZI());
        if (rec.kind == InputRecord::LIVOX)
        {
            auto msg = to_livox(rec);
            if (mutate)
                for (auto &pt : msg->points)
                    pt.offset_time = 0;
            p_pre->process(msg, from_file);
            p_pre->process(m.instantiate<livox_ros_driver::CustomMsg>(), from_bag);
        }
        else
        {
            auto msg = to_pointcloud2(rec);
            // 名前を変えるだけでは落ちない。PCL の createMapping は配置が構造体と同じなら隣の field と1回の memcpy にまとめ、
            // 対応の取れない field のバイトも写すため。値そのものを 0 にする
            if (mutate)
                for (const auto &f : msg->fields)
                    if (f.name == "t" || f.name == "time" || f.name == "timestamp")
                        for (size_t p = 0; p + msg->point_step <= msg->data.size(); p += msg->point_step)
                            std::memset(&msg->data[p + f.offset], 0, f.datatype == sensor_msgs::PointField::FLOAT64 ? 8 : 4);
            p_pre->process(msg, from_file);
            p_pre->process(m.instantiate<sensor_msgs::PointCloud2>(), from_bag);
        }
        check(same_points(*from_bag, *from_file), "preprocess output differs at record " + std::to_string(i));
        frames++;
        points += from_bag->size();
    });
    std::cout << "t6 frames=" << frames << " points=" << points << std::endl;
    check(frames > 0 && points > 0, "no preprocessed points");
    return failures ? 1 : 0;
}

int t13(const std::string &dir)
{
    // 時刻は ros::Time::fromSec(lidar_end_time) の結果をそのまま書くこと、値は %.9f、列は TUM の並びであることを確かめる
    const double lidar_end_time[3] = {1625132267.9414138794, 361.599273754, 0.5};
    const double pose[3][7] = {{1.5, -2.25, 3.125, 0.0, 0.0, 0.7071067811865476, 0.7071067811865476},
                               {-0.016141189, 0.069356490, 0.006019511, -0.004799494, 0.004952259, -0.009349858, 0.999932508},
                               {0, 0, 0, 0, 0, 0, 1}};
    {
        FileOutput out(dir);
        for (int i = 0; i < 3; i++)
        {
            nav_msgs::Odometry o;
            o.header.stamp = ros::Time().fromSec(lidar_end_time[i]);
            o.pose.pose.position.x = pose[i][0];
            o.pose.pose.position.y = pose[i][1];
            o.pose.pose.position.z = pose[i][2];
            o.pose.pose.orientation.x = pose[i][3];
            o.pose.pose.orientation.y = pose[i][4];
            o.pose.pose.orientation.z = pose[i][5];
            o.pose.pose.orientation.w = pose[i][6];
            out.odometry(o);
        }
        out.close();
    }
    std::ifstream tum(dir + "/traj.tum");
    std::string line;
    int n = 0;
    while (std::getline(tum, line))
    {
        check(n < 3, "too many lines");
        if (n >= 3)
            break;
        std::istringstream ss(line);
        std::string t;
        double v[7];
        ss >> t;
        for (double &x : v)
            ss >> x;
        std::string rest;
        check(ss && !(ss >> rest), "line " + std::to_string(n) + " does not have 8 columns");
        const size_t dot = t.find('.');
        check(dot != std::string::npos && t.size() - dot - 1 == 9, "time is not sec.nsec(9 digits): " + t);
        const double sec = std::stod(t.substr(0, dot)), nsec = std::stod(t.substr(dot + 1));
        const double err_ns = std::fabs((sec - std::floor(lidar_end_time[n])) * 1e9 + nsec -
                                        (lidar_end_time[n] - std::floor(lidar_end_time[n])) * 1e9);
        check(err_ns <= 1.0, "time differs from lidar_end_time by " + std::to_string(err_ns) + " ns");
        for (int k = 0; k < 7; k++)
            check(std::fabs(v[k] - pose[n][k]) <= 5e-10, "column " + std::to_string(k + 1) + " differs");
        n++;
    }
    check(n == 3, "expected 3 lines, got " + std::to_string(n));
    std::ifstream odom(dir + "/odom.bin", std::ios::binary | std::ios::ate);
    check(odom && odom.tellg() == 3 * (8 + 7 * 8 + 36 * 8), "odom.bin size");
    std::cout << "t13 lines=" << n << std::endl;
    return failures ? 1 : 0;
}
}

int main(int argc, char **argv)
{
    try
    {
        const std::string cmd = argc > 1 ? argv[1] : "";
        if (cmd == "t5" && argc == 5)
            return t5(argv[2], argv[3], argv[4]);
        if (cmd == "t6" && argc == 5)
            return t6(argv[2], argv[3], argv[4]);
        if (cmd == "t13" && argc == 3)
            return t13(argv[2]);
    }
    catch (const std::exception &e)
    {
        std::cerr << "FAIL: " << e.what() << std::endl;
        return 1;
    }
    std::cerr << "usage: harness_tests t5|t6 <input> <bag> <config> | t13 <dir>" << std::endl;
    return 2;
}
