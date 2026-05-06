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
        (root / "stdlib" / "types.mla").write_text(
            (repo_root / "stdlib" / "types.mla").read_text()
        )
        (root / "stdlib" / "std" / "strbuf.mla").write_text(
            (repo_root / "stdlib" / "std" / "strbuf.mla").read_text()
        )
        (root / "stdlib" / "src" / "std_math.c").write_text(
            (repo_root / "stdlib" / "src" / "std_math.c").read_text()
        )
        (root / "include").mkdir(parents=True, exist_ok=True)
        (root / "include" / "mlang_c_types.h").write_text(
            (repo_root / "include" / "mlang_c_types.h").read_text()
        )

        doc = root / "internal_def.mla"
        text = (
            "fn test_defs() -> i32 {\n"
            "  let iv: i32 = 1;\n"
            "  let value: string = String::new();\n"
            "  let cap: string = String::with_capacity(16);\n"
            "  String::free(cap);\n"
            "  struct Device {\n"
            "    @property(hidden) var value: i32;\n"
            "    @property(mutex, recursive) var guard: i32;\n"
            "    @property(protected) var scope: i32;\n"
            "  };\n"
            "  let letters: list<string> = [\"alpha\", \"beta\"];\n"
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

            cap_m_line, cap_m_char = position_of(text, "String::with_capacity")
            cap_m_char += len("String::")
            cap_m_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": cap_m_line, "character": cap_m_char},
                },
            )
            assert isinstance(cap_m_res, list) and cap_m_res, f"String::with_capacity definition missing: {cap_m_res!r}"
            cap_m_uri = cap_m_res[0].get("uri", "")
            assert cap_m_uri.endswith("/stdlib/std/strbuf.mla"), f"String::with_capacity uri mismatch: {cap_m_res!r}"
            cap_m_target = Path(cap_m_uri.removeprefix("file://"))
            cap_m_start = cap_m_res[0].get("range", {}).get("start", {})
            cap_m_target_line = int(cap_m_start.get("line", -1))
            assert cap_m_target_line >= 0, f"String::with_capacity target line missing: {cap_m_res!r}"
            cap_m_line_text = cap_m_target.read_text().splitlines()[cap_m_target_line]
            assert "pub fn with_capacity" in cap_m_line_text, f"String::with_capacity target text mismatch: {cap_m_line_text!r}"

            free_m_line, free_m_char = position_of(text, "String::free")
            free_m_char += len("String::")
            free_m_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": free_m_line, "character": free_m_char},
                },
            )
            assert isinstance(free_m_res, list) and free_m_res, f"String::free definition missing: {free_m_res!r}"
            free_m_uri = free_m_res[0].get("uri", "")
            assert free_m_uri.endswith("/stdlib/std/strbuf.mla"), f"String::free uri mismatch: {free_m_res!r}"
            free_m_target = Path(free_m_uri.removeprefix("file://"))
            free_m_start = free_m_res[0].get("range", {}).get("start", {})
            free_m_target_line = int(free_m_start.get("line", -1))
            assert free_m_target_line >= 0, f"String::free target line missing: {free_m_res!r}"
            free_m_line_text = free_m_target.read_text().splitlines()[free_m_target_line]
            assert "pub fn free" in free_m_line_text, f"String::free target text mismatch: {free_m_line_text!r}"

            cap_t_line, cap_t_char = position_of(text, "String::with_capacity")
            cap_t_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": cap_t_line, "character": cap_t_char},
                },
            )
            assert isinstance(cap_t_res, list) and cap_t_res, f"String type in with_capacity missing: {cap_t_res!r}"
            cap_t_uri = cap_t_res[0].get("uri", "")
            assert cap_t_uri.endswith("/stdlib/types.mla"), f"String type uri mismatch: {cap_t_res!r}"
            cap_t_target = Path(cap_t_uri.removeprefix("file://"))
            cap_t_start = cap_t_res[0].get("range", {}).get("start", {})
            cap_t_target_line = int(cap_t_start.get("line", -1))
            assert cap_t_target_line >= 0, f"String type line missing: {cap_t_res!r}"
            cap_t_line_text = cap_t_target.read_text().splitlines()[cap_t_target_line]
            assert "@builtin str8" in cap_t_line_text, f"String type target text mismatch: {cap_t_line_text!r}"

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
            assert type_uri.endswith("/stdlib/types.mla"), f"string type uri mismatch: {type_res!r}"
            type_target = Path(type_uri.removeprefix("file://"))
            type_start = type_res[0].get("range", {}).get("start", {})
            type_target_line = int(type_start.get("line", -1))
            assert type_target_line >= 0, f"string type target line missing: {type_res!r}"
            type_line_text = type_target.read_text().splitlines()[type_target_line]
            assert "@builtin str8" in type_line_text, f"string type target text mismatch: {type_line_text!r}"

            generic_line, generic_char = position_of(text, "list<string>")
            generic_char += len("list<")
            generic_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": generic_line, "character": generic_char},
                },
            )
            assert isinstance(generic_res, list) and generic_res, f"generic string type definition missing: {generic_res!r}"
            generic_uri = generic_res[0].get("uri", "")
            assert generic_uri.endswith("/stdlib/types.mla"), f"generic string type uri mismatch: {generic_res!r}"
            generic_target = Path(generic_uri.removeprefix("file://"))
            generic_start = generic_res[0].get("range", {}).get("start", {})
            generic_target_line = int(generic_start.get("line", -1))
            assert generic_target_line >= 0, f"generic string type target line missing: {generic_res!r}"
            generic_line_text = generic_target.read_text().splitlines()[generic_target_line]
            assert "@builtin str8" in generic_line_text, f"generic string type target text mismatch: {generic_line_text!r}"

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
            assert i32_uri.endswith("/stdlib/types.mla"), f"i32 type uri mismatch: {i32_res!r}"
            i32_target = Path(i32_uri.removeprefix("file://"))
            i32_start = i32_res[0].get("range", {}).get("start", {})
            i32_target_line = int(i32_start.get("line", -1))
            assert i32_target_line >= 0, f"i32 target line missing: {i32_res!r}"
            i32_line_text = i32_target.read_text().splitlines()[i32_target_line]
            assert "@builtin i32" in i32_line_text, f"i32 target text mismatch: {i32_line_text!r}"

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
            assert int_uri.endswith("/stdlib/types.mla"), f"int type uri mismatch: {int_res!r}"
            int_target = Path(int_uri.removeprefix("file://"))
            int_start = int_res[0].get("range", {}).get("start", {})
            int_target_line = int(int_start.get("line", -1))
            assert int_target_line >= 0, f"int target line missing: {int_res!r}"
            int_line_text = int_target.read_text().splitlines()[int_target_line]
            assert "@builtin i32" in int_line_text, f"int target text mismatch: {int_line_text!r}"

            hidden_line, hidden_char = position_of(text, "@property(hidden)")
            hidden_char += len("@property(")
            hidden_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": hidden_line, "character": hidden_char},
                },
            )
            assert isinstance(hidden_res, list) and hidden_res, f"hidden property definition missing: {hidden_res!r}"
            hidden_uri = hidden_res[0].get("uri", "")
            assert hidden_uri.endswith("/stdlib/types.mla"), f"hidden property uri mismatch: {hidden_res!r}"
            hidden_target = Path(hidden_uri.removeprefix("file://"))
            hidden_start = hidden_res[0].get("range", {}).get("start", {})
            hidden_target_line = int(hidden_start.get("line", -1))
            assert hidden_target_line >= 0, f"hidden property line missing: {hidden_res!r}"
            hidden_line_text = hidden_target.read_text().splitlines()[hidden_target_line]
            assert "@builtin hidden" in hidden_line_text, f"hidden property target text mismatch: {hidden_line_text!r}"

            recursive_line, recursive_char = position_of(text, "@property(mutex, recursive)")
            recursive_char += len("@property(mutex, ")
            recursive_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": recursive_line, "character": recursive_char},
                },
            )
            assert isinstance(recursive_res, list) and recursive_res, f"recursive property definition missing: {recursive_res!r}"
            recursive_uri = recursive_res[0].get("uri", "")
            assert recursive_uri.endswith("/stdlib/types.mla"), f"recursive property uri mismatch: {recursive_res!r}"
            recursive_target = Path(recursive_uri.removeprefix("file://"))
            recursive_start = recursive_res[0].get("range", {}).get("start", {})
            recursive_target_line = int(recursive_start.get("line", -1))
            assert recursive_target_line >= 0, f"recursive property line missing: {recursive_res!r}"
            recursive_line_text = recursive_target.read_text().splitlines()[recursive_target_line]
            assert "@builtin recursive" in recursive_line_text, f"recursive property target text mismatch: {recursive_line_text!r}"

            protected_line, protected_char = position_of(text, "@property(protected)")
            protected_char += len("@property(")
            protected_res = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": protected_line, "character": protected_char},
                },
            )
            assert isinstance(protected_res, list) and protected_res, f"protected property definition missing: {protected_res!r}"
            protected_uri = protected_res[0].get("uri", "")
            assert protected_uri.endswith("/stdlib/types.mla"), f"protected property uri mismatch: {protected_res!r}"
            protected_target = Path(protected_uri.removeprefix("file://"))
            protected_start = protected_res[0].get("range", {}).get("start", {})
            protected_target_line = int(protected_start.get("line", -1))
            assert protected_target_line >= 0, f"protected property line missing: {protected_res!r}"
            protected_line_text = protected_target.read_text().splitlines()[protected_target_line]
            assert "@builtin protected" in protected_line_text, f"protected property target text mismatch: {protected_line_text!r}"
        finally:
            client.close()

    print("PASS: mlangd-mla internal definition transcript (mlang + C internals)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
