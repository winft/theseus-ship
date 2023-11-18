/*
    SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "magiclamp_config.h"
// KConfigSkeleton
#include "magiclampconfig.h"

#include <base/config-kwin.h>
#include <kwineffects_interface.h>

#include <KPluginFactory>
#include <kconfiggroup.h>

#include <QVBoxLayout>

K_PLUGIN_CLASS(KWin::MagicLampEffectConfig)

namespace KWin
{

MagicLampEffectConfig::MagicLampEffectConfig(QObject* parent, const KPluginMetaData& data)
    : KCModule(parent, data)
{
    m_ui.setupUi(widget());

    MagicLampConfig::instance(KWIN_CONFIG);
    addConfig(MagicLampConfig::self(), widget());
}

void MagicLampEffectConfig::save()
{
    KCModule::save();
    OrgKdeKwinEffectsInterface interface(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QDBusConnection::sessionBus());
    interface.reconfigureEffect(QStringLiteral("magiclamp"));
}

} // namespace

#include "magiclamp_config.moc"
