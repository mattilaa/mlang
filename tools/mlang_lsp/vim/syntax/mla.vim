if exists("b:current_syntax")
  finish
endif

syn keyword mlangKeyword fn let var struct enum impl pub extern return if else match for in break continue mod use
syn keyword mlangBoolean true false
syn keyword mlangType void bool int float double string str8 str16 list map tuple i8 i16 i32 i64 u8 u16 u32 u64

syn match mlangBuiltinMacroTag /@builtin_macro\>\s\+\h\w*/ containedin=ALL
syn match mlangBuiltinAttributeTag /@builtin_attribute\>\s\+\h\w*/ containedin=ALL
syn match mlangComment "//.*$"
syn region mlangComment start="/\*" end="\*/"
syn region mlangString start=+"+ skip=+\\\\\|\\"+ end=+"+

" Attribute highlighting: keep `derive` and `test` in the same group.
syn region mlangAttribute start="#\[" end="\]" contains=mlangAttrName,mlangAttrType
syn keyword mlangAttrName derive test contained
syn keyword mlangAttrType Debug contained

syn match mlangMacro /\<[A-Za-z_][A-Za-z0-9_]*!/
syn match mlangNumber /\<[0-9][0-9_]*\(\.[0-9][0-9_]*f\?\)\?\>/

hi def link mlangKeyword Keyword
hi def link mlangBoolean Boolean
hi def link mlangType Type
hi def link mlangComment Comment
hi def link mlangBuiltinMacroTag Macro
hi def link mlangBuiltinAttributeTag Keyword
hi def link mlangString String
hi def link mlangAttribute PreProc
hi def link mlangAttrName Keyword
hi def link mlangAttrType Type
hi def link mlangMacro Macro
hi def link mlangNumber Number

let b:current_syntax = "mla"
