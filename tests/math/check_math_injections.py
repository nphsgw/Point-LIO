#!/usr/bin/env python3
"""Assert the exact set of rejected components, including unaffected controls."""
from pathlib import Path
import sys

from compare_math_golden import SCHEMA, compare_component, verify_manifest


def main():
    baseline, results = map(Path, sys.argv[1:])
    verify_manifest(baseline)
    for name in SCHEMA:
        filename = name + ".bin"
        if (baseline / filename).read_bytes() != (results / "baseline" / filename).read_bytes():
            raise RuntimeError("unmodified B is not byte-identical: " + name)
    expected = {
        "baseline": set(),
        "perturb": set(SCHEMA),
        "f32_round": set(SCHEMA),
        "left_boxplus": {"so3_boxplus", "state_boxplus"},
        "quat_order": {"quat_from_matrix"},
        "jacobian": {"df_dx_output"},
        "dt_shift": {"predict_state", "predict_cov"},
    }
    for mode, targets in expected.items():
        print(f"\nInjection: {mode}", flush=True)
        rejected = {name for name in SCHEMA if compare_component(name, baseline, results / mode)}
        if rejected != targets:
            raise RuntimeError(f"{mode}: rejected {sorted(rejected)}, expected {sorted(targets)}")
    print("\nPASS: unmodified B passes; all six injections reject exactly their target components")


if __name__ == "__main__":
    main()
