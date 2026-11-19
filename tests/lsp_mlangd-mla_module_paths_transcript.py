#!/usr/bin/env python3
"""Resolve unopened imported modules using workspace command-database paths."""
import argparse
import json
from pathlib import Path
import tempfile

from lsp_testlib import JsonRpcClient, position_of, to_uri


def check_case(server, location, relative):
    with tempfile.TemporaryDirectory(prefix='mlang-module-paths-') as directory:
        root = Path(directory).resolve()
        hardware = root / 'src/hal/hardware.mla'
        runtime = root / 'src/runtime/runtime.mla'
        hardware.parent.mkdir(parents=True)
        runtime.parent.mkdir(parents=True)
        hardware_text = ('/// Write a byte to the serial port.\n'
                         'pub fn uart_put(byte: u32) -> void { return; }\n'
                         '/// Exit through the firmware host.\n'
                         'pub fn semihost_exit(code: i32) -> void { return; }\n')
        runtime_text = '''mod hal::hardware;
use hal::hardware::*;
pub fn app_quit() -> void { semihost_exit(0); }
pub fn firmware_panic() -> void {
    let message: str8 = "panic";
    var i: u64 = 0;
    while message[i] != 0u8 {
        if message[i] == 10u8 { uart_put(13u32); }
        uart_put(u32(message[i]));
        i += 1;
    }
    semihost_exit(1);
}
'''
        hardware.write_text(hardware_text)
        runtime.write_text(runtime_text)
        search_path = 'src' if relative else str(root / 'src')
        if location == 'manifest':
            (root / 'mlang.toml').write_text('[tool.mlang]\nmodule_paths = ["src"]\n')
        else:
            database = root / location / 'mlang_commands.json'
            database.parent.mkdir(parents=True, exist_ok=True)
            database.write_text(json.dumps({'files': [str(runtime), str(hardware)],
                                            'module_paths': [search_path]}))
            if location == 'build':
                # An invalid workspace database must not hide the build fallback.
                (root / 'mlang_commands.json').write_text('{not valid json')

        client = JsonRpcClient([str(server), '--stdio'])
        try:
            client.request('initialize', {'processId': None, 'rootUri': to_uri(root), 'capabilities': {}})
            client.notify('initialized', {})
            # hardware.mla deliberately remains unopened in the editor.
            client.notify('textDocument/didOpen', {'textDocument': {
                'uri': to_uri(runtime), 'languageId': 'mlang', 'version': 1, 'text': runtime_text}})
            for word, description in [('semihost_exit', 'Exit through the firmware host.'),
                                      ('uart_put', 'Write a byte to the serial port.')]:
                for occurrence in (1, 2):
                    line, column = position_of(runtime_text, word, occurrence)
                    params = {'textDocument': {'uri': to_uri(runtime)},
                              'position': {'line': line, 'character': column + 2}}
                    definition = client.request('textDocument/definition', params)
                    expected_line, expected_column = position_of(hardware_text, word)
                    assert definition and definition[0]['uri'] == to_uri(hardware), (location, word, definition)
                    assert definition[0]['range']['start'] == {'line': expected_line, 'character': expected_column}
                    hover = client.request('textDocument/hover', params)
                    assert hover and f'fn {word}(' in hover['contents']['value'], (location, word, hover)
                    assert description in hover['contents']['value'], hover
            # Import lines should use the same search paths as function calls.
            for occurrence in (1, 2):
                line, column = position_of(runtime_text, 'hardware', occurrence)
                definition = client.request('textDocument/definition', {
                    'textDocument': {'uri': to_uri(runtime)},
                    'position': {'line': line, 'character': column + 2}})
                assert definition and definition[0]['uri'] == to_uri(hardware), (location, definition)
        finally:
            client.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mlangd', type=Path, required=True)
    args = parser.parse_args()
    for location, relative in [('', False), ('', True), ('build', True), ('manifest', True)]:
        check_case(args.mlangd.resolve(), location, relative)
    print('PASS: nested imports, command databases, relative/absolute paths, build fallback and manifests')


if __name__ == '__main__':
    main()
