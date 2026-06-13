/**
 * @file mxvk_wrapper.hpp
 * @brief Type-safe nullable pointer wrapper inspired by Rust's Option<T>.
 *
 * Wrapper<T> holds an optional pointer value and provides panic-on-null
 * accessors similar to Rust's unwrap() / expect() semantics.
 */
#ifndef _MXVK_WRAPPER_H_
#define _MXVK_WRAPPER_H_

#include "mxvk_exception.hpp"

#include <format>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace mxvk {

    /**
     * @concept WrapType
     * @brief Constrains Wrapper<T> to pointer types only.
     */
    template <typename T>
    concept WrapType = std::is_pointer_v<T>;

    /**
     * @class Wrapper
     * @brief A nullable smart wrapper around a raw pointer.
     *
     * Stores an optional pointer.  Accessors throw mx::Exception if the
     * pointer is null (unwrap, expect) or return a fallback (unwrap_or).
     *
     * @tparam T A raw pointer type (must satisfy WrapType concept).
     */
    template <WrapType T>
    class Wrapper {
      public:
        /** @brief Default constructor — initialises to nullopt (no value). */
        Wrapper() = default;

        /**
         * @brief Construct from a raw pointer.
         * @param t Pointer to wrap.
         */
        Wrapper(T t) : type{t} {}

        /**
         * @brief Construct in the empty (nullopt) state.
         * @param n std::nullopt.
         */
        Wrapper(std::nullopt_t) : type{std::nullopt} {}

        /** @brief Copy constructor. */
        Wrapper(const Wrapper<T> &) = default;

        /** @brief Move constructor. */
        Wrapper(Wrapper<T> &&) noexcept = default;

        ~Wrapper() = default;

        /**
         * @brief Assign from a raw pointer.
         * @param t Pointer to store.
         * @return Reference to this.
         */
        Wrapper<T> &operator=(T t) {
            type = t;
            return *this;
        }

        /**
         * @brief Reset to the empty (nullopt) state.
         * @param n std::nullopt.
         * @return Reference to this.
         */
        Wrapper<T> &operator=(std::nullopt_t) {
            type = std::nullopt;
            return *this;
        }

        /** @brief Copy-assign from another Wrapper. */
        Wrapper<T> &operator=(const Wrapper<T> &) = default;

        /** @brief Move-assign from another Wrapper. */
        Wrapper<T> &operator=(Wrapper<T> &&) noexcept = default;

        /**
         * @brief Check whether a non-null value is held.
         * @return @c true if a non-null pointer is stored.
         */
        [[nodiscard]] bool has_value() const noexcept {
            return type.has_value() && type.value() != nullptr;
        }

        /** @brief Check if a value is present and non-null. */
        [[nodiscard]] explicit operator bool() const noexcept { return has_value(); }

        /**
         * @brief Access the stored pointer without null checking.
         * @return The stored pointer (may be nullptr if constructed from nullopt).
         */
        [[nodiscard]] T value() const { return type.value_or(nullptr); }

        /**
         * @brief Unwrap with a custom panic message on null.
         * @param msg Message to include in the thrown mx::Exception.
         * @return The stored pointer.
         * @throws mx::Exception if the value is null or absent.
         */
        [[nodiscard]] T expect(const std::string &msg) const {
            if (has_value()) {
                return type.value();
            }
            throw mxvk::Exception(std::format("panic: {}", msg));
        }

        /**
         * @brief Unwrap the stored pointer, panicking on null.
         * @return The stored pointer.
         * @throws mx::Exception if the value is null or absent.
         */
        [[nodiscard]] T unwrap() const {
            if (has_value()) {
                return type.value();
            }
            throw mxvk::Exception("mxvk panic: Wrapper value is null");
        }

        /**
         * @brief Return the stored pointer or a fallback if null.
         * @param value Fallback pointer returned when no value is held.
         * @return The stored pointer, or @p value if null/absent.
         */
        [[nodiscard]] T unwrap_or(T fallback) const noexcept {
            if (has_value()) {
                return type.value();
            }
            return fallback;
        }

      private:
        std::optional<T> type = std::nullopt; ///< Internal optional storage.
    };

} // namespace mxvk

namespace mx {
    template <mxvk::WrapType T>
    using Wrapper = mxvk::Wrapper<T>;
} // namespace mx

#endif