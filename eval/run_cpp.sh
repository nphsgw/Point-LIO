#!/usr/bin/env bash
# C++ 版(A/B)を bag 1本に対して実行し、姿勢列を TUM 形式で出力する。動作確認・軌跡取得用。
# 使い方: eval/run_cpp.sh <avia-quick-shack|ouster-nc-quad-easy> <出力ディレクトリ> [イメージ]
# 環境変数: DATA_DIR(既定 ~/data/slamrun)、NO_BUILD=1(イメージ内の /ws/devel を使い catkin_make を省く。
#           ビルド済みイメージ用)、MP_PROC_NUM(スレッド数。未指定なら既定値)
# 再生は実時間(rosbag play)。性能の数値には使わない(docs/metrics-preregistration.md)。
set -euo pipefail

id=${1:?bag id}; out=${2:?out dir}; image=${3:-point-lio-noetic:latest}
repo=$(cd "$(dirname "$0")/.." && pwd)
data=${DATA_DIR:-$HOME/data/slamrun}

# docs/evaluation-data.md と同じ値。一致しない bag では実行しない。
case $id in
  avia-quick-shack)
    bag=avia/2020-09-16-quick-shack.bag; launch=run_avia.launch
    sha=05a56e75898f952766f136d1e5db64a35d202e990e3b1052558369d8384d7ffe ;;
  ouster-nc-quad-easy)
    bag=newer_college/2021-07-01-10-37-38-quad-easy-002.bag; launch=run_ouster_nc.launch
    sha=64e4d2cdaffc652c4c536e9718a24999d0dd24ab71d255738a449d9683fc4215 ;;
  *) echo "unknown bag id: $id" >&2; exit 2 ;;
esac

echo "$sha  $data/$bag" | sha256sum -c - >&2 || { echo "SHA256 mismatch; abort" >&2; exit 1; }
mkdir -p "$out"; out=$(cd "$out" && pwd)

{
  echo "bag_id=$id"; echo "bag_sha256=$sha"
  echo "image=$image"; echo "image_id=$(docker image inspect -f '{{.Id}}' "$image")"
  echo "git_head=$(git -C "$repo" rev-parse HEAD)"
  echo "git_dirty=$(git -C "$repo" status --porcelain | wc -l)"
  echo "mp_proc_num=${MP_PROC_NUM:-default}"; echo "no_build=${NO_BUILD:-0}"
} > "$out/meta.txt"

docker run --rm \
  -e ID="$id" -e BAG="/data/$bag" -e LAUNCH="$launch" -e NO_BUILD="${NO_BUILD:-0}" \
  ${MP_PROC_NUM:+-e MP_PROC_NUM="$MP_PROC_NUM"} \
  -v "$repo":/ws/src/point_lio:ro -v "$data":/data:ro -v "$out":/out \
  "$image" bash -c '
    set -eo pipefail
    . /opt/ros/noetic/setup.sh
    cd /ws
    if [ "$NO_BUILD" != 1 ]; then
      . /ws/devel/setup.sh
      # イメージの /ws/build は livox_ros_driver だけのホワイトリストを持つので、空にしないと本体が建たない
      catkin_make --cmake-args -DCATKIN_WHITELIST_PACKAGES="" > /out/build.log 2>&1
    fi
    . /ws/devel/setup.sh
    test -x /ws/devel/lib/point_lio/pointlio_mapping
    roscore > /out/roscore.log 2>&1 &
    until rostopic list > /dev/null 2>&1; do sleep 0.5; done
    roslaunch /ws/src/point_lio/eval/launch/$LAUNCH > /out/node.log 2>&1 &
    until rosnode list 2>/dev/null | grep -q laserMapping; do sleep 0.5; done
    sleep 3
    rosbag record -q -O /out/odom.bag /aft_mapped_to_init > /dev/null 2>&1 &
    sleep 2
    rosbag play -q "$BAG"
    sleep 5
    pkill -INT -f "rosbag record"; sleep 2
    pkill -INT -f "roslaunch" || true; sleep 2
    python3 /ws/src/point_lio/eval/odom_to_tum.py /out/odom.bag /aft_mapped_to_init /out/traj.tum
  '
echo "done: $out/traj.tum"
