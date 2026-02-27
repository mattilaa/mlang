#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from lsp_testlib import JsonRpcClient
from lsp_testlib import to_uri


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd-mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd-mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_folding_") as td:
        root = Path(td)
        file_path = root / "fold_case.mla"
        uri = to_uri(file_path)
        text = (
            "fn helper() -> i32 {\n"
            "    let x: i32 = 1;\n"
            "    return x;\n"
            "}\n"
            "\n"
            "fn main() -> i32 {\n"
            "    let y: i32 = helper();\n"
            "    return y;\n"
            "}\n"
        )
        file_path.write_text(text)

        client = JsonRpcClient([str(mlangd), "--stdio"])
        try:
            init = client.request(
                "initialize",
                {"processId": None, "rootUri": to_uri(root), "capabilities": {}},
            )
            assert isinstance(init, dict), "initialize did not return object"
            caps = init.get("capabilities", {})
            assert caps.get("foldingRangeProvider") is True, f"foldingRangeProvider missing: {caps!r}"
            client.notify("initialized", {})
            client.notify(
                "textDocument/didOpen",
                {
                    "textDocument": {
                        "uri": uri,
                        "languageId": "mlang",
                        "version": 1,
                        "text": text,
                    }
                },
            )

            folds = client.request("textDocument/foldingRange", {"textDocument": {"uri": uri}})
            assert isinstance(folds, list), f"foldingRange should return array: {folds!r}"
        finally:
            client.close()

    print("PASS: mlangd-mla folding range transcript")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
