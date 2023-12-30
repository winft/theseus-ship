/*
SPDX-FileCopyrightText: 2007 Christian Nitschkowski <christian.nitschkowski@kdemail.net>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KWIN_LOOKINGGLASS_CONFIG_H
#define KWIN_LOOKINGGLASS_CONFIG_H

#include <kcmodule.h>

#include "ui_lookingglass_config.h"

class KActionCollection;

namespace KWin
{

class LookingGlassEffectConfig : public KCModule
{
    Q_OBJECT
public:
    explicit LookingGlassEffectConfig(QObject* parent, const KPluginMetaData& data);

    void save() override;
    void defaults() override;

private:
    Ui::LookingGlassEffectConfigForm m_ui;
    KActionCollection* m_actionCollection;
};

} // namespace

#endif
