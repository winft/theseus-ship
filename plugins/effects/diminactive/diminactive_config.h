/*
SPDX-FileCopyrightText: 2007 Christian Nitschkowski <christian.nitschkowski@kdemail.net>
SPDX-FileCopyrightText: 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KWIN_DIMINACTIVE_CONFIG_H
#define KWIN_DIMINACTIVE_CONFIG_H

#include <KCModule>

#include "ui_diminactive_config.h"

namespace KWin
{

class DimInactiveEffectConfig : public KCModule
{
    Q_OBJECT

public:
    explicit DimInactiveEffectConfig(QObject* parent,
                                     const KPluginMetaData& data,
                                     const QVariantList& args);
    ~DimInactiveEffectConfig() override;

    void save() override;

private:
    ::Ui::DimInactiveEffectConfig m_ui;
};

} // namespace KWin

#endif
