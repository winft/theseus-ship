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

class ThumbnailAsideEffectConfigForm : public QWidget, public Ui::ThumbnailAsideEffectConfigForm
{
    Q_OBJECT
public:
    explicit ThumbnailAsideEffectConfigForm(QWidget* parent);
};

class ThumbnailAsideEffectConfig : public KCModule
{
    Q_OBJECT
public:
    explicit ThumbnailAsideEffectConfig(QObject* parent, const KPluginMetaData& data);
    ~ThumbnailAsideEffectConfig() override;

    void save() override;

private:
    ThumbnailAsideEffectConfigForm m_ui;
    KActionCollection* m_actionCollection;
};

} // namespace

#endif
