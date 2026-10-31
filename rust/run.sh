#!/usr/bin/env bash
# Pass arguments to Cargo in the pinned Rust environment. No host Cargo is used.
# Example: rust/run.sh build --release --locked --offline
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
[[ $# -gt 0 ]] || { echo "usage: $0 <cargo arguments ...>" >&2; exit 2; }
docker run --rm --network none --user "$(id -u):$(id -g)" \
    -e CARGO_TARGET_DIR=/tmp/pointlio-target \
    -v "$repo":/ws/src/point_lio:ro \
    -w /ws/src/point_lio/rust point-lio-rust cargo "$@"
