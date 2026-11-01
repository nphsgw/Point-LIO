#!/usr/bin/env bash
# Collect step 6 evidence. Disassembly still requires the function-by-function
# review described in agents/plans/math-state.md; this script alone is not a pass.
# Usage: bash rust/check_math_step6.sh <new directory outside the repository>
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
[[ $# = 1 ]] || { echo "usage: $0 <new output directory>" >&2; exit 2; }
output=$(realpath -m -- "$1")
case "$output" in "$repo"|"$repo/"*) echo 'output must be outside the repository' >&2; exit 2 ;; esac
[[ ! -e "$output" ]] || { echo 'output already exists' >&2; exit 2; }
(cd "$repo/golden/math" && sha256sum --quiet -c SHA256SUMS)
image=$(docker image inspect point-lio-rust --format '{{.Id}}')
mkdir -p "$output"
printf '%s\n' "$image" > "$output/image-id.txt"
git -C "$repo" rev-parse HEAD > "$output/source-commit.txt"
git -C "$repo" diff --binary > "$output/source-diff.patch"
# The diff omits untracked files; record them too (copies are hashed below).
git -C "$repo" status --porcelain --untracked-files=all > "$output/source-status.txt"
cp "$0" "$output/collector.sh"
python3 --version > "$output/python-version.txt"
python3 - "$repo" "$output" <<'PY'
from pathlib import Path
import hashlib
import shutil
import sys
repo, out = map(Path, sys.argv[1:])
for variant in ('baseline', 'separate', 'bl008-reverted'):
    shutil.copytree(repo / 'rust', out / 'sources' / variant,
                    ignore=shutil.ignore_patterns('target'))
p = out / 'sources/separate/pointlio-math/src/libm.rs'
p.write_text('''//! Diagnostic variant only: prevent LLVM combining sin and cos.
pub(crate) fn sin_cos(x: f64) -> (f64, f64) {
    let sin = x.sin();
    let cos = std::hint::black_box(x).cos();
    (sin, cos)
}
''')
p = out / 'sources/bl008-reverted/pointlio-math/src/so3.rs'
source = p.read_text()
start = source.index('        let identity = linalg::identity3();', source.index('if c < -1.0 + 5e-8 {'))
end = source.index('    } else if theta.abs() < 0.001 {', start)
# Restore A's expression only in BL-008's near-pi region. Keep BL-011's
# ordinary branch intact, so this mutation isolates BL-008.
source = source[:start] + '''        let scale = 0.5 * theta / theta.sin();
        k.map(|x| scale * x)
''' + source[end:]
p.write_text(source)
with (out / 'source-SHA256SUMS').open('w') as manifest:
    for p in sorted((out / 'sources').rglob('*')):
        if p.is_file():
            manifest.write(hashlib.sha256(p.read_bytes()).hexdigest() + '  ' + str(p.relative_to(out)) + '\n')
PY
for variant in separate bl008-reverted; do
    diff -ru "$output/sources/baseline" "$output/sources/$variant" > "$output/$variant.patch" || [[ $? = 1 ]]
done
docker run --rm -i --network none --user "$(id -u):$(id -g)" \
    -e OMP_NUM_THREADS=1 -v "$repo":/repo:ro -v "$output":/results \
    "$image" bash -s <<'SH'
set -euo pipefail
rustc -vV > /results/rustc.txt
dpkg-query -W libc6 binutils > /results/packages.txt
lscpu > /results/cpu.txt
env | sort > /results/container-env.txt
for variant in baseline separate bl008-reverted; do
    cd "/results/sources/$variant"
    export CARGO_TARGET_DIR="/tmp/target-$variant"
    profiles=release
    [[ $variant != baseline ]] || profiles='debug release'
    for profile in $profiles; do
        dest="/results/$variant/$profile"
        mkdir -p "$dest/output"
        flags=()
        [[ $profile != release ]] || flags=(--release)
        cargo build "${flags[@]}" --locked --offline --bin math_golden > "$dest/build.log" 2>&1
        bin="$CARGO_TARGET_DIR/$profile/math_golden"
        cp "$bin" "$dest/math_golden"
        nm -D "$bin" > "$dest/nm-D.txt"
        nm -n -C "$bin" > "$dest/nm.txt"
        objdump -d -C "$bin" > "$dest/disassembly.txt"
        objdump -R "$bin" > "$dest/relocations.txt"
        objdump -T "$bin" > "$dest/dynamic-symbols.txt"
        ldd "$bin" > "$dest/ldd.txt"
        LD_DEBUG=bindings "$bin" /repo/golden/math "$dest/output" > "$dest/generate.log" 2> "$dest/bindings.log"
    done
    if [[ $variant != separate ]]; then
        dest="/results/$variant"
        if cargo test --release --locked --offline --test math_l1 -- --test-threads=1 > "$dest/l1.log" 2>&1; then
            echo 0 > "$dest/l1-exit.txt"
        else
            echo "$?" > "$dest/l1-exit.txt"
        fi
    fi
    if [[ $variant = baseline ]]; then
        cargo clippy --release --locked --offline --all-targets -- -D warnings > /results/clippy.log 2>&1
        cargo fmt --all -- --check > /results/fmt.log 2>&1
        # Diagnostic build for inline source locations; the normal release above
        # remains the binary used for numeric and debug/release comparisons.
        mkdir -p /results/baseline/release-lines
        CARGO_PROFILE_RELEASE_DEBUG=1 cargo build --release --locked --offline --bin math_golden > /results/baseline/release-lines/build.log 2>&1
        cp "$CARGO_TARGET_DIR/release/math_golden" /results/baseline/release-lines/math_golden
    fi
done
SH
# Ubuntu 20.04 binutils cannot demangle Rust v0 symbols. Use the host's newer
# binutils only to inspect saved binaries, never to execute numerical code.
objdump --version > "$output/host-objdump-version.txt"
python3 - "$output" <<'PY'
from pathlib import Path
import re
import subprocess
import sys
out = Path(sys.argv[1])
for name in ('baseline/debug', 'baseline/release', 'baseline/release-lines', 'separate/release'):
    p = out / name
    dis = subprocess.check_output(['objdump', '-d', '-C', str(p / 'math_golden')], text=True)
    (p / 'disassembly-host.txt').write_text(dis)
    sites, locations = [], []
    function = ''
    for line in dis.splitlines():
        match = re.match(r'^([0-9a-f]+) <(.*)>:', line)
        if match:
            function = match[2]
        if re.search(r'<(?:sincos|sin|cos|acos|atan2)(?:@|>)', line):
            sites.append(function + '\n' + line)
            address = line.split(':', 1)[0].strip()
            locations.append(subprocess.check_output(
                ['addr2line', '-afiC', '-e', str(p / 'math_golden'), address], text=True))
    (p / 'trig-sites.txt').write_text('\n'.join(sites) + '\n')
    (p / 'trig-source-locations.txt').write_text('\n'.join(locations))
PY
for candidate in baseline/debug baseline/release separate/release bl008-reverted/release; do
    if python3 -B "$repo/tests/math/compare_math_golden.py" "$repo/golden/math" "$output/$candidate/output" > "$output/$candidate/comparison.log" 2>&1; then
        echo 0 > "$output/$candidate/comparison-exit.txt"
    else
        echo "$?" > "$output/$candidate/comparison-exit.txt"
    fi
done
python3 - "$repo" "$output" <<'PY'
from pathlib import Path
import sys
repo, out = map(Path, sys.argv[1:])
files = sorted((repo / 'golden/math').glob('*.bin'))
assert len(files) == 13
for p in files:
    a = (out / 'baseline/debug/output' / p.name).read_bytes()
    b = (out / 'baseline/release/output' / p.name).read_bytes()
    assert a == b, p.name
(out / 'byte-comparison.txt').write_text('PASS: debug/release, all 13 files byte-identical\n')
assert (out / 'baseline/l1-exit.txt').read_text().strip() == '0'
for profile in ('debug', 'release'):
    assert (out / f'baseline/{profile}/comparison-exit.txt').read_text().strip() == '0'
assert (out / 'bl008-reverted/l1-exit.txt').read_text().strip() == '101'
log = (out / 'bl008-reverted/l1.log').read_text()
for name in ('so3_log_finite_near_pi', 'so3_log_accurate_around_near_pi_branch'):
    assert f'test {name} ... FAILED' in log
# The comparison must fail in so3_log itself, not in some other component.
status = (out / 'bl008-reverted/release/comparison-exit.txt').read_text().strip()
lines = (out / 'bl008-reverted/release/comparison.log').read_text().splitlines()
if status == '1':
    assert any(line.startswith('FAIL so3_log:') for line in lines)
else:
    assert status == '2', status
    assert any(line.startswith('ERROR: ') and line.endswith('NaN/Inf')
               and '/so3_log.bin: record ' in line for line in lines)
print('Collected numeric evidence; disassembly and mutation failure causes require review.')
PY
printf 'Evidence: %s\n' "$output"
