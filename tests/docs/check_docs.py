#!/usr/bin/env python3
"""Check consistency between the project documents.

Usage: python3 -B tests/docs/check_docs.py [--root DIR]
Exit 0: no findings, 1: findings (one line each), 2: invalid repository.

Checks (docs/README.md section 5 lists what each one means):
  D1 relative links point to existing files (anchors are not checked)
  D2 backticked hex words are commits of this repository
  D3 every project document is linked from docs/README.md
  D4 ledger counts in docs/README.md match docs/bug-ledger.md
  D5 the preregistration status line matches its revision history
  D6 every checked box gives evidence (a commit or a document link)
Upstream documents (README.md, Log/, include/) are not checked.
"""
import argparse
from pathlib import Path
import re
import subprocess
import sys

HUB = "docs/README.md"
LEDGER = "docs/bug-ledger.md"
PREREG = "docs/metrics-preregistration.md"
# Hashes of other projects; prefixes of the full hash.
EXTERNAL_HASHES = {
    "b940084d7": "rustc 1.99.0",
    "5f94df478": "cargo 1.99.0",
}
# ROS message definitions are identified by 32-hex md5 sums.
MD5_LENGTH = 32

LINK = re.compile(r"\[[^\]]*\]\(([^)\s]+)\)")
HEX = re.compile(r"`([0-9a-f]{7,40})`")
CHECKED = re.compile(r"^\s*- \[[xX]\] ")
LEDGER_ROW = re.compile(r"^\| (BL-\d+) \| ([^|]+?) \|")
LEDGER_SUMMARY = re.compile(r"台帳(\d+)件\(([^)]+)\)")
REVISION = re.compile(r"^- \d{4}-\d{2}-\d{2}\((\d+)回目\)")
STATUS_REVISIONS = re.compile(r"(\d+)回改訂")


def project_documents(root):
    out = subprocess.run(["git", "-C", str(root), "ls-files", "--cached", "--others",
                          "--exclude-standard", "*.md"],
                         capture_output=True, text=True, check=True).stdout.split()
    keep = ("AGENTS.md",)
    prefixes = ("docs/", "agents/", "golden/")
    return sorted(p for p in out if p in keep or p.startswith(prefixes))


def prose_lines(text):
    """Yield (line number, line) outside fenced code blocks."""
    fenced = False
    for number, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith("```"):
            fenced = not fenced
            continue
        if not fenced:
            yield number, line


def is_commit(root, word):
    return subprocess.run(["git", "-C", str(root), "cat-file", "-e", word + "^{commit}"],
                          capture_output=True).returncode == 0


def relative_targets(line):
    for target in LINK.findall(line):
        if re.match(r"^[a-z]+:", target) or target.startswith("#"):
            continue
        yield target.split("#", 1)[0]


def check_links(root, path, lines):
    for number, line in lines:
        for target in relative_targets(line):
            if not (root / path).parent.joinpath(target).exists():
                yield f"{path}:{number}: [D1] link target does not exist: {target}"


def check_commits(root, path, lines):
    for number, line in lines:
        for word in HEX.findall(line):
            if len(word) == MD5_LENGTH or any(word.startswith(h) for h in EXTERNAL_HASHES):
                continue
            if not is_commit(root, word):
                yield f"{path}:{number}: [D2] not a commit of this repository: {word}"


def check_checkboxes(root, path, lines):
    for number, line in lines:
        if not CHECKED.match(line):
            continue
        has_commit = any(is_commit(root, w) for w in HEX.findall(line))
        has_document = any(t.endswith(".md") for t in relative_targets(line))
        if not (has_commit or has_document):
            yield f"{path}:{number}: [D6] checked box has no commit or document as evidence"


def check_hub_lists_documents(root, documents):
    hub = root / HUB
    linked = set()
    for _, line in prose_lines(hub.read_text(encoding="utf-8")):
        for target in relative_targets(line):
            linked.add((hub.parent / target).resolve())
    for path in documents:
        if path in (HUB, "AGENTS.md"):
            continue
        if (root / path).resolve() not in linked:
            yield f"{HUB}: [D3] project document is not linked: {path}"


def check_ledger_counts(root):
    counts = {}
    for line in (root / LEDGER).read_text(encoding="utf-8").splitlines():
        match = LEDGER_ROW.match(line)
        if match:
            counts[match.group(2)] = counts.get(match.group(2), 0) + 1
    total = sum(counts.values())
    found = 0
    for number, line in prose_lines((root / HUB).read_text(encoding="utf-8")):
        for match in LEDGER_SUMMARY.finditer(line):
            found += 1
            stated = {}
            for item in match.group(2).split("・"):
                part = re.fullmatch(r"(\D+)(\d+)", item)
                if part:
                    stated[part.group(1)] = int(part.group(2))
            if int(match.group(1)) != total or stated != counts:
                yield (f"{HUB}:{number}: [D4] ledger summary {match.group(0)} does not match "
                       f"{LEDGER}: {total} rows {counts}")
    if found == 0:
        yield f"{HUB}: [D4] no ledger summary of the form 台帳N件(状態M・…) found"


def check_preregistration(root):
    text = (root / PREREG).read_text(encoding="utf-8")
    history = [int(m.group(1)) for line in text.splitlines() if (m := REVISION.match(line))]
    status = [(n, line) for n, line in enumerate(text.splitlines(), 1) if line.startswith("状態:")]
    if not history or len(status) != 1:
        yield f"{PREREG}: [D5] expected one status line and a revision history"
        return
    number, line = status[0]
    stated = STATUS_REVISIONS.search(line)
    if not stated or int(stated.group(1)) != max(history):
        yield (f"{PREREG}:{number}: [D5] status line says "
               f"{stated.group(0) if stated else 'no revision count'}, history has {max(history)}")


def run(root):
    documents = project_documents(root)
    findings = []
    for path in documents:
        lines = list(prose_lines((root / path).read_text(encoding="utf-8")))
        findings += check_links(root, path, lines)
        findings += check_commits(root, path, lines)
        findings += check_checkboxes(root, path, lines)
    findings += check_hub_lists_documents(root, documents)
    findings += check_ledger_counts(root)
    findings += check_preregistration(root)
    return documents, findings


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    root = parser.parse_args().root.resolve()
    for required in (HUB, LEDGER, PREREG):
        if not (root / required).is_file():
            print(f"missing {required} under {root}", file=sys.stderr)
            return 2
    documents, findings = run(root)
    for finding in findings:
        print(finding)
    print(f"checked {len(documents)} documents, {len(findings)} findings", file=sys.stderr)
    return 1 if findings else 0


if __name__ == "__main__":
    sys.exit(main())
