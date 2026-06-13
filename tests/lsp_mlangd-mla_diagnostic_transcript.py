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
    ap.add_argument("--mlangd", default="/tmp/mlangd-mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd-mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_diag_") as td:
        root = Path(td)
        file_path = root / "diag_case.mla"
        uri = to_uri(file_path)

        text_ok = "fn ok() -> i32 { return 1; }\n"
        text_bad = "fn bad( -> i32 { return 1; }\n"
        text_warn = (
            "fn warn(queue_handle: i64) -> i32 {\n"
            "    if queue_handle == 0: { return 1; }\n"
            "    return 0;\n"
            "}\n"
        )
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
            open_push = client.read_until_notification("textDocument/publishDiagnostics")
            assert open_push.get("uri") == uri, f"unexpected open diagnostic uri: {open_push!r}"
            assert open_push.get("diagnostics") == [], (
                f"valid document should publish empty diagnostics: {open_push!r}"
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
            change_push = client.read_until_notification("textDocument/publishDiagnostics")
            assert change_push.get("uri") == uri, (
                f"unexpected change diagnostic uri: {change_push!r}"
            )
            push_items = change_push.get("diagnostics")
            assert isinstance(push_items, list) and push_items, (
                f"invalid document should publish diagnostics: {change_push!r}"
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

            client.notify(
                "textDocument/didChange",
                {
                    "textDocument": {"uri": uri, "version": 3},
                    "contentChanges": [{"text": text_warn}],
                },
            )
            warn_push = client.read_until_notification("textDocument/publishDiagnostics")
            assert warn_push.get("uri") == uri, (
                f"unexpected warning diagnostic uri: {warn_push!r}"
            )
            warn_items = warn_push.get("diagnostics")
            assert isinstance(warn_items, list) and warn_items, (
                f"plain colon guard should publish warning: {warn_push!r}"
            )
            assert any(
                item.get("severity") == 2
                and "plain if/else-if with ':' is discouraged" in item.get("message", "")
                for item in warn_items
                if isinstance(item, dict)
            ), f"plain colon warning missing from push diagnostics: {warn_push!r}"

            res5 = client.request("textDocument/diagnostic", {"textDocument": {"uri": uri}})
            _rid3, pull_warn_items = require_full(res5)
            assert any(
                item.get("severity") == 2
                and "plain if/else-if with ':' is discouraged" in item.get("message", "")
                for item in pull_warn_items
                if isinstance(item, dict)
            ), f"plain colon warning missing from pull diagnostics: {res5!r}"

            client.notify(
                "textDocument/didClose",
                {"textDocument": {"uri": uri}},
            )
            close_push = client.read_until_notification("textDocument/publishDiagnostics")
            assert close_push.get("uri") == uri, (
                f"unexpected close diagnostic uri: {close_push!r}"
            )
            assert close_push.get("diagnostics") == [], (
                f"closed document should clear diagnostics: {close_push!r}"
            )
        finally:
            client.close()

    print("PASS: mlangd-mla diagnostic transcript (resultId + unchanged)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
