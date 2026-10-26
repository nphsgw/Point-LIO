#!/usr/bin/env bash
# 評価用の起動設定で laserMapping を起動し、パラメータサーバの /laserMapping を YAML に書き出す(正準設定)。
# bag は再生しない。launch のインライン <param> と YAML の両方が反映された値を得るため。
# 使い方: eval/dump_params.sh <avia-quick-shack|ouster-nc-quad-easy> <出力 YAML> [イメージ]
# 毎回 catkin_make する(イメージの /ws/devel には本体が入っていない)。パラメータはソースに依存しない。
set -euo pipefail

id=${1:?bag id}; out=${2:?out yaml}; image=${3:-point-lio-noetic:latest}
repo=$(cd "$(dirname "$0")/.." && pwd)

case $id in
  avia-quick-shack) launch=run_avia.launch ;;
  ouster-nc-quad-easy) launch=run_ouster_nc.launch ;;
  *) echo "unknown bag id: $id" >&2; exit 2 ;;
esac

outdir=$(cd "$(dirname "$out")" && pwd); name=$(basename "$out")
docker run --rm -e LAUNCH="$launch" -e NAME="$name" \
  -v "$repo":/ws/src/point_lio:ro -v "$outdir":/out \
  "$image" bash -c '
    set -eo pipefail
    . /opt/ros/noetic/setup.sh
    cd /ws
    . /ws/devel/setup.sh
    catkin_make --cmake-args -DCATKIN_WHITELIST_PACKAGES="" > /dev/null 2>&1
    . /ws/devel/setup.sh
    roscore > /dev/null 2>&1 &
    until rostopic list > /dev/null 2>&1; do sleep 0.5; done
    roslaunch /ws/src/point_lio/eval/launch/$LAUNCH > /dev/null 2>&1 &
    until rosnode list 2>/dev/null | grep -q laserMapping; do sleep 0.5; done
    rosparam dump /out/$NAME /laserMapping
    pkill -INT -f roslaunch || true
  '
echo "wrote $out"
