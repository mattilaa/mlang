" Extend existing MLA syntax with builtin doc tags.
" This file is loaded after the primary syntax file, so it works even when
" another plugin owns b:current_syntax.

syn match mlangBuiltinMacroTag /@builtin_macro\>\s\+\h\w*/ containedin=ALL
syn match mlangBuiltinAttributeTag /@builtin_attribute\>\s\+\h\w*/ containedin=ALL

hi def link mlangBuiltinMacroTag Macro
hi def link mlangBuiltinAttributeTag Keyword
