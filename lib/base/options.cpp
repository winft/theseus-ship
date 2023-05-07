/*
    SPDX-FileCopyrightText: 1999, 2000 Matthias Ettrich <ettrich@kde.org>
    SPDX-FileCopyrightText: 2003 Lubos Lunak <l.lunak@kde.org>
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "options.h"

#include "logging.h"
#include "platform.h"

#include "config-kwin.h"
#include "render/platform.h"

#include "options_settings.h"
#include "utils/algorithm.h"

#include <kwingl/platform.h>

#include <QOpenGLContext>
#include <QProcess>

namespace KWin::base
{

OpenGLPlatformInterface defaultGlPlatformInterface(operation_mode mode)
{
    return should_use_wayland_for_compositing(mode) ? EglPlatformInterface : GlxPlatformInterface;
}

options_qobject::options_qobject(operation_mode mode)
    : windowing_mode{mode}
{
}

void options_qobject::setCompositingMode(int compositingMode)
{
    if (m_compositingMode == static_cast<CompositingType>(compositingMode)) {
        return;
    }
    m_compositingMode = static_cast<CompositingType>(compositingMode);
    Q_EMIT compositingModeChanged();
}

void options_qobject::setUseCompositing(bool useCompositing)
{
    if (m_useCompositing == useCompositing) {
        return;
    }
    m_useCompositing = useCompositing;
    Q_EMIT useCompositingChanged();
}

void options_qobject::setHiddenPreviews(render::x11::hidden_preview hiddenPreviews)
{
    if (m_hiddenPreviews == hiddenPreviews) {
        return;
    }
    m_hiddenPreviews = hiddenPreviews;
    Q_EMIT hiddenPreviewsChanged();
}

void options_qobject::setMaxFpsInterval(qint64 maxFpsInterval)
{
    if (m_maxFpsInterval == maxFpsInterval) {
        return;
    }
    m_maxFpsInterval = maxFpsInterval;
    Q_EMIT maxFpsIntervalChanged();
}

void options_qobject::setRefreshRate(uint refreshRate)
{
    if (m_refreshRate == refreshRate) {
        return;
    }
    m_refreshRate = refreshRate;
    Q_EMIT refreshRateChanged();
}

void options_qobject::setVBlankTime(qint64 vBlankTime)
{
    if (m_vBlankTime == vBlankTime) {
        return;
    }
    m_vBlankTime = vBlankTime;
    Q_EMIT vBlankTimeChanged();
}

void options_qobject::setGlStrictBinding(bool glStrictBinding)
{
    if (m_glStrictBinding == glStrictBinding) {
        return;
    }
    m_glStrictBinding = glStrictBinding;
    Q_EMIT glStrictBindingChanged();
}

void options_qobject::setGlStrictBindingFollowsDriver(bool glStrictBindingFollowsDriver)
{
    if (m_glStrictBindingFollowsDriver == glStrictBindingFollowsDriver) {
        return;
    }
    m_glStrictBindingFollowsDriver = glStrictBindingFollowsDriver;
    Q_EMIT glStrictBindingFollowsDriverChanged();
}

void options_qobject::setWindowsBlockCompositing(bool value)
{
    if (m_windowsBlockCompositing == value) {
        return;
    }
    m_windowsBlockCompositing = value;
    Q_EMIT windowsBlockCompositingChanged();
}

void options_qobject::setAnimationCurve(render::animation_curve curve)
{
    if (m_animationCurve == curve) {
        return;
    }

    qCDebug(KWIN_CORE) << "Setting animation curve: " << static_cast<int>(curve);
    m_animationCurve = curve;
    Q_EMIT animationCurveChanged();
}

void options::updateSettings()
{
    loadConfig();
    // Read button tooltip animation effect from kdeglobals
    // Since we want to allow users to enable window decoration tooltips
    // and not kstyle tooltips and vise-versa, we don't read the
    // "EffectNoTooltip" setting from kdeglobals.

    //    QToolTip::setGloballyEnabled( d->show_tooltips );
    // KDE4 this probably needs to be done manually in clients

    // Driver-specific config detection
    reloadCompositingSettings();

    Q_EMIT qobject->configChanged();
}

void options::loadConfig()
{
    m_settings->load();

    syncFromKcfgc();

    // TODO: should they be moved into reloadCompositingSettings?
    auto config = KConfigGroup(m_settings->config(), "Compositing");
    qobject->setMaxFpsInterval(1 * 1000 * 1000 * 1000
                               / config.readEntry("MaxFPS", options_qobject::defaultMaxFps()));
    qobject->setRefreshRate(config.readEntry("RefreshRate", options_qobject::defaultRefreshRate()));
    qobject->setVBlankTime(config.readEntry("VBlankTime", options_qobject::defaultVBlankTime())
                           * 1000); // config in micro, value in nano resolution

    // Modifier Only Shortcuts
    config = KConfigGroup(m_settings->config(), "ModifierOnlyShortcuts");
    m_modifierOnlyShortcuts.clear();
    if (config.hasKey("Shift")) {
        m_modifierOnlyShortcuts.insert(Qt::ShiftModifier, config.readEntry("Shift", QStringList()));
    }
    if (config.hasKey("Control")) {
        m_modifierOnlyShortcuts.insert(Qt::ControlModifier,
                                       config.readEntry("Control", QStringList()));
    }
    if (config.hasKey("Alt")) {
        m_modifierOnlyShortcuts.insert(Qt::AltModifier, config.readEntry("Alt", QStringList()));
    }
    m_modifierOnlyShortcuts.insert(
        Qt::MetaModifier,
        config.readEntry("Meta",
                         QStringList{QStringLiteral("org.kde.plasmashell"),
                                     QStringLiteral("/PlasmaShell"),
                                     QStringLiteral("org.kde.PlasmaShell"),
                                     QStringLiteral("activateLauncherMenu")}));
}

void options::syncFromKcfgc()
{
    qobject->setWindowsBlockCompositing(m_settings->windowsBlockCompositing());
    qobject->setAnimationCurve(m_settings->animationCurve());
}

bool options::loadCompositingConfig(bool force)
{
    KConfigGroup config(m_settings->config(), "Compositing");

    bool useCompositing = false;
    CompositingType compositingMode = NoCompositing;
    QString compositingBackend = config.readEntry("Backend", "OpenGL");
    if (compositingBackend == "QPainter")
        compositingMode = QPainterCompositing;
    else
        compositingMode = OpenGLCompositing;

    if (const char* c = getenv("KWIN_COMPOSE")) {
        switch (c[0]) {
        case 'O':
            qCDebug(KWIN_CORE) << "Compositing forced to OpenGL mode by environment variable";
            compositingMode = OpenGLCompositing;
            useCompositing = true;
            break;
        case 'Q':
            qCDebug(KWIN_CORE) << "Compositing forced to QPainter mode by environment variable";
            compositingMode = QPainterCompositing;
            useCompositing = true;
            break;
        case 'N':
            if (getenv("KDE_FAILSAFE"))
                qCDebug(KWIN_CORE) << "Compositing disabled forcefully by KDE failsafe mode";
            else
                qCDebug(KWIN_CORE) << "Compositing disabled forcefully by environment variable";
            compositingMode = NoCompositing;
            break;
        default:
            qCDebug(KWIN_CORE) << "Unknown KWIN_COMPOSE mode set, ignoring";
            break;
        }
    }
    qobject->setCompositingMode(compositingMode);

    auto const platformSupportsNoCompositing
        = !should_use_wayland_for_compositing(qobject->windowing_mode);

    if (qobject->m_compositingMode == NoCompositing && platformSupportsNoCompositing) {
        qobject->setUseCompositing(false);
        return false; // do not even detect compositing preferences if explicitly disabled
    }

    // it's either enforced by env or by initial resume from "suspend" or we check the settings
    qobject->setUseCompositing(useCompositing || force
                               || config.readEntry("Enabled",
                                                   options_qobject::defaultUseCompositing()
                                                       || !platformSupportsNoCompositing));

    if (!qobject->m_useCompositing)
        return false; // not enforced or necessary and not "enabled" by settings
    return true;
}

void options::reloadCompositingSettings(bool force)
{
    if (!loadCompositingConfig(force)) {
        return;
    }
    m_settings->load();
    syncFromKcfgc();

    // Compositing settings
    KConfigGroup config(m_settings->config(), "Compositing");

    qobject->setGlStrictBindingFollowsDriver(!config.hasKey("GLStrictBinding"));
    if (!qobject->isGlStrictBindingFollowsDriver()) {
        qobject->setGlStrictBinding(
            config.readEntry("GLStrictBinding", options_qobject::defaultGlStrictBinding()));
    }

    auto previews = options_qobject::defaultHiddenPreviews();
    // 4 - off, 5 - shown, 6 - always, other are old values
    int hps = config.readEntry("HiddenPreviews", 5);
    if (hps == 4)
        previews = render::x11::hidden_preview::never;
    else if (hps == 5)
        previews = render::x11::hidden_preview::shown;
    else if (hps == 6)
        previews = render::x11::hidden_preview::always;
    qobject->setHiddenPreviews(previews);
}

double options::animationTimeFactor() const
{
#ifndef KCMRULES
    return m_settings->animationDurationFactor();
#else
    return 0;
#endif
}

QStringList options::modifierOnlyDBusShortcut(Qt::KeyboardModifier mod) const
{
    return m_modifierOnlyShortcuts.value(mod);
}

bool options_qobject::isUseCompositing() const
{
    return m_useCompositing || should_use_wayland_for_compositing(windowing_mode);
}

options::options(operation_mode mode, KSharedConfigPtr config)
    : qobject{std::make_unique<options_qobject>(mode)}
    , m_settings(new Settings(config))
{
    m_settings->setDefaults();
    syncFromKcfgc();

    m_configWatcher = KConfigWatcher::create(m_settings->sharedConfig());

    // TODO(romangg): Is this connect necessary? We don't do it for other config data.
    QObject::connect(m_configWatcher.data(),
                     &KConfigWatcher::configChanged,
                     qobject.get(),
                     [this](KConfigGroup const& group, QByteArrayList const& names) {
                         if (group.name() == QLatin1String("KDE")
                             && names.contains(QByteArrayLiteral("AnimationDurationFactor"))) {
                             Q_EMIT qobject->animationSpeedChanged();
                         }
                     });
}

options::~options() = default;

}
