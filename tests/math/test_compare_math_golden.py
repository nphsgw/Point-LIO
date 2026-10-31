#!/usr/bin/env python3
"""Comparator contract tests; no golden data is modified."""
import contextlib
import io
from pathlib import Path
import struct
import tempfile
import unittest

from compare_math_golden import R, SCHEMA, compare_component


class ComparatorTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.b = Path(self.tmp.name) / "b"
        self.c = Path(self.tmp.name) / "c"
        self.b.mkdir()
        self.c.mkdir()

    def write(self, directory, name, values, inputs=None, count=1):
        ni, blocks, _ = SCHEMA[name]
        inputs = [0.] * ni if inputs is None else inputs
        data = struct.pack("<8s4I", b"PLGOLD01", 1, count, ni, sum(blocks))
        if count:
            data += struct.pack(f"<{ni + sum(blocks)}d", *(inputs + values))
        (directory / (name + ".bin")).write_bytes(data)

    def compare(self, name):
        with contextlib.redirect_stdout(io.StringIO()):
            return compare_component(name, self.b, self.c)

    def test_inclusive_tolerance_boundary(self):
        self.write(self.b, "so3_exp", [1.] * 9)
        self.write(self.c, "so3_exp", [1. + 2 * R] * 9)
        self.assertEqual(self.compare("so3_exp"), 0)
        self.write(self.c, "so3_exp", [1. + 2 * R + 2 ** -52] * 9)
        self.assertEqual(self.compare("so3_exp"), 1)

    def test_quaternion_sign(self):
        self.write(self.b, "quat_from_matrix", [0., 0., 0., 1.])
        self.write(self.c, "quat_from_matrix", [-0., -0., -0., -1.])
        self.assertEqual(self.compare("quat_from_matrix"), 0)
        self.write(self.c, "quat_from_matrix", [1., 0., 0., 0.])
        self.assertEqual(self.compare("quat_from_matrix"), 1)

    def test_bit_equality_includes_zero_sign(self):
        self.write(self.b, "so3_log", [0., 0., 0.])
        self.write(self.c, "so3_log", [-0., 0., 0.])
        self.assertEqual(self.compare("so3_log"), 1)

    def test_state_blocks_do_not_share_scale(self):
        b = [1.] * 42
        b[0] = 1e12
        c = b.copy()
        c[24] += 1e-9
        for name in ("state_boxplus", "predict_state"):
            self.write(self.b, name, b)
            self.write(self.c, name, c)
            self.assertEqual(self.compare(name), 1)

    def test_f_output_zero_block_has_zero_tolerance(self):
        b = [0.] * 30
        b[0] = 1e12
        c = b.copy()
        c[6] = 1e-100
        self.write(self.b, "f_output", b)
        self.write(self.c, "f_output", c)
        self.assertEqual(self.compare("f_output"), 1)

    def test_nonfinite_rejected_on_either_side(self):
        for directory in (self.b, self.c):
            for value in (float("nan"), float("inf"), -float("inf")):
                self.write(self.b, "so3_exp", [1.] * 9)
                self.write(self.c, "so3_exp", [1.] * 9)
                self.write(directory, "so3_exp", [value] * 9)
                with self.assertRaisesRegex(ValueError, "NaN/Inf"):
                    self.compare("so3_exp")

    def test_input_bits_must_match(self):
        self.write(self.b, "so3_exp", [1.] * 9)
        self.write(self.c, "so3_exp", [1.] * 9, inputs=[-0., 0., 0.])
        with self.assertRaisesRegex(ValueError, "input bits"):
            self.compare("so3_exp")

    def test_empty_and_truncated_files_rejected(self):
        self.write(self.b, "so3_exp", [1.] * 9)
        self.write(self.c, "so3_exp", [], count=0)
        with self.assertRaisesRegex(ValueError, "empty records"):
            self.compare("so3_exp")
        self.write(self.c, "so3_exp", [1.] * 9)
        path = self.c / "so3_exp.bin"
        path.write_bytes(path.read_bytes()[:-1])
        with self.assertRaisesRegex(ValueError, "file length"):
            self.compare("so3_exp")


if __name__ == "__main__":
    unittest.main()
