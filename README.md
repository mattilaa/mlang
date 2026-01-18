# mlang
MLang - Programming Language

## LSP
Minimal LSP server lives at `tools/mlang_lsp/mlang_lsp.py` (stdio).

```sh
python3 tools/mlang_lsp/mlang_lsp.py --stdio
```

## C++ LSP
C++ LSP server target is `mlang-lsp` (stdio).

```sh
cmake -S . -B build
cmake --build build
./build/mlang-lsp --stdio
```

Use with UVim:

```sh
uvim --mlang-lsp --mlang-lsp-path ./build/mlang-lsp
```
