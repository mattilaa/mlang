#!/usr/bin/env python3
"""Resolve embedded builtins and ordinary calls in an external firmware workspace."""
import argparse
from pathlib import Path
import tempfile

from lsp_testlib import JsonRpcClient, position_of, to_uri


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mlangd', required=True, type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='mlang-hardware-lsp-') as directory:
        root = Path(directory)
        path = root / 'src/runtime/memory.mla'
        path.parent.mkdir(parents=True)
        text = '''pub fn memory_copy(dst: ptr<u8>, src: ptr<u8>, size: u64) -> ptr<u8> {
    unsafe {
        var i: u64 = 0;
        while i < size { volatile_store(dst + i, volatile_load(src + i)); i += 1; }
        return dst;
    }
}
pub fn memory_move(dst: ptr<u8>, src: ptr<u8>, size: u64) -> ptr<u8> {
    unsafe {
        if ptr_address(dst) < ptr_address(src) { return memory_copy(dst, src, size); }
        var i: u64 = size;
        while i > 0 { i -= 1; volatile_store(dst + i, volatile_load(src + i)); }
        return dst;
    }
}
pub fn address(value: u64) -> ptr<u8> {
    unsafe { compiler_barrier(); return ptr_from_address<u8>(value); }
}
'''
        path.write_text(text)
        client = JsonRpcClient([str(args.mlangd.resolve()), '--stdio'])
        try:
            client.request('initialize', {'processId': None, 'rootUri': to_uri(root), 'capabilities': {}})
            client.notify('initialized', {})

            def open_document(file, source):
                client.notify('textDocument/didOpen', {'textDocument': {
                    'uri': to_uri(file), 'languageId': 'mlang', 'version': 1, 'text': source}})

            def query(method, word, occurrence=1, file=path, source=text):
                line, column = position_of(source, word, occurrence)
                return client.request('textDocument/' + method, {
                    'textDocument': {'uri': to_uri(file)},
                    'position': {'line': line, 'character': column + 2}})

            open_document(path, text)
            # Two builtins and a user function share a line. Resolve each token.
            local = query('definition', 'memory_copy', 2)
            expected_line, expected_column = position_of(text, 'memory_copy')
            assert local and local[0]['uri'] == to_uri(path), local
            assert local[0]['range']['start'] == {'line': expected_line, 'character': expected_column}, local
            local_hover = query('hover', 'memory_copy', 2)
            assert local_hover and 'fn memory_copy(' in local_hover['contents']['value'], local_hover

            for word in ('ptr_address', 'ptr_from_address', 'volatile_load', 'volatile_store', 'compiler_barrier'):
                definition = query('definition', word)
                assert definition and definition[0]['uri'].endswith('/stdlib/types.mla'), (word, definition)
                reference = Path(definition[0]['uri'].removeprefix('file://'))
                line = definition[0]['range']['start']['line']
                assert reference.read_text().splitlines()[line] == '// @builtin_fn ' + word
                hover = query('hover', word)
                assert hover and word in hover['contents']['value'], (word, hover)
                assert 'Unsafe' in hover['contents']['value'], (word, hover)

            # Incomplete documents must offer builtins not already used in them.
            for index, (prefix, word) in enumerate((('ptr_a', 'ptr_address'), ('ptr_f', 'ptr_from_address'),
                                                   ('volatile_l', 'volatile_load'), ('volatile_s', 'volatile_store'),
                                                   ('compiler_b', 'compiler_barrier'))):
                file = root / f'completion_{index}.mla'
                source = 'fn probe() -> void { ' + prefix + ' }\n'
                file.write_text(source)
                open_document(file, source)
                line, column = position_of(source, prefix)
                items = client.request('textDocument/completion', {
                    'textDocument': {'uri': to_uri(file)},
                    'position': {'line': line, 'character': column + len(prefix)}})
                item = next((item for item in items if item.get('label') == word), None)
                assert item and item.get('kind') == 3, (word, item)
                assert 'unsafe' in item.get('detail', '').lower(), (word, item)
                assert word in str(item.get('documentation', '')), (word, item)

            shadow = root / 'shadow.mla'
            shadow_text = 'fn ptr_address(value: i32) -> i32 { return value; }\nfn use_shadow() -> i32 { return ptr_address(7); }\n'
            shadow.write_text(shadow_text)
            open_document(shadow, shadow_text)
            definition = query('definition', 'ptr_address', 2, shadow, shadow_text)
            assert definition and definition[0]['uri'] == to_uri(shadow), definition
            hover = query('hover', 'ptr_address', 2, shadow, shadow_text)
            assert hover and 'fn ptr_address(value: i32)' in hover['contents']['value'], hover
        finally:
            client.close()
    print('PASS: hardware builtins, same-line user calls, completion, hover and shadowing')


if __name__ == '__main__':
    main()
