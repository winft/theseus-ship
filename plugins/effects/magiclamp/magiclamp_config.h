/*
    SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KWIN_MAGICLAMP_CONFIG_H
#define KWIN_MAGICLAMP_CONFIG_H

#include <KCModule>

#include "ui_magiclamp_config.h"

namespace KWin
{

class MagicLampEffectConfig : public KCModule
{
    Q_OBJECT
public:
    explicit MagicLampEffectConfig(QObject* parent, const KPluginMetaData& data);

public Q_SLOTS:
    void save() override;

private:
    Ui::MagicLampEffectConfigForm m_ui;
};

} // namespace

#endif
