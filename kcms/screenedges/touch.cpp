/*
SPDX-FileCopyrightText: 2008 Martin Gräßlin <mgraesslin@kde.org>
SPDX-FileCopyrightText: 2009 Lucas Murray <lmurray@undefinedfire.com>
SPDX-FileCopyrightText: 2020 Cyril Rossi <cyril.rossi@enioka.com>

SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "touch.h"
#include <kwin_effects_interface.h>

#include <KConfigGroup>
#include <KLocalizedString>
#include <KPluginFactory>
#include <KPackage/Package>
#include <KPackage/PackageLoader>
#include <QtDBus>
#include <QVBoxLayout>

#include "kwintouchscreenedgeconfigform.h"
#include "kwintouchscreenedgeeffectsettings.h"
#include "kwintouchscreendata.h"
#include "kwintouchscreensettings.h"
#include "kwintouchscreenscriptsettings.h"

K_PLUGIN_FACTORY_WITH_JSON(KWinScreenEdgesConfigFactory, "kcm_kwintouchscreen.json", registerPlugin<theseus_ship::KWinScreenEdgesConfig>(); registerPlugin<theseus_ship::KWinTouchScreenData>();)

namespace theseus_ship
{

KWinScreenEdgesConfig::KWinScreenEdgesConfig(QObject *parent, const KPluginMetaData &data)
    : KCModule(parent, data)
    , m_form(new KWinTouchScreenEdgeConfigForm(widget()))
    , m_config(KSharedConfig::openConfig("kwinrc"))
    , m_data(new KWinTouchScreenData(this))
{
    QVBoxLayout *layout = new QVBoxLayout(widget());
    layout->addWidget(m_form);

    monitorInit();

    connect(m_form, &KWinTouchScreenEdgeConfigForm::saveNeededChanged, this, &KWinScreenEdgesConfig::unmanagedWidgetChangeState);
    connect(m_form, &KWinTouchScreenEdgeConfigForm::defaultChanged, this, &KWinScreenEdgesConfig::unmanagedWidgetDefaultState);
}

KWinScreenEdgesConfig::~KWinScreenEdgesConfig()
{
}

void KWinScreenEdgesConfig::load()
{
    KCModule::load();
    m_data->settings()->load();
    for (KWinTouchScreenScriptSettings *setting : qAsConst(m_scriptSettings)) {
        setting->load();
    }
    for (KWinTouchScreenEdgeEffectSettings* setting : qAsConst(m_effectSettings)) {
        setting->load();
    }

    monitorLoadSettings();
    monitorLoadDefaultSettings();
    m_form->reload();
}

void KWinScreenEdgesConfig::save()
{
    monitorSaveSettings();
    m_data->settings()->save();
    for (KWinTouchScreenScriptSettings *setting : qAsConst(m_scriptSettings)) {
        setting->save();
    }
    for (KWinTouchScreenEdgeEffectSettings *setting : qAsConst(m_effectSettings)) {
        setting->save();
    }

    // Reload saved settings to ScreenEdge UI
    monitorLoadSettings();
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

void KWinScreenEdgesConfig::monitorInit()
{
    m_form->monitorHideEdge(como::win::electric_border::top_left, true);
    m_form->monitorHideEdge(como::win::electric_border::top_right, true);
    m_form->monitorHideEdge(como::win::electric_border::bottom_right, true);
    m_form->monitorHideEdge(como::win::electric_border::bottom_left, true);

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
    const QString cubeName = "Cube";
    m_form->monitorAddItem(i18n("%1 - Cube", cubeName));
    m_form->monitorAddItem(i18n("%1 - Cylinder", cubeName));
    m_form->monitorAddItem(i18n("%1 - Sphere", cubeName));

    m_form->monitorAddItem(i18n("Toggle window switching"));
    m_form->monitorAddItem(i18n("Toggle alternative window switching"));

    KConfigGroup config(m_config, QStringLiteral("Plugins"));
    const auto effects = KPackage::PackageLoader::self()->listPackages(QStringLiteral("KWin/Script"), QStringLiteral("kwin/builtin-effects/")) << KPackage::PackageLoader::self()->listPackages(QStringLiteral("KWin/Script"), QStringLiteral("kwin/effects/"));

    for (KPluginMetaData const& effect : effects) {
        if (!effect.value(QStringLiteral("X-KWin-Border-Activate"), false)) {
            continue;
        }

        if (!config.readEntry(effect.pluginId() + QStringLiteral("Enabled"), effect.isEnabledByDefault())) {
            continue;
        }
        m_effects << effect.pluginId();
        m_form->monitorAddItem(effect.name());
        m_effectSettings[effect.pluginId()] = new KWinTouchScreenEdgeEffectSettings(effect.pluginId(), this);
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
        m_scriptSettings[script.pluginId()] = new KWinTouchScreenScriptSettings(script.pluginId(), this);
    }

    monitorShowEvent();
}

void KWinScreenEdgesConfig::monitorLoadSettings()
{
    // Load ElectricBorderActions
    m_form->monitorChangeEdge(como::win::electric_border::top, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->top()));
    m_form->monitorChangeEdge(como::win::electric_border::right, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->right()));
    m_form->monitorChangeEdge(como::win::electric_border::bottom, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->bottom()));
    m_form->monitorChangeEdge(como::win::electric_border::left, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->left()));

    // Load effect-specific actions:

    // Present Windows BorderActivateAll
    m_form->monitorChangeEdge(m_data->settings()->touchBorderActivateAll(), PresentWindowsAll);
    // PresentWindows BorderActivate
    m_form->monitorChangeEdge(m_data->settings()->touchBorderActivatePresentWindows(), PresentWindowsCurrent);
    // PresentWindows BorderActivateClass
    m_form->monitorChangeEdge(m_data->settings()->touchBorderActivateClass(), PresentWindowsClass);

    // Desktop Cube BorderActivate
    m_form->monitorChangeEdge(m_data->settings()->touchBorderActivateCube(), Cube);
    // Desktop Cube BorderActivateCylinder
    m_form->monitorChangeEdge(m_data->settings()->touchBorderActivateCylinder(), Cylinder);
    // Desktop Cube BorderActivateSphere
    m_form->monitorChangeEdge(m_data->settings()->touchBorderActivateSphere(), Sphere);

    // TabBox BorderActivate
    m_form->monitorChangeEdge(m_data->settings()->touchBorderActivateTabBox(), TabBox);
    // Alternative TabBox
    m_form->monitorChangeEdge(m_data->settings()->touchBorderAlternativeActivate(), TabBoxAlternative);

    // Dinamically loaded effects
    int lastIndex = EffectCount;
    for (int i = 0; i < m_effects.size(); i++) {
        m_form->monitorChangeEdge(m_effectSettings[m_effects[i]]->touchBorderActivate(), lastIndex);
        ++lastIndex;
    }

    // Scripts
    for (int i=0; i < m_scripts.size(); i++) {
        m_form->monitorChangeEdge(m_scriptSettings[m_scripts[i]]->touchBorderActivate(), lastIndex);
        ++lastIndex;
    }
}

void KWinScreenEdgesConfig::monitorLoadDefaultSettings()
{
    m_form->monitorChangeDefaultEdge(como::win::electric_border::top, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->defaultTopValue()));
    m_form->monitorChangeDefaultEdge(como::win::electric_border::right, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->defaultRightValue()));
    m_form->monitorChangeDefaultEdge(como::win::electric_border::bottom, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->defaultBottomValue()));
    m_form->monitorChangeDefaultEdge(como::win::electric_border::left, KWinScreenEdgesConfig::electricBorderActionFromString(m_data->settings()->defaultLeftValue()));

    // Present Windows BorderActivateAll
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultTouchBorderActivateAllValue(), PresentWindowsAll);
    // PresentWindows BorderActivate
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultTouchBorderActivatePresentWindowsValue(), PresentWindowsCurrent);
    // PresentWindows BorderActivateClass
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultTouchBorderActivateClassValue(), PresentWindowsClass);

    // Desktop Cube BorderActivate
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultTouchBorderActivateCubeValue(), Cube);
    // Desktop Cube BorderActivateCylinder
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultTouchBorderActivateCylinderValue(), Cylinder);
    // Desktop Cube BorderActivateSphere
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultTouchBorderActivateSphereValue(), Sphere);

    // TabBox BorderActivate
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultTouchBorderActivateTabBoxValue(), TabBox);
    // Alternative TabBox
    m_form->monitorChangeDefaultEdge(m_data->settings()->defaultTouchBorderAlternativeActivateValue(), TabBoxAlternative);
}

void KWinScreenEdgesConfig::monitorSaveSettings()
{
    // Save ElectricBorderActions
    m_data->settings()->setTop(KWinScreenEdgesConfig::electricBorderActionToString(m_form->selectedEdgeItem(como::win::electric_border::top)));
    m_data->settings()->setRight(KWinScreenEdgesConfig::electricBorderActionToString(m_form->selectedEdgeItem(como::win::electric_border::right)));
    m_data->settings()->setBottom(KWinScreenEdgesConfig::electricBorderActionToString(m_form->selectedEdgeItem(como::win::electric_border::bottom)));
    m_data->settings()->setLeft(KWinScreenEdgesConfig::electricBorderActionToString(m_form->selectedEdgeItem(como::win::electric_border::left)));

    // Save effect-specific actions:

    // Present Windows
    m_data->settings()->setTouchBorderActivateAll(m_form->monitorCheckEffectHasEdgeInt(PresentWindowsAll));
    m_data->settings()->setTouchBorderActivatePresentWindows(m_form->monitorCheckEffectHasEdgeInt(PresentWindowsCurrent));
    m_data->settings()->setTouchBorderActivateClass(m_form->monitorCheckEffectHasEdgeInt(PresentWindowsClass));

    // Desktop Cube
    m_data->settings()->setTouchBorderActivateCube(m_form->monitorCheckEffectHasEdgeInt(Cube));
    m_data->settings()->setTouchBorderActivateCylinder(m_form->monitorCheckEffectHasEdgeInt(Cylinder));
    m_data->settings()->setTouchBorderActivateSphere(m_form->monitorCheckEffectHasEdgeInt(Sphere));

    // TabBox
    m_data->settings()->setTouchBorderActivateTabBox(m_form->monitorCheckEffectHasEdgeInt(TabBox));
    m_data->settings()->setTouchBorderAlternativeActivate(m_form->monitorCheckEffectHasEdgeInt(TabBoxAlternative));

    // Dinamically loaded effects
    int lastIndex = EffectCount;
    for (int i = 0; i < m_effects.size(); i++) {
        m_effectSettings[m_effects[i]]->setTouchBorderActivate(m_form->monitorCheckEffectHasEdgeInt(lastIndex));
        ++lastIndex;
    }

    // Scripts
    for (int i = 0; i < m_scripts.size(); i++) {
        m_scriptSettings[m_scripts[i]]->setTouchBorderActivate(m_form->monitorCheckEffectHasEdgeInt(lastIndex));
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

    // Disable Edge if TouchEdges group entries are immutable
    m_form->monitorEnableEdge(como::win::electric_border::top, !m_data->settings()->isTopImmutable());
    m_form->monitorEnableEdge(como::win::electric_border::right, !m_data->settings()->isRightImmutable());
    m_form->monitorEnableEdge(como::win::electric_border::bottom, !m_data->settings()->isBottomImmutable());
    m_form->monitorEnableEdge(como::win::electric_border::left, !m_data->settings()->isLeftImmutable());
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

} // namespace

#include "touch.moc"
