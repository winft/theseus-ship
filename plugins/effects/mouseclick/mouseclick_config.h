/*
SPDX-FileCopyrightText: 2012 Filip Wieladek <wattos@gmail.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KWIN_MOUSECLICK_CONFIG_H
#define KWIN_MOUSECLICK_CONFIG_H

#include <kcmodule.h>

#include "ui_mouseclick_config.h"

class KActionCollection;

namespace KWin
{

class MouseClickEffectConfig : public KCModule
{
    Q_OBJECT
public:
    explicit MouseClickEffectConfig(QObject* parent, const KPluginMetaData& data);

    void save() override;

private:
    Ui::MouseClickEffectConfigForm m_ui;
    KActionCollection* m_actionCollection;
};

} // namespace

#endif
