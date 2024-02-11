/*
SPDX-FileCopyrightText: 2008 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
SPDX-FileCopyrightText: 2020 Cyril Rossi <cyril.rossi@enioka.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "main.h"

#include <kwin_effects_interface.h>
#include <como/win/types.h>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KPackage/Package>
#include <KPackage/PackageLoader>
#include <QtDBus>
#include <QVBoxLayout>

#include "kwinscreenedgeconfigform.h"
#include "kwinscreenedgedata.h"
#include "kwinscreenedgeeffectsettings.h"
#include "kwinscreenedgesettings.h"
#include "kwinscreenedgescriptsettings.h"

K_PLUGIN_FACTORY_WITH_JSON(KWinScreenEdgesConfigFactory, "kcm_kwinscreenedges.json", registerPlugin<theseus_ship::KWinScreenEdgesConfig>(); registerPlugin<theseus_ship::KWinScreenEdgeData>();)

namespace theseus_ship
{

KWinScreenEdgesConfig::KWinScreenEdgesConfig(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
    , m_form(new KWinScreenEdgesConfigForm(widget()))
    , m_config(KSharedConfig::openConfig("kwinrc"))
    , m_data(new KWinScreenEdgeData(this))
{
    QVBoxLayout *layout = new QVBoxLayout(widget());
    layout->addWidget(m_form);

    addConfig(m_data->settings(), m_form);

    monitorInit();

    connect(this, &KWinScreenEdgesConfig::defaultsIndicatorsVisibleChanged, m_form, [this]() {
        m_form->setDefaultsIndicatorsVisible(defaultsIndicatorsVisible());
    });
    connect(m_form, &KWinScreenEdgesConfigForm::saveNeededChanged, this, &KWinScreenEdgesConfig::unmanagedWidgetChangeState);
    connect(m_form, &KWinScreenEdgesConfigForm::defaultChanged, this, &KWinScreenEdgesConfig::unmanagedWidgetDefaultState);
}

KWinScreenEdgesConfig::~KWinScreenEdgesConfig()
{
}

void KWinScreenEdgesConfig::load()
{
    KCModule::load();
    m_data->settings()->load();
    for (KWinScreenEdgeScriptSettings *setting : qAsConst(m_scriptSettings)) {
        setting->load();
    }
    for (KWinScreenEdgeEffectSettings *setting : qAsConst(m_effectSettings)) {
        setting->load();
    }

    monitorLoadSettings();
    monitorLoadDefaultSettings();
    m_form->setRemainActiveOnFullscreen(m_data->settings()->remainActiveOnFullscreen());
    m_form->setElectricBorderCornerRatio(m_data->settings()->electricBorderCornerRatio());
    m_form->setDefaultElectricBorderCornerRatio(m_data->settings()->defaultElectricBorderCornerRatioValue());
    m_form->reload();
}

void KWinScreenEdgesConfig::save()
{
    monitorSaveSettings();
    m_data->settings()->setRemainActiveOnFullscreen(m_form->remainActiveOnFullscreen());
    m_data->settings()->setElectricBorderCornerRatio(m_form->electricBorderCornerRatio());
    m_data->settings()->save();
    for (KWinScreenEdgeScriptSettings *setting : qAsConst(m_scriptSettings)) {
        setting->save();
    }
    for (KWinScreenEdgeEffectSettings* setting : qAsConst(m_effectSettings)) {
        setting->save();
    }

    // Reload saved settings to ScreenEdge UI
    monitorLoadSettings();
    m_form->setElectricBorderCornerRatio(m_data->settings()->electricBorderCornerRatio());
    m_form->setRemainActiveOnFullscreen(m_data->settings()->remainActiveOnFullscreen());
    m_form->reload();

    // Reload KWin.
    QDBusMessage message = QDBusMessage::createSignal("/KWin", "org.kde.KWin", "reloadConfig");
    QDBusConnection::sessionBus().send(message);
    // and reconfigure the effects
    OrgKdeKwinEffectsInterface interface(QStringLiteral("org.kde.KWin"),
                                             QStringLiteral("/Effects"),
                                             QDBusConnection::sessionBus());
    interface.reconfigureEffect(QStringLiteral("windowview"));
    interface.reconfigureEffect(QStringLiteral("cube"));
    for (auto const& effectId : qAsConst(m_effects)) {
        interface.reconfigureEffect(effectId);
    }

    KCModule::save();
}

void KWinScreenEdgesConfig::defaults()
{
    m_form->setDefaults();

    KCModule::defaults();
}

//-----------------------------------------------------------------------------
// Monitor

static QList<KPluginMetaData> listBuiltinEffects()
{
    const QString rootDirectory = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                         QStringLiteral("kwin/builtin-effects"),
                                                         QStandardPaths::LocateDirectory);

    QList<KPluginMetaData> ret;

    const QStringList nameFilters{QStringLiteral("*.json")};
    QDirIterator it(rootDirectory, nameFilters, QDir::Files);
    while (it.hasNext()) {
        it.next();
        if (const KPluginMetaData metaData = KPluginMetaData::fromJsonFile(it.filePath()); metaData.isValid()) {
            ret.append(metaData);
        }
    }

    return ret;
}

static QList<KPluginMetaData> listScriptedEffects()
{
    return KPackage::PackageLoader::self()->listPackages(QStringLiteral("KWin/Effect"), QStringLiteral("kwin/effects/"));
}

void KWinScreenEdgesConfig::monitorInit()
{
    m_form->monitorAddItem(i18n("No Action"));
    m_form->monitorAddItem(i18n("Show Desktop"));
    m_form->monitorAddItem(i18n("Lock Screen"));
    m_form->monitorAddItem(i18n("Show KRunner"));
    m_form->monitorAddItem(i18n("Application Launcher"));

    // TODO: Find a better way to get the display name of the present windows,
    // Maybe install metadata.json files?
    const QString presentWindowsName = i18n("Present Windows");
    m_form->monitorAddItem(i18n("%1 - All Desktops", presentWindowsName));
    m_form->monitorAddItem(i18n("%1 - Current Desktop", presentWindowsName));
    m_form->monitorAddItem(i18n("%1 - Current Application", presentWindowsName));
    auto cubeName = QString("Cube");
    m_form->monitorAddItem(i18n("%1 - Cube", cubeName));
    m_form->monitorAddItem(i18n("%1 - Cylinder", cubeName));
    m_form->monitorAddItem(i18n("%1 - Sphere", cubeName));

    m_form->monitorAddItem(i18n("Toggle window switching"));
    m_form->monitorAddItem(i18n("Toggle alternative window switching"));

    KConfigGroup config(m_config, QStringLiteral("Plugins"));
    const auto effects = listBuiltinEffects() << listScriptedEffects();

    for (KPluginMetaData const& effect : effects) {
        if (!effect.value(QStringLiteral("X-KWin-Border-Activate"), false)) {
            continue;
        }

        if (!config.readEntry(effect.pluginId() + QStringLiteral("Enabled"), effect.isEnabledByDefault())) {
            continue;
        }
        m_effects << effect.pluginId();
        m_form->monitorAddItem(effect.name());
        m_effectSettings[effect.pluginId()] = new KWinScreenEdgeEffectSettings(effect.pluginId(), this);
    }

    const QString scriptFolder = QStringLiteral("kwin/scripts/");
    const auto scripts = KPackage::PackageLoader::self()->listPackages(QStringLiteral("KWin/Script"), scriptFolder);

    for (const KPluginMetaData &script: scripts) {
        if (!script.value(QStringLiteral("X-KWin-Border-Activate"), false)) {
            continue;
        }

        if (!config.readEntry(script.pluginId() + QStringLiteral("Enabled"), script.isEnabledByDefault())) {
            continue;
        }
        m_scripts << script.pluginId();
        m_form->monitorAddItem(script.name());
        m_scriptSettings[script.pluginId()] = new KWinScreenEdgeScriptSettings(script.pluginId(), this);
    }

    monitorShowEvent();
}

void KWinScreenEdgesConfig::monitorLoadSettings()
{
    // Load ElectricBorderActions
    m_form->monitorChangeEdge(como::win::electric_border::top, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->top()));
    m_form->monitorChangeEdge(como::win::electric_border::top_right, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->topRight()));
    m_form->monitorChangeEdge(como::win::electric_border::right, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->right()));
    m_form->monitorChangeEdge(como::win::electric_border::bottom_right, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->bottomRight()));
    m_form->monitorChangeEdge(como::win::electric_border::bottom, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->bottom()));
    m_form->monitorChangeEdge(como::win::electric_border::bottom_left, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->bottomLeft()));
    m_form->monitorChangeEdge(como::win::electric_border::left, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->left()));
    m_form->monitorChangeEdge(como::win::electric_border::top_left, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->topLeft()));

    // Load effect-specific actions:

    // PresentWindows BorderActivateAll
    m_form->monitorChangeEdge(m_data->settings()->borderActivateAll(), PresentWindowsAll);

    // PresentWindows BorderActivate
    m_form->monitorChangeEdge(m_data->settings()->borderActivatePresentWindows(), PresentWindowsCurrent);

    // PresentWindows BorderActivateClass
    m_form->monitorChangeEdge(m_data->settings()->borderActivateClass(), PresentWindowsClass);

    // Desktop Cube
    m_form->monitorChangeEdge(m_data->settings()->borderActivateCube(), Cube);
    m_form->monitorChangeEdge(m_data->settings()->borderActivateCylinder(), Cylinder);
    m_form->monitorChangeEdge(m_data->settings()->borderActivateSphere(), Sphere);

    // TabBox
    m_form->monitorChangeEdge(m_data->settings()->borderActivateTabBox(), TabBox);
    // Alternative TabBox
    m_form->monitorChangeEdge(m_data->settings()->borderAlternativeActivate(), TabBoxAlternative);

    // Dinamically loaded effects
    int lastIndex = EffectCount;
    for (int i = 0; i < m_effects.size(); i++) {
        m_form->monitorChangeEdge(m_effectSettings[m_effects[i]]->borderActivate(), lastIndex);
        ++lastIndex;
    }

    // Scripts
    for (int i = 0; i < m_scripts.size(); i++) {
        m_form->monitorChangeEdge(m_scriptSettings[m_scripts[i]]->borderActivate(), lastIndex);
        ++lastIndex;
    }
}

void KWinScreenEdgesConfig::monitorLoadDefaultSettings()
{
    // Load ElectricBorderActions
    m_form->monitorChangeDefaultEdge(como::win::electric_border::top, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->defaultTopValue()));
    m_form->monitorChangeDefaultEdge(como::win::electric_border::top_right, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->defaultTopRightValue()));
    m_form->monitorChangeDefaultEdge(como::win::electric_border::right, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->defaultRightValue()));
    m_form->monitorChangeDefaultEdge(como::win::electric_border::bottom_right, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->defaultBottomRightValue()));
    m_form->monitorChangeDefaultEdge(como::win::electric_border::bottom, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->defaultBottomValue()));
    m_form->monitorChangeDefaultEdge(como::win::electric_border::bottom_left, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->defaultBottomLeftValue()));
    m_form->monitorChangeDefaultEdge(como::win::electric_border::left, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->defaultLeftValue()));
    m_form->monitorChangeDefaultEdge(como::win::electric_border::top_left, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->defaultTopLeftValue()));

    // Load effect-specific actions:

    // PresentWindows BorderActivateAll
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultBorderActivateAllValue(), PresentWindowsAll);

    // PresentWindows BorderActivate
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultBorderActivatePresentWindowsValue(), PresentWindowsCurrent);

    // PresentWindows BorderActivateClass
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultBorderActivateClassValue(), PresentWindowsClass);

    // Desktop Cube
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultBorderActivateCubeValue(), Cube);
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultBorderActivateCylinderValue(), Cylinder);
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultBorderActivateSphereValue(), Sphere);

    // TabBox
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultBorderActivateTabBoxValue(), TabBox);
    // Alternative TabBox
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultBorderAlternativeActivateValue(), TabBoxAlternative);
}

void KWinScreenEdgesConfig::monitorSaveSettings()
{
    // Save ElectricBorderActions
    m_data->settings()->setTop(KWinScreenEdgesConfig::electricBorderActionToString(m_form->selectedEdgeItem(como::win::electric_border::top)));
    m_data->settings()->setTopRight(KWinScreenEdgesConfig::electricBorderActionToString(m_form->selectedEdgeItem(como::win::electric_border::top_right)));
    m_data->settings()->setRight(KWinScreenEdgesConfig::electricBorderActionToString(m_form->selectedEdgeItem(como::win::electric_border::right)));
    m_data->settings()->setBottomRight(KWinScreenEdgesConfig::electricBorderActionToString(m_form->selectedEdgeItem(como::win::electric_border::bottom_right)));
    m_data->settings()->setBottom(KWinScreenEdgesConfig::electricBorderActionToString(m_form->selectedEdgeItem(como::win::electric_border::bottom)));
    m_data->settings()->setBottomLeft(KWinScreenEdgesConfig::electricBorderActionToString(m_form->selectedEdgeItem(como::win::electric_border::bottom_left)));
    m_data->settings()->setLeft(KWinScreenEdgesConfig::electricBorderActionToString(m_form->selectedEdgeItem(como::win::electric_border::left)));
    m_data->settings()->setTopLeft(KWinScreenEdgesConfig::electricBorderActionToString(m_form->selectedEdgeItem(como::win::electric_border::top_left)));

    // Save effect-specific actions:

    // Present Windows
    m_data->settings()->setBorderActivateAll(m_form->monitorCheckEffectHasEdgeInt(PresentWindowsAll));
    m_data->settings()->setBorderActivatePresentWindows(m_form->monitorCheckEffectHasEdgeInt(PresentWindowsCurrent));
    m_data->settings()->setBorderActivateClass(m_form->monitorCheckEffectHasEdgeInt(PresentWindowsClass));

    // Desktop Cube
    m_data->settings()->setBorderActivateCube(m_form->monitorCheckEffectHasEdgeInt(Cube));
    m_data->settings()->setBorderActivateCylinder(m_form->monitorCheckEffectHasEdgeInt(Cylinder));
    m_data->settings()->setBorderActivateSphere(m_form->monitorCheckEffectHasEdgeInt(Sphere));

    // TabBox
    m_data->settings()->setBorderActivateTabBox(m_form->monitorCheckEffectHasEdgeInt(TabBox));
    m_data->settings()->setBorderAlternativeActivate(m_form->monitorCheckEffectHasEdgeInt(TabBoxAlternative));

    // Dinamically loaded effects
    int lastIndex = EffectCount;
    for (int i = 0; i < m_effects.size(); i++) {
        m_effectSettings[m_effects[i]]->setBorderActivate(m_form->monitorCheckEffectHasEdgeInt(lastIndex));
        ++lastIndex;
    }

    // Scripts
    for (int i = 0; i < m_scripts.size(); i++) {
        m_scriptSettings[m_scripts[i]]->setBorderActivate(m_form->monitorCheckEffectHasEdgeInt(lastIndex));
        ++lastIndex;
    }
}

void KWinScreenEdgesConfig::monitorShowEvent()
{
    // Check if they are enabled
    KConfigGroup config(m_config, QStringLiteral("Plugins"));

    // Present Windows
    bool enabled = config.readEntry("windowviewEnabled", true);
    m_form->monitorItemSetEnabled(PresentWindowsCurrent, enabled);
    m_form->monitorItemSetEnabled(PresentWindowsAll, enabled);

    // Desktop Cube
    enabled = config.readEntry("cube", true);
    m_form->monitorItemSetEnabled(Cube, enabled);
    m_form->monitorItemSetEnabled(Cylinder, enabled);
    m_form->monitorItemSetEnabled(Sphere, enabled);

    // tabbox, depends on reasonable focus policy.
    KConfigGroup config2(m_config, QStringLiteral("Windows"));
    QString focusPolicy = config2.readEntry("FocusPolicy", QString());
    bool reasonable = focusPolicy != "FocusStrictlyUnderMouse" && focusPolicy != "FocusUnderMouse";
    m_form->monitorItemSetEnabled(TabBox, reasonable);
    m_form->monitorItemSetEnabled(TabBoxAlternative, reasonable);

    // Disable Edge if ElectricBorders group entries are immutable
    m_form->monitorEnableEdge(como::win::electric_border::top, !m_data->settings()->isTopImmutable());
    m_form->monitorEnableEdge(como::win::electric_border::top_right, !m_data->settings()->isTopRightImmutable());
    m_form->monitorEnableEdge(como::win::electric_border::right, !m_data->settings()->isRightImmutable());
    m_form->monitorEnableEdge(como::win::electric_border::bottom_right, !m_data->settings()->isBottomRightImmutable());
    m_form->monitorEnableEdge(como::win::electric_border::bottom, !m_data->settings()->isBottomImmutable());
    m_form->monitorEnableEdge(como::win::electric_border::bottom_left, !m_data->settings()->isBottomLeftImmutable());
    m_form->monitorEnableEdge(como::win::electric_border::left, !m_data->settings()->isLeftImmutable());
    m_form->monitorEnableEdge(como::win::electric_border::top_left, !m_data->settings()->isTopLeftImmutable());

    // Disable ElectricBorderCornerRatio if entry is immutable
    m_form->setElectricBorderCornerRatioEnabled(!m_data->settings()->isElectricBorderCornerRatioImmutable());
}

int KWinScreenEdgesConfig::electricBorderActionFromString(const QString &string)
{
    QString lowerName = string.toLower();
    if (lowerName == QStringLiteral("showdesktop")) {
        return static_cast<int>(como::win::electric_border_action::show_desktop);
    }
    if (lowerName == QStringLiteral("lockscreen")) {
        return static_cast<int>(como::win::electric_border_action::lockscreen);
    }
    if (lowerName == QStringLiteral("krunner")) {
        return static_cast<int>(como::win::electric_border_action::krunner);
    }
    if (lowerName == QStringLiteral("applicationlauncher")) {
        return static_cast<int>(como::win::electric_border_action::application_launcher);
    }
    return static_cast<int>(como::win::electric_border_action::none);
}

QString KWinScreenEdgesConfig::electricBorderActionToString(int action)
{
    switch (action) {
    case 1:
        return QStringLiteral("ShowDesktop");
    case 2:
        return QStringLiteral("LockScreen");
    case 3:
        return QStringLiteral("KRunner");
    case 4:
        return QStringLiteral("ApplicationLauncher");
    default:
        return QStringLiteral("None");
    }
}

}

#include "main.moc"
