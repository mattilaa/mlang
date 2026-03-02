import re
from pathlib import Path

def normalize_params(params: str) -> str:
    return ' '.join(params.replace('\n', ' ').split()).strip()


def strip_defaults(params: str) -> str:
    if not params:
        return ''
    entries = []
    for part in params.split(','):
        if not part.strip():
            continue
        entry = part.split('=')[0].strip()
        entries.append(entry)
    return ', '.join(entries)

def arg_names(params: str) -> str:
    if not params:
        return ''
    names = []
    for part in params.split(','):
        part = part.strip()
        if not part:
            continue
        name = part.split()[-1]
        names.append(name)
    return ', '.join(names)

def main() -> None:
    files = ['include/ast.h', 'src/parser.y']
    pattern = re.compile(r'ASTNode\*\s+((?:create|add|set)_[A-Za-z0-9_]+)\s*\(([^;]*?)\);', re.DOTALL)
    entries = []
    seen = set()
    for file in files:
        text = Path(file).read_text()
        for match in pattern.finditer(text):
            name = match.group(1)
            if name in seen:
                continue
            seen.add(name)
            params = normalize_params(match.group(2))
            entries.append((name, params))
    ast_cpp = Path('src/ast.cpp').read_text()
    for name, _ in entries:
        pattern_def = re.compile(rf'(ASTNode\*\s+){name}\s*\(')
        ast_cpp, count = pattern_def.subn(rf'\1{name}_impl(', ast_cpp, count=1)
        if count == 0:
            if f'{name}_impl(' in ast_cpp:
                continue
            print(f'Warning: definition for {name} not found in src/ast.cpp', flush=True)
    Path('src/ast.cpp').write_text(ast_cpp)

    available = {
        match.group(1) for match in re.finditer(r'ASTNode\*\s+((?:create|add|set)_[A-Za-z0-9_]+)_impl\s*\(', ast_cpp)
    }
    filtered = [(name, params) for name, params in entries if name in available]

    header = ['// Generated helper declarations when bridging AST creation from MLang helpers.',
              '#ifndef AST_IMPL_H',
              '#define AST_IMPL_H',
              '',
              '#include "ast.h"',
              '']
    for name, params in filtered:
        decl_params = strip_defaults(params)
        header.append(f'ASTNode* {name}_impl({decl_params});' if decl_params else f'ASTNode* {name}_impl();')
    header.append('')
    header.append('#endif // AST_IMPL_H')
    Path('include/ast_impl.h').write_text('\n'.join(header) + '\n')

    bridge_lines = ['#include "ast.h"', '#include "ast_impl.h"', '', 'extern "C" {', '']
    for name, params in filtered:
        decl_params = strip_defaults(params)
        decl = f'ASTNode* {name}({decl_params})' if decl_params else f'ASTNode* {name}()'
        args = arg_names(params)
        call = f'{name}_impl({args})' if args else f'{name}_impl()'
        bridge_lines.append(decl)
        bridge_lines.append('{')
        bridge_lines.append(f'    return {call};')
        bridge_lines.append('}')
        bridge_lines.append('')
    bridge_lines.append('} // extern "C"')
    Path('src/ast_bridge.cpp').write_text('\n'.join(bridge_lines) + '\n')

if __name__ == '__main__':
    main()
