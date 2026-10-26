// pointlio_offline: ハーネスの入力ファイルを読み、ROS を使わずに最後まで処理して、結果をファイルに書く。
// 使い方: pointlio_offline <入力ファイル> <正準設定 YAML> <出力ディレクトリ> [--dump-params <ファイル>]
//
// 送り込み(ハーネスのプラン §3.4): レコードを記録順に1件ずつ、ROS ノードと同じコールバックに渡し、
// 渡すたびに sync_packages が true を返す限り process_measures を呼ぶ。入力が尽きた時点で処理できないフレームは処理しない。
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "harness_input.h"
#include "li_initialization.h"
#include "mapping_core.h"
#include "offline_output.h"
#include "yaml_params.h"

int main(int argc, char **argv)
{
    if (argc != 4 && !(argc == 6 && std::strcmp(argv[4], "--dump-params") == 0))
    {
        std::cerr << "usage: pointlio_offline <input> <config.yaml> <out_dir> [--dump-params <file>]" << std::endl;
        return 2;
    }
    try
    {
        YamlParamSource params(argv[2]);
        readParameters(params);
        if (argc == 6)
        {
            std::ofstream f(argv[5]);
            dump_parameters(f);
            if (!f)
                throw std::runtime_error(std::string("cannot write ") + argv[5]);
        }
        // Log/ への書き出し(runtime_pos_log)は ROS ノードだけが行う
        if (runtime_pos_log)
            throw std::runtime_error("runtime_pos_log_enable must be false");
        // 地図は /cloud_registered と同じ点を累積する。scan_publish_en が偽だと publish_frame_world が点を渡さない
        if (!scan_pub_en)
            throw std::runtime_error("publish/scan_publish_en must be true");
        if (cut_frame_init || con_frame)
            throw std::runtime_error("cut_frame and con_frame are not supported");

        cout << "lidar_type: " << lidar_type << endl;
        mapping_init();

        InputReader reader(argv[1]);
        FileOutput out(argv[3]);
        InputRecord rec;
        size_t n_imu = 0, n_lidar = 0;
        while (reader.next(rec))
        {
            if (rec.kind == InputRecord::IMU)
            {
                imu_cbk(to_imu(rec));
                n_imu++;
            }
            else if (p_pre->lidar_type == AVIA) // ROS ノードの購読と同じ選び方
            {
                livox_pcl_cbk(to_livox(rec));
                n_lidar++;
            }
            else
            {
                standard_pcl_cbk(to_pointcloud2(rec));
                n_lidar++;
            }
            while (sync_packages(Measures))
                process_measures(out);
        }
        mapping_finish();
        out.close();
        std::cout << "records imu=" << n_imu << " lidar=" << n_lidar << " odometry=" << out.odometry_count()
                  << " cloud_frames=" << out.cloud_frame_count() << std::endl;
    }
    catch (const std::exception &e)
    {
        std::cerr << "pointlio_offline: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
