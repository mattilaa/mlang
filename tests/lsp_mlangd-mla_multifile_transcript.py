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

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_multifile_") as td:
        root = Path(td)
        (root / "lib").mkdir(parents=True, exist_ok=True)
        (root / "app").mkdir(parents=True, exist_ok=True)

        impl_base = root / "lib" / "base_impl.mla"
        impl_derived = root / "lib" / "derived_impl.mla"
        refs_util = root / "lib" / "util_refs.mla"
        refs_main = root / "app" / "refs_main.mla"
        refs_other = root / "app" / "refs_other.mla"

        impl_base_text = (
            "struct Base {\n"
            "  fn run(self: Base) -> i32 { return 1; }\n"
            "};\n"
        )
        impl_derived_text = (
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
        refs_other_text = (
            "use lib::util_refs::util_ref;\n"
            "fn helper() -> i32 {\n"
            "  return util_ref();\n"
            "}\n"
        )

        impl_base.write_text(impl_base_text)
        impl_derived.write_text(impl_derived_text)
        refs_util.write_text(refs_util_text)
        refs_main.write_text(refs_main_text)
        refs_other.write_text(refs_other_text)

        client = JsonRpcClient([str(mlangd), "--stdio"])
        try:
            init = client.request(
                "initialize",
                {"processId": None, "rootUri": to_uri(root), "capabilities": {}},
            )
            assert isinstance(init, dict), "initialize did not return object"
            client.notify("initialized", {})

            open_doc(client, impl_base, impl_base_text)
            open_doc(client, impl_derived, impl_derived_text)
            open_doc(client, refs_util, refs_util_text)
            open_doc(client, refs_main, refs_main_text)
            open_doc(client, refs_other, refs_other_text)

            impl_line, impl_char = position_of(impl_base_text, "run(self: Base)")
            impl_res = client.request(
                "textDocument/implementation",
                {
                    "textDocument": {"uri": to_uri(impl_base)},
                    "position": {"line": impl_line, "character": impl_char},
                },
            )
            assert isinstance(impl_res, list) and impl_res, f"implementation should be non-empty: {impl_res!r}"
            derived_line, _ = position_of(impl_derived_text, "run(self: Derived)")
            assert any(
                item.get("uri") == to_uri(impl_derived)
                and item.get("range", {}).get("start", {}).get("line") == derived_line
                for item in impl_res
            ), f"expected Derived::run implementation from another file: {impl_res!r}"

            refs_line, refs_char = position_of(refs_main_text, "util_ref();", nth=1)
            refs_res = client.request(
                "textDocument/references",
                {
                    "textDocument": {"uri": to_uri(refs_main)},
                    "position": {"line": refs_line, "character": refs_char},
                    "context": {"includeDeclaration": True},
                },
            )
            assert isinstance(refs_res, list) and len(refs_res) >= 4, f"unexpected references: {refs_res!r}"
            uris = {item.get("uri") for item in refs_res if isinstance(item, dict)}
            assert to_uri(refs_util) in uris, f"references missing definition uri: {uris!r}"
            assert to_uri(refs_main) in uris, f"references missing main uri: {uris!r}"
            assert to_uri(refs_other) in uris, f"references missing second-file uri: {uris!r}"

            refs_no_decl = client.request(
                "textDocument/references",
                {
                    "textDocument": {"uri": to_uri(refs_main)},
                    "position": {"line": refs_line, "character": refs_char},
                    "context": {"includeDeclaration": False},
                },
            )
            assert isinstance(refs_no_decl, list) and refs_no_decl, f"unexpected references(includeDeclaration=false): {refs_no_decl!r}"
            uris_no_decl = {item.get("uri") for item in refs_no_decl if isinstance(item, dict)}
            assert to_uri(refs_util) not in uris_no_decl, f"definition uri should be excluded when includeDeclaration=false: {uris_no_decl!r}"
            assert to_uri(refs_main) in uris_no_decl, f"main references missing when includeDeclaration=false: {uris_no_decl!r}"
            assert to_uri(refs_other) in uris_no_decl, f"second-file references missing when includeDeclaration=false: {uris_no_decl!r}"
        finally:
            client.close()

    print("PASS: mlangd-mla multi-file transcript (implementation + references)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
