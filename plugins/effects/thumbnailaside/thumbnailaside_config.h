/*
SPDX-FileCopyrightText: 2007 Christian Nitschkowski <christian.nitschkowski@kdemail.net>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#ifndef KWIN_THUMBNAILASIDE_CONFIG_H
#define KWIN_THUMBNAILASIDE_CONFIG_H

#include <KCModule>

#include "ui_thumbnailaside_config.h"

class KActionCollection;

namespace KWin
{

class ThumbnailAsideEffectConfig : public KCModule
{
    Q_OBJECT
public:
    explicit ThumbnailAsideEffectConfig(QObject* parent, const KPluginMetaData& data);

    void save() override;

private:
    Ui::ThumbnailAsideEffectConfigForm m_ui;
    KActionCollection* m_actionCollection;
};

} // namespace

#endif
