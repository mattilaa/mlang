#ifndef MLANG_OWNERSHIP_MODEL_H
#define MLANG_OWNERSHIP_MODEL_H

/**
 * @file ownership_model.h
 * @brief Ownership and move semantics model for Mlang (phase 1).
 */

/**
 * @defgroup ownership_model Ownership Model
 * @ingroup language_reference
 * @brief Foundational ownership rules used as a base for borrow checking.
 *
 * This describes the initial `Copy` vs `move-only` classification that the
 * compiler uses before full borrow-lifetime analysis is implemented.
 */

/**
 * @defgroup ownership_copy_types Copy Types
 * @ingroup ownership_model
 * @brief Values that are duplicated by assignment/passing.
 *
 * The current compiler classifies the following as `Copy`:
 * - scalar numeric types
 *   (`i8..i64`, `u8..u64`, `f32`, `f64`)
 * - `bool`
 * - raw pointers (`ptr<T>`)
 * - `string`, `str8`, and `str16` values (pointer-like, shallow copies)
 * - enums
 * - tuples where all elements are `Copy`
 * - structs where all fields are `Copy`
 */

/**
 * @defgroup ownership_move_types Move-Only Types
 * @ingroup ownership_model
 * @brief Values that transfer ownership when assigned or passed by value.
 *
 * The current compiler classifies the following as move-only by default:
 * - `list<T>`
 * - `map<K, V>`
 * - unknown or generic-parameter-backed struct types when field ownership
 *   cannot be fully resolved at classification time
 *
 * Recursive type cycles are conservatively treated as move-only.
 *
 * `std::copy(value)` explicitly requests a copy without consuming `value`.
 * It follows the type's existing `Copy` semantics; it does not call a custom
 * clone method or recursively duplicate pointed-to resources. In particular,
 * copying a string or raw pointer copies the pointer, not its target. Copying
 * a struct copies its fields according to their normal `Copy` semantics.
 *
 * For a list, `std::copy` allocates independent backing storage and copies the
 * element bytes only when the element type is `Copy`. This is a shallow
 * element-wise value copy: pointer-like fields and strings inside elements
 * still refer to the same targets. Lists with move-only or unresolved element
 * types, and other move-only values, are rejected. There is currently no
 * general `Clone` trait or user-defined copy hook.
 *
 * Mlang closures are currently inline compiler constructs, not storable
 * callable values. Rust-style borrowed-by-default captures with lifetime
 * checks, and explicit owned/move captures, are a design direction rather
 * than implemented behavior.
 */

/**
 * @brief Ownership classification used by the compiler pipeline.
 * @ingroup ownership_model
 */
typedef enum MlangOwnershipClass
{
    /**
     * @brief The type is copied on assignment/pass-by-value.
     */
    MLANG_OWNERSHIP_COPY = 0,
    /**
     * @brief The type moves ownership on assignment/pass-by-value.
     */
    MLANG_OWNERSHIP_MOVE_ONLY = 1
} MlangOwnershipClass;

#endif
