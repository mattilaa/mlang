# Mlang LSP (minimal)

This is a minimal LSP server for Mlang focused on cross-file navigation.

## Features
- Go to definition for `fn`, `struct`, and `mod` symbols
- Workspace symbol search
- Document symbol list
- References (simple word match)

## Run

```sh
python3 tools/mlang_lsp/mlang_lsp.py --stdio
```

## UVim (manual wiring)

UVim can start a generic LSP server via the `--python-lsp` or `--robot-lsp`
flags, but it gates which filetypes trigger LSP requests. If you want UVim to
use this server for `.mla` files, we can add a dedicated Mlang LSP option and
filetype check in UVim. Let me know if you want me to wire that up.

## Vim/UVim Syntax Highlighting
This repo now includes Vim syntax files for `.mla` and `.mlastub`:
- `tools/mlang_lsp/vim/ftdetect/mla.vim`
- `tools/mlang_lsp/vim/syntax/mla.vim`

To enable them in Vim/UVim, add this directory to `runtimepath`:

```vim
set runtimepath+=/path/to/mlang/tools/mlang_lsp/vim
```

These rules highlight attributes like `#[derive(Debug)]` and `#[test]` with
`derive` and `test` using the same keyword style.

## Notes
- Indexing is regex-based and ignores tokens inside comments/strings.
- Module navigation also falls back to matching `.mla` filenames when a symbol
  isn’t found.
