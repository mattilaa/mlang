#!/usr/bin/env python3
import argparse
import tempfile
from pathlib import Path

from lsp_testlib import JsonRpcClient
from lsp_testlib import to_uri


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd_mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd_mla not found: {mlangd}")

    with tempfile.TemporaryDirectory(prefix="mlangd_mla_semantic_") as td:
        root = Path(td)
        file_path = root / "semantic_case.mla"
        uri = to_uri(file_path)
        text = (
            "fn sem_fn(x: i32) -> i32 { return x; }\n"
        )
        file_path.write_text(text)

        client = JsonRpcClient([str(mlangd), "--stdio"])
        try:
            init = client.request(
                "initialize",
                {"processId": None, "rootUri": to_uri(root), "capabilities": {}},
            )
            assert isinstance(init, dict), "initialize did not return object"
            legend = (
                init.get("capabilities", {})
                .get("semanticTokensProvider", {})
                .get("legend", {})
            )
            modifiers = legend.get("tokenModifiers", [])
            assert "declaration" in modifiers and "static" in modifiers, f"semantic token modifiers legend missing: {legend!r}"

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

            sem = client.request(
                "textDocument/semanticTokens/full",
                {"textDocument": {"uri": uri}},
            )
            assert isinstance(sem, dict), f"semanticTokens/full should return object: {sem!r}"
            data = sem.get("data", [])
            assert isinstance(data, list), f"semantic token data should be array: {sem!r}"
            assert data, "semantic token data should not be empty"
            assert len(data) % 5 == 0, f"semantic token data length should be multiple of 5: len={len(data)}"

            # token tuple: [deltaLine, deltaStart, length, tokenType, tokenModifiers]
            token_types = [data[i + 3] for i in range(0, len(data), 5)]
            token_mods = [data[i + 4] for i in range(0, len(data), 5)]
            assert 2 in token_types, f"expected function token type (2): {token_types!r}"
            assert any(m > 0 for m in token_mods), f"expected non-zero token modifiers: {token_mods!r}"
        finally:
            client.close()

    print("PASS: mlangd_mla semantic tokens transcript (typed + modifiers)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
