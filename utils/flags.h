/*
    SPDX-FileCopyrightText: 2019 Ivan Roberto de Oliveira
    SPDX-FileCopyrightText: 2020 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: MIT
*/
#pragma once

#include <type_traits>

/**
 * To enable bitwise operations on enum classes use the ENUM_FLAGS macro:
 *   enum class MyFlags {
 *      None = 0b0000,
 *      One = 0b0001,
 *      Two = 0b0010,
 *      Three = 0b0100,
 *   };
 *   ENUM_FLAGS(MyFlags)
 *
 * You may find it cumbersome to check for the presence or absence of specific
 * values in enum class. For example:
 *   MyFlags bm = ...;
 *   MyFlags oneAndThree = (MyFlags::One | MyFlags::Three);
 *   // Check if either bit one or three is set
 *   if (bm & oneAndThree != MyFlags::None) {
 *       ...
 *   }
 *   // Check if both bits one and three are set
 *   if (bm & oneAndThree == oneAndThree) {
 *       ...
 *   }
 *
 * Wrap the enum class into the flags type:
 *   MyFlags some_flags = ...;
 *   MyFlags one_and_three = (MyFlags::One | MyFlags::Three);
 *   auto wrapped_flags = flags(some_flags);
 *   // Check if either bit one or three is set
 *   if (wrapped_flags.AnyOf(one_and_three)) {
 *       ...
 *   }
 *   // Check if both bits one and three are set
 *   if (wrapped_flags.AllOf(one_and_three)) {
 *       ...
 *   }
 *   // Check if any bit is set
 *   if (wrapped_flags) {
 *       ...
 *   }
 *   // Convert back to the enum class
 *   MyFlags back_to_enum = wrapped_flags;
 *
 * Sources:
 * https://www.strikerx3.dev/cpp/2019/02/27/typesafe-enum-class-bitmasks-in-cpp.html
 * https://gpfault.net/posts/typesafe-bitmasks.txt.html
 * https://blog.bitwigglers.org/using-enum-classes-as-type-safe-bitmasks
 * https://www.justsoftwaresolutions.co.uk/cplusplus/using-enum-classes-as-bitfields.html
 */

#define ENUM_FLAGS(X)                                                                              \
    template<>                                                                                     \
    struct is_flags_enum<X> {                                                                      \
        static const bool enable = true;                                                           \
    };

template<typename Enum>
struct is_flags_enum {
    static constexpr bool enable = false;
};

template<typename Enum>
inline constexpr bool is_flags_enum_v = is_flags_enum<Enum>::enable;

// ----- Bitwise operators ----------------------------------------------------

template<typename Enum>
typename std::enable_if_t<is_flags_enum_v<Enum>, Enum> operator|(Enum lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<underlying>(lhs) | static_cast<underlying>(rhs));
}

template<typename Enum>
constexpr std::enable_if_t<is_flags_enum_v<Enum>, Enum> operator&(Enum lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<underlying>(lhs) & static_cast<underlying>(rhs));
}

template<typename Enum>
typename std::enable_if_t<is_flags_enum_v<Enum>, Enum> operator^(Enum lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(static_cast<underlying>(lhs) ^ static_cast<underlying>(rhs));
}

template<typename Enum>
typename std::enable_if_t<is_flags_enum_v<Enum>, Enum> operator~(Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    return static_cast<Enum>(~static_cast<underlying>(rhs));
}

template<typename Enum>
typename std::enable_if_t<is_flags_enum_v<Enum>, bool> operator!(Enum rhs)
{
    return rhs == static_cast<Enum>(0);
}

// ----- Bitwise assignment operators -----------------------------------------

template<typename Enum>
typename std::enable_if_t<is_flags_enum_v<Enum>, Enum> operator|=(Enum& lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    lhs = static_cast<Enum>(static_cast<underlying>(lhs) | static_cast<underlying>(rhs));
    return lhs;
}

template<typename Enum>
typename std::enable_if_t<is_flags_enum_v<Enum>, Enum> operator&=(Enum& lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    lhs = static_cast<Enum>(static_cast<underlying>(lhs) & static_cast<underlying>(rhs));
    return lhs;
}

template<typename Enum>
typename std::enable_if_t<is_flags_enum_v<Enum>, Enum> operator^=(Enum& lhs, Enum rhs)
{
    using underlying = typename std::underlying_type_t<Enum>;
    lhs = static_cast<Enum>(static_cast<underlying>(lhs) ^ static_cast<underlying>(rhs));
    return lhs;
}

// ----- flags container --------------------------------------------------

template<typename Enum>
class flags
{
    Enum const value;
    static constexpr Enum none_value = static_cast<Enum>(0);

public:
    using underlying_type = typename std::underlying_type_t<Enum>;

    constexpr flags()
        : value(none_value)
    {
    }

    constexpr flags(Enum value)
        : value(value)
    {
        static_assert(is_flags_enum_v<Enum>);
    }

    // Convert back to enum if required.
    inline operator Enum() const
    {
        return value;
    }

    // Convert to true if there is any bit set in the bitmask.
    inline operator bool() const
    {
        return any();
    }

    inline bool operator!() const
    {
        return !any();
    }

    // Returns true if any bit is set.
    inline bool any() const
    {
        return value != none_value;
    }

    // Returns true if all bits are clear.
    inline bool none() const
    {
        return value == none_value;
    }

    // Returns true if any bit in the given mask is set.
    inline bool any_of(Enum const& mask) const
    {
        return (value & mask) != none_value;
    }

    // Returns true if all bits in the given mask are set.
    inline bool all_of(Enum const& mask) const
    {
        return (value & mask) == mask;
    }

    // Returns true if none of the bits in the given mask are set.
    inline bool none_of(Enum const& mask) const
    {
        return (value & mask) == none_value;
    }

    // Returns true if any bits excluding the mask are set.
    inline bool any_except(Enum const& mask) const
    {
        return (value & ~mask) != none_value;
    }

    // Returns true if no bits excluding the mask are set.
    inline bool none_except(Enum const& mask) const
    {
        return (value & ~mask) == none_value;
    }
};
