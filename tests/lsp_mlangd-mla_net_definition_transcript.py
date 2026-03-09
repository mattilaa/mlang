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


def def_req(client: JsonRpcClient, uri: str, line: int, char: int) -> list[dict]:
    res = client.request(
        "textDocument/definition",
        {
            "textDocument": {"uri": uri},
            "position": {"line": line, "character": char},
        },
    )
    assert isinstance(res, list) and res, f"expected non-empty definition result: {res!r}"
    return res


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--mlangd", default="/tmp/mlangd-mla")
    args = ap.parse_args()

    mlangd = Path(args.mlangd)
    if not mlangd.exists():
        raise SystemExit(f"mlangd-mla not found: {mlangd}")

    repo_root = Path(__file__).resolve().parents[1]
    net_path = repo_root / "stdlib" / "std" / "net.mla"
    types_path = repo_root / "stdlib" / "types.mla"
    net_text = net_path.read_text()
    bind_line, _ = position_of(net_text, "pub fn bind(")
    local_port_line, _ = position_of(net_text, "pub fn local_port(")

    with tempfile.TemporaryDirectory(prefix="mlangd-mla_net_def_") as td:
        root = Path(td)
        doc = root / "net_def.mla"
        text = (
            "mod std::net;\n"
            "use std::net::TcpListener;\n"
            "\n"
            "fn main() -> i32 {\n"
            "    let listener_r: Result<TcpListener, string> = TcpListener::bind(\"127.0.0.1\", 0);\n"
            "    if listener_r.is_err() { return 1; }\n"
            "    let listener = listener_r.unwrap();\n"
            "    let port_r: Result<i64, string> = listener.local_port();\n"
            "    if port_r.is_err() { return 1; }\n"
            "    return 0;\n"
            "}\n"
        )
        doc.write_text(text)

        client = JsonRpcClient([str(mlangd), "--stdio"])
        try:
            init = client.request(
                "initialize",
                {"processId": None, "rootUri": to_uri(repo_root), "capabilities": {}},
            )
            assert isinstance(init, dict), "initialize did not return object"
            client.notify("initialized", {})

            open_doc(client, doc, text)
            doc_uri = to_uri(doc)

            line, ch = position_of(text, "TcpListener::bind")
            ch += len("TcpListener::")
            bind_res = def_req(client, doc_uri, line, ch)
            bind_start = bind_res[0].get("range", {}).get("start", {})
            assert bind_res[0].get("uri") == to_uri(net_path), f"bind uri mismatch: {bind_res!r}"
            assert bind_start.get("line") == bind_line, f"bind line mismatch: {bind_res!r}"

            line, ch = position_of(text, "listener.local_port")
            ch += len("listener.")
            local_port_res = def_req(client, doc_uri, line, ch)
            local_port_start = local_port_res[0].get("range", {}).get("start", {})
            assert local_port_res[0].get("uri") == to_uri(net_path), f"local_port uri mismatch: {local_port_res!r}"
            assert local_port_start.get("line") == local_port_line, f"local_port line mismatch: {local_port_res!r}"

            line, ch = position_of(text, "listener_r.is_err")
            ch += len("listener_r.")
            is_err_res = def_req(client, doc_uri, line, ch)
            is_err_start = is_err_res[0].get("range", {}).get("start", {})
            assert is_err_res[0].get("uri") == to_uri(types_path), f"is_err uri mismatch: {is_err_res!r}"
            got_line = is_err_start.get("line")
            assert got_line in (89, 90), f"is_err line mismatch: {is_err_res!r}"
        finally:
            client.close()

    print("PASS: mlangd-mla net method definitions transcript")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
