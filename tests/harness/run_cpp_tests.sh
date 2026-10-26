#!/usr/bin/env bash
# ハーネスの C++ テスト(T5・T6・T13)を Docker 内でビルドして実行する。
# 使い方: tests/harness/run_cpp_tests.sh [t5|t6|t13 ...](既定はすべて)
# 環境変数: DATA_DIR(既定 ~/data/slamrun)、INPUT_DIR(既定 ~/data/slamrun-input)、MUTATE(harness_tests に渡す)
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
data=${DATA_DIR:-$HOME/data/slamrun}; input=${INPUT_DIR:-$HOME/data/slamrun-input}
tests=${*:-t13 t5 t6}

docker run --rm -e TESTS="$tests" -e MUTATE="${MUTATE:-}" \
  -v "$repo":/ws/src/point_lio:ro -v "$data":/data:ro -v "$input":/input:ro \
  point-lio-noetic bash -c '
    set -eo pipefail
    [ -z "$MUTATE" ] && unset MUTATE
    . /opt/ros/noetic/setup.sh
    cd /ws
    . /ws/devel/setup.sh
    catkin_make --cmake-args -DCATKIN_WHITELIST_PACKAGES="" > /tmp/build.log 2>&1 || { tail -30 /tmp/build.log; exit 1; }
    bin=/ws/devel/lib/point_lio/harness_tests
    test -x $bin || { echo "FAIL: harness_tests was not built (rosbag not found?)"; exit 1; }
    cfg=/ws/src/point_lio/eval/config/canonical
    status=0
    for t in $TESTS; do
      if [ $t = t13 ]; then
        d=$(mktemp -d); $bin t13 $d || status=1
      else
        $bin $t /input/avia-quick-shack.plin /data/avia/2020-09-16-quick-shack.bag $cfg/avia-quick-shack.yaml || status=1
        $bin $t /input/ouster-nc-quad-easy.plin /data/newer_college/2021-07-01-10-37-38-quad-easy-002.bag \
          $cfg/ouster-nc-quad-easy.yaml || status=1
      fi
    done
    [ $status = 0 ] && echo "ALL PASSED" || echo "SOME FAILED"
    exit $status
  '
