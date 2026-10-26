#!/usr/bin/env python3
"""累積点群どうしの Chamfer 距離(事前登録の定義)を出す。

点群の入力は2形式:
- *.bag: /cloud_registered の全フレームを累積する(run_cpp.sh の cloud.bag)
- *.bin: pointlio_offline の cloud.bin(1点 = float32 × 8。先頭の3つが x, y, z。形式は src/offline_output.h)
累積点群を 0.1 m のボクセルで間引き(ボクセル内の点の重心。PCL の VoxelGrid と同じ考え方)、
双方向の最近傍距離の平均を、さらに双方向で平均する。
使い方(作業用 venv。rosbags・scipy が必要): chamfer.py <基準> <比較>
出力: 基準の点数 比較の点数 間引き後の点数(基準, 比較) Chamfer [m]
"""
import sys

import numpy as np
from scipy.spatial import cKDTree

VOXEL = 0.1


def load(path):
    if path.endswith(".bin"):
        return np.fromfile(path, dtype=np.float32).reshape(-1, 8)[:, :3].astype(np.float64)
    from rosbags.rosbag1 import Reader
    from rosbags.typesys import Stores, get_typestore
    ts = get_typestore(Stores.ROS1_NOETIC)
    chunks = []
    with Reader(path) as reader:
        conns = [c for c in reader.connections if c.topic == "/cloud_registered"]
        for conn, _, raw in reader.messages(connections=conns):
            m = ts.deserialize_ros1(raw, conn.msgtype)
            off = {f.name: f.offset for f in m.fields}
            a = np.frombuffer(m.data, dtype=np.uint8).reshape(-1, m.point_step)
            xyz = [a[:, off[k]:off[k] + 4].copy().view(np.float32).ravel() for k in ("x", "y", "z")]
            chunks.append(np.stack(xyz, axis=1))
    if not chunks:
        sys.exit("no /cloud_registered in %s" % path)
    return np.concatenate(chunks).astype(np.float64)


def voxel_down(p):
    keys = np.floor(p / VOXEL).astype(np.int64)
    _, inv = np.unique(keys, axis=0, return_inverse=True)
    inv = inv.ravel()
    n = inv.max() + 1
    out = np.zeros((n, 3))
    np.add.at(out, inv, p)
    return out / np.bincount(inv, minlength=n)[:, None]


def main():
    a, b = load(sys.argv[1]), load(sys.argv[2])
    da, db = voxel_down(a), voxel_down(b)
    ab = cKDTree(db).query(da)[0].mean()
    ba = cKDTree(da).query(db)[0].mean()
    print(len(a), len(b), len(da), len(db), "%.6e" % ((ab + ba) / 2))


if __name__ == "__main__":
    main()
