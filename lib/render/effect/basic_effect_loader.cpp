/*
SPDX-FileCopyrightText: 2014 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "basic_effect_loader.h"

#include <KConfigGroup>

namespace KWin::render
{

basic_effect_loader::basic_effect_loader(KSharedConfig::Ptr config)
    : m_config{config}
{
}

load_effect_flags basic_effect_loader::readConfig(QString const& effectName,
                                                  bool defaultValue) const
{
    Q_ASSERT(m_config);
    KConfigGroup plugins(m_config, QStringLiteral("Plugins"));

    auto const key = effectName + QStringLiteral("Enabled");

    // do we have a key for the effect?
    if (plugins.hasKey(key)) {
        // we have a key in the config, so read the enabled state
        const bool load = plugins.readEntry(key, defaultValue);
        return load ? load_effect_flags::load : load_effect_flags();
    }
    // we don't have a key, so we just use the enabled by default value
    if (defaultValue) {
        return load_effect_flags::load | load_effect_flags::check_default_function;
    }
    return load_effect_flags();
}

}
