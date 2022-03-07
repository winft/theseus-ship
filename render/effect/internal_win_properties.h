/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QVariant>
#include <string_view>
#include <variant>

namespace KWin::render
{

template<typename PropType>
class internal_effect_property
{
public:
    using type = PropType;
    internal_effect_property(std::string_view name)
        : name{name}
    {
    }
    static PropType convert(QVariant const& var, bool& ok)
    {
        if (!var.canConvert<PropType>()) {
            ok = false;
            return {};
        }
        ok = true;
        return var.value<PropType>();
    }
    std::string_view const name;
};

using internal_region_property = internal_effect_property<QRegion>;

// TODO(romangg): Instead of constructing the array at runtime, we could try to create the list of
//                types at compile time with the property name as template argument of the
//                internal_effect_property class.
inline std::array<std::variant<internal_region_property>, 1> get_internal_blur_properties()
{
    return {internal_region_property("kwin_blur")};
}

}
