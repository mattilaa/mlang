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
            "        return result == 13 ? 0 : 1;\n"
            "    }\n"
            "    let ov: i64 = __mlang_std_unordered_map_i64_i64_get_or(ev_count_map_h, k,-1);\n"
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
        assert "        return result == 13 ? 0 : 1;\n" in out, (
            "expected default ternary colon spacing to keep a space before ':'"
        )
        assert "__mlang_std_unordered_map_i64_i64_get_or(ev_count_map_h, k, -1);" in out, (
            "expected comma spacing before negative arguments"
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
        assert "        return result == 13 ? 0 : 1;\n" in out_compact_rel, (
            "expected ternary colon spacing to remain independent of relational spacing"
        )

        (root / ".mlang-format").write_text(
            "SpaceBeforeTernaryColon: false\n"
        )
        out_compact_ternary_colon = subprocess.run(
            [str(formatter), str(file_path)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        assert "        return result == 13 ? 0: 1;\n" in out_compact_ternary_colon, (
            "expected ternary colon spacing to be configurable"
        )

        condition_path = root / "multiline_condition_case.mla"
        condition_path.write_text(
            "fn main() -> i32 {\n"
            "if pat_note_map_h == 0 || pat_v1_map_h == 0 || pat_v2_map_h == 0 ||\n"
            "    pat_custom_map_h == 0 || pat_cell_active_map_h == 0 ||\n"
            "    ev_event_map_h == 0 || ev_val1_map_h == 0 || ev_val2_map_h == 0 ||\n"
            "    ev_mod_map_h == 0 || ev_m1_map_h == 0 || ev_m2_map_h == 0 ||\n"
            "    ev_count_map_h == 0 {\n"
            "return 1;\n"
            "}\n"
            "return 0;\n"
            "}\n"
        )
        (root / ".mlang-format").write_text(
            "ConditionContinuationIndentWidth: 3\n"
        )
        out_condition = subprocess.run(
            [str(formatter), str(condition_path)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        assert "    if pat_note_map_h == 0 || pat_v1_map_h == 0 || pat_v2_map_h == 0 ||\n" in out_condition, (
            "expected multiline condition opener to keep block indentation"
        )
        assert "       pat_custom_map_h == 0 || pat_cell_active_map_h == 0 ||\n" in out_condition, (
            "expected default condition continuation indent to align after 'if '"
        )
        assert "       ev_count_map_h == 0 {\n" in out_condition, (
            "expected final condition continuation row to keep condition indent"
        )

        (root / ".mlang-format").write_text(
            "ConditionContinuationIndentWidth: 0\n"
        )
        out_condition_no_extra = subprocess.run(
            [str(formatter), str(condition_path)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        assert "    pat_custom_map_h == 0 || pat_cell_active_map_h == 0 ||\n" in out_condition_no_extra, (
            "expected condition continuation indentation to be configurable off"
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


        args_path = root / "multiline_args_case.mla"
        args_path.write_text(
            "fn route_walking_minutes(\n"
            "from_bar: i64,\n"
            "to_bar: i64,\n"
            "hill_froms: &list<i64>\n"
            ") -> i64 {\n"
            "return find_hill_index(\n"
            "from_bar,\n"
            "to_bar,\n"
            "hill_froms\n"
            ");\n"
            "}\n"
        )
        (root / ".mlang-format").write_text(
            "ContinuationIndentWidth: 8\n"
            "IndentFunctionSignatureClosingParen: true\n"
        )
        out_args = subprocess.run(
            [str(formatter), str(args_path)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        assert "fn route_walking_minutes(\n" in out_args, (
            "expected multiline function header to remain multiline"
        )
        assert "        from_bar: i64,\n" in out_args, (
            "expected multiline function parameters to use continuation indent"
        )
        assert "        to_bar: i64,\n" in out_args, (
            "expected subsequent multiline function parameters to use continuation indent"
        )
        assert "        ) -> i64 {\n" in out_args, (
            "expected configured function signature closing paren to keep continuation indent"
        )
        assert "            from_bar,\n" in out_args, (
            "expected multiline call arguments inside a block to include block + continuation indent"
        )
        assert "    );\n" in out_args, (
            "expected multiline call closing paren to keep only block indent"
        )

        (root / ".mlang-format").write_text(
            "ContinuationIndentWidth: 0\n"
            "IndentFunctionSignatureClosingParen: false\n"
        )
        out_no_continuation = subprocess.run(
            [str(formatter), str(args_path)],
            check=True,
            capture_output=True,
            text=True,
        ).stdout
        assert "from_bar: i64,\n" in out_no_continuation, (
            "expected continuation indentation to be configurable off"
        )
        assert "    from_bar,\n" in out_no_continuation, (
            "expected block indentation to remain when continuation indentation is disabled"
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
