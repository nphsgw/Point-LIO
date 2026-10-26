#!/usr/bin/env python3
"""ハーネスの結合テストの判定(ハーネスのプラン T7〜T10、T12、T14)。作業用 venv(evo・rosbags・scipy)で実行する。

ROS 経路の実行は eval/run_cpp.sh の出力ディレクトリ、ファイル駆動の実行は eval/run_offline.sh の出力ディレクトリを渡す。
丸めのない比較(ハッシュ)は eval/hash_run.py と同じ定義: 姿勢は時刻・位置・姿勢・共分散の float64、点群は fields のバイト。
使い方:
  check_runs.py t7  <bag> <基準の run01> <ファイル駆動の出力>...   ファイル駆動 vs 改造前 ROS の中央値実行
  check_runs.py t8  <bag> <基準の run01> <ROS の出力>...           改造後 ROS vs 改造前 ROS の中央値実行
  check_runs.py t9  <bag> <ファイル駆動の出力>...                   ファイル駆動の実行間の ATE の最大値(報告のみ)
  check_runs.py t10 <bag> <ファイル駆動の出力>...                   健全性
  check_runs.py t12 <ファイル駆動の出力> <ROS の出力>                時刻逆転で捨てた件数
  check_runs.py t14 <bag> <ファイル駆動の出力> <ROS の出力>           地図出力
不合格なら終了コード 1。
"""
import copy
import hashlib
import itertools
import math
import os
import struct
import subprocess
import sys

import numpy as np
from evo.core import metrics, sync
from evo.core.metrics import PoseRelation, Unit
from evo.tools import file_interface

# agents/plans/ros-free-harness.md §5 の tau_harness(手順0の45組の最大値)。§5 の表は4桁に丸めてあるので、ここは丸めない値。
# Chamfer は §5 の T14(手順0の10回で 0)
TAU = {
    "avia-quick-shack": {"ATE": 1.0857878733838616e-16, "RPE10m_t": 3.1031676915590914e-17, "RPE10m_r": 0.0,
                         "RPE10f_t": 1.241845408162805e-17, "RPE10f_r": 0.0, "chamfer": 0.0},
    "ouster-nc-quad-easy": {"ATE": 6.601602597014484e-15, "RPE10m_t": 6.732515223982312e-16, "RPE10m_r": 0.0,
                            "RPE10f_t": 1.0514108660213017e-16, "RPE10f_r": 0.0, "chamfer": 0.0},
}
POSES = {"avia-quick-shack": (487, 487), "ouster-nc-quad-easy": (1985, 1985)}  # 手順0の10回の姿勢数の範囲
HERE = os.path.dirname(os.path.abspath(__file__))
failed = False


def fail(msg):
    global failed
    failed = True
    print("FAIL:", msg)


def is_offline(d):
    return os.path.exists(os.path.join(d, "odom.bin"))


def odom_records(d):
    """(時刻 ns, 7 値) の列と、丸めのないハッシュ"""
    if is_offline(d):
        raw = open(os.path.join(d, "odom.bin"), "rb").read()
        rec = struct.Struct("<II7d36d")
        rows = [rec.unpack_from(raw, i) for i in range(0, len(raw), rec.size)]
        return [(r[0] * 10**9 + r[1], r[2:9]) for r in rows], hashlib.sha256(raw).hexdigest()
    from rosbags.rosbag1 import Reader
    from rosbags.typesys import Stores, get_typestore
    ts, h, rows = get_typestore(Stores.ROS1_NOETIC), hashlib.sha256(), []
    with Reader(os.path.join(d, "odom.bag")) as r:
        conns = [c for c in r.connections if c.topic == "/aft_mapped_to_init"]
        for c, _, raw in r.messages(connections=conns):
            m = ts.deserialize_ros1(raw, c.msgtype)
            p, q = m.pose.pose.position, m.pose.pose.orientation
            v = (p.x, p.y, p.z, q.x, q.y, q.z, q.w)
            h.update(struct.pack("<II7d", m.header.stamp.sec, m.header.stamp.nanosec, *v))
            h.update(struct.pack("<36d", *m.pose.covariance))
            rows.append((m.header.stamp.sec * 10**9 + m.header.stamp.nanosec, v))
    return rows, h.hexdigest()


def cloud_frames(d):
    """フレームごとの (時刻 ns, 点数) と、fields のバイトのハッシュ"""
    if is_offline(d):
        frames = [tuple(int(x) for x in l.split()) for l in open(os.path.join(d, "cloud_frames.txt"))]
        h = hashlib.sha256()
        with open(os.path.join(d, "cloud.bin"), "rb") as f:
            for b in iter(lambda: f.read(1 << 24), b""):
                h.update(b)
        return [(s * 10**9 + n, k) for s, n, k in frames], h.hexdigest()
    from rosbags.rosbag1 import Reader
    from rosbags.typesys import Stores, get_typestore
    ts, h, frames = get_typestore(Stores.ROS1_NOETIC), hashlib.sha256(), []
    with Reader(os.path.join(d, "cloud.bag")) as r:
        conns = [c for c in r.connections if c.topic == "/cloud_registered"]
        for c, _, raw in r.messages(connections=conns):
            m = ts.deserialize_ros1(raw, c.msgtype)
            a = np.asarray(m.data, dtype=np.uint8).reshape(-1, m.point_step)
            cols = sorted({f.offset + k for f in m.fields for k in range(4)})
            h.update(a[:, cols].tobytes())
            frames.append((m.header.stamp.sec * 10**9 + m.header.stamp.nanosec, len(a)))
    return frames, h.hexdigest()


def traj(d):
    return file_interface.read_tum_trajectory_file(os.path.join(d, "traj.tum"))


def metrics_of(ref, est):
    r, e = sync.associate_trajectories(copy.deepcopy(ref), copy.deepcopy(est))
    out = {}
    ea = copy.deepcopy(e)
    ea.align(r, correct_scale=False)
    m = metrics.APE(PoseRelation.translation_part)
    m.process_data((r, ea))
    out["ATE"] = m.get_statistic(metrics.StatisticsType.rmse)
    for tag, d, u in (("10m", 10, Unit.meters), ("10f", 10, Unit.frames)):
        for k, rel in (("t", PoseRelation.translation_part), ("r", PoseRelation.rotation_angle_deg)):
            m = metrics.RPE(rel, delta=d, delta_unit=u, all_pairs=False)
            m.process_data((r, e))
            out["RPE%s_%s" % (tag, k)] = m.get_statistic(metrics.StatisticsType.rmse)
    return out


def compare_to_baseline(bag, base, runs):
    """T7・T8 共通: 丸めのない一致と、tau_harness 以下か、姿勢の件数と時刻が基準と一致するかを判定する"""
    base_rows, base_hash = odom_records(base)
    base_traj = traj(base)
    if not runs:
        fail("no runs")
    for d in runs:
        rows, hsh = odom_records(d)
        exact = hsh == base_hash
        first = next((i for i, (a, b) in enumerate(zip(rows, base_rows)) if a != b), None)
        if first is None and len(rows) != len(base_rows):
            first = min(len(rows), len(base_rows))
        m = metrics_of(base_traj, traj(d))
        over = [k for k, v in m.items() if not v <= TAU[bag][k]]
        print("%s exact=%s poses=%d first_diff=%s %s" % (d, exact, len(rows), first,
              " ".join("%s=%.3e" % kv for kv in m.items())))
        if over:
            fail("%s exceeds tau_harness in %s (first differing pose %s)" % (d, over, first))
        # evo は 0.01 s 以内の時刻差を対応づけ、片方にしかない姿勢を捨てるので、件数と時刻はここで照合する
        if len(rows) != len(base_rows):
            fail("%s pose count %d differs from baseline %d" % (d, len(rows), len(base_rows)))
        stamp = next((i for i, (a, b) in enumerate(zip(rows, base_rows)) if a[0] != b[0]), None)
        if stamp is not None:
            fail("%s stamp differs from baseline at pose %d (%d ns vs %d ns)" % (
                d, stamp, rows[stamp][0], base_rows[stamp][0]))


def t9(bag, runs):
    if len(runs) < 2:
        fail("t9 needs at least 2 runs")
    ates = [metrics_of(traj(a), traj(b))["ATE"] for a, b in itertools.combinations(runs, 2)]
    hashes = {odom_records(d)[1] for d in runs}
    print("t9 runs=%d pairs=%d max_ATE=%.3e distinct_odom_hashes=%d" % (len(runs), len(ates), max(ates), len(hashes)))


def t10(bag, runs):
    lo, hi = POSES[bag]
    if not runs:
        fail("no runs")
    for d in runs:
        rows, _ = odom_records(d)
        t = np.loadtxt(os.path.join(d, "traj.tum"), ndmin=2)
        bad = not np.all(np.isfinite(t)) or not all(all(math.isfinite(x) for x in v) for _, v in rows)
        steps = np.linalg.norm(np.diff(t[:, 1:4], axis=0), axis=1) if len(t) > 1 else np.zeros(0)
        print("%s poses=%d finite=%s max_step=%.3f" % (d, len(t), not bad, steps.max() if len(steps) else 0))
        if bad:
            fail("%s has NaN/Inf" % d)
        if not lo <= len(t) <= hi or len(rows) != len(t):
            fail("%s pose count %d outside [%d, %d]" % (d, len(t), lo, hi))
        if len(steps) and not steps.max() <= 10.0:
            fail("%s adjacent translation exceeds 10 m" % d)


def loop_back(path):
    text = open(path, errors="replace").read()
    return text.count("lidar loop back"), text.count("imu loop back")


def t12(offline, ros):
    a, b = loop_back(os.path.join(offline, "run.log")), loop_back(os.path.join(ros, "node.log"))
    print("t12 dropped (lidar, imu): offline=%s ros=%s" % (a, b))
    if a != b:
        fail("dropped counts differ")


def t14(bag, offline, ros):
    fo, ho = cloud_frames(offline)
    fr, hr = cloud_frames(ros)
    print("t14 frames offline=%d ros=%d points offline=%d ros=%d exact=%s" % (
        len(fo), len(fr), sum(k for _, k in fo), sum(k for _, k in fr), ho == hr))
    if fo != fr:
        fail("frame stamps or point counts differ")
    out = subprocess.run([sys.executable, "-I", os.path.join(HERE, "../../eval/chamfer.py"),
                          os.path.join(ros, "cloud.bag"), os.path.join(offline, "cloud.bin")],
                         check=True, capture_output=True, text=True).stdout.split()
    chamfer = float(out[-1])
    print("t14 chamfer=%.3e (tau %.3e)" % (chamfer, TAU[bag]["chamfer"]))
    if not chamfer <= TAU[bag]["chamfer"]:
        fail("chamfer exceeds tau")


def main():
    cmd, args = sys.argv[1], sys.argv[2:]
    if cmd in ("t7", "t8"):
        compare_to_baseline(args[0], args[1], args[2:])
    elif cmd == "t9":
        t9(args[0], args[1:])
    elif cmd == "t10":
        t10(args[0], args[1:])
    elif cmd == "t12":
        t12(args[0], args[1])
    elif cmd == "t14":
        t14(args[0], args[1], args[2])
    else:
        sys.exit("unknown command " + cmd)
    print("FAILED" if failed else "PASSED")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
