#!/usr/bin/env python3
"""Compare two PLGOLD01 directories using metrics-preregistration.md's appendix.

Usage: python3 tests/math/compare_math_golden.py BASELINE CANDIDATE
Exit 0: all pass, 1: numeric mismatch, 2: invalid/missing data.
The baseline directory must contain a valid SHA256SUMS manifest.
"""
import argparse
import hashlib
import math
from pathlib import Path
import struct
import sys

STATE = (3, 9, 9, 3, 3, 3, 3, 3, 3, 3)
VECTOR = (3,) * 10
R = 64 * 2 ** -53
# input length, output blocks, relative tolerance (None means bit equality)
SCHEMA = {
    "so3_exp": (3, (9,), R),
    "so3_log": (9, (3,), None),
    "so3_boxplus": (12, (9,), R),
    "so3_boxminus": (18, (3,), None),
    "a_matrix": (3, (9,), None),
    "so3math_exp3": (3, (9,), R),
    "quat_from_matrix": (9, (4,), R),
    "state_boxplus": (72, STATE, R),
    "state_boxminus": (84, VECTOR, None),
    "f_output": (42, VECTOR, R),
    "df_dx_output": (42, (900,), R),
    "predict_state": (43, STATE, R),
    "predict_cov": (1843, (900,), 1e-10),
}


def verify_manifest(directory):
    entries = {}
    for line in (directory / "SHA256SUMS").read_text().splitlines():
        digest, name = line.split()
        if name in entries:
            raise ValueError("duplicate manifest entry")
        entries[name] = digest
    if set(entries) != {name + ".bin" for name in SCHEMA}:
        raise ValueError("manifest must list exactly the 13 components")
    for name, digest in entries.items():
        if hashlib.sha256((directory / name).read_bytes()).hexdigest() != digest:
            raise ValueError("SHA256 mismatch: " + name)


def read_records(path, in_len, out_len):
    data = path.read_bytes()
    if len(data) < 24:
        raise ValueError(f"{path}: truncated header")
    magic, version, count, ni, no = struct.unpack_from("<8s4I", data)
    if magic != b"PLGOLD01" or version != 1 or (ni, no) != (in_len, out_len) or count == 0:
        raise ValueError(f"{path}: invalid header/schema or empty records")
    stride = 8 * (ni + no)
    if len(data) != 24 + count * stride:
        raise ValueError(f"{path}: invalid file length")
    records = []
    for i in range(count):
        start = 24 + i * stride
        inp = data[start:start + ni * 8]
        raw = data[start + ni * 8:start + stride]
        values = struct.unpack(f"<{no}d", raw)
        if not all(math.isfinite(x) for x in struct.unpack(f"<{ni}d", inp) + values):
            raise ValueError(f"{path}: record {i}: NaN/Inf")
        records.append((inp, raw, values))
    return records


def compare_component(name, baseline, candidate):
    ni, blocks, relative = SCHEMA[name]
    b = read_records(baseline / (name + ".bin"), ni, sum(blocks))
    c = read_records(candidate / (name + ".bin"), ni, sum(blocks))
    if len(b) != len(c):
        raise ValueError(name + ": record count mismatch")
    failed = 0
    first = ""
    for i, ((bi, br, bv), (ci, cr, cv)) in enumerate(zip(b, c)):
        if bi != ci:
            raise ValueError(f"{name}: record {i}: input bits differ")
        if name == "quat_from_matrix" and sum(x * y for x, y in zip(bv, cv)) < 0:
            cv = tuple(-x for x in cv)
        bad = []
        if relative is None:
            bad = [j for j in range(len(bv)) if br[8*j:8*j+8] != cr[8*j:8*j+8]]
        else:
            offset = 0
            for size in blocks:
                scale = max(abs(x) for x in bv[offset:offset + size])
                for j in range(offset, offset + size):
                    if abs(cv[j] - bv[j]) > relative * scale + relative * abs(bv[j]):
                        bad.append(j)
                offset += size
        if bad:
            failed += 1
            if not first:
                j = bad[0]
                first = f" first=record:{i},component:{j},b:{bv[j]:.17g},c:{cv[j]:.17g}"
    print(f"{'FAIL' if failed else 'PASS'} {name}: {failed}/{len(b)} records differ{first}")
    return failed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    args = parser.parse_args()
    try:
        verify_manifest(args.baseline)
        for directory in (args.baseline, args.candidate):
            if {p.name for p in directory.glob("*.bin")} != {n + ".bin" for n in SCHEMA}:
                raise ValueError(f"{directory}: expected exactly 13 .bin files")
        failed = [compare_component(name, args.baseline, args.candidate) for name in SCHEMA]
        return int(any(failed))
    except (OSError, ValueError, struct.error) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
