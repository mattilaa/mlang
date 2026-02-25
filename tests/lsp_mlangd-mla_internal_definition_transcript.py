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
        (root / "stdlib" / "src").mkdir(parents=True, exist_ok=True)
        (root / "stdlib" / "std" / "strbuf.mla").write_text(
            (repo_root / "stdlib" / "std" / "strbuf.mla").read_text()
        )
        (root / "stdlib" / "std" / "vec.mla").write_text(
            (repo_root / "stdlib" / "std" / "vec.mla").read_text()
        )
        (root / "stdlib" / "std" / "map.mla").write_text(
            (repo_root / "stdlib" / "std" / "map.mla").read_text()
        )
        (root / "stdlib" / "std" / "result.mla").write_text(
            (repo_root / "stdlib" / "std" / "result.mla").read_text()
        )
        (root / "stdlib" / "src" / "std_math.c").write_text(
            (repo_root / "stdlib" / "src" / "std_math.c").read_text()
        )
        (root / "mlang_c_types.h").write_text((repo_root / "mlang_c_types.h").read_text())

        doc = root / "internal_def.mla"
        text = (
            "use type VecAlias<T> = list<T>;\n"
            "fn test_defs() -> i32 {\n"
            "  let iv: i32 = 1;\n"
            "  let value: string = String::new();\n"
            "  let ids: VecAlias<i32> = [7, 8, 9];\n"
            "  println!(\"ids.len={}\", ids.len());\n"
            "  let scores: map<string, i32> = {\"Alice\": 95, \"Bob\": 87};\n"
            "  println!(\"scores.len={}\", scores.len());\n"
            "  for name in scores.keys() {\n"
            "    println!(\"Name: {}\", name);\n"
            "  }\n"
            "  for score in scores.values() {\n"
            "    println!(\"Score: {}\", score);\n"
            "  }\n"
            "  for entry in scores.entries() {\n"
            "    println!(\"{} scored {}\", entry.0, entry.1);\n"
            "  }\n"
            "  var s: string = String::new();\n"
            "  s.push_str(\"ok\");\n"
            "  let rr: Result<i32, string> = Ok<i32, string>(7);\n"
            "  println!(\"rr.ok={}\", rr.unwrap());\n"
            "  let num: int = 1;\n"
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

            len_line, len_char = position_of(text, "ids.len")
            len_char += len("ids.")
            len_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": len_line, "character": len_char},
                },
            )
            assert isinstance(len_res, list) and len_res, f"list len definition missing: {len_res!r}"
            len_uri = len_res[0].get("uri", "")
            assert len_uri.endswith("/stdlib/std/vec.mla"), f"list len uri mismatch: {len_res!r}"
            len_target = Path(len_uri.removeprefix("file://"))
            len_start = len_res[0].get("range", {}).get("start", {})
            len_target_line = int(len_start.get("line", -1))
            assert len_target_line >= 0, f"list len target line missing: {len_res!r}"
            len_line_text = len_target.read_text().splitlines()[len_target_line]
            assert "pub fn len" in len_line_text, f"list len target text mismatch: {len_line_text!r}"

            keys_line, keys_char = position_of(text, "scores.keys")
            keys_char += len("scores.")
            keys_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": keys_line, "character": keys_char},
                },
            )
            assert isinstance(keys_res, list) and keys_res, f"map keys definition missing: {keys_res!r}"
            keys_uri = keys_res[0].get("uri", "")
            assert keys_uri.endswith("/stdlib/std/map.mla"), f"map keys uri mismatch: {keys_res!r}"
            keys_target = Path(keys_uri.removeprefix("file://"))
            keys_start = keys_res[0].get("range", {}).get("start", {})
            keys_target_line = int(keys_start.get("line", -1))
            assert keys_target_line >= 0, f"map keys target line missing: {keys_res!r}"
            keys_line_text = keys_target.read_text().splitlines()[keys_target_line]
            assert "pub fn keys" in keys_line_text, f"map keys target text mismatch: {keys_line_text!r}"

            map_len_line, map_len_char = position_of(text, "scores.len")
            map_len_char += len("scores.")
            map_len_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": map_len_line, "character": map_len_char},
                },
            )
            assert isinstance(map_len_res, list) and map_len_res, f"map len definition missing: {map_len_res!r}"
            map_len_uri = map_len_res[0].get("uri", "")
            assert map_len_uri.endswith("/stdlib/std/map.mla"), f"map len uri mismatch: {map_len_res!r}"
            map_len_target = Path(map_len_uri.removeprefix("file://"))
            map_len_start = map_len_res[0].get("range", {}).get("start", {})
            map_len_target_line = int(map_len_start.get("line", -1))
            assert map_len_target_line >= 0, f"map len target line missing: {map_len_res!r}"
            map_len_line_text = map_len_target.read_text().splitlines()[map_len_target_line]
            assert "pub fn len" in map_len_line_text, f"map len target text mismatch: {map_len_line_text!r}"

            values_line, values_char = position_of(text, "scores.values")
            values_char += len("scores.")
            values_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": values_line, "character": values_char},
                },
            )
            assert isinstance(values_res, list) and values_res, f"map values definition missing: {values_res!r}"
            values_uri = values_res[0].get("uri", "")
            assert values_uri.endswith("/stdlib/std/map.mla"), f"map values uri mismatch: {values_res!r}"
            values_target = Path(values_uri.removeprefix("file://"))
            values_start = values_res[0].get("range", {}).get("start", {})
            values_target_line = int(values_start.get("line", -1))
            assert values_target_line >= 0, f"map values target line missing: {values_res!r}"
            values_line_text = values_target.read_text().splitlines()[values_target_line]
            assert "pub fn values" in values_line_text, f"map values target text mismatch: {values_line_text!r}"

            entries_line, entries_char = position_of(text, "scores.entries")
            entries_char += len("scores.")
            entries_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": entries_line, "character": entries_char},
                },
            )
            assert isinstance(entries_res, list) and entries_res, f"map entries definition missing: {entries_res!r}"
            entries_uri = entries_res[0].get("uri", "")
            assert entries_uri.endswith("/stdlib/std/map.mla"), f"map entries uri mismatch: {entries_res!r}"
            entries_target = Path(entries_uri.removeprefix("file://"))
            entries_start = entries_res[0].get("range", {}).get("start", {})
            entries_target_line = int(entries_start.get("line", -1))
            assert entries_target_line >= 0, f"map entries target line missing: {entries_res!r}"
            entries_line_text = entries_target.read_text().splitlines()[entries_target_line]
            assert "pub fn entries" in entries_line_text, f"map entries target text mismatch: {entries_line_text!r}"

            push_line, push_char = position_of(text, "s.push_str")
            push_char += len("s.")
            push_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": push_line, "character": push_char},
                },
            )
            assert isinstance(push_res, list) and push_res, f"string push_str definition missing: {push_res!r}"
            push_uri = push_res[0].get("uri", "")
            assert push_uri.endswith("/stdlib/std/strbuf.mla"), f"string push_str uri mismatch: {push_res!r}"
            push_target = Path(push_uri.removeprefix("file://"))
            push_start = push_res[0].get("range", {}).get("start", {})
            push_target_line = int(push_start.get("line", -1))
            assert push_target_line >= 0, f"string push_str target line missing: {push_res!r}"
            push_line_text = push_target.read_text().splitlines()[push_target_line]
            assert "pub fn push_str" in push_line_text, f"string push_str target text mismatch: {push_line_text!r}"

            unwrap_line, unwrap_char = position_of(text, "rr.unwrap")
            unwrap_char += len("rr.")
            unwrap_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": unwrap_line, "character": unwrap_char},
                },
            )
            assert isinstance(unwrap_res, list) and unwrap_res, f"result unwrap definition missing: {unwrap_res!r}"
            unwrap_uri = unwrap_res[0].get("uri", "")
            assert unwrap_uri.endswith("/stdlib/std/result.mla"), f"result unwrap uri mismatch: {unwrap_res!r}"
            unwrap_target = Path(unwrap_uri.removeprefix("file://"))
            unwrap_start = unwrap_res[0].get("range", {}).get("start", {})
            unwrap_target_line = int(unwrap_start.get("line", -1))
            assert unwrap_target_line >= 0, f"result unwrap target line missing: {unwrap_res!r}"
            unwrap_line_text = unwrap_target.read_text().splitlines()[unwrap_target_line]
            assert "pub fn unwrap" in unwrap_line_text, f"result unwrap target text mismatch: {unwrap_line_text!r}"

            i32_line, i32_char = position_of(text, "i32 =")
            i32_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": i32_line, "character": i32_char},
                },
            )
            assert isinstance(i32_res, list) and i32_res, f"i32 definition missing: {i32_res!r}"
            i32_uri = i32_res[0].get("uri", "")
            i32_target = Path(i32_uri.removeprefix("file://"))
            i32_start = i32_res[0].get("range", {}).get("start", {})
            i32_target_line = int(i32_start.get("line", -1))
            assert i32_target_line >= 0, f"i32 target line missing: {i32_res!r}"
            i32_line_text = i32_target.read_text().splitlines()[i32_target_line]
            assert "int32_t" in i32_line_text, f"i32 target text mismatch: {i32_line_text!r}"

            int_line, int_char = position_of(text, "int =")
            int_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": int_line, "character": int_char},
                },
            )
            assert isinstance(int_res, list) and int_res, f"int definition missing: {int_res!r}"
            int_uri = int_res[0].get("uri", "")
            int_target = Path(int_uri.removeprefix("file://"))
            int_start = int_res[0].get("range", {}).get("start", {})
            int_target_line = int(int_start.get("line", -1))
            assert int_target_line >= 0, f"int target line missing: {int_res!r}"
            int_line_text = int_target.read_text().splitlines()[int_target_line]
            assert "int32_t" in int_line_text, f"int target text mismatch: {int_line_text!r}"
        finally:
            client.close()

    print("PASS: mlangd-mla internal definition transcript (mlang + C internals)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
