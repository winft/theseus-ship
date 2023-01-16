/*
    KWin - the KDE window manager
    This file is part of the KDE project.

    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2018 David Edmundson <davidedmundson@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "effect.h"

#include "space.h"

#include "base/options.h"
#include "input/platform.h"

#include <kwineffects/effect_window.h>
#include <kwineffects/effects_handler.h>

#include <KConfigGroup>
#include <KGlobalAccel>
#include <kconfigloader.h>

#include <QAction>
#include <QFile>
#include <QQmlEngine>
#include <QStandardPaths>

Q_DECLARE_METATYPE(KSharedConfigPtr)

namespace KWin::scripting
{

struct AnimationSettings {
    enum {
        Type = 1 << 0,
        Curve = 1 << 1,
        Delay = 1 << 2,
        Duration = 1 << 3,
        FullScreen = 1 << 4,
        KeepAlive = 1 << 5
    };
    AnimationEffect::Attribute type;
    QEasingCurve::Type curve;
    QJSValue from;
    QJSValue to;
    int delay;
    uint duration;
    uint set;
    uint metaData;
    bool fullScreenEffect;
    bool keepAlive;
};

AnimationSettings animationSettingsFromObject(const QJSValue& object,
                                              base::options_qobject::AnimationCurve anim_curve)
{
    AnimationSettings settings;
    settings.set = 0;
    settings.metaData = 0;

    settings.to = object.property(QStringLiteral("to"));
    settings.from = object.property(QStringLiteral("from"));

    const QJSValue duration = object.property(QStringLiteral("duration"));
    if (duration.isNumber()) {
        settings.duration = duration.toUInt();
        settings.set |= AnimationSettings::Duration;
    } else {
        settings.duration = 0;
    }

    const QJSValue delay = object.property(QStringLiteral("delay"));
    if (delay.isNumber()) {
        settings.delay = delay.toInt();
        settings.set |= AnimationSettings::Delay;
    } else {
        settings.delay = 0;
    }

    const QJSValue curve = object.property(QStringLiteral("curve"));
    if (curve.isNumber()) {
        settings.curve = static_cast<QEasingCurve::Type>(curve.toInt());
        settings.set |= AnimationSettings::Curve;
    } else {
        auto get_qt_curve = [](base::options_qobject::AnimationCurve curve) {
            switch (curve) {
            case base::options_qobject::AnimationCurve::Quadratic:
                return QEasingCurve::InOutQuart;
            case base::options_qobject::AnimationCurve::Cubic:
                return QEasingCurve::InOutCubic;
            case base::options_qobject::AnimationCurve::Quartic:
                return QEasingCurve::InOutQuad;
            case base::options_qobject::AnimationCurve::Sine:
                return QEasingCurve::InOutSine;
            default:
                return QEasingCurve::Linear;
            }
        };
        settings.curve = get_qt_curve(anim_curve);
    }

    const QJSValue type = object.property(QStringLiteral("type"));
    if (type.isNumber()) {
        settings.type = static_cast<AnimationEffect::Attribute>(type.toInt());
        settings.set |= AnimationSettings::Type;
    } else {
        settings.type = static_cast<AnimationEffect::Attribute>(-1);
    }

    const QJSValue isFullScreen = object.property(QStringLiteral("fullScreen"));
    if (isFullScreen.isBool()) {
        settings.fullScreenEffect = isFullScreen.toBool();
        settings.set |= AnimationSettings::FullScreen;
    } else {
        settings.fullScreenEffect = false;
    }

    const QJSValue keepAlive = object.property(QStringLiteral("keepAlive"));
    if (keepAlive.isBool()) {
        settings.keepAlive = keepAlive.toBool();
        settings.set |= AnimationSettings::KeepAlive;
    } else {
        settings.keepAlive = true;
    }

    return settings;
}

static KWin::FPx2 fpx2FromScriptValue(const QJSValue& value)
{
    if (value.isNull()) {
        return FPx2();
    }
    if (value.isNumber()) {
        return FPx2(value.toNumber());
    }
    if (value.isObject()) {
        const QJSValue value1 = value.property(QStringLiteral("value1"));
        const QJSValue value2 = value.property(QStringLiteral("value2"));
        if (!value1.isNumber() || !value2.isNumber()) {
            qCDebug(KWIN_SCRIPTING) << "Cannot cast scripted FPx2 to C++";
            return FPx2();
        }
        return FPx2(value1.toNumber(), value2.toNumber());
    }
    return FPx2();
}

bool effect::supported(EffectsHandler& effects)
{
    return effects.animationsSupported();
}

effect::effect(EffectsHandler& effects,
               std::function<base::options&()> get_options,
               std::function<QSize()> get_screen_size)
    : AnimationEffect()
    , effects{effects}
    , m_engine(new QJSEngine(this))
    , m_scriptFile(QString())
    , get_options{get_options}
    , get_screen_size{get_screen_size}
{
    connect(&effects, &EffectsHandler::activeFullScreenEffectChanged, this, [this]() {
        auto fullScreenEffect = this->effects.activeFullScreenEffect();
        if (fullScreenEffect == m_activeFullScreenEffect) {
            return;
        }
        if (m_activeFullScreenEffect == this || fullScreenEffect == this) {
            Q_EMIT isActiveFullScreenEffectChanged();
        }
        m_activeFullScreenEffect = fullScreenEffect;
    });
}

effect::~effect()
{
}

bool effect::init(const QString& effectName, const QString& pathToScript)
{
    qRegisterMetaType<QJSValueList>();
    qRegisterMetaType<EffectWindowList>();

    QFile scriptFile(pathToScript);
    if (!scriptFile.open(QIODevice::ReadOnly)) {
        qCDebug(KWIN_SCRIPTING) << "Could not open script file: " << pathToScript;
        return false;
    }
    m_effectName = effectName;
    m_scriptFile = pathToScript;

    // does the effect contain an KConfigXT file?
    const QString kconfigXTFile
        = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                 QLatin1String(KWIN_NAME "/effects/") + m_effectName
                                     + QLatin1String("/contents/config/main.xml"));
    if (!kconfigXTFile.isNull()) {
        KConfigGroup cg
            = QCoreApplication::instance()->property("config").value<KSharedConfigPtr>()->group(
                QStringLiteral("Effect-%1").arg(m_effectName));
        QFile xmlFile(kconfigXTFile);
        m_config = new KConfigLoader(cg, &xmlFile, this);
        m_config->load();
    }

    m_engine->installExtensions(QJSEngine::ConsoleExtension);

    QJSValue globalObject = m_engine->globalObject();

    QJSValue effectsObject = m_engine->newQObject(&effects);
    QQmlEngine::setObjectOwnership(&effects, QQmlEngine::CppOwnership);
    globalObject.setProperty(QStringLiteral("effects"), effectsObject);

    QJSValue selfObject = m_engine->newQObject(this);
    QQmlEngine::setObjectOwnership(this, QQmlEngine::CppOwnership);
    globalObject.setProperty(QStringLiteral("effect"), selfObject);

    // desktopChanged is overloaded, which is problematic. Old code exposed the signal also
    // with parameters. QJSEngine does not so we have to fake it.
    effectsObject.setProperty(QStringLiteral("desktopChanged(int,int)"),
                              effectsObject.property(QStringLiteral("desktopChangedLegacy")));
    effectsObject.setProperty(QStringLiteral("desktopChanged(int,int,KWin::EffectWindow*)"),
                              effectsObject.property(QStringLiteral("desktopChanged")));

    globalObject.setProperty(QStringLiteral("Effect"),
                             m_engine->newQMetaObject(&effect::staticMetaObject));
    globalObject.setProperty(QStringLiteral("KWin"),
                             m_engine->newQMetaObject(&qt_script_space::staticMetaObject));
    globalObject.setProperty(QStringLiteral("Globals"),
                             m_engine->newQMetaObject(&KWin::staticMetaObject));
    globalObject.setProperty(QStringLiteral("QEasingCurve"),
                             m_engine->newQMetaObject(&QEasingCurve::staticMetaObject));

    static const QStringList globalProperties{
        QStringLiteral("animationTime"),
        QStringLiteral("displayWidth"),
        QStringLiteral("displayHeight"),

        QStringLiteral("registerShortcut"),
        QStringLiteral("registerScreenEdge"),
        QStringLiteral("registerTouchScreenEdge"),
        QStringLiteral("unregisterScreenEdge"),
        QStringLiteral("unregisterTouchScreenEdge"),

        QStringLiteral("animate"),
        QStringLiteral("set"),
        QStringLiteral("retarget"),
        QStringLiteral("redirect"),
        QStringLiteral("complete"),
        QStringLiteral("cancel"),
    };

    for (const QString& propertyName : globalProperties) {
        globalObject.setProperty(propertyName, selfObject.property(propertyName));
    }

    const QJSValue result = m_engine->evaluate(QString::fromUtf8(scriptFile.readAll()));

    if (result.isError()) {
        qCWarning(KWIN_SCRIPTING,
                  "%s:%d: error: %s",
                  qPrintable(scriptFile.fileName()),
                  result.property(QStringLiteral("lineNumber")).toInt(),
                  qPrintable(result.property(QStringLiteral("message")).toString()));
        return false;
    }

    return true;
}

void effect::animationEnded(KWin::EffectWindow* w, Attribute a, uint meta)
{
    AnimationEffect::animationEnded(w, a, meta);
    Q_EMIT animationEnded(w, 0);
}

QString effect::pluginId() const
{
    return m_effectName;
}

bool effect::isActiveFullScreenEffect() const
{
    return effects.activeFullScreenEffect() == this;
}

QJSValue effect::animate_helper(const QJSValue& object, AnimationType animationType)
{
    QJSValue windowProperty = object.property(QStringLiteral("window"));
    if (!windowProperty.isObject()) {
        m_engine->throwError(QStringLiteral("Window property missing in animation options"));
        return QJSValue();
    }

    EffectWindow* window = qobject_cast<EffectWindow*>(windowProperty.toQObject());
    if (!window) {
        m_engine->throwError(QStringLiteral("Window property references invalid window"));
        return QJSValue();
    }

    // global
    QVector<AnimationSettings> settings{
        animationSettingsFromObject(object, get_options().qobject->animationCurve())};

    QJSValue animations = object.property(QStringLiteral("animations")); // array
    if (!animations.isUndefined()) {
        if (!animations.isArray()) {
            m_engine->throwError(QStringLiteral("Animations provided but not an array"));
            return QJSValue();
        }

        const int length = static_cast<int>(animations.property(QStringLiteral("length")).toInt());
        for (int i = 0; i < length; ++i) {
            QJSValue value = animations.property(QString::number(i));
            if (value.isObject()) {
                AnimationSettings s
                    = animationSettingsFromObject(value, get_options().qobject->animationCurve());
                const uint set = s.set | settings.at(0).set;
                // Catch show stoppers (incompletable animation)
                if (!(set & AnimationSettings::Type)) {
                    m_engine->throwError(
                        QStringLiteral("Type property missing in animation options"));
                    return QJSValue();
                }
                if (!(set & AnimationSettings::Duration)) {
                    m_engine->throwError(
                        QStringLiteral("Duration property missing in animation options"));
                    return QJSValue();
                }
                // Complete local animations from global settings
                if (!(s.set & AnimationSettings::Duration)) {
                    s.duration = settings.at(0).duration;
                }
                if (!(s.set & AnimationSettings::Curve)) {
                    s.curve = settings.at(0).curve;
                }
                if (!(s.set & AnimationSettings::Delay)) {
                    s.delay = settings.at(0).delay;
                }
                if (!(s.set & AnimationSettings::FullScreen)) {
                    s.fullScreenEffect = settings.at(0).fullScreenEffect;
                }
                if (!(s.set & AnimationSettings::KeepAlive)) {
                    s.keepAlive = settings.at(0).keepAlive;
                }

                s.metaData = 0;
                typedef QMap<AnimationEffect::MetaType, QString> MetaTypeMap;
                static MetaTypeMap metaTypes(
                    {{AnimationEffect::SourceAnchor, QStringLiteral("sourceAnchor")},
                     {AnimationEffect::TargetAnchor, QStringLiteral("targetAnchor")},
                     {AnimationEffect::RelativeSourceX, QStringLiteral("relativeSourceX")},
                     {AnimationEffect::RelativeSourceY, QStringLiteral("relativeSourceY")},
                     {AnimationEffect::RelativeTargetX, QStringLiteral("relativeTargetX")},
                     {AnimationEffect::RelativeTargetY, QStringLiteral("relativeTargetY")},
                     {AnimationEffect::Axis, QStringLiteral("axis")}});

                for (auto it = metaTypes.constBegin(), end = metaTypes.constEnd(); it != end;
                     ++it) {
                    QJSValue metaVal = value.property(*it);
                    if (metaVal.isNumber()) {
                        AnimationEffect::setMetaData(it.key(), metaVal.toInt(), s.metaData);
                    }
                }

                settings << s;
            }
        }
    }

    if (settings.count() == 1) {
        const uint set = settings.at(0).set;
        if (!(set & AnimationSettings::Type)) {
            m_engine->throwError(QStringLiteral("Type property missing in animation options"));
            return QJSValue();
        }
        if (!(set & AnimationSettings::Duration)) {
            m_engine->throwError(QStringLiteral("Duration property missing in animation options"));
            return QJSValue();
        }
    } else if (!(settings.at(0).set & AnimationSettings::Type)) { // invalid global
        settings.removeAt(0); // -> get rid of it, only used to complete the others
    }

    if (settings.isEmpty()) {
        m_engine->throwError(QStringLiteral("No animations provided"));
        return QJSValue();
    }

    QJSValue array = m_engine->newArray(settings.length());
    for (int i = 0; i < settings.count(); i++) {
        const AnimationSettings& setting = settings[i];
        int animationId;
        if (animationType == AnimationType::Set) {
            animationId = set(window,
                              setting.type,
                              setting.duration,
                              setting.to,
                              setting.from,
                              setting.metaData,
                              setting.curve,
                              setting.delay,
                              setting.fullScreenEffect,
                              setting.keepAlive);
        } else {
            animationId = animate(window,
                                  setting.type,
                                  setting.duration,
                                  setting.to,
                                  setting.from,
                                  setting.metaData,
                                  setting.curve,
                                  setting.delay,
                                  setting.fullScreenEffect,
                                  setting.keepAlive);
        }
        array.setProperty(i, animationId);
    }

    return array;
}

quint64 effect::animate(KWin::EffectWindow* window,
                        KWin::AnimationEffect::Attribute attribute,
                        int ms,
                        const QJSValue& to,
                        const QJSValue& from,
                        uint metaData,
                        int curve,
                        int delay,
                        bool fullScreen,
                        bool keepAlive)
{
    QEasingCurve qec;
    if (curve < QEasingCurve::Custom)
        qec.setType(static_cast<QEasingCurve::Type>(curve));
    else if (curve == GaussianCurve)
        qec.setCustomType(qecGaussian);
    return AnimationEffect::animate(window,
                                    attribute,
                                    metaData,
                                    ms,
                                    fpx2FromScriptValue(to),
                                    qec,
                                    delay,
                                    fpx2FromScriptValue(from),
                                    fullScreen,
                                    keepAlive);
}

QJSValue effect::animate(const QJSValue& object)
{
    return animate_helper(object, AnimationType::Animate);
}

quint64 effect::set(KWin::EffectWindow* window,
                    KWin::AnimationEffect::Attribute attribute,
                    int ms,
                    const QJSValue& to,
                    const QJSValue& from,
                    uint metaData,
                    int curve,
                    int delay,
                    bool fullScreen,
                    bool keepAlive)
{
    QEasingCurve qec;
    if (curve < QEasingCurve::Custom)
        qec.setType(static_cast<QEasingCurve::Type>(curve));
    else if (curve == GaussianCurve)
        qec.setCustomType(qecGaussian);
    return AnimationEffect::set(window,
                                attribute,
                                metaData,
                                ms,
                                fpx2FromScriptValue(to),
                                qec,
                                delay,
                                fpx2FromScriptValue(from),
                                fullScreen,
                                keepAlive);
}

QJSValue effect::set(const QJSValue& object)
{
    return animate_helper(object, AnimationType::Set);
}

bool effect::retarget(quint64 animationId, const QJSValue& newTarget, int newRemainingTime)
{
    return AnimationEffect::retarget(animationId, fpx2FromScriptValue(newTarget), newRemainingTime);
}

bool effect::retarget(const QList<quint64>& animationIds,
                      const QJSValue& newTarget,
                      int newRemainingTime)
{
    return std::all_of(animationIds.begin(), animationIds.end(), [&](quint64 animationId) {
        return retarget(animationId, newTarget, newRemainingTime);
    });
}

bool effect::redirect(quint64 animationId, Direction direction, TerminationFlags terminationFlags)
{
    return AnimationEffect::redirect(animationId, direction, terminationFlags);
}

bool effect::redirect(const QList<quint64>& animationIds,
                      Direction direction,
                      TerminationFlags terminationFlags)
{
    return std::all_of(animationIds.begin(), animationIds.end(), [&](quint64 animationId) {
        return redirect(animationId, direction, terminationFlags);
    });
}

bool effect::complete(quint64 animationId)
{
    return AnimationEffect::complete(animationId);
}

bool effect::complete(const QList<quint64>& animationIds)
{
    return std::all_of(animationIds.begin(), animationIds.end(), [&](quint64 animationId) {
        return complete(animationId);
    });
}

bool effect::cancel(quint64 animationId)
{
    return AnimationEffect::cancel(animationId);
}

bool effect::cancel(const QList<quint64>& animationIds)
{
    bool ret = false;
    for (const quint64& animationId : animationIds) {
        ret |= cancel(animationId);
    }
    return ret;
}

bool effect::isGrabbed(EffectWindow* w, effect::DataRole grabRole)
{
    void* e = w->data(static_cast<KWin::DataRole>(grabRole)).value<void*>();
    if (e) {
        return e != this;
    } else {
        return false;
    }
}

bool effect::grab(EffectWindow* w, DataRole grabRole, bool force)
{
    void* grabber = w->data(grabRole).value<void*>();

    if (grabber == this) {
        return true;
    }

    if (grabber != nullptr && grabber != this && !force) {
        return false;
    }

    w->setData(grabRole, QVariant::fromValue(static_cast<void*>(this)));

    return true;
}

bool effect::ungrab(EffectWindow* w, DataRole grabRole)
{
    void* grabber = w->data(grabRole).value<void*>();

    if (grabber == nullptr) {
        return true;
    }

    if (grabber != this) {
        return false;
    }

    w->setData(grabRole, QVariant());

    return true;
}

void effect::reconfigure(ReconfigureFlags flags)
{
    AnimationEffect::reconfigure(flags);
    if (m_config) {
        m_config->read();
    }
    Q_EMIT configChanged();
}

void effect::registerShortcut(const QString& objectName,
                              const QString& text,
                              const QString& keySequence,
                              const QJSValue& callback)
{
    if (!callback.isCallable()) {
        m_engine->throwError(QStringLiteral("Shortcut handler must be callable"));
        return;
    }
    QAction* action = new QAction(this);
    action->setObjectName(objectName);
    action->setText(text);
    const QKeySequence shortcut = QKeySequence(keySequence);
    KGlobalAccel::self()->setShortcut(action, QList<QKeySequence>() << shortcut);
    effects.registerGlobalShortcut(shortcut, action);
    connect(action, &QAction::triggered, this, [this, action, callback]() {
        QJSValue actionObject = m_engine->newQObject(action);
        QQmlEngine::setObjectOwnership(action, QQmlEngine::CppOwnership);
        QJSValue(callback).call(QJSValueList{actionObject});
    });
}

bool effect::borderActivated(ElectricBorder edge)
{
    auto it = border_callbacks.find(edge);
    if (it == border_callbacks.end()) {
        return false;
    }

    for (auto const& callback : qAsConst(it->second)) {
        QJSValue(callback).call();
    }
    return true;
}

QJSValue effect::readConfig(const QString& key, const QJSValue& defaultValue)
{
    if (!m_config) {
        return defaultValue;
    }
    return m_engine->toScriptValue(m_config->property(key));
}

int effect::displayWidth() const
{
    return get_screen_size().width();
}

int effect::displayHeight() const
{
    return get_screen_size().height();
}

int effect::animationTime(int defaultTime) const
{
    return Effect::animationTime(defaultTime);
}

bool effect::registerScreenEdge(int edge, const QJSValue& callback)
{
    if (!callback.isCallable()) {
        m_engine->throwError(QStringLiteral("Screen edge handler must be callable"));
        return false;
    }

    auto it = border_callbacks.find(edge);
    if (it != border_callbacks.end()) {
        it->second.append(callback);
        return true;
    }

    // Not yet registered.
    // TODO(romangg): Better go here via internal types, than using the singleton interface.
    effects.reserveElectricBorder(static_cast<ElectricBorder>(edge), this);
    border_callbacks.insert({edge, {callback}});
    return true;
}

bool effect::unregisterScreenEdge(int edge)
{
    auto it = border_callbacks.find(edge);
    if (it == border_callbacks.end()) {
        // not previously registered
        return false;
    }
    effects.unreserveElectricBorder(static_cast<ElectricBorder>(edge), this);
    border_callbacks.erase(it);
    return true;
}

bool effect::registerTouchScreenEdge(int edge, const QJSValue& callback)
{
    if (touch_border_callbacks.find(edge) != touch_border_callbacks.end()) {
        return false;
    }
    if (!callback.isCallable()) {
        m_engine->throwError(QStringLiteral("Touch screen edge handler must be callable"));
        return false;
    }

    auto action = new QAction(this);
    connect(action, &QAction::triggered, this, [callback]() { QJSValue(callback).call(); });
    effects.registerTouchBorder(KWin::ElectricBorder(edge), action);
    touch_border_callbacks.insert({edge, action});
    return true;
}

bool effect::unregisterTouchScreenEdge(int edge)
{
    auto it = touch_border_callbacks.find(edge);
    if (it == touch_border_callbacks.end()) {
        return false;
    }
    delete it->second;
    touch_border_callbacks.erase(it);
    return true;
}

QJSEngine* effect::engine() const
{
    return m_engine;
}

} // namespace
