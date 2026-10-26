#!/usr/bin/env python3
"""抽出器の結合テスト(ハーネスのプラン T1〜T4、T12 の種別の列)。

入力ファイルは docs/harness-input-format.md の仕様だけに基づいて読む(抽出器のコードを使わない)。
評価データや入力ファイルがなければ失敗する(スキップしない)。
環境変数: DATA_DIR(bag の置き場所)、INPUT_DIR(入力ファイルの置き場所)、REPO(リポジトリ)
実行: tests/harness/run_extract_tests.sh
"""
import hashlib
import os
import struct
import subprocess
import unittest

import rosbag
import yaml

DATA_DIR = os.environ.get("DATA_DIR", "/data")
INPUT_DIR = os.environ.get("INPUT_DIR", "/input")
REPO = os.environ.get("REPO", "/ws/src/point_lio")

BAGS = {
    "avia-quick-shack": ("avia/2020-09-16-quick-shack.bag",
                         "05a56e75898f952766f136d1e5db64a35d202e990e3b1052558369d8384d7ffe",
                         {"imu": 9953, "lidar": 491}),
    "ouster-nc-quad-easy": ("newer_college/2021-07-01-10-37-38-quad-easy-002.bag",
                            "64e4d2cdaffc652c4c536e9718a24999d0dd24ab71d255738a449d9683fc4215",
                            {"imu": 19902, "lidar": 1991}),
}
KIND_TYPE = {1: "sensor_msgs/Imu", 2: "sensor_msgs/PointCloud2", 3: "livox_ros_driver/CustomMsg"}
HEAD = struct.Struct("<B3xIIIII")


def input_path(bag_id):
    return os.path.join(INPUT_DIR, bag_id + ".plin")


def read_records(path):
    """仕様どおりにレコードを読む。(kind, bag_sec, bag_nsec, st_sec, st_nsec, payload) を返す。"""
    with open(path, "rb") as f:
        assert f.read(8) == b"PLIOIN01", "bad magic"
        ver, rsv = struct.unpack("<II", f.read(8))
        assert (ver, rsv) == (1, 0)
        while True:
            h = f.read(HEAD.size)
            if not h:
                return
            assert len(h) == HEAD.size, "truncated record header"
            kind, bs, bn, ss, sn, n = HEAD.unpack(h)
            payload = f.read(n)
            assert len(payload) == n, "truncated payload"
            yield kind, bs, bn, ss, sn, payload


def topics(bag_id):
    with open(os.path.join(REPO, "eval/config/canonical", bag_id + ".yaml")) as f:
        c = yaml.safe_load(f)
    return c["common"]["lid_topic"], c["common"]["imu_topic"]


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for b in iter(lambda: f.read(1 << 24), b""):
            h.update(b)
    return h.hexdigest()


class ExtractTest(unittest.TestCase):
    def check_bag(self, bag_id):
        rel, sha, _ = BAGS[bag_id]
        bag = os.path.join(DATA_DIR, rel)
        self.assertEqual(sha256(bag), sha, "bag SHA256 mismatch")
        self.assertTrue(os.path.isfile(input_path(bag_id)), "input file missing: " + input_path(bag_id))
        return bag

    def test_t1_counts(self):
        for bag_id, (_, _, want) in BAGS.items():
            with self.subTest(bag=bag_id):
                self.check_bag(bag_id)
                got = {"imu": 0, "lidar": 0}
                for kind, *_ in read_records(input_path(bag_id)):
                    got["imu" if kind == 1 else "lidar"] += 1
                self.assertEqual(got, want)

    def test_t2_t12_matches_bag(self):
        """全レコードのペイロードと時刻を bag と照合し、先頭・末尾・等間隔の各件は復元した値でも照合する。"""
        for bag_id in BAGS:
            with self.subTest(bag=bag_id):
                bag = self.check_bag(bag_id)
                lid, imu = topics(bag_id)
                with rosbag.Bag(bag) as b:
                    total = b.get_message_count(topic_filters=[lid, imu])
                    sample = set(range(50)) | set(range(total - 50, total)) | set(range(0, total, max(1, total // 200)))
                    it = b.read_messages(topics=[lid, imu], raw=True)
                    n = deep = 0
                    for rec, (topic, raw, t) in zip(read_records(input_path(bag_id)), it):
                        kind, bs, bn, ss, sn, payload = rec
                        datatype, data, md5, _, pytype = raw
                        self.assertEqual(KIND_TYPE[kind], datatype, "kind order differs at %d" % n)
                        self.assertEqual((bs, bn), (t.secs, t.nsecs), "bag time differs at %d" % n)
                        self.assertEqual(payload, bytes(data), "payload differs at %d" % n)
                        if n in sample:
                            got, want = pytype(), pytype()
                            got.deserialize(payload)
                            want.deserialize(bytes(data))
                            self.assertEqual(got, want)
                            self.assertEqual((ss, sn), (want.header.stamp.secs, want.header.stamp.nsecs))
                            deep += 1
                        n += 1
                    self.assertEqual(n, total, "record count differs from bag")
                    self.assertEqual(sum(1 for _ in read_records(input_path(bag_id))), total)
                    self.assertGreater(deep, 200)

    def test_t3_deterministic(self):
        for bag_id in BAGS:
            with self.subTest(bag=bag_id):
                bag = self.check_bag(bag_id)
                again = input_path(bag_id) + ".t3"
                try:
                    subprocess.run(["python3", os.path.join(REPO, "eval/extract_input.py"), bag,
                                    os.path.join(REPO, "eval/config/canonical", bag_id + ".yaml"), again],
                                   check=True, stdout=subprocess.DEVNULL)
                    self.assertEqual(sha256(again), sha256(input_path(bag_id)))
                finally:
                    if os.path.exists(again):
                        os.remove(again)

    def test_t4_stamp_monotonic(self):
        for bag_id in BAGS:
            with self.subTest(bag=bag_id):
                self.check_bag(bag_id)
                last, count = {}, {}
                for kind, _, _, ss, sn, _ in read_records(input_path(bag_id)):
                    t = ss * 10**9 + sn
                    self.assertGreaterEqual(t, last.get(kind, -1), "header stamp goes back (kind %d)" % kind)
                    last[kind] = t
                    count[kind] = count.get(kind, 0) + 1
                self.assertEqual(len(count), 2)
                self.assertTrue(all(v > 0 for v in count.values()))


if __name__ == "__main__":
    unittest.main()
