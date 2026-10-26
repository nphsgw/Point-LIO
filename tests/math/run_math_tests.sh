#!/usr/bin/env bash
# 数学と状態の表現の L1 テスト(tests/math/math_tests.cpp)を Docker 内でビルドして実行する。
# 使い方: tests/math/run_math_tests.sh [gtest の引数 ...](例: --gtest_filter='So3Log.*')
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)

docker run --rm -v "$repo":/ws/src/point_lio:ro point-lio-noetic bash -c '
    set -eo pipefail
    . /opt/ros/noetic/setup.sh
    cd /ws
    . /ws/devel/setup.sh
    catkin_make --cmake-args -DCATKIN_WHITELIST_PACKAGES="" > /tmp/build.log 2>&1 || { tail -30 /tmp/build.log; exit 1; }
    bin=/ws/devel/lib/point_lio/math_tests
    test -x $bin || { echo "FAIL: math_tests was not built (GTest not found?)"; exit 1; }
    $bin "$@"
  ' bash "$@"
