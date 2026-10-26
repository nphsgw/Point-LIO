#!/usr/bin/env python3
"""run_cpp.sh の出力ディレクトリごとに、記録した出力を丸めずにハッシュする。決定性の確認用。

traj.tum は %.9f に丸めてあるので、ビット一致はこちらで確かめる。
- odom: /aft_mapped_to_init の時刻(sec, nsec)、位置、クォータニオン、共分散36個(float64 のまま)
- cloud: /cloud_registered の各点の、fields に載ったバイトだけ(パディングは未初期化で毎回変わるため除く)
- stamp: /cloud_registered のヘッダ時刻と seq
使い方(Docker 内): hash_run.py <出力ディレクトリ>...
出力: odom のハッシュ、姿勢数、cloud のハッシュ、フレーム数、stamp のハッシュ、ディレクトリ
"""
import hashlib
import struct
import sys

import numpy as np
import rosbag


def main():
    for d in sys.argv[1:]:
        ho, n = hashlib.sha256(), 0
        with rosbag.Bag(d + "/odom.bag") as bag:
            for _, m, _ in bag.read_messages(topics=["/aft_mapped_to_init"]):
                p, q = m.pose.pose.position, m.pose.pose.orientation
                ho.update(struct.pack("<II7d", m.header.stamp.secs, m.header.stamp.nsecs,
                                      p.x, p.y, p.z, q.x, q.y, q.z, q.w))
                ho.update(struct.pack("<36d", *m.pose.covariance))
                n += 1
        hc, hs, nc = hashlib.sha256(), hashlib.sha256(), 0
        with rosbag.Bag(d + "/cloud.bag") as bag:
            for _, m, _ in bag.read_messages(topics=["/cloud_registered"]):
                a = np.frombuffer(m.data, dtype=np.uint8).reshape(-1, m.point_step)
                cols = sorted({f.offset + k for f in m.fields for k in range(4)})  # 全 field が FLOAT32
                assert all(f.datatype == 7 and f.count == 1 for f in m.fields)
                hc.update(a[:, cols].tobytes())
                hs.update(b"%d.%09d %d;" % (m.header.stamp.secs, m.header.stamp.nsecs, m.header.seq))
                nc += 1
        if n == 0 or nc == 0:
            sys.exit("no messages in %s" % d)
        print(ho.hexdigest()[:16], n, hc.hexdigest()[:16], nc, hs.hexdigest()[:16], d)


if __name__ == "__main__":
    main()
