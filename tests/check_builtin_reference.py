#!/usr/bin/env python3
"""Keep the editor's builtin reference in step with lexer words and concepts."""
from collections import Counter
from pathlib import Path
import re


def main():
    root = Path(__file__).resolve().parents[1]
    text = (root / "stdlib/types.mla").read_text()
    entries = re.findall(r"^// @builtin(?:_fn)? (\w+)$", text, re.M)
    duplicates = [name for name, count in Counter(entries).items() if count > 1]
    assert not duplicates, f"Duplicate builtin reference entries: {duplicates}"
    documented = set(entries)
    rules = re.findall(r'^"([a-zA-Z_][a-zA-Z_0-9]*)"\s+\{(.*?)(?=^\S|\Z)', (root / "src/lexer.l").read_text(), re.M | re.S)
    words = {name for name, body in rules if re.search(r"\breturn\s+[A-Z_][A-Z_0-9]*\s*;", body)}
    assert "requires" not in documented, "Removed syntax must not remain in the builtin reference"
    concepts = set(re.findall(r"^pub concept (\w+)<", (root / "stdlib/std/concepts.mla").read_text(), re.M))
    expected = words | concepts | {"operator", "mut", "free_objs"}
    assert not expected - documented, f"Undocumented builtin entries: {sorted(expected - documented)}"
    for name in expected:
        block = re.search(rf"^// @builtin(?:_fn)? {name}\n((?://[^\n]*\n)+)", text, re.M)
        assert block and block.group(1).strip("/ \n"), f"Missing reference text for {name}"
    print(f"PASS: reference covers {len(words)} lexer words, {len(concepts)} standard concepts, and extra builtin forms")


if __name__ == "__main__":
    main()
