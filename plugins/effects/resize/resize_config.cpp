/*
    SPDX-FileCopyrightText: 2010 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "resize_config.h"
// KConfigSkeleton
#include "resizeconfig.h"

#include <base/config-kwin.h>
#include <kwineffects_interface.h>

#include <KPluginFactory>
#include <kconfiggroup.h>

#include <QVBoxLayout>

K_PLUGIN_CLASS(KWin::ResizeEffectConfig)

namespace KWin
{

ResizeEffectConfig::ResizeEffectConfig(QObject* parent, const KPluginMetaData& data)
    : KCModule(parent, data)
{
    m_ui.setupUi(widget());

    ResizeConfig::instance(KWIN_CONFIG);
    addConfig(ResizeConfig::self(), widget());

    load();
}

void ResizeEffectConfig::save()
{
    KCModule::save();
    OrgKdeKwinEffectsInterface interface(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QDBusConnection::sessionBus());
    interface.reconfigureEffect(QStringLiteral("resize"));
}

} // namespace

#include "resize_config.moc"
