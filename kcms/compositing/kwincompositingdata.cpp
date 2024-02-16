/*
    SPDX-FileCopyrightText: 2020 Cyril Rossi <cyril.rossi@enioka.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "kwincompositingdata.h"

#include "kwincompositing_setting.h"

KWinCompositingData::KWinCompositingData(QObject* parent)
    : KCModuleData(parent)
    , m_settings(new KWinCompositingSetting(this))

{
}

bool KWinCompositingData::isDefaults() const
{
    bool defaults = true;
    auto const items = m_settings->items();
    for (const auto& item : items) {
        if (item->key() != QStringLiteral("OpenGLIsUnsafe")) {
            defaults &= item->isDefault();
        }
    }
    return defaults;
}
