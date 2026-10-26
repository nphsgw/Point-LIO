#!/usr/bin/env bash
# pointlio_offline(ファイル駆動、ROS 非依存)を入力ファイル1本に対して実行する。
# 出力: traj.tum、odom.bin、cloud.bin、cloud_frames.txt(形式は src/offline_output.h)、params.txt、meta.txt、git_diff.patch、git_status.txt、build.log、run.log
# 使い方: eval/run_offline.sh <avia-quick-shack|ouster-nc-quad-easy> <出力ディレクトリ> [イメージ]
# 入力ファイルは eval/extract_input.py で作ったもの(INPUT_DIR、既定 ~/data/slamrun-input)。SHA256 が一致しなければ中止する。
# 環境変数: INPUT_DIR、EXTRA_CXX_FLAGS(run_cpp.sh と同じ。CMAKE_CXX_FLAGS_DEBUG に渡す)、
#           OMP_NUM_THREADS(run_cpp.sh と同じ。設定したときだけコンテナに渡す)、
#           INPUT_FILE(既定の入力ファイルの代わりに使う。SHA256 は検査しない。テストが落ちることの確認用)
set -euo pipefail

id=${1:?bag id}; out=${2:?out dir}; image=${3:-point-lio-noetic:latest}
repo=$(cd "$(dirname "$0")/.." && pwd)
input_dir=${INPUT_DIR:-$HOME/data/slamrun-input}

# docs/evaluation-data.md の入力ファイルの値
case $id in
  avia-quick-shack) sha=cf90642183710ed725a3402296796eceec53088d77b362da2c38b2b1a404ac29 ;;
  ouster-nc-quad-easy) sha=571d5063f5121b7e745f1f8843a92a01a735734a1be7eafaa641638db0cd5a59 ;;
  *) echo "unknown bag id: $id" >&2; exit 2 ;;
esac

if [ -n "${INPUT_FILE:-}" ]; then
  input=$(cd "$(dirname "$INPUT_FILE")" && pwd)/$(basename "$INPUT_FILE"); sha=unchecked
else
  input=$input_dir/$id.plin
  echo "$sha  $input" | sha256sum -c - >&2 || { echo "SHA256 mismatch; abort" >&2; exit 1; }
fi
mkdir -p "$out"; out=$(cd "$out" && pwd)

{
  echo "bag_id=$id"; echo "input=$input"; echo "input_sha256=$sha"
  echo "image=$image"; echo "image_id=$(docker image inspect -f '{{.Id}}' "$image")"
  echo "git_head=$(git -C "$repo" rev-parse HEAD)"
  echo "git_dirty=$(git -C "$repo" status --porcelain | wc -l)"
  echo "extra_cxx_flags=${EXTRA_CXX_FLAGS:-}"
  echo "omp_num_threads=${OMP_NUM_THREADS:-unset}"
  echo "cpu=$(LC_ALL=C lscpu | sed -n 's/^Model name: *//p')"
} > "$out/meta.txt"
# git_dirty は変更のあるファイルの数。差分そのもの(未追跡のファイルは一覧だけ)を残す
git -C "$repo" diff HEAD > "$out/git_diff.patch"
git -C "$repo" status --porcelain > "$out/git_status.txt"

omp=()
[ -n "${OMP_NUM_THREADS:-}" ] && omp=(-e OMP_NUM_THREADS="$OMP_NUM_THREADS")

docker run --rm -e ID="$id" -e EXTRA_CXX_FLAGS="${EXTRA_CXX_FLAGS:-}" "${omp[@]}" \
  -v "$repo":/ws/src/point_lio:ro -v "$input":/input.plin:ro -v "$out":/out \
  "$image" bash -c '
    set -eo pipefail
    . /opt/ros/noetic/setup.sh
    cd /ws
    echo "compiler=$(c++ --version | head -1)" >> /out/meta.txt
    . /ws/devel/setup.sh
    extra=()
    [ -n "$EXTRA_CXX_FLAGS" ] && extra=(-DCMAKE_CXX_FLAGS_DEBUG="$EXTRA_CXX_FLAGS" -DCMAKE_VERBOSE_MAKEFILE=ON)
    catkin_make --cmake-args -DCATKIN_WHITELIST_PACKAGES="" "${extra[@]}" > /out/build.log 2>&1
    /ws/devel/lib/point_lio/pointlio_offline /input.plin /ws/src/point_lio/eval/config/canonical/$ID.yaml /out \
      --dump-params /out/params.txt > /out/run.log 2>&1
  '
tail -1 "$out/run.log"
