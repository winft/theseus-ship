/*
    SPDX-FileCopyrightText: 2010 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KWIN_RESIZE_CONFIG_H
#define KWIN_RESIZE_CONFIG_H

#include <KCModule>

#include "ui_resize_config.h"

namespace KWin
{

class ResizeEffectConfig : public KCModule
{
    Q_OBJECT
public:
    explicit ResizeEffectConfig(QObject* parent, const KPluginMetaData& data);

public Q_SLOTS:
    void save() override;

private:
    Ui::ResizeEffectConfigForm m_ui;
};

} // namespace

#endif
