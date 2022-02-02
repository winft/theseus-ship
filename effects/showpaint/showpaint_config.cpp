/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 2018 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

#include "showpaint_config.h"

#include <KAboutData>
#include <KActionCollection>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KShortcutsEditor>

#include <QAction>

K_PLUGIN_CLASS(KWin::ShowPaintEffectConfig)

namespace KWin
{

ShowPaintEffectConfig::ShowPaintEffectConfig(QWidget *parent, const QVariantList &args)
    : KCModule(parent, args)
    , m_ui(new Ui::ShowPaintEffectConfig)
{
    m_ui->setupUi(this);

    auto *actionCollection = new KActionCollection(this, QStringLiteral("kwin"));

    actionCollection->setComponentDisplayName(i18n("KWin"));
    actionCollection->setConfigGroup(QStringLiteral("ShowPaint"));
    actionCollection->setConfigGlobal(true);

    QAction *toggleAction = actionCollection->addAction(QStringLiteral("Toggle"));
    toggleAction->setText(i18n("Toggle Show Paint"));
    toggleAction->setProperty("isConfigurationAction", true);
    KGlobalAccel::self()->setDefaultShortcut(toggleAction, {});
    KGlobalAccel::self()->setShortcut(toggleAction, {});

    m_ui->shortcutsEditor->addCollection(actionCollection);

    connect(m_ui->shortcutsEditor, &KShortcutsEditor::keyChange,
            this, &ShowPaintEffectConfig::markAsChanged);

    load();
}

ShowPaintEffectConfig::~ShowPaintEffectConfig()
{
    // If save() is called, undo() has no effect.
    m_ui->shortcutsEditor->undo();

    delete m_ui;
}

void ShowPaintEffectConfig::save()
{
    KCModule::save();
    m_ui->shortcutsEditor->save();
}

void ShowPaintEffectConfig::defaults()
{
    m_ui->shortcutsEditor->allDefault();
    KCModule::defaults();
}

} // namespace KWin

#include "showpaint_config.moc"
