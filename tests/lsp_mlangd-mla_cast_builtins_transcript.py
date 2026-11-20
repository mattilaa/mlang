#!/usr/bin/env python3
"""Primitive casts in external projects must jump to MLang's builtin reference."""
import argparse
import json
from pathlib import Path
import tempfile
from urllib.parse import unquote, urlparse

from lsp_testlib import JsonRpcClient, position_of, to_uri


CASTS = ('i8', 'i16', 'i32', 'i64', 'u8', 'u16', 'u32', 'u64', 'f32', 'f64', 'bool', 'bit')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mlangd', type=Path, required=True)
    args = parser.parse_args()
    repository = Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='mlang-cast-reference-') as directory:
        root = Path(directory).resolve()
        path = root / 'src/hal/hardware.mla'
        path.parent.mkdir(parents=True)
        # There is no workspace-local stdlib. Competing native definitions
        # make the old C-header preference reproducible on every host.
        (root / 'include').mkdir()
        (root / 'include/mlang_c_types.h').write_text(
            '\n'.join(f'typedef int {prefix}{width}_t;' for prefix in ('int', 'uint') for width in (8, 16, 32, 64)))
        (root / 'mlang_commands.json').write_text(json.dumps({
            'module_paths': [str(repository / 'stdlib')], 'files': [str(path)]}))
        text = '''fn fw_byte() -> u32 { return 7; }
fn fw_select(key: u16) -> void { return; }
fn read_bytes(size: u32, capacity: u64, out: ptr<u8>, matches: bool, key: u16) -> i32 {
    unsafe {
        var i: u64 = 0;
        if matches {
            fw_select(key);
            i = 0;
            while i < u64(size) && i < capacity { out[i] = u8(fw_byte()); i += 1; }
            return i32(size);
        }
        return 0;
    }
}
fn conversions() -> void {
'''
        for kind in CASTS:
            text += f'    let value_{kind}: {kind} = {kind}(1);\n'
        literals = [(digits + kind, kind) for kind in CASTS[:8]
                    for digits in ('13', '0x2A', '0b101')]
        for index, (literal, kind) in enumerate(literals):
            text += f'    let literal_{index}: {kind} = {literal};\n'
        text += '    let value_13u32: u32 = 13;\n    let hex_digits: u32 = 0xF32;\n'
        text += '}\nfn uart_put(value: u32) -> void {}\nfn send_cr() -> void { uart_put(13u32); }\n'
        path.write_text(text)
        client = JsonRpcClient([str(args.mlangd.resolve()), '--stdio'], cwd=root)
        try:
            client.request('initialize', {'processId': None, 'rootUri': to_uri(root), 'capabilities': {}})
            client.notify('initialized', {})
            client.notify('textDocument/didOpen', {'textDocument': {
                'uri': to_uri(path), 'languageId': 'mlang', 'version': 1, 'text': text}})
            queries = [('u64(size)', 'u64'), ('u8(fw_byte())', 'u8'), ('i32(size)', 'i32')]
            queries += [(kind + '(1)', kind) for kind in CASTS]
            queries += literals + [('13u32);', 'u32')]
            for needle, kind in queries:
                line, column = position_of(text, needle)
                if needle[0].isdigit():
                    column += needle.index(kind)
                # Uvim retries token boundaries, and gd on '(' should also work.
                for offset in range(len(kind) + 1):
                    params = {'textDocument': {'uri': to_uri(path)},
                              'position': {'line': line, 'character': column + offset}}
                    result = client.request('textDocument/definition', params)
                    assert isinstance(result, list) and result, (needle, offset, result)
                    target = result[0]
                    assert target['uri'].endswith('/stdlib/types.mla'), (needle, offset, result)
                    reference = Path(unquote(urlparse(target['uri']).path))
                    target_line = target['range']['start']['line']
                    assert reference.read_text().splitlines()[target_line] == '// @builtin ' + kind
                    hover = client.request('textDocument/hover', params)
                    assert hover and 'builtin: ' + kind in hover['contents']['value'], (needle, hover)
            for needle in ('13;', '0xF32;'):
                line, column = position_of(text, needle)
                params = {'textDocument': {'uri': to_uri(path)},
                          'position': {'line': line, 'character': column + len(needle) - 2}}
                hover = client.request('textDocument/hover', params)
                assert not hover or 'builtin: ' not in hover['contents']['value'], (needle, hover)
            for needle, name in [('fw_byte());', 'fw_byte'), ('fw_select(key);', 'fw_select'),
                                 ('value_13u32:', 'value_13u32')]:
                line, column = position_of(text, needle)
                result = client.request('textDocument/definition', {
                    'textDocument': {'uri': to_uri(path)},
                    'position': {'line': line, 'character': column + 2}})
                assert result and result[0]['uri'] == to_uri(path), (name, result)
        finally:
            client.close()
    print('PASS: primitive casts, 24 typed integer literals and token-boundary navigation')


if __name__ == '__main__':
    main()
