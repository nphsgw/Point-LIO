#!/usr/bin/env bash
# イメージの全パッケージの版と livox_ros_driver のコミットを、docker/packages.lock と Dockerfile の値に照合する。
# 使い方: docker/check_packages.sh [イメージ](既定 point-lio-noetic:latest)。一致しなければ差分を出して終了コード 1。
set -euo pipefail

image=${1:-point-lio-noetic:latest}
here=$(cd "$(dirname "$0")" && pwd)
livox=3d240d5666129e1a3052e78ee8487a04b08fdda3

diff <(docker run --rm "$image" dpkg-query -W -f='${Package}=${Version}\n' | sort) "$here/packages.lock"
got=$(docker run --rm "$image" git -C /ws/src/livox_ros_driver rev-parse HEAD)
[ "$got" = "$livox" ] || { echo "livox_ros_driver: $got (expected $livox)" >&2; exit 1; }
echo "ok: $image matches packages.lock and livox_ros_driver $livox"
