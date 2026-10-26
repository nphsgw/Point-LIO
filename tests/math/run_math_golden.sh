#!/usr/bin/env bash
# 数学と状態の表現の L2 ゴールデン出力(golden/math)を Docker 内で照合する。書き出しは人の承認を得てから行う。
# 使い方: tests/math/run_math_golden.sh check|write
set -euo pipefail

mode=${1:?check|write}
repo=$(cd "$(dirname "$0")/../.." && pwd)
golden=$repo/golden/math
case $mode in
  check) mount="$golden:/golden:ro"; (cd "$golden" && sha256sum -c --quiet SHA256SUMS) ;;
  write) mkdir -p "$golden"; mount="$golden:/golden" ;;
  *) echo "usage: $0 check|write" >&2; exit 2 ;;
esac

docker run --rm -e MODE="$mode" -e OWNER="$(id -u):$(id -g)" -v "$repo":/ws/src/point_lio:ro -v "$mount" point-lio-noetic bash -c '
    set -eo pipefail
    . /opt/ros/noetic/setup.sh
    cd /ws
    . /ws/devel/setup.sh
    catkin_make --cmake-args -DCATKIN_WHITELIST_PACKAGES="" > /tmp/build.log 2>&1 || { tail -30 /tmp/build.log; exit 1; }
    bin=/ws/devel/lib/point_lio/math_golden
    test -x $bin || { echo "FAIL: math_golden was not built"; exit 1; }
    $bin $MODE /golden
    [ "$MODE" = write ] && chown "$OWNER" /golden/*.bin || true
  '
if [ "$mode" = write ]; then (cd "$golden" && sha256sum *.bin > SHA256SUMS); fi
