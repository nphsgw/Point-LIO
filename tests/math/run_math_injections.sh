#!/usr/bin/env bash
# Run B and six injections against the same comparator used later for C.
# Usage: bash tests/math/run_math_injections.sh /absolute/scratch/output
# f32_round substitutes output rounding for f32 computation (approved 2026-10-31).
set -euo pipefail
repo=$(cd "$(dirname "$0")/../.." && pwd)
output=${1:?absolute scratch output directory required}
[[ "$output" = /* ]] || { echo 'absolute output path required' >&2; exit 2; }
# Resolve symlinks before checking, and require a new directory to avoid overwrites.
output=$(realpath -m "$output")
case "$output" in "$repo/golden"|"$repo/golden/"*) echo 'golden/ is forbidden' >&2; exit 2 ;; esac
[[ ! -e "$output" ]] || { echo 'output directory already exists' >&2; exit 2; }
(cd "$repo/golden/math" && sha256sum --quiet -c SHA256SUMS)
bash "$repo/docker/check_packages.sh" point-lio-noetic:pinned
mkdir -p "$output"
python3 -B "$repo/tests/math/prepare_math_injections.py" "$repo/tests/math/math_golden.cpp" "$output/math_golden.cpp"
docker image inspect point-lio-noetic:pinned --format '{{.Id}}' > "$output/image-id.txt"
git -C "$repo" rev-parse HEAD > "$output/source-commit.txt"
git -C "$repo" diff --binary > "$output/source-diff.patch"
docker run --rm -e OMP_NUM_THREADS=1 -e OWNER="$(id -u):$(id -g)" \
    -v "$repo":/ws/src/point_lio:ro -v "$output":/results \
    -v "$output/math_golden.cpp":/ws/src/point_lio/tests/math/math_golden.cpp:ro \
    point-lio-noetic:pinned bash -c '
    set -eo pipefail
    . /opt/ros/noetic/setup.sh
    . /ws/devel/setup.sh
    cd /ws
    catkin_make --cmake-args -DCATKIN_WHITELIST_PACKAGES="" -DCMAKE_VERBOSE_MAKEFILE=ON > /results/build.log 2>&1 || { tail -40 /results/build.log; exit 1; }
    for mode in baseline perturb f32_round left_boxplus quat_order jacobian dt_shift; do
        mkdir /results/$mode
        PL_MATH_INJECTION=$mode /ws/devel/lib/point_lio/math_golden write /results/$mode > /results/$mode/generate.log
    done
    chown -R "$OWNER" /results
    '
python3 -B "$repo/tests/math/check_math_injections.py" "$repo/golden/math" "$output" | tee "$output/comparison.log"
