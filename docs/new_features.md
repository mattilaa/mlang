# New Features Guide {#new_features}

This page is a wiki-oriented index of recent and notable MLang features. Each
entry describes the feature and links directly to an example that demonstrates
the behavior. When a feature did not already have a standalone example, a
focused `.mla` example was added under `examples/`.

## Core Language

| Feature | Description |
|---|---|
| Namespace blocks and aliases | Group declarations under qualified names and shorten long paths with local aliases; see [`examples/namespace_demo.mla`](../examples/namespace_demo.mla). Reference: [Language Syntax](language_syntax.md#namespace-blocks). |
| Trait objects with `dyn Trait` | Use runtime dispatch at function boundaries, return dyn values, and pass dyn objects through modules; see [`examples/dyn_trait_demo/main.mla`](../examples/dyn_trait_demo/main.mla) and [`examples/dyn_trait_field_demo/main.mla`](../examples/dyn_trait_field_demo/main.mla). Reference: [Language Syntax](language_syntax.md#trait_objects_dyn). |
| Generics and trait bounds | Write generic functions/impls with trait constraints for static dispatch; see [`examples/generics_example.mla`](../examples/generics_example.mla), [`examples/generic_trait_bounds_demo/main.mla`](../examples/generic_trait_bounds_demo/main.mla), and [`examples/trait_advanced_demo/main.mla`](../examples/trait_advanced_demo/main.mla). |
| Type aliases | Use concrete, generic, global, and block-scoped aliases with `alias`; see [`examples/type_alias_demo.mla`](../examples/type_alias_demo.mla). Reference: [Language Syntax](language_syntax.md#type-aliases-alias--use-type). |
| Explicit primitive names | Use width-explicit integer/floating/string types such as `i32`, `f64`, `str8`, and `str16`; see [`examples/str_types.mla`](../examples/str_types.mla), [`examples/std_string_demo.mla`](../examples/std_string_demo.mla), and [`examples/std_sed_str16_demo.mla`](../examples/std_sed_str16_demo.mla). Reference: [Quick Guide](quick_guide.md#common-types). |
| Function return type inference | Omit `-> Type` for non-extern functions when return paths infer consistently; see [`examples/function_return_inference_demo.mla`](../examples/function_return_inference_demo.mla). Reference: [Language Syntax](language_syntax.md#function-return-type-inference). |
| `main` return type defaulting | Write `fn main() { ... }` for the common `i32` main return; see [`examples/main.mla`](../examples/main.mla). Reference: [Language Syntax](language_syntax.md#main-return-type-defaulting). |
| Typed `var` zero initialization | Declare mutable typed storage with implicit zero initialization or explicit `{}` zero initialization; see [`examples/zero_init_demo.mla`](../examples/zero_init_demo.mla). Reference: [Language Syntax](language_syntax.md#typed-var-declarations-without-initializers). |
| Type-name `.name` property | Read a value's static type name through `.name`, while real `name` fields still win; see [`examples/type_name_property_demo.mla`](../examples/type_name_property_demo.mla). Reference: [Language Syntax](language_syntax.md#type-name-property-name). |
| Field brace initialization | Put defaults directly on struct fields and override nested fields with dotted paths; see [`examples/field_brace_init_demo.mla`](../examples/field_brace_init_demo.mla). |
| Associated functions and method visibility | Define module-backed associated functions and visibility-scoped methods; see [`examples/associated_functions_demo/main.mla`](../examples/associated_functions_demo/main.mla) and [`examples/method_visibility_demo/main.mla`](../examples/method_visibility_demo/main.mla). |

## Control Flow and Expressions

| Feature | Description |
|---|---|
| Plain block `if` / `else if` | Prefer colon-free block conditionals; see [`examples/block_pattern_demo.mla`](../examples/block_pattern_demo.mla). Reference: [Language Syntax](language_syntax.md#if--else-if-syntax). |
| Guarded `if let` / `if var` and guarded `while` | Add guard expressions to bindings and loops; see [`examples/guarded_if_while_demo.mla`](../examples/guarded_if_while_demo.mla). Reference: [Language Syntax](language_syntax.md#guarded-if-forms). |
| `switch` / `case` | Use switch-style branching for integral and enum-like values; see [`examples/switch_demo.mla`](../examples/switch_demo.mla). Reference: [Language Syntax](language_syntax.md#switch--case). |
| Ternary operator | Use `condition ? a : b` for expression-level selection; see [`examples/ternary_example.mla`](../examples/ternary_example.mla). |
| `break` and `continue` | Control loop flow explicitly; see [`examples/break_continue.mla`](../examples/break_continue.mla). |
| `for` loops and ranges | Iterate ranges and containers directly; see [`examples/for_loop_example.mla`](../examples/for_loop_example.mla). |
| Pipe operator `|>` | Compose function calls left-to-right; see [`examples/pipe_operator_demo.mla`](../examples/pipe_operator_demo.mla). Reference: [Language Syntax](language_syntax.md#pipe_operator). |
| Lambdas, closures, and folds | Use inline lambdas, captured state, and fold expressions over lists; see [`examples/lambda_fold_demo.mla`](../examples/lambda_fold_demo.mla), [`examples/lambda_fold_advanced.mla`](../examples/lambda_fold_advanced.mla), [`examples/lambda_fold_patterns.mla`](../examples/lambda_fold_patterns.mla), and [`examples/functional_closure_fold_demo.mla`](../examples/functional_closure_fold_demo.mla). Reference: [Language Syntax](language_syntax.md#lambda--fold-expressions). |
| `cexpr` compile-time evaluation | Fold expressions, functions, declarations, branches, and type dispatch at compile time; see [`examples/cexpr_twice_demo.mla`](../examples/cexpr_twice_demo.mla), [`examples/cexpr_float_demo.mla`](../examples/cexpr_float_demo.mla), [`examples/cexpr_decl_demo.mla`](../examples/cexpr_decl_demo.mla), [`examples/cexpr_if_demo.mla`](../examples/cexpr_if_demo.mla), and [`examples/cexpr_generic_type_id_demo.mla`](../examples/cexpr_generic_type_id_demo.mla). Reference: [Language Syntax](language_syntax.md#cexpr-compile-time-evaluation). |

## Data Types and Containers

| Feature | Description |
|---|---|
| `Option`, `Result`, and `match` | Model fallible and optional values without sentinel values; see [`examples/functional_option_result_demo.mla`](../examples/functional_option_result_demo.mla), [`examples/result_usage.mla`](../examples/result_usage.mla), [`examples/result_match.mla`](../examples/result_match.mla), and [`examples/enum_option_match.mla`](../examples/enum_option_match.mla). Reference: [Quick Guide](quick_guide.md#results-and-match). |
| Enum printing, strings, and explicit backing types | Use enum helpers, string/hex formatting, and explicit enum backing storage; see [`examples/enum_print_demo.mla`](../examples/enum_print_demo.mla), [`examples/enum_string_hex_demo.mla`](../examples/enum_string_hex_demo.mla), and [`examples/enum_backing_valid.mla`](../examples/enum_backing_valid.mla). Reference: [Language Syntax](language_syntax.md#enums-with-explicit-backing-type). |
| `list<T>` / `Vec<T>` methods | Build and mutate growable lists with push/pop/search/sort helpers; see [`examples/std_vec_demo.mla`](../examples/std_vec_demo.mla). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdvec). |
| Container-to-container `.extend(...)` | Append compatible lists, arrays, maps, literals, and nested containers into mutable destinations; see [`examples/container_extend_demo.mla`](../examples/container_extend_demo.mla). Reference: [Language Syntax](language_syntax.md#lambda--fold-expressions). |
| Fixed-capacity `array<T, N>` | Use fixed-capacity arrays with checked literal/fill initialization, indexing, `push`, `extend`, and `fill`; see [`examples/array_demo.mla`](../examples/array_demo.mla). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdarray). |
| Literal storage safety | Return literals, iterate literal containers, and extend nested literal-backed containers without dangling storage; see [`examples/literal_storage_safety_demo.mla`](../examples/literal_storage_safety_demo.mla). Reference: [Ownership Model Notes](ownership_model.h). |
| Maps and iteration | Use map literals, indexing, and iteration patterns; see [`examples/map_list.mla`](../examples/map_list.mla) and [`examples/map_iteration.mla`](../examples/map_iteration.mla). |
| Tuples and slices/spans | Use tuple values and borrowed contiguous views; see [`examples/tuple_example.mla`](../examples/tuple_example.mla), [`examples/slice.mla`](../examples/slice.mla), and [`examples/std_span_demo.mla`](../examples/std_span_demo.mla). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdspan). |
| `bit` and `size_of` | Use packed bit fields and compile-time/runtime size queries; see [`examples/bit_packed_struct_demo.mla`](../examples/bit_packed_struct_demo.mla) and [`examples/bit_sizeof_demo.mla`](../examples/bit_sizeof_demo.mla). Reference: [Language Syntax](language_syntax.md#bit-and-size_of). |
| Builder object syntax | Build structured nested objects with full `struct` clauses or compact `field` clauses; see [`examples/builder_object_json_demo.mla`](../examples/builder_object_json_demo.mla) and [`examples/builder_object_field_demo.mla`](../examples/builder_object_field_demo.mla). Reference: [Language Syntax](language_syntax.md#builder_syntax). |
| `@property(...)` fields | Generate accessor methods and property metadata for fields, including hidden/protected behavior; see [`examples/property_fields_demo.mla`](../examples/property_fields_demo.mla). Reference: [Language Attributes](language_attributes.md#property). |

## Safety, Ownership, and Low-Level Control

| Feature | Description |
|---|---|
| Borrowing and ownership diagnostics | Use references and mutable references with compiler checks around moves and borrowed storage; see [`examples/borrowing_demo.mla`](../examples/borrowing_demo.mla), [`examples/borrowing.mla`](../examples/borrowing.mla), and [`examples/borrow_patterns.mla`](../examples/borrow_patterns.mla). Reference: [Ownership Model Notes](ownership_model.h). |
| RAII and scope cleanup | Clean up resources through scope-exit/free-method patterns; see [`examples/raii_free_method_demo.mla`](../examples/raii_free_method_demo.mla) and [`examples/scope_exit_drop_demo.mla`](../examples/scope_exit_drop_demo.mla). |
| Exceptions with cleanup-aware unwinding | Throw and catch `Exception` values while preserving cleanup semantics; see [`examples/exceptions_try_catch_demo.mla`](../examples/exceptions_try_catch_demo.mla). Reference: [Language Syntax](language_syntax.md#exceptions-throw-and-trycatch). |
| Raw pointers and C-like access | Work with pointers when low-level interop requires it; see [`examples/pointer_access.mla`](../examples/pointer_access.mla). |
| C interop and type mappings | Call C functions and map C-compatible types; see [`examples/ffi_add.mla`](../examples/ffi_add.mla), [`examples/ffi_cos.mla`](../examples/ffi_cos.mla), [`examples/c_type_mappings.mla`](../examples/c_type_mappings.mla), [`examples/c_lib_usage.mla`](../examples/c_lib_usage.mla), [`examples/c_lib_file_io_demo.mla`](../examples/c_lib_file_io_demo.mla), and [`examples/c_lib_text_parse_demo.mla`](../examples/c_lib_text_parse_demo.mla). Reference: [Quick Guide](quick_guide.md#c-interop). |
| Inline assembly | Embed target-specific assembly for low-level code paths; see [`examples/inline_asm_x64_demo.mla`](../examples/inline_asm_x64_demo.mla), [`examples/inline_asm_aarch64_demo.mla`](../examples/inline_asm_aarch64_demo.mla), and [`examples/platform_inline_asm_demo.mla`](../examples/platform_inline_asm_demo.mla). Reference: [Language Syntax](language_syntax.md#inline-assembly-asm). |
| Platform macros and conditional regions | Branch on platform/architecture or remove source regions before parsing; see [`examples/std_platform_demo.mla`](../examples/std_platform_demo.mla) and [`examples/platform_region_demo.mla`](../examples/platform_region_demo.mla). Reference: [Language Syntax](language_syntax.md#platform-macros). |

## Attributes, Tests, and Derived Code

| Feature | Description |
|---|---|
| `#[test]` functions | Mark test functions and run them with `mlang --tests`; see [`examples/mlang_attributes.mla`](../examples/mlang_attributes.mla) and [`tests/test_sample.mla`](../tests/test_sample.mla). Reference: [Language Attributes](language_attributes.md#test). |
| `#[fixture]` tests | Run each test method against a fresh fixture instance with setup/teardown hooks; see [`examples/test_fixture_example.mla`](../examples/test_fixture_example.mla) and [`tests/fixture_tests.mla`](../tests/fixture_tests.mla). Reference: [Language Attributes](language_attributes.md#fixture). |
| Mock expectations | Use `std::testing` mocks with EXPECT_CALL-style cardinality and programmed return values; see [`examples/testing_mock_example.mla`](../examples/testing_mock_example.mla), [`examples/expect_call_example.mla`](../examples/expect_call_example.mla), and [`tests/expect_call_tests.mla`](../tests/expect_call_tests.mla). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdtesting). |
| `#[derive(Debug)]` | Generate debug formatting for structs; see [`examples/mlang_attributes.mla`](../examples/mlang_attributes.mla). Reference: [Language Attributes](language_attributes.md#derivedebug). |
| `#[derive(Json)]` | Generate `to_json()` and `Type::from_json(...)` for supported structs, including inherited/property-backed fields; see [`examples/std_json_derive_demo.mla`](../examples/std_json_derive_demo.mla) and [`examples/property_fields_demo.mla`](../examples/property_fields_demo.mla). Reference: [Language Attributes](language_attributes.md#derivejson). |
| Inline attributes | Request or prevent inlining with `#[inline]`, `#[inline(always)]`, and `#[inline(never)]`; see [`examples/inline_attrs.mla`](../examples/inline_attrs.mla). Reference: [Language Attributes](language_attributes.md#inline). |

## Standard Library Feature Examples

| Feature | Description |
|---|---|
| Argument parsing and environment help | Parse CLI flags and render help text; see [`examples/argparser_demo.mla`](../examples/argparser_demo.mla) and [`examples/env_help_demo/main.mla`](../examples/env_help_demo/main.mla). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdargparser). |
| Filesystem and path IO | Read, write, seek, and iterate file contents; see [`examples/std_fs_demo.mla`](../examples/std_fs_demo.mla), [`examples/std_fs_rw.mla`](../examples/std_fs_rw.mla), [`examples/std_fs_seek.mla`](../examples/std_fs_seek.mla), and [`examples/std_fs_lines.mla`](../examples/std_fs_lines.mla). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdfs). |
| IO traits and RAII | Use stdin/stdout/file-like adapters and cleanup-friendly IO wrappers; see [`examples/std_io_demo.mla`](../examples/std_io_demo.mla), [`examples/std_io_input_demo.mla`](../examples/std_io_input_demo.mla), [`examples/std_io_traits_demo.mla`](../examples/std_io_traits_demo.mla), and [`examples/std_io_raii_demo.mla`](../examples/std_io_raii_demo.mla). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdio). |
| JSON and JSON-RPC | Parse/stringify JSON and use JSON-RPC transport/runtime helpers; see [`examples/std_json_demo.mla`](../examples/std_json_demo.mla), [`examples/std_json_derive_demo.mla`](../examples/std_json_derive_demo.mla), [`examples/std_jsonrpc_runtime_demo.mla`](../examples/std_jsonrpc_runtime_demo.mla), and [`examples/std_jsonrpc_stdio_loop_demo.mla`](../examples/std_jsonrpc_stdio_loop_demo.mla). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdjson). |
| Networking and protocols | Build TCP clients/servers and framed protocol demos; see [`examples/std_net_demo.mla`](../examples/std_net_demo.mla), [`examples/std_net_mt_server.mla`](../examples/std_net_mt_server.mla), [`examples/std_net_mt_client.mla`](../examples/std_net_mt_client.mla), [`examples/std_protocol_demo.mla`](../examples/std_protocol_demo.mla), and [`examples/protocol_mt`](../examples/protocol_mt). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdnet). |
| System IPC, named pipes, and local sockets | Exchange bytes between processes with `std::ipc::NamedPipe` or local stream sockets via `std::ipc::LocalListener` / `std::ipc::LocalStream`; see [`examples/std_ipc_named_pipe_demo.mla`](../examples/std_ipc_named_pipe_demo.mla) and [`examples/std_ipc_local_socket_demo.mla`](../examples/std_ipc_local_socket_demo.mla). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdipc). |
| Audio hardware output | List output devices by integer id, print device names, and open the default or selected output with command-line sample rate and latency buffer frames through `std::audio`, using CoreAudio on macOS or JACK2 on Linux; see [`examples/std_audio_sine_demo.mla`](../examples/std_audio_sine_demo.mla), [`examples/std_audio_vst3_style_preview.mla`](../examples/std_audio_vst3_style_preview.mla), [`examples/std_audio_simd_dsp_demo.mla`](../examples/std_audio_simd_dsp_demo.mla), and [`examples/package_manager_vst3_coreaudio_synth`](../examples/package_manager_vst3_coreaudio_synth). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdaudio). |
| Threads, synchronization, and concurrency | Use threads, mutexes, atomics, channels, wait groups, and lock-free queues; see [`examples/std_thread_demo.mla`](../examples/std_thread_demo.mla), [`examples/thread_basic.mla`](../examples/thread_basic.mla), [`examples/thread_multi.mla`](../examples/thread_multi.mla), [`examples/thread_mutex_atomic.mla`](../examples/thread_mutex_atomic.mla), [`examples/std_sync_demo.mla`](../examples/std_sync_demo.mla), and [`examples/std_concurrent_demo.mla`](../examples/std_concurrent_demo.mla). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdthread). |
| Process execution | Spawn child processes and interact with foreground commands; see [`examples/std_process_demo.mla`](../examples/std_process_demo.mla) and [`examples/std_process_foreground_demo.mla`](../examples/std_process_foreground_demo.mla). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdprocess). |
| Regex and sed-like replacement | Compile regexes and run substitution workflows; see [`examples/std_regex_demo.mla`](../examples/std_regex_demo.mla), [`examples/std_sed_demo.mla`](../examples/std_sed_demo.mla), and [`examples/std_sed_str16_demo.mla`](../examples/std_sed_str16_demo.mla). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdregex). |
| Math, rand, hash, FFT, and algorithms | Use numeric helpers, random values, hashes, FFT utilities, and algorithms; see [`examples/std_math_demo.mla`](../examples/std_math_demo.mla), [`examples/std_rand_demo.mla`](../examples/std_rand_demo.mla), [`examples/std_hash_demo.mla`](../examples/std_hash_demo.mla), [`examples/std_fft_demo.mla`](../examples/std_fft_demo.mla), and [`examples/fft_example/main.mla`](../examples/fft_example/main.mla). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdmath). |
| SIMD vector math | Add, subtract, multiply, horizontally reduce numeric vectors, compute running sums, find min/max values, run boolean `any`/`all` checks, perform integer gate/shift operations over normal lists, and operate on packed `std::bitset::BitSet` values; see [`examples/std_simd_demo.mla`](../examples/std_simd_demo.mla). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdsimd). |
| Terminal, ESC widgets, and image output | Render terminal colors, TUI widgets, truecolor images, and interactive demos; see [`examples/std_term_demo.mla`](../examples/std_term_demo.mla), [`examples/std_esc_demo.mla`](../examples/std_esc_demo.mla), [`examples/esc_widgets/tracker_ui_demo.mla`](../examples/esc_widgets/tracker_ui_demo.mla), [`examples/image_truecolor_demo.mla`](../examples/image_truecolor_demo.mla), and [`examples/uml_ui_generator`](../examples/uml_ui_generator). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdterm). |
| Serialization and bytes | Work with binary buffers, serde traits, and protocol wire formats; see [`examples/std_protocol_demo.mla`](../examples/std_protocol_demo.mla) and [`tests/std_bytes_tests.mla`](../tests/std_bytes_tests.mla). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdserde). |

## Package and Tooling Examples

| Feature | Description |
|---|---|
| Package manager basics | Use manifests, bins, tasks, dependency fetching, builds, runs, and tests; see [`examples/package_manager_multi_bins`](../examples/package_manager_multi_bins), [`examples/package_manager_workspace_fetch`](../examples/package_manager_workspace_fetch), and [`examples/package_manager_task_graph`](../examples/package_manager_task_graph). Reference: [Package Manager](package_manager.md). |
| Multilanguage package builds | Build packages that combine MLang with C/C++ sources and native linker settings; see [`examples/package_manager_multilanguage_example`](../examples/package_manager_multilanguage_example), [`examples/package_manager_static_cjson`](../examples/package_manager_static_cjson), [`examples/package_manager_git_cjson`](../examples/package_manager_git_cjson), and [`examples/package_manager_oscilloscope_demo`](../examples/package_manager_oscilloscope_demo). |
| Cross/architecture package examples | Configure architecture-specific package builds and native SDK demos; see [`examples/package_manager_linux_aarch64_qemu`](../examples/package_manager_linux_aarch64_qemu), [`examples/package_manager_vst3_sdk_example`](../examples/package_manager_vst3_sdk_example), and [`examples/package_manager_vst3_coreaudio_synth`](../examples/package_manager_vst3_coreaudio_synth). |
| Compiler API and LSP workflows | Query compiler/editor services and validate LSP behavior through transcript tests; see [`examples/std_compiler_demo.mla`](../examples/std_compiler_demo.mla), [`tests/lsp_mlangd-mla_hover_transcript.py`](../tests/lsp_mlangd-mla_hover_transcript.py), [`tests/lsp_mlangd-mla_completion_docs_transcript.py`](../tests/lsp_mlangd-mla_completion_docs_transcript.py), and [`tests/lsp_mlangd-mla_rename_transcript.py`](../tests/lsp_mlangd-mla_rename_transcript.py). Reference: [Stdlib Module API](stdlib_mlang_api.md#stdcompiler). |

## See Also

- [Quick Guide](quick_guide.md)
- [Language Syntax](language_syntax.md)
- [Language Attributes](language_attributes.md)
- [Stdlib Module API](stdlib_mlang_api.md)
- [Package Manager](package_manager.md)
- [Examples](examples.md)
