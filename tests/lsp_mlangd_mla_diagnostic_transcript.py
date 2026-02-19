#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from lsp_testlib import JsonRpcClient
from lsp_testlib import to_uri


def require_full(res: object) -> tuple[str, list]:
    assert isinstance(res, dict), f"diagnostic response must be object: {res!r}"
    assert res.get("kind") == "full", f"expected full diagnostic report: {res!r}"
    rid = res.get("resultId")
    assert isinstance(rid, str) and rid, f"expected non-empty resultId: {res!r}"
    items = res.get("items")
    assert isinstance(items, list), f"expected items array: {res!r}"
    return rid, items


def require_unchanged(res: object, expected_rid: str) -> None:
    assert isinstance(res, dict), f"diagnostic response must be object: {res!r}"
    assert res.get("kind") == "unchanged", f"expected unchanged report: {res!r}"
    assert res.get("resultId") == expected_rid, (
        f"unexpected unchanged resultId: {res!r}, expected {expected_rid!r}"
    )


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd_mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd_mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd_mla_diag_") as td:
        root = Path(td)
        file_path = root / "diag_case.mla"
        uri = to_uri(file_path)

        text_ok = "fn ok() -> i32 { return 1; }\n"
        text_bad = "fn bad( -> i32 { return 1; }\n"
        file_path.write_text(text_ok)

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
                        "uri": uri,
                        "languageId": "mlang",
                        "version": 1,
                        "text": text_ok,
                    }
                },
            )

            res1 = client.request("textDocument/diagnostic", {"textDocument": {"uri": uri}})
            rid1, _items1 = require_full(res1)

            res2 = client.request(
                "textDocument/diagnostic",
                {"textDocument": {"uri": uri}, "previousResultId": rid1},
            )
            require_unchanged(res2, rid1)

            client.notify(
                "textDocument/didChange",
                {
                    "textDocument": {"uri": uri, "version": 2},
                    "contentChanges": [{"text": text_bad}],
                },
            )

            res3 = client.request(
                "textDocument/diagnostic",
                {"textDocument": {"uri": uri}, "previousResultId": rid1},
            )
            rid2, items2 = require_full(res3)
            assert rid2 != rid1, f"resultId should change after content change: {res3!r}"
            assert items2, f"expected syntax diagnostics after invalid edit: {res3!r}"

            res4 = client.request(
                "textDocument/diagnostic",
                {"textDocument": {"uri": uri}, "previousResultId": rid2},
            )
            require_unchanged(res4, rid2)
        finally:
            client.close()

    print("PASS: mlangd_mla diagnostic transcript (resultId + unchanged)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
