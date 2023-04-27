/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "device.h"

#include <KConfigGroup>
#include <KSharedConfig>
#include <functional>
#include <string>
#include <unordered_map>
#include <variant>

namespace KWin::input::control
{

enum class config_key {
    enabled,
};

template<typename Dev, typename Type>
class config_data
{
public:
    config_data(std::string key,
                std::function<void(Dev*, Type)> setter,
                std::function<Type(Dev*)> preset)
        : key{key}
        , setter{setter}
        , preset{preset}
    {
    }

    std::string key;
    std::function<void(Dev*, Type)> setter;
    std::function<Type(Dev*)> preset;
};

template<typename Dev, typename Key, typename Type>
void write_entry(Dev* device, Key const& key, Type const& value)
{
    auto& config = device->config;

    if (!config->group.isValid()) {
        return;
    }
    if (!config->writable) {
        return;
    }

    auto it = config->map.find(key);
    assert(it != config->map.end());

    auto data = std::get<config_data<Dev, Type>>((*it).second);

    config->group.writeEntry(data.key.c_str(), value);
    config->group.sync();
}

template<typename Dev, typename Data>
void read_entry(Dev* device, Data const& data)
{
    auto value = device->config->group.readEntry(data.key.c_str(), data.preset(device));
    data.setter(device, value);
}

template<std::size_t N>
struct num {
    static constexpr auto value = N;
};

template<typename F, std::size_t... Is>
void for_variant(F func, std::index_sequence<Is...>)
{
    (func(num<Is>{}), ...);
}

template<std::size_t N, typename F>
void for_variant(F func)
{
    for_variant(func, std::make_index_sequence<N>());
}

template<typename Dev>
void load_config(Dev* device)
{
    auto& config = device->config;
    config->writable = false;

    for (auto& [key, var] : config->map) {
        using data_variant = typename decltype(config->map)::mapped_type;

        // Required for clang compiler.
        auto& local_var = var;

        auto load_from_variant = [&device, &config, &local_var](auto index) {
            if (auto const& val
                = std::get_if<std::variant_alternative_t<index.value, data_variant>>(&local_var)) {
                if (config->group.hasKey(val->key.c_str())) {
                    read_entry(device, *val);
                }
            }
        };

        for_variant<std::variant_size_v<data_variant>>(load_from_variant);
    };

    config->writable = true;
}

class device_config
{
public:
    virtual ~device_config() = default;

    KConfigGroup group;
    bool writable{true};

    using dev_cfg_bool = config_data<device, bool>;
    using dev_cfg_variant = std::variant<dev_cfg_bool>;

    std::unordered_map<config_key, dev_cfg_variant> map{
        {config_key::enabled,
         dev_cfg_bool("Enabled",
                      &device::set_enabled,
                      []([[maybe_unused]] auto dev) { return true; })},
    };
};

}
