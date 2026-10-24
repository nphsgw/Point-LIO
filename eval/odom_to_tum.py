#!/usr/bin/env python3
"""rosbag の nav_msgs/Odometry を TUM 形式(t x y z qx qy qz qw)に書き出す。

使い方: odom_to_tum.py <odom.bag> <topic> <out.tum>
"""
import sys

import rosbag


def main():
    bag_path, topic, out_path = sys.argv[1:4]
    n = 0
    with rosbag.Bag(bag_path) as bag, open(out_path, "w") as out:
        for _, m, _ in bag.read_messages(topics=[topic]):
            p, q = m.pose.pose.position, m.pose.pose.orientation
            out.write("%.9f %.9f %.9f %.9f %.9f %.9f %.9f %.9f\n"
                      % (m.header.stamp.to_sec(), p.x, p.y, p.z, q.x, q.y, q.z, q.w))
            n += 1
    if n == 0:
        sys.exit("no messages on %s" % topic)
    print("wrote %d poses" % n)


if __name__ == "__main__":
    main()
