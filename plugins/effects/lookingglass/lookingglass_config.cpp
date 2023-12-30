/*
SPDX-FileCopyrightText: 2007 Christian Nitschkowski <christian.nitschkowski@kdemail.net>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "lookingglass_config.h"

// KConfigSkeleton
#include "lookingglassconfig.h"

#include <base/config-kwin.h>
#include <kwineffects_interface.h>

#include <QAction>

#include <KActionCollection>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KPluginFactory>
#include <kconfiggroup.h>

#include <QDebug>
#include <QVBoxLayout>
#include <QWidget>

K_PLUGIN_CLASS(KWin::LookingGlassEffectConfig)

namespace KWin
{

LookingGlassEffectConfig::LookingGlassEffectConfig(QObject* parent, const KPluginMetaData& data)
    : KCModule(parent, data)
{
    m_ui.setupUi(widget());

    LookingGlassConfig::instance(KWIN_CONFIG);
    addConfig(LookingGlassConfig::self(), widget());
    connect(
        m_ui.editor, &KShortcutsEditor::keyChange, this, &LookingGlassEffectConfig::markAsChanged);

    // Shortcut config. The shortcut belongs to the component "kwin"!
    m_actionCollection = new KActionCollection(widget(), QStringLiteral("kwin"));

    m_actionCollection->setComponentDisplayName(i18n("KWin"));
    m_actionCollection->setConfigGroup(QStringLiteral("LookingGlass"));
    m_actionCollection->setConfigGlobal(true);

    QAction* a;
    a = m_actionCollection->addAction(KStandardAction::ZoomIn);
    a->setProperty("isConfigurationAction", true);
    KGlobalAccel::self()->setDefaultShortcut(
        a, QList<QKeySequence>() << static_cast<Qt::Key>(Qt::META) + Qt::Key_Equal);
    KGlobalAccel::self()->setShortcut(
        a, QList<QKeySequence>() << static_cast<Qt::Key>(Qt::META) + Qt::Key_Equal);

    a = m_actionCollection->addAction(KStandardAction::ZoomOut);
    a->setProperty("isConfigurationAction", true);
    KGlobalAccel::self()->setDefaultShortcut(
        a, QList<QKeySequence>() << static_cast<Qt::Key>(Qt::META) + Qt::Key_Minus);
    KGlobalAccel::self()->setShortcut(
        a, QList<QKeySequence>() << static_cast<Qt::Key>(Qt::META) + Qt::Key_Minus);

    a = m_actionCollection->addAction(KStandardAction::ActualSize);
    a->setProperty("isConfigurationAction", true);
    KGlobalAccel::self()->setDefaultShortcut(
        a, QList<QKeySequence>() << static_cast<Qt::Key>(Qt::META) + Qt::Key_0);
    KGlobalAccel::self()->setShortcut(
        a, QList<QKeySequence>() << static_cast<Qt::Key>(Qt::META) + Qt::Key_0);

    m_ui.editor->addCollection(m_actionCollection);
}

void LookingGlassEffectConfig::save()
{
    qDebug() << "Saving config of LookingGlass";
    KCModule::save();

    m_ui.editor->save(); // undo() will restore to this state from now on

    OrgKdeKwinEffectsInterface interface(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QDBusConnection::sessionBus());
    interface.reconfigureEffect(QStringLiteral("lookingglass"));
}

void LookingGlassEffectConfig::defaults()
{
    m_ui.editor->allDefault();
    KCModule::defaults();
}

} // namespace

#include "lookingglass_config.moc"
