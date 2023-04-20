/*
    SPDX-FileCopyrightText: 2008 Cédric Borgese <cedric.borgese@gmail.com>
    SPDX-FileCopyrightText: 2008 Lucas Murray <lmurray@undefinedfire.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KWIN_WOBBLYWINDOWS_CONFIG_H
#define KWIN_WOBBLYWINDOWS_CONFIG_H

#include <kcmodule.h>

#include "ui_wobblywindows_config.h"

namespace KWin
{

class WobblyWindowsEffectConfig : public KCModule
{
    Q_OBJECT
public:
    explicit WobblyWindowsEffectConfig(QObject* parent,
                                       const KPluginMetaData& data,
                                       const QVariantList& args);
    ~WobblyWindowsEffectConfig() override;

public Q_SLOTS:
    void save() override;

private Q_SLOTS:
    void wobblinessChanged();

private:
    ::Ui::WobblyWindowsEffectConfigForm m_ui;
};

} // namespace

#endif
