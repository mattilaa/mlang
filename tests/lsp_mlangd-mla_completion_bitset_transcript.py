#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from lsp_testlib import JsonRpcClient
from lsp_testlib import position_of
from lsp_testlib import to_uri


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd-mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd-mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_bitset_completion_") as td:
        root = Path(td)
        doc = root / "bitset_completion.mla"
        text = (
            "mod std::bitset;\n"
            "use std::bitset::BitSet;\n"
            "\n"
            "fn main() -> i32 {\n"
            "  let r: Result<BitSet, str8> = BitSet::new(8);\n"
            "  let bs: BitSet = r.unwrap();\n"
            "  bs.\n"
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
            client.notify(
                "textDocument/didOpen",
                {
                    "textDocument": {
                        "uri": to_uri(doc),
                        "languageId": "mlang",
                        "version": 1,
                        "text": text,
                    }
                },
            )

            line, char = position_of(text, "bs.")
            char += len("bs.")
            res = client.request(
                "textDocument/completion",
                {
                    "textDocument": {"uri": to_uri(doc)},
                    "position": {"line": line, "character": char},
                },
            )
            assert isinstance(res, list) and res, f"bitset member completion should return items: {res!r}"

            labels = {it.get("label") for it in res if isinstance(it, dict)}
            assert "set" in labels, f"BitSet member completion missing set(): {res!r}"
            assert "get" in labels, f"BitSet member completion missing get(): {res!r}"
            assert "count_ones" in labels, f"BitSet member completion missing count_ones(): {res!r}"
        finally:
            client.close()

    print("PASS: mlangd-mla completion transcript includes BitSet members")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
