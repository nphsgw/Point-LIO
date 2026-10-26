#!/usr/bin/env python3
"""bag から LiDAR と IMU を取り出し、ハーネスの入力ファイルを書く(形式は docs/harness-input-format.md)。

ペイロードは bag に記録された ROS1 のシリアライズのまま書く。並びは bag の記録時刻の順(rosbag の読み出し順)。
使い方(Docker 内): extract_input.py <bag> <正準設定 YAML> <出力ファイル>
"""
import os
import struct
import sys

import rosbag
import yaml

KIND = {"sensor_msgs/Imu": 1, "sensor_msgs/PointCloud2": 2, "livox_ros_driver/CustomMsg": 3}
MD5 = {"sensor_msgs/Imu": "6a62c6daae103f4ff57a132d6f95cec2",
       "sensor_msgs/PointCloud2": "1158d486dd51d683ce2f1be655c3c181",
       "livox_ros_driver/CustomMsg": "e4d6829bdfe657cb6c21a746c86b21a6"}
HEAD = struct.Struct("<B3xIIIII")


def main():
    bag_path, config, out = sys.argv[1:4]
    with open(config) as f:
        common = yaml.safe_load(f)["common"]
    lid, imu = common["lid_topic"], common["imu_topic"]
    tmp = out + ".tmp"
    counts = {}
    with rosbag.Bag(bag_path) as bag, open(tmp, "wb") as f:
        f.write(b"PLIOIN01" + struct.pack("<II", 1, 0))
        for topic, (datatype, data, md5, _, _), t in bag.read_messages(topics=[lid, imu], raw=True):
            if datatype not in KIND or md5 != MD5[datatype]:
                sys.exit("unexpected type on %s: %s %s" % (topic, datatype, md5))
            if (topic == imu) != (datatype == "sensor_msgs/Imu"):
                sys.exit("topic %s has type %s" % (topic, datatype))
            data = bytes(data)
            st_sec, st_nsec = struct.unpack_from("<II", data, 4)  # Header: seq, stamp
            f.write(HEAD.pack(KIND[datatype], t.secs, t.nsecs, st_sec, st_nsec, len(data)))
            f.write(data)
            counts[datatype] = counts.get(datatype, 0) + 1
    if len(counts) != 2:
        os.remove(tmp)
        sys.exit("expected LiDAR and IMU messages, got %s" % counts)
    os.replace(tmp, out)
    print(" ".join("%s=%d" % kv for kv in sorted(counts.items())))


if __name__ == "__main__":
    main()
