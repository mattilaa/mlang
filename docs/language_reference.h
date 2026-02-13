#ifndef MLANG_LANGUAGE_REFERENCE_H
#define MLANG_LANGUAGE_REFERENCE_H

/**
 * @file language_reference.h
 * @brief Mlang language-level builtins: types, keywords, macros, and attributes.
 */

/**
 * @defgroup language_reference Mlang Language Reference
 * @brief Compiler-recognized language constructs and builtins.
 *
 * This page documents syntax-level features that are recognized directly by
 * the compiler and parser (not regular user-defined symbols).
 */

/**
 * @defgroup language_types Builtin Types And Constructors
 * @ingroup language_reference
 * @brief Core language types and constructor forms.
 */

/**
 * @brief Optional value type.
 * @ingroup language_types
 *
 * `Option<T>` represents either `Some(value)` or `None`.
 *
 * Example:
 * @code{.mla}
 * let maybe: Option<i32> = Some<i32>(10);
 * @endcode
 */
typedef struct MlangOption MlangOption;

/**
 * @brief Success/error result type.
 * @ingroup language_types
 *
 * `Result<T, E>` represents either `Ok(value)` or `Err(error)`.
 */
typedef struct MlangResult MlangResult;

/**
 * @brief Option constructor for present values.
 * @ingroup language_types
 *
 * Syntax form:
 * @code{.mla}
 * Some<T>(value)
 * @endcode
 */
void Some(void);

/**
 * @brief Option constructor for absent values.
 * @ingroup language_types
 *
 * Syntax form:
 * @code{.mla}
 * None<T>()
 * @endcode
 */
void None(void);

/**
 * @brief Result constructor for success values.
 * @ingroup language_types
 *
 * Syntax form:
 * @code{.mla}
 * Ok<T, E>(value)
 * @endcode
 */
void Ok(void);

/**
 * @brief Result constructor for error values.
 * @ingroup language_types
 *
 * Syntax form:
 * @code{.mla}
 * Err<T, E>(error)
 * @endcode
 */
void Err(void);

/**
 * @defgroup language_keywords Keywords
 * @ingroup language_reference
 * @brief Language keywords with special syntax behavior.
 */

/**
 * @brief Pattern matching expression keyword.
 * @ingroup language_keywords
 *
 * Example:
 * @code{.mla}
 * let value: i32 = match maybe {
 *     Some(x) => x,
 *     None => 0,
 * };
 * @endcode
 */
void match_keyword(void);

/**
 * @defgroup language_macros Builtin Macros
 * @ingroup language_reference
 * @brief Builtin formatting and assertion macros.
 */

/**
 * @brief Print to stdout with newline.
 * @ingroup language_macros
 */
void println_macro(void);

/**
 * @brief Print to stdout without newline.
 * @ingroup language_macros
 */
void print_macro(void);

/**
 * @brief Print to stderr with newline.
 * @ingroup language_macros
 */
void eprintln_macro(void);

/**
 * @brief Print to stderr without newline.
 * @ingroup language_macros
 */
void eprint_macro(void);

/**
 * @brief Debug-only print macro.
 * @ingroup language_macros
 */
void debug_macro(void);

/**
 * @brief Format macro that returns a string.
 * @ingroup language_macros
 */
void format_macro(void);

/**
 * @brief Equality assertion macro.
 * @ingroup language_macros
 */
void assert_eq_macro(void);

/**
 * @defgroup language_attributes Builtin Attributes
 * @ingroup language_reference
 * @brief Rust-like attributes recognized by Mlang.
 */

/**
 * @brief `#[derive(Debug)]` on structs.
 * @ingroup language_attributes
 *
 * Enables debug formatting (`{:?}` and `{:#?}`) and direct struct formatting
 * in builtin formatting macros.
 */
void attr_derive_debug(void);

/**
 * @brief `#[test]` on functions.
 * @ingroup language_attributes
 *
 * Marks a function as a test target for `mlang test` and
 * `mlang run tests`.
 */
void attr_test(void);

#endif
