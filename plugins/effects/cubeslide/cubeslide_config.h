/*
    SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KWIN_CUBESLIDE_CONFIG_H
#define KWIN_CUBESLIDE_CONFIG_H

#include <kcmodule.h>

#include "ui_cubeslide_config.h"

namespace KWin
{

class CubeSlideEffectConfig : public KCModule
{
    Q_OBJECT
public:
    explicit CubeSlideEffectConfig(QObject* parent, const KPluginMetaData& data);

public Q_SLOTS:
    void save() override;

private:
    Ui::CubeSlideEffectConfigForm m_ui;
};

} // namespace

#endif
