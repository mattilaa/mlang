#!/usr/bin/env python3
"""ceval/cinit expose builtin hover, definition and completion documentation."""
import argparse
import json
from pathlib import Path
import tempfile
from urllib.parse import unquote, urlparse

from lsp_testlib import JsonRpcClient, position_of, to_uri


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mlangd', type=Path, required=True)
    args = parser.parse_args()
    repository = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='mlang-const-specifiers-') as directory:
        root = Path(directory).resolve()
        path = root / 'main.mla'
        text = '''ceval fn initial() -> i32 { return 40; }
cinit var total: i32 = initial();
fn next() -> i32 {
    cinit static var count: i32 = initial();
    count += 1;
    return count;
}
fn main() -> i32 { return next() - total; }
'''
        path.write_text(text)
        (root / 'mlang_commands.json').write_text(json.dumps({
            'module_paths': [str(repository / 'stdlib')], 'files': [str(path)]}))
        client = JsonRpcClient([str(args.mlangd.resolve()), '--stdio'], cwd=root)
        try:
            client.request('initialize', {'rootUri': to_uri(root), 'capabilities': {}})
            client.notify('initialized', {})
            client.notify('textDocument/didOpen', {'textDocument': {
                'uri': to_uri(path), 'languageId': 'mlang', 'version': 1, 'text': text}})
            for keyword in ('ceval', 'cinit'):
                line, column = position_of(text, keyword)
                for offset in range(len(keyword)):
                    params = {'textDocument': {'uri': to_uri(path)},
                              'position': {'line': line, 'character': column + offset}}
                    result = client.request('textDocument/definition', params)
                    assert result and result[0]['uri'].endswith('/stdlib/types.mla'), (keyword, result)
                    target = result[0]
                    reference = Path(unquote(urlparse(target['uri']).path))
                    assert reference.read_text().splitlines()[target['range']['start']['line']] == '// @builtin ' + keyword
                    hover = client.request('textDocument/hover', params)
                    assert hover and keyword in hover['contents']['value'], (keyword, hover)
                params['position']['character'] = column + 2
                items = client.request('textDocument/completion', params)
                item = next((item for item in items if item['label'] == keyword), None)
                assert item and item.get('kind') == 14, (keyword, item)
                assert item.get('documentation', {}).get('value'), (keyword, item)
        finally:
            client.close()
    print('PASS: ceval/cinit builtin hover, definition and completion in an external workspace')


if __name__ == '__main__':
    main()
