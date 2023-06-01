/*
    SPDX-FileCopyrightText: 2009 Martin Gräßlin <mgraesslin@kde.org>

SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "cubeslide_config.h"
// KConfigSkeleton
#include "cubeslideconfig.h"
#include <kwinconfig.h>
#include <kwineffects_interface.h>

#include <KPluginFactory>
#include <QVBoxLayout>
#include <kconfiggroup.h>

K_PLUGIN_CLASS(KWin::CubeSlideEffectConfig)

namespace KWin
{

CubeSlideEffectConfigForm::CubeSlideEffectConfigForm(QWidget* parent)
    : QWidget(parent)
{
    setupUi(this);
}

CubeSlideEffectConfig::CubeSlideEffectConfig(QObject* parent,
                                             const KPluginMetaData& data,
                                             const QVariantList& args)
    : KCModule(parent, data, args)
{
    m_ui = new CubeSlideEffectConfigForm(widget());

    QVBoxLayout* layout = new QVBoxLayout(widget());
    layout->addWidget(m_ui);

    CubeSlideConfig::instance(KWIN_CONFIG);
    addConfig(CubeSlideConfig::self(), m_ui);

    load();
}

void CubeSlideEffectConfig::save()
{
    KCModule::save();
    OrgKdeKwinEffectsInterface interface(
        QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QDBusConnection::sessionBus());
    interface.reconfigureEffect(QStringLiteral("cubeslide"));
}

} // namespace

#include "cubeslide_config.moc"
