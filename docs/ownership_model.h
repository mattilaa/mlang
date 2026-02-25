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
 * In phase 1, the following are classified as `Copy`:
 * - scalar numeric types
 *   (`int`, `i8..i64`, `u8..u64`, `f32`, `f64`, `float` alias, `double` alias)
 * - `bool`
 * - raw pointers (`ptr<T>`)
 * - enums
 * - tuples where all elements are `Copy`
 * - structs where all fields are `Copy`
 */

/**
 * @defgroup ownership_move_types Move-Only Types
 * @ingroup ownership_model
 * @brief Values that transfer ownership when assigned or passed by value.
 *
 * In phase 1, the following are classified as move-only by default:
 * - `string`, `str8`, `str16`
 * - `list<T>`
 * - `map<K, V>`
 * - unknown or generic-parameter-backed struct types when field ownership
 *   cannot be fully resolved at classification time
 *
 * Recursive type cycles are conservatively treated as move-only.
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
