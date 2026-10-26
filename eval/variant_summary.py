#!/usr/bin/env python3
"""演算順の変種(事前登録「演算順の変種と感度分析」の1〜5)をまとめる。作業用 venv(evo・rosbags・scipy)で実行する。

入力は eval/run_offline.sh の出力を <根>/<変種>/<bag>/run<NN> に並べたもの(変種は V0〜V4)。
- 1: V0 の各指標の最小・最大(V0 の各実行の、中央値実行に対する値)
- 2: V0 の中央値実行(他の実行との ATE の中央値が最小の1回)
- 3: 各変種の実行が互いにビット一致するか。一致しない変種は全実行を組に含める
- 4: 変種どうしの全組の ATE・RPE・Chamfer の最大値(τ。感度分析で、合否には使わない)
- 5: 正解があれば、各変種の代表実行の正解に対する ATE と、その最大・最小
指標の定義は tests/harness/check_runs.py と同じ(ATE は SE(3) 整列・スケールなし、RPE は 10 m と 10 フレーム。
evo は秒単位の区間を持たないので、事前登録の「1 s」の代わりに 10 フレームを使う)。
使い方: variant_summary.py <bag> <根> [正解の TUM]
"""
import copy
import glob
import itertools
import os
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "../tests/harness"))
import check_runs  # noqa: E402
from evo.core import metrics, sync  # noqa: E402
from evo.tools import file_interface  # noqa: E402

KEYS = ("ATE", "RPE10m_t", "RPE10m_r", "RPE10f_t", "RPE10f_r")


def chamfer(a, b):
    out = subprocess.run([sys.executable, "-I", os.path.join(HERE, "chamfer.py"), os.path.join(a, "cloud.bin"),
                          os.path.join(b, "cloud.bin")], check=True, capture_output=True, text=True).stdout.split()
    return float(out[-1])


def gt_ate(gt, d):
    r, e = sync.associate_trajectories(copy.deepcopy(gt), check_runs.traj(d), max_diff=0.01)
    e.align(r, correct_scale=False)
    m = metrics.APE(metrics.PoseRelation.translation_part)
    m.process_data((r, e))
    return m.get_statistic(metrics.StatisticsType.rmse)


def main():
    bag, root = sys.argv[1], sys.argv[2]
    gt = file_interface.read_tum_trajectory_file(sys.argv[3]) if len(sys.argv) > 3 else None
    runs = {v: sorted(glob.glob(os.path.join(root, v, bag, "run*"))) for v in ("V0", "V1", "V2", "V3", "V4")}
    # 各実行の C++ のソース(src/・include/)が A-v1 と同じであることを確かめる。
    # git_head の木を比べ、作業ツリーの差分(git_status.txt)がそこに触れていないことを見る
    repo = os.path.join(HERE, "..")
    tree = lambda rev, path: subprocess.run(["git", "-C", repo, "rev-parse", "%s:%s" % (rev, path)], check=True,
                                            capture_output=True, text=True).stdout.strip()
    states = {}
    for v, rs in runs.items():
        if not rs:
            sys.exit("no runs for %s" % v)
        for d in rs:
            meta = dict(l.split("=", 1) for l in open(os.path.join(d, "meta.txt")).read().splitlines() if "=" in l)
            head = meta["git_head"]
            for path in ("src", "include"):
                if tree(head, path) != tree("A-v1", path):
                    sys.exit("%s: %s differs from A-v1 at %s" % (d, path, head))
            status = open(os.path.join(d, "git_status.txt")).read().split("\n")
            if any(l[3:].startswith(("src/", "include/")) for l in status if l):
                sys.exit("%s: working tree changes src/ or include/" % d)
            states[(head[:7], meta["git_dirty"])] = states.get((head[:7], meta["git_dirty"]), 0) + 1

    print("## %s\n" % bag)
    print("実行時の git の状態(C++ のソースはすべて A-v1 と同一): " +
          "、".join("%s・git_dirty=%s が %d 回" % (h, dd, n) for (h, dd), n in sorted(states.items())) + "\n")
    print("| 変種 | 実行数 | 姿勢数 | ビット一致 | extra_cxx_flags |")
    print("| --- | --- | --- | --- | --- |")
    hashes = {}
    for v, rs in runs.items():
        hs = [check_runs.odom_records(d)[1] for d in rs]
        hashes[v] = hs
        n = len(check_runs.odom_records(rs[0])[0])
        flags = [l.split("=", 1)[1] for l in open(os.path.join(rs[0], "meta.txt")).read().splitlines()
                 if l.startswith("extra_cxx_flags=")][0]
        print("| %s | %d | %d | %s | `%s` |" % (v, len(rs), n, "はい" if len(set(hs)) == 1 else "いいえ(%d 種)" % len(set(hs)),
                                               flags or "(既定)"))

    # 2: V0 の中央値実行
    v0 = runs["V0"]
    med = {a: statistics.median(check_runs.metrics_of(check_runs.traj(a), check_runs.traj(b))["ATE"]
                                for b in v0 if b != a) for a in v0}
    median_run = min(v0, key=lambda a: (med[a], a))
    print("\nV0 の中央値実行: `%s`(他の実行との ATE の中央値 %.3e m)" % (os.path.relpath(median_run, root), med[median_run]))

    # 1: V0 の各指標の範囲(中央値実行に対する値)
    v0m = [check_runs.metrics_of(check_runs.traj(median_run), check_runs.traj(d)) for d in v0 if d != median_run]
    print("\nV0 の各実行の、中央値実行に対する値の範囲: " +
          "、".join("%s %.3e〜%.3e" % (k, min(m[k] for m in v0m), max(m[k] for m in v0m)) for k in KEYS))

    # 3・4: 代表実行と変種どうしの組
    reps = {"V0": [median_run]}
    for v in ("V1", "V2", "V3", "V4"):
        reps[v] = runs[v][:1] if len(set(hashes[v])) == 1 else runs[v]
    print("\n| 組 | ATE [m] | RPE 10 m 並進 [m] | RPE 10 m 回転 [deg] | RPE 10 f 並進 [m] | RPE 10 f 回転 [deg] | Chamfer [m] |")
    print("| --- | --- | --- | --- | --- | --- | --- |")
    tau = {k: 0.0 for k in KEYS + ("chamfer",)}
    for va, vb in itertools.combinations(reps, 2):
        for a in reps[va]:
            for b in reps[vb]:
                m = check_runs.metrics_of(check_runs.traj(a), check_runs.traj(b))
                m["chamfer"] = chamfer(a, b)
                for k in tau:
                    tau[k] = max(tau[k], m[k])
                print("| %s–%s | %s |" % (va, vb, " | ".join("%.3e" % m[k] for k in KEYS + ("chamfer",))))
    print("| **τ(最大値)** | %s |" % " | ".join("**%.3e**" % tau[k] for k in KEYS + ("chamfer",)))

    # 5: 正解に対する ATE
    if gt is not None:
        print("\n| 変種 | 正解に対する ATE [m] |\n| --- | --- |")
        vals = []
        for v, ds in reps.items():
            for d in ds:
                x = gt_ate(gt, d)
                vals.append(x)
                print("| %s | %.6f |" % (v, x))
        print("\n正解に対する ATE: 最大 %.6f m、最小 %.6f m、幅 %.6f m(A の値。事前登録の Δ_gt は B から求める)"
              % (max(vals), min(vals), max(vals) - min(vals)))


if __name__ == "__main__":
    main()
