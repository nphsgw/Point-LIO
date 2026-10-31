#!/usr/bin/env bash
# Build the Rust environment without rebuilding or updating the C++ base.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
expected=sha256:4ffccd043d071a088d7b09e80eb78cbef3b61d777e2b4f146768bac3def2dead
actual=$(docker image inspect point-lio-noetic:pinned --format '{{.Id}}')
[[ "$actual" = "$expected" ]] || {
    echo "FAIL: base image $actual (expected $expected)" >&2
    exit 1
}
bash "$repo/docker/check_packages.sh" point-lio-noetic:pinned
docker build --pull=false -t point-lio-rust -f "$repo/docker/Dockerfile.rust" "$repo/docker"
docker image inspect point-lio-rust --format '{{.Id}}'
