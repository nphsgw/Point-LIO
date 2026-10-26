#!/usr/bin/env bash
# 抽出器のテスト(T1〜T4、T12 の種別の列)を Docker 内で実行する。
# 使い方: tests/harness/run_extract_tests.sh [テスト名 ...(例: ExtractTest.test_t1_counts)]
# 環境変数: DATA_DIR(既定 ~/data/slamrun)、INPUT_DIR(既定 ~/data/slamrun-input)
set -euo pipefail

repo=$(cd "$(dirname "$0")/../.." && pwd)
data=${DATA_DIR:-$HOME/data/slamrun}; input=${INPUT_DIR:-$HOME/data/slamrun-input}
mkdir -p "$input"
tests=test_extract
[ $# -gt 0 ] && tests=$(printf 'test_extract.%s ' "$@")

docker run --rm -e TESTS="$tests" -v "$repo":/ws/src/point_lio:ro -v "$data":/data:ro -v "$input":/input \
  point-lio-noetic bash -c '. /opt/ros/noetic/setup.sh && . /ws/devel/setup.sh &&
    cd /ws/src/point_lio/tests/harness && python3 -m unittest -v $TESTS'
