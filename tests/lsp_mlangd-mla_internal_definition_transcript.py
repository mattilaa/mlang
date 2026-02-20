#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from lsp_testlib import JsonRpcClient
from lsp_testlib import position_of
from lsp_testlib import to_uri


def open_doc(client: JsonRpcClient, path: Path, text: str, version: int = 1) -> None:
    client.notify(
        "textDocument/didOpen",
        {
            "textDocument": {
                "uri": to_uri(path),
                "languageId": "mlang",
                "version": version,
                "text": text,
            }
        },
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd-mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd-mla not found: {mlangd}")

    repo_root = Path(__file__).resolve().parents[1]

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_internal_def_") as td:
        root = Path(td)
        (root / "stdlib" / "std").mkdir(parents=True, exist_ok=True)
        (root / "stdlib" / "std" / "strbuf.mla").write_text(
            (repo_root / "stdlib" / "std" / "strbuf.mla").read_text()
        )
        (root / "mlang_c_types.h").write_text((repo_root / "mlang_c_types.h").read_text())

        doc = root / "internal_def.mla"
        text = (
            "fn test_defs() -> i32 {\n"
            "  let value: string = String::new();\n"
            "  return 0;\n"
            "}\n"
        )
        doc.write_text(text)

        client = JsonRpcClient([str(mlangd), "--stdio"])
        try:
            init = client.request(
                "initialize",
                {"processId": None, "rootUri": to_uri(root), "capabilities": {}},
            )
            assert isinstance(init, dict), "initialize did not return object"
            client.notify("initialized", {})

            open_doc(client, doc, text)

            new_line, new_char = position_of(text, "String::new")
            new_char += len("String::")
            new_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": new_line, "character": new_char},
                },
            )
            assert isinstance(new_res, list) and new_res, f"String::new definition missing: {new_res!r}"
            new_uri = new_res[0].get("uri", "")
            assert new_uri.endswith("/stdlib/std/strbuf.mla"), f"String::new uri mismatch: {new_res!r}"
            new_target = Path(new_uri.removeprefix("file://"))
            new_start = new_res[0].get("range", {}).get("start", {})
            new_target_line = int(new_start.get("line", -1))
            assert new_target_line >= 0, f"String::new target line missing: {new_res!r}"
            new_line_text = new_target.read_text().splitlines()[new_target_line]
            assert "pub fn new" in new_line_text, f"String::new target text mismatch: {new_line_text!r}"

            type_line, type_char = position_of(text, "string =")
            type_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": type_line, "character": type_char},
                },
            )
            assert isinstance(type_res, list) and type_res, f"string type definition missing: {type_res!r}"
            type_uri = type_res[0].get("uri", "")
            assert type_uri.endswith("/mlang_c_types.h"), f"string type uri mismatch: {type_res!r}"
            type_target = Path(type_uri.removeprefix("file://"))
            type_start = type_res[0].get("range", {}).get("start", {})
            type_target_line = int(type_start.get("line", -1))
            assert type_target_line >= 0, f"string type target line missing: {type_res!r}"
            type_line_text = type_target.read_text().splitlines()[type_target_line]
            assert "mlang_string" in type_line_text, f"string type target text mismatch: {type_line_text!r}"
        finally:
            client.close()

    print("PASS: mlangd-mla internal definition transcript (mlang + C internals)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
