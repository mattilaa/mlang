#!/usr/bin/env python3
import argparse
import os
import sys
from dataclasses import dataclass
from typing import Dict, Iterable, List, Optional


DEFAULT_CONFIG: Dict[str, object] = {
    "BasedOnStyle": "Rust",
    "IndentWidth": 4,
    "MaxWidth": 100,
    "SpaceAfterComma": True,
    "SpaceAfterColon": True,
    "SpaceAroundOperators": True,
}


KEYWORDS = {
    "if",
    "else",
    "for",
    "in",
    "fn",
    "struct",
    "impl",
    "pub",
    "return",
    "let",
    "var",
    "mod",
    "use",
}

MACRO_BANG_WORDS = {"println", "print", "eprintln", "eprint"}


SPACE_AROUND_OPS = {
    "=",
    "==",
    "!=",
    "<=",
    ">=",
    "<",
    ">",
    "+",
    "-",
    "*",
    "/",
    "->",
}


NO_SPACE_AROUND_OPS = {"..", "..="}
NO_SPACE_BEFORE = {",", ";", ")", "]", "}", ".", "::", ":"}
NO_SPACE_AFTER = {"(", "[", "{", ".", "::"}


@dataclass
class Token:
    kind: str
    text: str


def find_config(start_path: str, root_path: Optional[str] = None) -> Optional[str]:
    if os.path.isfile(start_path):
        current = os.path.dirname(start_path)
    else:
        current = start_path
    root_path = os.path.abspath(root_path) if root_path else None
    while True:
        candidate = os.path.join(current, ".mlang-format")
        if os.path.isfile(candidate):
            return candidate
        if root_path and os.path.abspath(current) == root_path:
            return None
        parent = os.path.dirname(current)
        if parent == current:
            return None
        current = parent


def load_config(config_path: Optional[str]) -> Dict[str, object]:
    config = dict(DEFAULT_CONFIG)
    if not config_path:
        return config
    try:
        with open(config_path, "r", encoding="utf-8") as handle:
            for raw_line in handle:
                line = raw_line.strip()
                if not line or line.startswith("#") or line.startswith("//"):
                    continue
                if ":" not in line:
                    continue
                key, value = line.split(":", 1)
                key = key.strip()
                value = value.strip()
                if value.lower() in {"true", "false"}:
                    config[key] = value.lower() == "true"
                else:
                    try:
                        config[key] = int(value)
                    except ValueError:
                        config[key] = value
    except OSError:
        return config
    return config


def tokenize(text: str) -> Iterable[Token]:
    i = 0
    n = len(text)
    while i < n:
        ch = text[i]
        if ch in " \t\r":
            i += 1
            continue
        if ch == "\n":
            yield Token("NEWLINE", "\n")
            i += 1
            continue
        if text.startswith("//", i):
            end = text.find("\n", i)
            if end == -1:
                end = n
            yield Token("COMMENT", text[i:end])
            i = end
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end == -1:
                end = n - 2
            end += 2
            yield Token("COMMENT", text[i:end])
            i = end
            continue
        if ch == '"':
            start = i
            i += 1
            while i < n:
                if text[i] == "\\" and i + 1 < n:
                    i += 2
                    continue
                if text[i] == '"':
                    i += 1
                    break
                i += 1
            yield Token("STRING", text[start:i])
            continue
        if ch.isalpha() or ch == "_":
            start = i
            i += 1
            while i < n and (text[i].isalnum() or text[i] == "_"):
                i += 1
            yield Token("WORD", text[start:i])
            continue
        if ch.isdigit():
            start = i
            i += 1
            while i < n and (text[i].isdigit() or text[i] == "_"):
                i += 1
            if i + 1 < n and text[i] == "." and text[i + 1].isdigit():
                i += 1
                while i < n and (text[i].isdigit() or text[i] == "_"):
                    i += 1
            yield Token("NUMBER", text[start:i])
            continue

        for op in ("..=", "::", "==", "!=", "<=", ">=", "->", ".."):
            if text.startswith(op, i):
                yield Token("OP", op)
                i += len(op)
                break
        else:
            yield Token("OP", ch)
            i += 1


def _needs_space(
    prev: Optional[Token],
    cur: Token,
    space_after_comma: bool,
    space_after_colon: bool,
    space_around_ops: bool,
    type_context: bool,
) -> bool:
    if prev is None:
        return False
    if prev.kind == "OP" and prev.text in NO_SPACE_AFTER:
        return False
    if cur.kind == "OP" and cur.text in NO_SPACE_BEFORE:
        return False
    if prev.kind == "WORD" and cur.kind == "OP" and cur.text == "!":
        return False
    if prev.kind == "OP" and prev.text in NO_SPACE_AROUND_OPS:
        return False
    if cur.kind == "OP" and cur.text in NO_SPACE_AROUND_OPS:
        return False
    if type_context:
        if prev.kind == "OP" and prev.text in {"<", ">"}:
            return False
        if cur.kind == "OP" and cur.text in {"<", ">"}:
            return False
    if cur.kind == "OP" and cur.text == "{":
        if prev.kind == "OP" and prev.text in {")", "]", "}"}:
            return True
    if prev.kind == "OP" and prev.text == "}":
        if cur.kind in {"WORD", "NUMBER", "STRING"}:
            return True
    if prev.kind == "OP" and prev.text == ":":
        if not space_after_colon:
            return False
        if cur.kind == "OP" and cur.text in {",", ";", ")", "]", "}"}:
            return False
        return True
    if prev.kind == "OP" and prev.text == ",":
        if not space_after_comma:
            return False
        if cur.kind == "OP" and cur.text in {")", "]", "}"}:
            return False
        return True
    if space_around_ops:
        if cur.kind == "OP" and cur.text in SPACE_AROUND_OPS:
            return True
        if prev.kind == "OP" and prev.text in SPACE_AROUND_OPS:
            return True
    if prev.kind in {"WORD", "NUMBER", "STRING"} and cur.kind in {
        "WORD",
        "NUMBER",
        "STRING",
    }:
        return True
    if prev.kind == "WORD" and prev.text in KEYWORDS and cur.text == "(":
        return True
    if prev.kind == "WORD" and cur.kind == "OP" and cur.text == "{":
        return True
    return False


def format_text(text: str, config: Optional[Dict[str, object]] = None) -> str:
    if config is None:
        config = DEFAULT_CONFIG
    indent_width = int(config.get("IndentWidth", 4))
    indent_unit = " " * indent_width
    space_after_comma = bool(config.get("SpaceAfterComma", True))
    space_after_colon = bool(config.get("SpaceAfterColon", True))
    space_around_ops = bool(config.get("SpaceAroundOperators", True))

    output: List[str] = []
    indent_level = 0
    at_line_start = True
    prev: Optional[Token] = None
    type_context = False
    generic_depth = 0
    struct_name_pending = False
    decl_pending = False
    fn_signature_pending = False
    in_fn_params = False
    fn_param_depth = 0

    def write_indent() -> None:
        output.append(indent_unit * indent_level)

    tokens = list(tokenize(text))

    def looks_like_generic_literal(start_idx: int) -> bool:
        depth = 0
        for j in range(start_idx, len(tokens)):
            tok = tokens[j]
            if tok.kind in {"NEWLINE", "COMMENT"}:
                continue
            if tok.kind == "OP":
                if tok.text == "<":
                    depth += 1
                elif tok.text == ">":
                    depth -= 1
                    if depth == 0:
                        k = j + 1
                        while k < len(tokens) and tokens[k].kind in {"NEWLINE", "COMMENT"}:
                            k += 1
                        return (
                            k < len(tokens)
                            and tokens[k].kind == "OP"
                            and tokens[k].text == "{"
                        )
        return False

    for idx, token in enumerate(tokens):
        if (
            token.kind == "OP"
            and token.text == "<"
            and not type_context
            and prev is not None
            and prev.kind == "WORD"
            and looks_like_generic_literal(idx)
        ):
            type_context = True
        if token.kind == "NEWLINE":
            output.append("\n")
            at_line_start = True
            prev = None
            continue

        if token.kind == "COMMENT":
            if at_line_start:
                write_indent()
                at_line_start = False
            else:
                if output and not output[-1].endswith(" "):
                    output.append(" ")
            output.append(token.text)
            if "\n" in token.text:
                at_line_start = token.text.endswith("\n")
                prev = None
            else:
                prev = token
            continue

        if token.kind == "OP" and token.text == "}":
            decremented = False
            if at_line_start:
                indent_level = max(indent_level - 1, 0)
                decremented = True
                write_indent()
                at_line_start = False
            else:
                if _needs_space(
                    prev,
                    token,
                    space_after_comma,
                    space_after_colon,
                    space_around_ops,
                    type_context,
                ):
                    output.append(" ")
            output.append("}")
            if not decremented:
                indent_level = max(indent_level - 1, 0)
            prev = token
            continue

        if at_line_start:
            write_indent()
            at_line_start = False
        else:
            if _needs_space(
                prev,
                token,
                space_after_comma,
                space_after_colon,
                space_around_ops,
                type_context,
            ):
                output.append(" ")

        output.append(token.text)

        if token.kind == "OP" and token.text == "{":
            indent_level += 1

        if token.kind == "WORD" and token.text == "struct":
            struct_name_pending = True
        if token.kind == "WORD" and token.text == "fn":
            fn_signature_pending = True
        if token.kind == "WORD" and token.text in {"let", "var"}:
            decl_pending = True
        elif token.kind == "WORD" and struct_name_pending:
            struct_name_pending = False
            type_context = True

        if token.kind == "OP" and token.text == "(":
            if fn_signature_pending:
                fn_signature_pending = False
                in_fn_params = True
                fn_param_depth = 1
            elif in_fn_params:
                fn_param_depth += 1
        elif token.kind == "OP" and token.text == ")" and in_fn_params:
            fn_param_depth -= 1
            if fn_param_depth <= 0:
                in_fn_params = False

        if token.kind == "OP" and token.text == ":" and (decl_pending or in_fn_params):
            type_context = True
        if token.kind == "OP" and token.text == "->":
            type_context = True
        if decl_pending and token.kind == "OP" and token.text in {"=", ";"} and not type_context:
            decl_pending = False

        if type_context:
            if token.kind == "OP" and token.text == "<":
                generic_depth += 1
            elif token.kind == "OP" and token.text == ">" and generic_depth > 0:
                generic_depth -= 1
            elif (
                token.kind == "OP"
                and token.text in {"=", ";", "{", "}", ")", "]", ","}
                and generic_depth == 0
            ):
                type_context = False
                if token.text in {"=", ";"}:
                    decl_pending = False

        prev = token

    formatted = "".join(output)
    if formatted and not formatted.endswith("\n"):
        formatted += "\n"
    return formatted


def format_file(path: str, in_place: bool, root_path: Optional[str] = None) -> str:
    with open(path, "r", encoding="utf-8") as handle:
        text = handle.read()
    config_path = find_config(path, root_path=root_path)
    config = load_config(config_path)
    formatted = format_text(text, config)
    if in_place:
        with open(path, "w", encoding="utf-8") as handle:
            handle.write(formatted)
    return formatted


def main() -> None:
    parser = argparse.ArgumentParser(description="Format Mlang source files.")
    parser.add_argument("path", nargs="?", help="Path to .mla file (default: stdin)")
    parser.add_argument("--in-place", action="store_true", help="Rewrite file in place")
    parser.add_argument("--root", default=None, help="Workspace root for config search")
    args = parser.parse_args()

    if args.path:
        formatted = format_file(args.path, args.in_place, root_path=args.root)
        if not args.in_place:
            print(formatted, end="")
        return

    text = sys.stdin.read()
    config_path = find_config(os.getcwd(), root_path=args.root)
    config = load_config(config_path)
    print(format_text(text, config), end="")


if __name__ == "__main__":
    main()
