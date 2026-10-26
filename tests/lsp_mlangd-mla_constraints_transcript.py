#!/usr/bin/env python3
"""Completion, hover, and semantic coloring for constraint keywords."""
import argparse
import tempfile
from pathlib import Path

from lsp_testlib import JsonRpcClient, position_of, to_uri


def tokens_from_response(response):
    data = response.get("data", [])
    assert len(data) % 5 == 0, response
    line = column = 0
    tokens = {}
    for i in range(0, len(data), 5):
        dl, dc, length, kind, modifiers = data[i:i + 5]
        assert dl >= 0 and dc >= 0, (dl, dc)
        line += dl
        column = column + dc if dl == 0 else dc
        tokens[(line, column)] = (length, kind, modifiers)
    return tokens


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mlangd", default="/tmp/mlangd-mla")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="mlangd_constraints_") as directory:
        root = Path(directory)
        path = root / "constraints.mla"
        uri = to_uri(path)
        text = (
            "concept Addable<T> = req(value: T) {\n"
            "  typename T;\n"
            "  { value + value } noexcept;\n"
            "  where type_id(T) == i32;\n"
            "};\n"
            "generic<T> where Addable<T>\n"
            "fn twice(value: T) -> T { return value + value; }\n"
            "fn main() -> i32 {\n"
            '  let note = "req where concept"; // req where concept\n'
            "  /* req\n"
            "     where concept */\n"
            "  return twice(21i32);\n"
            "}\n"
        )
        path.write_text(text)
        client = JsonRpcClient([str(Path(args.mlangd).resolve()), "--stdio"])
        try:
            client.request("initialize", {"processId": None, "rootUri": to_uri(root), "capabilities": {}})
            client.notify("initialized", {})
            client.notify("textDocument/didOpen", {"textDocument": {
                "uri": uri, "languageId": "mlang", "version": 1, "text": text,
            }})
            diagnostics = client.request("textDocument/diagnostic", {"textDocument": {"uri": uri}})
            assert not diagnostics.get("items"), diagnostics
            for keyword, description in (("req", "unevaluated"), ("where", "boolean"), ("concept", "predicate")):
                line, column = position_of(text, keyword)
                hover = client.request("textDocument/hover", {
                    "textDocument": {"uri": uri}, "position": {"line": line, "character": column + 1},
                })
                assert description in hover.get("contents", {}).get("value", ""), hover

            response = client.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}})
            tokens = tokens_from_response(response)
            for keyword in ("concept", "req", "typename", "noexcept", "where", "generic"):
                position = position_of(text, keyword)
                assert tokens.get(position) == (len(keyword), 0, 0), (keyword, tokens)
            for line_no, line in enumerate(text.splitlines()):
                if 'let note' in line or '/* req' in line or 'where concept */' in line:
                    for keyword in ("req", "where", "concept"):
                        start = 0
                        while (column := line.find(keyword, start)) >= 0:
                            assert tokens.get((line_no, column), (0, -1, 0))[1] != 0, (line_no, column, tokens)
                            start = column + len(keyword)

            for version, (prefix, keyword, description) in enumerate((
                ("re", "req", "unevaluated"),
                ("whe", "where", "boolean"),
                ("conc", "concept", "predicate"),
            ), start=2):
                partial = "fn main() -> i32 { return 0; }\n" + prefix
                client.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": version}, "contentChanges": [{"text": partial}]})
                items = client.request("textDocument/completion", {
                    "textDocument": {"uri": uri}, "position": {"line": 1, "character": len(prefix)},
                })
                item = next((item for item in items if item.get("label") == keyword), None)
                assert item and item.get("kind") == 14, (keyword, items)
                assert description in item.get("documentation", {}).get("value", ""), item
                assert item.get("detail", "").startswith("keyword:"), item

            # Highlighting also works when there are no indexed declarations.
            only_concept = "concept Only<T> = req { typename T; };\n"
            client.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 5}, "contentChanges": [{"text": only_concept}]})
            tokens = tokens_from_response(client.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}}))
            for keyword in ("concept", "req", "typename"):
                assert tokens.get(position_of(only_concept, keyword)) == (len(keyword), 0, 0), tokens
            removed = "generic<T> requires true fn old(value: T) -> T { return value; }\nre"
            client.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 6}, "contentChanges": [{"text": removed}]})
            diagnostics = client.request("textDocument/diagnostic", {"textDocument": {"uri": uri}})
            assert any("MLANG-E1020" in item.get("message", "") for item in diagnostics.get("items", [])), diagnostics
            items = client.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 1, "character": 2}})
            assert not any(item.get("label") == "requires" for item in items), items
            tokens = tokens_from_response(client.request("textDocument/semanticTokens/full", {"textDocument": {"uri": uri}}))
            assert tokens.get(position_of(removed, "requires"), (0, -1, 0))[1] != 0, tokens
        finally:
            client.close()
    print("PASS: constraint keyword completion, hover, and semantic tokens")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
