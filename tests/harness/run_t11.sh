#!/usr/bin/env bash
# T11 設定の同値: ROS ノード(評価用の起動設定)が readParameters 後に持つ値と、pointlio_offline が正準設定から読んだ値を、全キーで比べる。
# 使い方: tests/harness/run_t11.sh <avia-quick-shack|ouster-nc-quad-easy> <pointlio_offline の出力ディレクトリ(params.txt がある)>
set -euo pipefail

id=${1:?bag id}; offline=${2:?offline out dir}
repo=$(cd "$(dirname "$0")/../.." && pwd)
case $id in
  avia-quick-shack) launch=run_avia.launch ;;
  ouster-nc-quad-easy) launch=run_ouster_nc.launch ;;
  *) echo "unknown bag id: $id" >&2; exit 2 ;;
esac
test -s "$offline/params.txt" || { echo "FAIL: $offline/params.txt missing"; exit 1; }
tmp=$(mktemp -d)

docker run --rm -e LAUNCH="$launch" -v "$repo":/ws/src/point_lio:ro -v "$tmp":/out point-lio-noetic bash -c '
    set -eo pipefail
    . /opt/ros/noetic/setup.sh
    cd /ws
    . /ws/devel/setup.sh
    catkin_make --cmake-args -DCATKIN_WHITELIST_PACKAGES="" > /dev/null 2>&1
    . /ws/devel/setup.sh
    roscore > /dev/null 2>&1 &
    until rostopic list > /dev/null 2>&1; do sleep 0.5; done
    POINTLIO_DUMP_PARAMS=/out/params_ros.txt roslaunch /ws/src/point_lio/eval/launch/$LAUNCH > /dev/null 2>&1 &
    until rosnode list 2>/dev/null | grep -q laserMapping && [ -s /out/params_ros.txt ]; do sleep 0.5; done
    sleep 1
    pkill -INT -f roslaunch || true
  '
n_ros=$(wc -l < "$tmp/params_ros.txt"); n_off=$(wc -l < "$offline/params.txt")
# 期待するキー数は dump_parameters が並べる変数の数
want=$(grep -o 'DUMP(' "$repo/src/parameters.cpp" | wc -l); want=$((want - 1))  # マクロ定義の1つを除く
echo "keys: ros=$n_ros offline=$n_off expected=$want"
if [ "$n_ros" -eq "$want" ] && [ "$n_off" -eq "$want" ] && diff "$tmp/params_ros.txt" "$offline/params.txt"; then
  echo "PASS T11 $id"
else
  echo "FAIL T11 $id"; exit 1
fi
