#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from lsp_testlib import JsonRpcClient
from lsp_testlib import position_of
from lsp_testlib import to_uri


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="build/mlangd-mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd-mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd_lsp_it_") as td:
        root = Path(td)

        impl_file = root / "impl.mla"
        refs_util = root / "lib" / "util_refs.mla"
        refs_main = root / "refs_main.mla"
        rename_file = root / "rename_case.mla"
        imports_file = root / "imports_case.mla"
        cfg_main = root / "app" / "cfg_main.mla"
        cfg_util = root / "modules" / "lib" / "util_cfg.mla"
        manifest = root / "mlang.toml"
        refs_util.parent.mkdir(parents=True, exist_ok=True)
        cfg_main.parent.mkdir(parents=True, exist_ok=True)
        cfg_util.parent.mkdir(parents=True, exist_ok=True)

        impl_text = (
            "struct Base {\n"
            "  fn run(self: Base) -> i32 { return 1; }\n"
            "};\n"
            "struct Derived : Base {\n"
            "  fn run(self: Derived) -> i32 { return 2; }\n"
            "};\n"
        )
        refs_util_text = "fn util_ref() -> i32 { return 9; }\n"
        refs_main_text = (
            "use lib::util_refs::util_ref;\n"
            "fn main() -> i32 {\n"
            "  let a: i32 = util_ref();\n"
            "  let b: i32 = util_ref();\n"
            "  return a + b;\n"
            "}\n"
        )
        rename_text = (
            "fn main() -> i32 {\n"
            "  let alpha: i32 = 1;\n"
            "  let beta: i32 = alpha + 1;\n"
            "  return beta;\n"
            "}\n"
        )
        imports_text = (
            "use z::z;\n"
            "use a::a;\n"
            "use a::a;\n"
            "fn main() -> i32 { return 0; }\n"
        )
        cfg_main_text = (
            "use lib::util_cfg::util_cfg;\n"
            "fn main() -> i32 {\n"
            "  return util_cfg();\n"
            "}\n"
        )
        cfg_util_text = "fn util_cfg() -> i32 { return 7; }\n"
        manifest_text = (
            "[tool.mlang]\n"
            "module_paths = [\"modules\"]\n"
        )

        impl_file.write_text(impl_text)
        refs_util.write_text(refs_util_text)
        refs_main.write_text(refs_main_text)
        rename_file.write_text(rename_text)
        imports_file.write_text(imports_text)
        cfg_main.write_text(cfg_main_text)
        cfg_util.write_text(cfg_util_text)
        manifest.write_text(manifest_text)

        client = JsonRpcClient([str(mlangd), "--stdio"])
        try:
            init = client.request(
                "initialize",
                {
                    "processId": None,
                    "rootUri": to_uri(root),
                    "capabilities": {},
                },
            )
            assert isinstance(init, dict), "initialize did not return object"
            client.notify("initialized", {})

            for path, text in [
                (impl_file, impl_text),
                (refs_util, refs_util_text),
                (refs_main, refs_main_text),
                (rename_file, rename_text),
                (imports_file, imports_text),
                (cfg_main, cfg_main_text),
            ]:
                client.notify(
                    "textDocument/didOpen",
                    {
                        "textDocument": {
                            "uri": to_uri(path),
                            "languageId": "mlang",
                            "version": 1,
                            "text": text,
                        }
                    },
                )

            impl_line, impl_char = position_of(impl_text, "run(self: Base)")
            impl_res = client.request(
                "textDocument/implementation",
                {
                    "textDocument": {"uri": to_uri(impl_file)},
                    "position": {"line": impl_line, "character": impl_char},
                },
            )
            assert isinstance(impl_res, list) and impl_res, "implementation result is empty"
            derived_line, _ = position_of(impl_text, "run(self: Derived)")
            assert any(
                item.get("uri") == to_uri(impl_file)
                and item.get("range", {}).get("start", {}).get("line") == derived_line
                for item in impl_res
            ), f"expected Derived::run implementation in {impl_res!r}"

            refs_line, refs_char = position_of(refs_main_text, "util_ref();", nth=1)
            refs_res = client.request(
                "textDocument/references",
                {
                    "textDocument": {"uri": to_uri(refs_main)},
                    "position": {"line": refs_line, "character": refs_char},
                    "context": {"includeDeclaration": True},
                },
            )
            assert isinstance(refs_res, list) and len(refs_res) >= 3, f"unexpected references: {refs_res!r}"
            assert any(item.get("uri") == to_uri(refs_util) for item in refs_res), "missing definition reference"

            ren_line, ren_char = position_of(rename_text, "alpha + 1")
            rename_res = client.request(
                "textDocument/rename",
                {
                    "textDocument": {"uri": to_uri(rename_file)},
                    "position": {"line": ren_line, "character": ren_char},
                    "newName": "beta",
                },
            )
            assert rename_res is None, f"unsafe rename should be blocked, got: {rename_res!r}"

            fmt_res = client.request(
                "textDocument/rangeFormatting",
                {
                    "textDocument": {"uri": to_uri(imports_file)},
                    "range": {"start": {"line": 0, "character": 0}, "end": {"line": 3, "character": 0}},
                    "options": {"tabSize": 2, "insertSpaces": True},
                },
            )
            assert isinstance(fmt_res, list), "rangeFormatting must return edit array"

            code_action_res = client.request(
                "textDocument/codeAction",
                {
                    "textDocument": {"uri": to_uri(imports_file)},
                    "range": {"start": {"line": 0, "character": 0}, "end": {"line": 3, "character": 0}},
                    "context": {"diagnostics": [], "only": ["source.organizeImports"]},
                },
            )
            assert isinstance(code_action_res, list) and code_action_res, "expected organizeImports code action"
            organize = next(
                (a for a in code_action_res if a.get("kind") == "source.organizeImports"),
                None,
            )
            assert organize is not None, f"missing source.organizeImports: {code_action_res!r}"
            edits = (
                organize.get("edit", {})
                .get("changes", {})
                .get(to_uri(imports_file), [])
            )
            assert edits, f"organizeImports returned no edits: {organize!r}"
            new_text = edits[0].get("newText", "")
            assert new_text == "use a::a;\nuse z::z;\n", f"unexpected organizeImports text: {new_text!r}"

            cfg_line, cfg_char = position_of(cfg_main_text, "util_cfg();")
            cfg_def = client.request(
                "textDocument/definition",
                {
                    "textDocument": {"uri": to_uri(cfg_main)},
                    "position": {"line": cfg_line, "character": cfg_char},
                },
            )
            assert isinstance(cfg_def, list) and cfg_def, f"cfg definition result empty: {cfg_def!r}"
            assert any(
                item.get("uri") == to_uri(cfg_util)
                and item.get("range", {}).get("start", {}).get("line") == 0
                for item in cfg_def
            ), f"expected cfg module definition in {cfg_def!r}"

            print("LSP integration transcript checks passed.")
        finally:
            client.close()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
