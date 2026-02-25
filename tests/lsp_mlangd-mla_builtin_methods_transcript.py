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

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_builtin_methods_") as td:
        root = Path(td)
        (root / "stdlib" / "std").mkdir(parents=True, exist_ok=True)
        (root / "stdlib" / "src").mkdir(parents=True, exist_ok=True)
        for src in ["strbuf.mla", "vec.mla", "map.mla", "result.mla"]:
            (root / "stdlib" / "std" / src).write_text(
                (repo_root / "stdlib" / "std" / src).read_text()
            )
        (root / "stdlib" / "src" / "std_math.c").write_text(
            (repo_root / "stdlib" / "src" / "std_math.c").read_text()
        )
        (root / "mlang_c_types.h").write_text((repo_root / "mlang_c_types.h").read_text())

        doc = root / "builtin_methods.mla"
        text = (
            "use type VecAlias<T> = list<T>;\n"
            "fn test_builtin_methods() -> i32 {\n"
            "  let ids: VecAlias<i32> = [7, 8, 9];\n"
            "  let _ids_len: i64 = ids.len();\n"
            "  let _ids_empty: bool = ids.is_empty();\n"
            "  ids.push(10);\n"
            "  let _ids_pop: i32 = ids.pop();\n"
            "  ids.clear();\n"
            "  let _ids_contains: bool = ids.contains(7);\n"
            "  let _ids_index: i64 = ids.index_of(7);\n"
            "  ids.sort();\n"
            "  ids.sort_desc();\n"
            "  ids.reverse();\n"
            "  ids.dedup();\n"
            "  let _ids_first: i32 = ids.first();\n"
            "  let _ids_last: i32 = ids.last();\n"
            "  let scores: map<string, i32> = {\"Alice\": 95, \"Bob\": 87};\n"
            "  let _scores_len: i64 = scores.len();\n"
            "  let _scores_keys: list<string> = scores.keys();\n"
            "  let _scores_values: list<i32> = scores.values();\n"
            "  let _scores_entries: list<tuple<string, i32>> = scores.entries();\n"
            "  var s: string = String::with_capacity(32);\n"
            "  let _s_len: i64 = s.len();\n"
            "  let _s_empty: int = s.is_empty();\n"
            "  s.push_str(\"ok\");\n"
            "  let _s_clone: string = s.clone();\n"
            "  String::free(s);\n"
            "  let rr_ok: Result<i32, string> = Ok<i32, string>(7);\n"
            "  let _r_ok: bool = rr_ok.is_ok();\n"
            "  let _r_err: bool = rr_ok.is_err();\n"
            "  let _r_unwrap: i32 = rr_ok.unwrap();\n"
            "  let rr_err: Result<i32, string> = Err<i32, string>(\"x\");\n"
            "  let _r_unwrap_err: string = rr_err.unwrap_err();\n"
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

            def assert_def(token: str, advance: int, suffix: str, fn_text: str) -> None:
                line, ch = position_of(text, token)
                ch += advance
                res = client.request(
                    "textDocument/definition",
                    {
                        "textDocument": {"uri": to_uri(doc)},
                        "position": {"line": line, "character": ch},
                    },
                )
                assert isinstance(res, list) and res, f"definition missing for {token}: {res!r}"
                uri = res[0].get("uri", "")
                assert uri.endswith(suffix), f"uri mismatch for {token}: {res!r}"
                target = Path(uri.removeprefix("file://"))
                start = res[0].get("range", {}).get("start", {})
                target_line = int(start.get("line", -1))
                assert target_line >= 0, f"target line missing for {token}: {res!r}"
                target_line_text = target.read_text().splitlines()[target_line]
                assert fn_text in target_line_text, (
                    f"target text mismatch for {token}: {target_line_text!r}"
                )

            vec_checks = [
                ("ids.len", len("ids."), "/stdlib/std/vec.mla", "pub fn len"),
                ("ids.is_empty", len("ids."), "/stdlib/std/vec.mla", "pub fn is_empty"),
                ("ids.push", len("ids."), "/stdlib/std/vec.mla", "pub fn push"),
                ("ids.pop", len("ids."), "/stdlib/std/vec.mla", "pub fn pop"),
                ("ids.clear", len("ids."), "/stdlib/std/vec.mla", "pub fn clear"),
                ("ids.contains", len("ids."), "/stdlib/std/vec.mla", "pub fn contains"),
                ("ids.index_of", len("ids."), "/stdlib/std/vec.mla", "pub fn index_of"),
                ("ids.sort_desc", len("ids."), "/stdlib/std/vec.mla", "pub fn sort_desc"),
                ("ids.sort", len("ids."), "/stdlib/std/vec.mla", "pub fn sort"),
                ("ids.reverse", len("ids."), "/stdlib/std/vec.mla", "pub fn reverse"),
                ("ids.dedup", len("ids."), "/stdlib/std/vec.mla", "pub fn dedup"),
                ("ids.first", len("ids."), "/stdlib/std/vec.mla", "pub fn first"),
                ("ids.last", len("ids."), "/stdlib/std/vec.mla", "pub fn last"),
            ]
            map_checks = [
                ("scores.len", len("scores."), "/stdlib/std/map.mla", "pub fn len"),
                ("scores.keys", len("scores."), "/stdlib/std/map.mla", "pub fn keys"),
                ("scores.values", len("scores."), "/stdlib/std/map.mla", "pub fn values"),
                ("scores.entries", len("scores."), "/stdlib/std/map.mla", "pub fn entries"),
            ]
            str_checks = [
                (
                    "String::with_capacity",
                    len("String::"),
                    "/stdlib/std/strbuf.mla",
                    "pub fn with_capacity",
                ),
                ("s.push_str", len("s."), "/stdlib/std/strbuf.mla", "pub fn push_str"),
                ("s.clone", len("s."), "/stdlib/std/strbuf.mla", "pub fn clone"),
                ("String::free", len("String::"), "/stdlib/std/strbuf.mla", "pub fn free"),
            ]
            result_checks = [
                ("rr_ok.is_ok", len("rr_ok."), "/stdlib/std/result.mla", "pub fn is_ok"),
                ("rr_ok.is_err", len("rr_ok."), "/stdlib/std/result.mla", "pub fn is_err"),
                ("rr_ok.unwrap", len("rr_ok."), "/stdlib/std/result.mla", "pub fn unwrap"),
                (
                    "rr_err.unwrap_err",
                    len("rr_err."),
                    "/stdlib/std/result.mla",
                    "pub fn unwrap_err",
                ),
            ]

            for token, adv, suffix, fn_text in vec_checks + map_checks + str_checks + result_checks:
                assert_def(token, adv, suffix, fn_text)

            print("PASS: mlangd-mla builtin methods definition transcript")
        finally:
            client.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
