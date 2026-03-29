#!/usr/bin/env python3
import argparse
import subprocess
import tempfile
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlang-format", default="/tmp/mlang-format-mla")
    args = ap.parse_args()

    formatter = Path(args.mlang_format)
    if not formatter.exists():
        raise SystemExit(f"mlang-format binary not found: {formatter}")

    with tempfile.TemporaryDirectory(prefix="mlang_format_spacing_") as td:
        root = Path(td)
        file_path = root / "spacing_case.mla"
        file_path.write_text(
            "fn factorial    (n: i32) -> i32 {\n"
            "    var    result: i32 = 1;\n"
            "    if result<10: {\n"
            "        result = result + 1;\n"
            "    }\n"
            "    for i    in 1..n + 1 {\n"
            "        result = result * i;\n"
            "        result*=2;\n"
            "        result * = 3;\n"
            "        result+=-3;\n"
            "    }\n"
            "    return result;\n"
            "}\n"
            "\n"
            "fn    main() -> i32 {\n"
            "    let sum: i64 = sum_range(0, 10);  // 45\n"
            "    let fact: i32 = factorial(5);      // 120\n"
            "    return 0;\n"
            "}\n"
        )

        out = subprocess.run(
            [str(formatter), str(file_path)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout

        assert "fn factorial(n: i32) -> i32 {\n" in out, (
            "expected function spacing normalization for factorial"
        )
        assert "    var result: i32 = 1;\n" in out, (
            "expected var spacing normalization"
        )
        assert "    for i in 1..n + 1 {\n" in out, (
            "expected for/in spacing normalization"
        )
        assert "        result *= 2;\n" in out, (
            "expected compound assignment operator spacing normalization"
        )
        assert "        result *= 3;\n" in out, (
            "expected spaced compound assignment to normalize to '*='"
        )
        assert "        result += -3;\n" in out, (
            "expected unary minus after compound assignment to remain compact"
        )
        assert "    if result < 10: {\n" in out, (
            "expected default relational operator spacing normalization"
        )
        assert "fn main() -> i32 {\n" in out, (
            "expected function spacing normalization for main"
        )
        assert "let sum: i64 = sum_range(0, 10);" in out, (
            "expected sum assignment spacing normalization"
        )
        assert "let fact: i32 = factorial(5);" in out, (
            "expected factorial assignment spacing normalization"
        )

        (root / ".mlang-format").write_text(
            "SpaceAroundRelationalOperators: false\n"
        )
        out_compact_rel = subprocess.run(
            [str(formatter), str(file_path)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        assert "    if result<10: {\n" in out_compact_rel, (
            "expected compact relational operators when disabled"
        )

        multiline_path = root / "multiline_string_case.mla"
        multiline_path.write_text(
            "fn main() -> i32 {\n"
            "let asm_text: str8 = \"mov $0, $1\n"
            "    add $0, $0, $2\n"
            "ret\";\n"
            "let msg: str8 = \"Hello\n"
            "  world\";\n"
            "return 0;\n"
            "}\n"
        )

        out_multiline = subprocess.run(
            [str(formatter), str(multiline_path)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        assert "fn main() -> i32 {\n" in out_multiline, (
            "expected multiline case to keep outer function formatting"
        )
        assert 'let asm_text: str8 = "mov $0, $1\n' in out_multiline, (
            "expected formatter to preserve multiline string opening line"
        )
        assert "    add $0, $0, $2\n" in out_multiline, (
            "expected formatter to preserve multiline asm row alignment"
        )
        assert 'ret";\n' in out_multiline, (
            "expected formatter to preserve multiline string closing line"
        )
        assert 'let msg: str8 = "Hello\n' in out_multiline, (
            "expected formatter to preserve generic multiline string opening line"
        )
        assert '  world";\n' in out_multiline, (
            "expected formatter to preserve generic multiline string indentation"
        )

    print("PASS: mlang-format spacing e2e (including multiline string preservation)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
