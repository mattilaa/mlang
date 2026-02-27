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

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_workspace_diag_") as td:
        root = Path(td)
        ok_path = root / "ok.mla"
        bad_path = root / "bad.mla"
        ok_uri = to_uri(ok_path)
        bad_uri = to_uri(bad_path)

        ok_text = (
            "fn ok() -> i32 {\n"
            "    return 1;\n"
            "}\n"
        )
        bad_text = "fn bad( -> i32 { return 1; }\n"
        ok_path.write_text(ok_text)
        bad_path.write_text(bad_text)

        client = JsonRpcClient([str(mlangd), "--stdio"])
        try:
            init = client.request(
                "initialize",
                {"processId": None, "rootUri": to_uri(root), "capabilities": {}},
            )
            assert isinstance(init, dict), "initialize did not return object"
            diag_caps = init.get("capabilities", {}).get("diagnosticProvider", {})
            assert diag_caps.get("workspaceDiagnostics") is True, f"workspace diagnostics capability missing: {diag_caps!r}"
            client.notify("initialized", {})

            client.notify(
                "textDocument/didOpen",
                {
                    "textDocument": {
                        "uri": ok_uri,
                        "languageId": "mlang",
                        "version": 1,
                        "text": ok_text,
                    }
                },
            )
            client.notify(
                "textDocument/didOpen",
                {
                    "textDocument": {
                        "uri": bad_uri,
                        "languageId": "mlang",
                        "version": 1,
                        "text": bad_text,
                    }
                },
            )

            ws = client.request("workspace/diagnostic", {"identifier": "mlangd-mla"})
            assert isinstance(ws, dict), f"workspace/diagnostic should return object: {ws!r}"
            items = ws.get("items")
            assert isinstance(items, list), f"workspace/diagnostic.items should be list: {ws!r}"
            assert len(items) >= 2, f"workspace/diagnostic should include opened docs: {ws!r}"

            per_uri = {item.get("uri"): item for item in items if isinstance(item, dict)}
            assert ok_uri in per_uri, f"ok doc missing from workspace diagnostics: {ws!r}"
            assert bad_uri in per_uri, f"bad doc missing from workspace diagnostics: {ws!r}"

            ok_report = per_uri[ok_uri]
            bad_report = per_uri[bad_uri]
            assert ok_report.get("kind") == "full", f"ok report should be full: {ok_report!r}"
            assert bad_report.get("kind") == "full", f"bad report should be full: {bad_report!r}"
            ok_diags = ok_report.get("items", [])
            bad_diags = bad_report.get("items", [])
            assert isinstance(ok_diags, list) and isinstance(bad_diags, list), f"invalid diagnostic items shape: {ws!r}"
            assert len(bad_diags) >= 1, f"expected diagnostics for bad file: {bad_report!r}"
        finally:
            client.close()

    print("PASS: mlangd-mla workspace diagnostic transcript")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
