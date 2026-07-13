# New Features Guide {#new_features}

This page is a wiki-oriented index for recently added MLang features. It links
each feature to its reference documentation and to an example or focused
regression test that shows the feature in use.

## Language Features

| Feature | What changed | Docs | Examples |
|---|---|---|---|
| Namespace blocks and namespace aliases | Declarations can be grouped under qualified paths, with local aliases for shorter names. | [Language Syntax](language_syntax.md#namespace-blocks) | [`examples/namespace_demo.mla`](../examples/namespace_demo.mla) |
| Trait objects with `dyn Trait` | Function boundaries can accept, return, and pass through runtime-dispatched trait objects. | [Language Syntax](language_syntax.md#trait_objects_dyn) | [`examples/dyn_trait_demo/main.mla`](../examples/dyn_trait_demo/main.mla), [`examples/dyn_trait_field_demo/main.mla`](../examples/dyn_trait_field_demo/main.mla) |
| Type aliases with `alias` | Global and block-scoped aliases, including generic aliases, are supported. | [Language Syntax](language_syntax.md#type-aliases-alias--use-type) | [`examples/type_alias_demo.mla`](../examples/type_alias_demo.mla) |
| Explicit primitive names | Numeric and string types use width-explicit spellings such as `i32`, `f64`, `str8`, and `str16`. Legacy `int`, `float`, `double`, `string`, and `utf8` spellings are removed. | [Quick Guide](quick_guide.md#common-types), [Language Syntax](language_syntax.md#numeric-primitive-names) | [`examples/std_string_demo.mla`](../examples/std_string_demo.mla), [`examples/std_sed_str16_demo.mla`](../examples/std_sed_str16_demo.mla) |
| Platform macros and architecture-gated functions | Source can branch on compile-time platform and architecture predicates, and duplicate target-specific functions can be gated by attribute. | [Language Syntax](language_syntax.md#platform-macros), [Language Syntax](language_syntax.md#architecture-gated-functions) | [`examples/platform_inline_asm_demo.mla`](../examples/platform_inline_asm_demo.mla), [`examples/inline_asm_x64_demo.mla`](../examples/inline_asm_x64_demo.mla), [`examples/inline_asm_aarch64_demo.mla`](../examples/inline_asm_aarch64_demo.mla) |
| Conditional source regions | Whole source regions can be filtered before parsing for platform-specific definitions. | [Language Syntax](language_syntax.md#conditional-regions) | [`examples/platform_region_demo.mla`](../examples/platform_region_demo.mla) |
| Type-name property | Values expose `.name` for logging their static type name unless a real `name` field exists. | [Language Syntax](language_syntax.md#type-name-property-name) | [`tests/type_name_property_tests.mla`](../tests/type_name_property_tests.mla) |
| Typed `var` without initializer | Mutable typed storage can be zero-initialized, with `{}` as the explicit spelling. | [Language Syntax](language_syntax.md#typed-var-declarations-without-initializers) | [`tests/static_storage_tests.mla`](../tests/static_storage_tests.mla) |
| `cexpr` compile-time evaluation | Compile-time expressions, `cexpr fn`, `cexpr if`, `cexpr` declarations, and type-based generic dispatch are supported. | [Language Syntax](language_syntax.md#cexpr-compile-time-evaluation) | [`examples/cexpr_twice_demo.mla`](../examples/cexpr_twice_demo.mla), [`examples/cexpr_float_demo.mla`](../examples/cexpr_float_demo.mla), [`examples/cexpr_decl_demo.mla`](../examples/cexpr_decl_demo.mla), [`examples/cexpr_if_demo.mla`](../examples/cexpr_if_demo.mla), [`examples/cexpr_generic_type_id_demo.mla`](../examples/cexpr_generic_type_id_demo.mla) |
| Plain block `if` / `else if` | Colon-free block syntax is the preferred form; legacy plain-colon syntax warns. | [Language Syntax](language_syntax.md#if--else-if-syntax) | [`tests/if_let_var_combinations_tests.mla`](../tests/if_let_var_combinations_tests.mla) |
| Guarded `if let` / `if var` | Guarded bindings support typed, untyped, and equality initializers. | [Language Syntax](language_syntax.md#guarded-if-forms) | [`tests/if_let_var_combinations_tests.mla`](../tests/if_let_var_combinations_tests.mla), [`tests/while_guard_tests.mla`](../tests/while_guard_tests.mla) |
| Return type inference | Non-extern functions can omit `-> Type` when returns infer consistently. | [Language Syntax](language_syntax.md#function-return-type-inference) | [`tests/function_return_inference_tests.mla`](../tests/function_return_inference_tests.mla) |
| Lambdas and folds | Inline lambdas and list folds are supported for compact functional patterns. | [Language Syntax](language_syntax.md#lambda--fold-expressions) | [`examples/lambda_fold_demo.mla`](../examples/lambda_fold_demo.mla), [`examples/lambda_fold_advanced.mla`](../examples/lambda_fold_advanced.mla), [`examples/functional_closure_fold_demo.mla`](../examples/functional_closure_fold_demo.mla) |

## Containers and Safety

| Feature | What changed | Docs | Examples |
|---|---|---|---|
| Container-to-container `extend` | Mutable lists, maps, and fixed arrays can append compatible containers or literals with `.extend(...)`. | [Language Syntax](language_syntax.md#lambda--fold-expressions), [Stdlib Module API](stdlib_mlang_api.md#stdvec), [Stdlib Module API](stdlib_mlang_api.md#stdarray) | [`tests/mla_tests.cpp`](../tests/mla_tests.cpp) (`ListExtendsFromList`, `ListExtendsFromArray`, `MapExtendsFromMap`, `FixedArrayExtendsFromVecWithinCapacity`) |
| Nested container transfer | `.extend(...)` handles elements that are themselves lists, maps, or arrays. | [Language Syntax](language_syntax.md#lambda--fold-expressions) | [`tests/mla_tests.cpp`](../tests/mla_tests.cpp) (`ListExtendsWithNestedListElements`, `ListExtendsWithNestedMapElements`, `FixedArrayExtendsWithNestedListElements`, `FixedArrayExtendsWithNestedMapElements`) |
| Fixed-capacity `array<T, N>` checks | Literal and fill initialization, `push`, `extend`, indexing, and `fill(value)` are checked against capacity. | [Stdlib Module API](stdlib_mlang_api.md#stdarray), [Language Syntax](language_syntax.md#lambda--fold-expressions) | [`examples/array_demo.mla`](../examples/array_demo.mla), [`tests/mla_tests.cpp`](../tests/mla_tests.cpp) (`FixedArrayRejectsProvableExtendOverflowAtCompileTime`, `FixedArrayKeepsRuntimeGuardForUnknownLengthOverflow`) |
| Array/list literal storage safety | Array-fill literals and returned literals now preserve storage correctly across loops, returns, and nested containers. | [Ownership Model Notes](ownership_model.h) | [`tests/mla_tests.cpp`](../tests/mla_tests.cpp) (`ArrayFillBackedStorageCanGrow`, `FixedArrayFillSetsAllSlots`) |
| Borrowing and ownership diagnostics | The compiler has stronger checks around moved values, borrowed values, and container-backed storage. | [Ownership Model Notes](ownership_model.h) | [`examples/borrowing_demo.mla`](../examples/borrowing_demo.mla), [`examples/borrow_patterns.mla`](../examples/borrow_patterns.mla), [`tests/borrow_checker_tests.mla`](../tests/borrow_checker_tests.mla) |

## Attributes, Testing, and Derives

| Feature | What changed | Docs | Examples |
|---|---|---|---|
| `#[fixture]` tests | Test methods can share setup/teardown through a fixture impl. | [Language Attributes](language_attributes.md#fixture) | [`examples/test_fixture_example.mla`](../examples/test_fixture_example.mla), [`tests/fixture_tests.mla`](../tests/fixture_tests.mla) |
| Mock expectations | `std::testing` supports mock call cardinality and programmed return values. | [Stdlib Module API](stdlib_mlang_api.md#stdtesting) | [`examples/testing_mock_example.mla`](../examples/testing_mock_example.mla), [`examples/expect_call_example.mla`](../examples/expect_call_example.mla), [`tests/expect_call_tests.mla`](../tests/expect_call_tests.mla) |
| JSON derive | `#[derive(Json)]` can emit and parse JSON for supported structs. | [Language Attributes](language_attributes.md#derivejson), [Stdlib Module API](stdlib_mlang_api.md#stdjson) | [`examples/std_json_derive_demo.mla`](../examples/std_json_derive_demo.mla), [`examples/builder_object_json_demo.mla`](../examples/builder_object_json_demo.mla) |
| Builder object syntax | Structured builder clauses can render to JSON using full `struct` clauses or compact `field` declarations. | [Language Syntax](language_syntax.md#builder_syntax) | [`examples/builder_object_json_demo.mla`](../examples/builder_object_json_demo.mla), [`examples/builder_object_field_demo.mla`](../examples/builder_object_field_demo.mla) |

## Tooling and Package Examples

| Feature | What changed | Docs | Examples |
|---|---|---|---|
| Package manager workflows | `mlang pkg` supports manifests, bins, tasks, dependency fetching, and build configuration. | [Package Manager](package_manager.md) | [`examples/package_manager_multi_bins`](../examples/package_manager_multi_bins), [`examples/package_manager_workspace_fetch`](../examples/package_manager_workspace_fetch), [`examples/package_manager_task_graph`](../examples/package_manager_task_graph) |
| Multilanguage package builds | Package examples can combine MLang with C/C++ sources and native linker settings. | [Package Manager](package_manager.md) | [`examples/package_manager_multilanguage_example`](../examples/package_manager_multilanguage_example), [`examples/package_manager_static_cjson`](../examples/package_manager_static_cjson), [`examples/package_manager_git_cjson`](../examples/package_manager_git_cjson) |
| LSP-focused tooling | `mlangd` and `mlangd-mla` have transcript tests for hover, completion, rename, diagnostics, formatting, and definitions. | [`mlangd` manual](../docs/man/mlangd.1), [`mlangd-mla` manual](../docs/man/mlangd-mla.1) | [`tests/lsp_mlangd-mla_hover_transcript.py`](../tests/lsp_mlangd-mla_hover_transcript.py), [`tests/lsp_mlangd-mla_completion_docs_transcript.py`](../tests/lsp_mlangd-mla_completion_docs_transcript.py), [`tests/lsp_mlangd-mla_rename_transcript.py`](../tests/lsp_mlangd-mla_rename_transcript.py) |

## See Also

- [Quick Guide](quick_guide.md)
- [Language Syntax](language_syntax.md)
- [Language Attributes](language_attributes.md)
- [Stdlib Module API](stdlib_mlang_api.md)
- [Examples](examples.md)
