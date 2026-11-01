#!/usr/bin/env python3
"""Document checker contract tests: each check must fail on an injected error."""
from pathlib import Path
import subprocess
import tempfile
import unittest

from check_docs import run

HUB = """# hub

台帳2件(分類済み1・仮説1)

- [spec](spec.md)、[ledger](bug-ledger.md)、[prereg](metrics-preregistration.md)
- [plan](../agents/plans/p.md)
"""
LEDGER = """| ID | 状態 | 分類案 |
| --- | --- | --- |
| BL-001 | 分類済み | (a) |
| BL-002 | 仮説 | (b) |
"""
PREREG = """# prereg

状態: 承認済み。許容誤差の規則を2回改訂

## 改訂履歴

- 2026-10-26(1回目): a
- 2026-10-26(2回目): b
"""


class CheckDocsTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = Path(tmp.name)
        self.write("docs/README.md", HUB)
        self.write("docs/bug-ledger.md", LEDGER)
        self.write("docs/metrics-preregistration.md", PREREG)
        self.write("docs/spec.md", "# spec\n")
        self.write("agents/plans/p.md", "# plan\n")
        self.git("init", "-q")
        self.git("add", ".")
        self.git("-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "-m", "fixture")
        self.commit = self.git("rev-parse", "--short=7", "HEAD").strip()

    def git(self, *args):
        return subprocess.run(["git", "-C", str(self.root), *args],
                              capture_output=True, text=True, check=True).stdout

    def write(self, path, text):
        (self.root / path).parent.mkdir(parents=True, exist_ok=True)
        (self.root / path).write_text(text, encoding="utf-8")

    def append(self, path, text):
        with (self.root / path).open("a", encoding="utf-8") as f:
            f.write(text)

    def codes(self):
        documents, findings = run(self.root)
        self.assertEqual(len(documents), 5 + len(list(self.root.glob("docs/new*.md"))))
        return sorted(f.split("[", 1)[1][:2] for f in findings)

    def test_fixture_is_clean(self):
        self.assertEqual(self.codes(), [])

    def test_d1_broken_link(self):
        self.append("docs/spec.md", "[x](missing.md)\n")
        self.assertEqual(self.codes(), ["D1"])

    def test_d1_ignores_urls_anchors_and_code_blocks(self):
        self.append("docs/spec.md", "[a](https://example.com/x.md) [b](#top) [c](spec.md#s)\n"
                                    "```\n[d](missing.md)\n```\n")
        self.assertEqual(self.codes(), [])

    def test_d2_unknown_hash(self):
        self.append("docs/spec.md", "`deadbee`\n")
        self.assertEqual(self.codes(), ["D2"])

    def test_d2_accepts_commits_md5_and_external_hashes(self):
        self.append("docs/spec.md", f"`{self.commit}` `{'a' * 32}` `b940084d7`\n")
        self.assertEqual(self.codes(), [])

    def test_d3_unlisted_document(self):
        self.write("docs/new.md", "# new\n")
        self.assertEqual(self.codes(), ["D3"])

    def test_d4_count_mismatch(self):
        self.append("docs/bug-ledger.md", "| BL-003 | 仮説 | (c) |\n")
        self.assertEqual(self.codes(), ["D4"])

    def test_d4_state_mismatch(self):
        self.write("docs/bug-ledger.md", LEDGER.replace("| BL-002 | 仮説", "| BL-002 | 再現済み"))
        self.assertEqual(self.codes(), ["D4"])

    def test_d4_missing_summary(self):
        self.write("docs/README.md", HUB.replace("台帳2件(分類済み1・仮説1)", ""))
        self.assertEqual(self.codes(), ["D4"])

    def test_d5_stale_status_line(self):
        self.append("docs/metrics-preregistration.md", "- 2026-11-01(3回目): c\n")
        self.assertEqual(self.codes(), ["D5"])

    def test_d6_checked_box_without_evidence(self):
        self.append("docs/README.md", "- [x] done\n- [ ] open\n")
        self.assertEqual(self.codes(), ["D6"])

    def test_d6_checked_box_with_evidence(self):
        self.append("docs/README.md", f"- [x] a (`{self.commit}`)\n- [x] b ([spec](spec.md))\n")
        self.assertEqual(self.codes(), [])

    def test_d6_unknown_hash_is_not_evidence(self):
        self.append("docs/README.md", "- [x] a (`deadbee`)\n")
        self.assertEqual(self.codes(), ["D2", "D6"])


if __name__ == "__main__":
    unittest.main()
