/*
SPDX-FileCopyrightText: 2007 Christian Nitschkowski <christian.nitschkowski@kdemail.net>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KWIN_MOUSEMARK_CONFIG_H
#define KWIN_MOUSEMARK_CONFIG_H

#include <kcmodule.h>

#include "ui_mousemark_config.h"

class KActionCollection;

namespace KWin
{

class MouseMarkEffectConfig : public KCModule
{
    Q_OBJECT
public:
    explicit MouseMarkEffectConfig(QObject* parent, const KPluginMetaData& data);

    void load() override;
    void save() override;

private:
    void updateSpinBoxSuffix();

    Ui::MouseMarkEffectConfigForm m_ui;
    KActionCollection* m_actionCollection;
};

} // namespace

#endif
