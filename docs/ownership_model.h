#ifndef MLANG_OWNERSHIP_MODEL_H
#define MLANG_OWNERSHIP_MODEL_H

/**
 * @file ownership_model.h
 * @brief Ownership and borrow-checking model for Mlang.
 */

/**
 * @defgroup ownership_model Ownership Model
 * @ingroup language_reference
 * @brief Ownership, moves, and local borrow lifetimes enforced by the compiler.
 *
 * The compiler tracks local shared and exclusive borrows, rejects overlapping
 * conflicts, and releases a local borrow after its last use. Aggregate values
 * that retain a borrow keep the source protected for the aggregate's live
 * range. Some aggregate operations remain conservative; see the language
 * syntax reference for current restrictions.
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
 * Mutable borrows stored in aggregates transfer from their source pointer and
 * keep the borrowed place exclusively reserved while the aggregate is live.
 * At present, an aggregate may retain only one mutable borrow; copying or
 * extracting that borrow is rejected. Shared aggregate borrows retain their
 * tracked source places, including disjoint fields where the compiler can
 * identify them.
 *
 * Callable values, runtime closure environments, function-type annotations,
 * and named-function items are implemented. A closure that borrows locals
 * across a boundary is conservatively rejected unless the compiler can prove
 * the callee does not let it escape. General closure copying and explicit
 * owned/move captures are not part of the current ownership model.
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
