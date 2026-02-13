" Extend existing Mlang syntax with builtin doc tags.
" Some setups use filetype=mlang instead of mla.

syn match mlangBuiltinMacroTag /@builtin_macro\>\s\+\h\w*/ containedin=ALL
syn match mlangBuiltinAttributeTag /@builtin_attribute\>\s\+\h\w*/ containedin=ALL

hi def link mlangBuiltinMacroTag Macro
hi def link mlangBuiltinAttributeTag Keyword
