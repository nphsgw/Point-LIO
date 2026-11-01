#!/usr/bin/env bash
# Generate candidate outputs in Docker, then use the unchanged Python comparator.
# Usage: [POINTLIO_MATH_STUB=real|nan|zero] rust/run_math_golden.sh <new output directory>
# Optional POINTLIO_MATH_COMPONENT selects a partial comparison, explicitly logged.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
[[ $# = 1 ]] || { echo "usage: $0 <new output directory>" >&2; exit 2; }
output=$(realpath -m -- "$1")
case "$output" in "$repo"|"$repo/"*) echo 'output must be outside the repository' >&2; exit 2 ;; esac
[[ ! -e "$output" ]] || { echo 'output directory already exists' >&2; exit 2; }
stub=${POINTLIO_MATH_STUB:-real}
component=${POINTLIO_MATH_COMPONENT:-}
[[ "$stub" = real || "$stub" = nan || "$stub" = zero ]] || { echo 'POINTLIO_MATH_STUB must be real, nan or zero' >&2; exit 2; }
(cd "$repo/golden/math" && sha256sum --quiet -c SHA256SUMS)
image=$(docker image inspect point-lio-rust --format '{{.Id}}')
mkdir -p -- "$output"
python3 --version > "$output/python-version.txt"
printf '%s\n' "$image" > "$output/image-id.txt"
printf '%s\n' "$stub" > "$output/stub.txt"
printf '%s\n' "${component:-all}" > "$output/component.txt"
git -C "$repo" rev-parse HEAD > "$output/source-commit.txt"
git -C "$repo" diff --binary > "$output/source-diff.patch"
(
    cd "$repo"
    git ls-files -co --exclude-standard -z -- rust |
        while IFS= read -r -d '' source; do
            case "$source" in
                *.rs|*.toml|*.lock|*.sh) printf '%s\0' "$source" ;;
            esac
        done |
        LC_ALL=C sort -zu | xargs -0 -r sha256sum --
) > "$output/source-SHA256SUMS"
docker run --rm --network none --user "$(id -u):$(id -g)" \
    -e CARGO_TARGET_DIR=/tmp/pointlio-target -e OMP_NUM_THREADS=1 \
    -e POINTLIO_MATH_STUB="$stub" -e POINTLIO_MATH_COMPONENT="$component" \
    -v "$repo":/ws/src/point_lio:ro -v "$output":/results \
    -w /ws/src/point_lio/rust "$image" bash -c '
    set -euo pipefail
    rustc -vV > /results/rustc.txt
    cargo build --release --locked --offline --bin math_golden > /results/build.log 2>&1 || { cat /results/build.log; exit 1; }
    "$CARGO_TARGET_DIR/release/math_golden" /ws/src/point_lio/golden/math /results > /results/generate.log 2>&1
    '
compare() {
    if [[ -z "$component" ]]; then
        python3 -B "$repo/tests/math/compare_math_golden.py" "$repo/golden/math" "$output"
    else
        # Same registered comparison function, with an explicit partial scope.
        python3 -B - "$repo" "$output" "$component" <<'PYTHON'
import sys
from pathlib import Path
sys.path.insert(0, str(Path(sys.argv[1]) / "tests/math"))
from compare_math_golden import SCHEMA, verify_manifest, compare_component
baseline = Path(sys.argv[1]) / "golden/math"
name = sys.argv[3]
try:
    if name not in SCHEMA:
        raise ValueError("unknown component: " + name)
    verify_manifest(baseline)
    print("PARTIAL comparison: " + name + "; other components are not judged", flush=True)
    result = int(compare_component(name, baseline, Path(sys.argv[2])) > 0)
except (OSError, ValueError) as error:
    print("ERROR: " + str(error), file=sys.stderr)
    result = 2
sys.exit(result)
PYTHON
    fi
}
if compare > "$output/comparison.log" 2>&1; then
    status=0
else
    status=$?
fi
printf '%s\n' "$status" > "$output/comparison-exit.txt"
cat "$output/comparison.log"
exit "$status"
