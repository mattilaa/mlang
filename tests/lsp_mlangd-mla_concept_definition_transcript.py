#!/usr/bin/env python3
"""Concept and builtin navigation, hover, completion, and document symbols."""
import argparse
from pathlib import Path
import re
import tempfile
from lsp_testlib import JsonRpcClient, position_of, to_uri


def definition(client, uri, text, word):
    line, column = position_of(text, word)
    result = client.request("textDocument/definition", {"textDocument": {"uri": uri}, "position": {"line": line, "character": column + 1}})
    if isinstance(result, dict) and "uri" in result:
        return result
    assert isinstance(result, list) and result, (word, result)
    return result[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mlangd", default="/tmp/mlangd-mla")
    parser.add_argument("--navigation-only", action="store_true", help="Check legacy servers without concept completion metadata")
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[1]
    reference = repo / "stdlib/types.mla"
    module = repo / "stdlib/std/concepts.mla"
    reference_text = reference.read_text()
    module_text = module.read_text()
    concepts = re.findall(r"^pub concept (\w+)<", module_text, re.M)
    keywords = ["where", "req", "concept", "generic", "typename", "noexcept", "requires", "array", "narrow_cast", "is_constructible", "volatile", "operator", "likely", "unlikely", "type_id", "type", "in", "free_objs"]
    with tempfile.TemporaryDirectory(prefix="mlangd_concept_def_") as directory:
        root = Path(directory); path = root / "concepts.mla"; uri = to_uri(path)
        client = JsonRpcClient([str(Path(args.mlangd).resolve()), "--stdio"])
        try:
            client.request("initialize", {"processId": None, "rootUri": to_uri(root), "capabilities": {}})
            client.notify("initialized", {})
            text = "\n".join(keywords) + "\n"
            path.write_text(text)
            client.notify("textDocument/didOpen", {"textDocument": {"uri": uri, "languageId": "mlang", "version": 1, "text": text}})
            for keyword in keywords:
                location = definition(client, uri, text, keyword + "\n")
                marker = "// @builtin_fn " if keyword == "free_objs" else "// @builtin "
                expected, _ = position_of(reference_text, marker + keyword + "\n")
                if keyword == "array" and location["uri"] != to_uri(reference):
                    array_file = repo / "stdlib/std/array.mla"
                    expected, _ = position_of(array_file.read_text(), "// @builtin array")
                    assert location["uri"] == to_uri(array_file), (keyword, location)
                else:
                    assert location["uri"] == to_uri(reference), (keyword, location)
                assert location["range"]["start"]["line"] == expected, (keyword, location)
                if keyword != "array" or location["uri"] == to_uri(reference):
                    assert location["range"]["start"]["character"] == len(marker), (keyword, location)
            text = (
                "mod std::concepts;\nuse std::concepts::*;\n"
                "use std::concepts::same_as as Equal;\n"
                "/// A local reusable predicate.\n"
                "concept Local<T> = req(value: T) { where arithmetic<T>; { value + value } -> same_as<T>; };\n"
                "generic<T> where Local<T> fn twice(value: T) -> T { return value + value; }\n"
                "fn main() -> i32 {\n"
                "  static_assert!(Equal<i32, i32>);\n"
                "  static_assert!(std::concepts::same_as<i32, i32>);\n"
                "  static_assert!(integral<i32> && signed_integral<i32> && unsigned_integral<u32> && floating_point<f64>);\n"
                "  return twice(21i32);\n}\n"
            )
            client.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 2}, "contentChanges": [{"text": text}]})
            for name in concepts:
                # Find a use, bypassing specific-import syntax for same_as.
                needle = name + "<"
                location = definition(client, uri, text, needle)
                expected, _ = position_of(module_text, "pub concept " + name + "<")
                assert location["uri"] == to_uri(module), (name, location)
                assert location["range"]["start"]["line"] == expected, (name, location)
            qualified = definition(client, uri, text, "same_as<i32, i32>")
            assert qualified["uri"] == to_uri(module), qualified
            location = definition(client, uri, text, "Equal<i32")
            assert location["uri"] == to_uri(module), location
            location = definition(client, uri, text, "Local<T> fn")
            assert location["uri"] == uri, location
            expected, _ = position_of(text, "concept Local")
            assert location["range"]["start"]["line"] == expected, location
            if args.navigation_only:
                print("PASS: builtin and concept definitions, aliases, and local concepts")
                return
            line, column = position_of(text, "arithmetic<T>")
            hover = client.request("textDocument/hover", {"textDocument": {"uri": uri}, "position": {"line": line, "character": column + 1}})
            assert "concept arithmetic" in hover["contents"]["value"], hover
            symbols = client.request("textDocument/documentSymbol", {"textDocument": {"uri": uri}})
            assert any(symbol.get("name") == "Local" and symbol.get("kind") == 11 for symbol in symbols), symbols
            partial = "mod std::concepts;\nuse std::concepts::*;\nfn main() -> i32 { ari }\n"
            client.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 3}, "contentChanges": [{"text": partial}]})
            line, column = position_of(partial, "ari")
            items = client.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": line, "character": column + 3}})
            item = next((item for item in items if item.get("label") == "arithmetic"), None)
            assert item and item.get("kind") == 11, items
            assert "integral" in item.get("documentation", {}).get("value", ""), item
            shadow = "mod std::concepts;\nuse std::concepts::*;\nfn main() -> i32 { let arithmetic: i32 = 1; return arithmetic; }\n"
            client.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 4}, "contentChanges": [{"text": shadow}]})
            location = definition(client, uri, shadow, "arithmetic;")
            assert location["uri"] == uri, location
            private_module = root / "private_concepts.mla"
            private_module.write_text("concept Hidden<T> = true;\npub concept Visible<T> = true;\n")
            visibility = "mod private_concepts;\nuse private_concepts::*;\nfn main() -> i32 { static_assert!(Visible<i32>); return 0; }\n"
            client.notify("textDocument/didChange", {"textDocument": {"uri": uri, "version": 5}, "contentChanges": [{"text": visibility}]})
            location = definition(client, uri, visibility, "Visible<i32>")
            assert location["uri"] == to_uri(private_module), location
            symbols = client.request("textDocument/completion", {"textDocument": {"uri": uri}, "position": {"line": 2, "character": 0}})
            assert not any(symbol.get("label") == "Hidden" for symbol in symbols), symbols
        finally:
            client.close()
    print("PASS: builtin and concept definitions, aliases, hover, completion, and symbols")


if __name__ == "__main__":
    main()
