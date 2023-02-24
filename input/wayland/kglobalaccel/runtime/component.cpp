/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "component.h"

#include "global_shortcut_context.h"
#include "global_shortcuts_registry.h"

#include "input/logging.h"

#include <QKeySequence>
#include <QStringList>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <private/qtx11extras_p.h>
#else
#include <QX11Info>
#endif

static QList<QKeySequence> keysFromString(QString const& str)
{
    QList<QKeySequence> ret;
    if (str == QLatin1String("none")) {
        return ret;
    }
    QStringList const strList = str.split(QLatin1Char('\t'));
    for (QString const& s : strList) {
        QKeySequence key = QKeySequence::fromString(s, QKeySequence::PortableText);
        if (!key.isEmpty()) { // sanity check just in case
            ret.append(key);
        }
    }
    return ret;
}

static QString stringFromKeys(const QList<QKeySequence>& keys)
{
    if (keys.isEmpty()) {
        return QStringLiteral("none");
    }
    QString ret;
    for (QKeySequence const& key : keys) {
        ret.append(key.toString(QKeySequence::PortableText));
        ret.append(QLatin1Char('\t'));
    }
    ret.chop(1);
    return ret;
}

Component::Component(GlobalShortcutsRegistry& registry,
                     QString const& uniqueName,
                     QString const& friendlyName)
    : _uniqueName(uniqueName)
    , _friendlyName(friendlyName)
    , _registry(&registry)
{
    // Make sure we do no get uniquenames still containing the context
    Q_ASSERT(uniqueName.indexOf(QLatin1Char('|')) == -1);

    QString const DEFAULT(QStringLiteral("default"));
    createGlobalShortcutContext(DEFAULT, QStringLiteral("Default Context"));
    _current = _contexts.value(DEFAULT);
}

Component::~Component()
{
    // We delete all shortcuts from all contexts
    qDeleteAll(_contexts);
}

bool Component::activateGlobalShortcutContext(QString const& uniqueName)
{
    if (!_contexts.value(uniqueName)) {
        createGlobalShortcutContext(uniqueName, QStringLiteral("TODO4"));
        return false;
    }

    // Deactivate the current contexts shortcuts
    deactivateShortcuts();

    // Switch the context
    _current = _contexts.value(uniqueName);

    return true;
}

void Component::activateShortcuts()
{
    for (auto shortcut : std::as_const(_current->_actionsMap)) {
        shortcut->setActive();
    }
}

QList<GlobalShortcut*> Component::allShortcuts(QString const& contextName) const
{
    auto context = _contexts.value(contextName);
    return context ? context->_actionsMap.values() : QList<GlobalShortcut*>{};
}

QList<KGlobalShortcutInfo> Component::allShortcutInfos(QString const& contextName) const
{
    auto context = _contexts.value(contextName);
    return context ? context->allShortcutInfos() : QList<KGlobalShortcutInfo>{};
}

bool Component::cleanUp()
{
    bool changed = false;

    const auto actions = _current->_actionsMap;
    for (auto shortcut : actions) {
        qCDebug(KWIN_INPUT) << _current->_actionsMap.size();
        if (!shortcut->isPresent()) {
            changed = true;
            shortcut->unRegister();
        }
    }

    if (changed) {
        _registry->writeSettings();
        // We could be destroyed after this call!
    }

    return changed;
}

bool Component::createGlobalShortcutContext(QString const& uniqueName, QString const& friendlyName)
{
    if (_contexts.value(uniqueName)) {
        qCDebug(KWIN_INPUT) << "Shortcut Context " << uniqueName << "already exists for component "
                            << _uniqueName;
        return false;
    }
    _contexts.insert(uniqueName, new GlobalShortcutContext(uniqueName, friendlyName, this));
    return true;
}

GlobalShortcutContext* Component::currentContext()
{
    return _current;
}

QDBusObjectPath Component::dbusPath() const
{
    auto isNonAscii = [](QChar ch) {
        char const c = ch.unicode();
        bool const isAscii = c == '_' //
            || (c >= 'A' && c <= 'Z') //
            || (c >= 'a' && c <= 'z') //
            || (c >= '0' && c <= '9');
        return !isAscii;
    };

    auto dbusPath = _uniqueName;
    // DBus path can only contain ASCII characters, any non-alphanumeric char should
    // be turned into '_'
    std::replace_if(dbusPath.begin(), dbusPath.end(), isNonAscii, QLatin1Char('_'));

    // QDBusObjectPath could be a little bit easier to handle :-)
    return QDBusObjectPath(QLatin1String("/component/") + dbusPath);
}

void Component::deactivateShortcuts(bool temporarily)
{
    for (auto shortcut : std::as_const(_current->_actionsMap)) {
        if (temporarily                             //
            && _uniqueName == QLatin1String("kwin") //
            && shortcut->uniqueName() == QLatin1String("Block Global Shortcuts")) {
            continue;
        }
        shortcut->setInactive();
    }
}

void Component::emitGlobalShortcutPressed(const GlobalShortcut& shortcut)
{
    // pass X11 timestamp
    long const timestamp = QX11Info::appTime();

    if (shortcut.context()->component() != this) {
        return;
    }

    Q_EMIT globalShortcutPressed(
        shortcut.context()->component()->uniqueName(), shortcut.uniqueName(), timestamp);
}

void Component::emitGlobalShortcutReleased(const GlobalShortcut& shortcut)
{
    // pass X11 timestamp
    long const timestamp = QX11Info::appTime();

    if (shortcut.context()->component() != this) {
        return;
    }

    Q_EMIT globalShortcutReleased(
        shortcut.context()->component()->uniqueName(), shortcut.uniqueName(), timestamp);
}

void Component::invokeShortcut(QString const& shortcutName, QString const& context)
{
    auto shortcut = getShortcutByName(shortcutName, context);
    if (shortcut) {
        emitGlobalShortcutPressed(*shortcut);
    }
}

QString Component::friendlyName() const
{
    return !_friendlyName.isEmpty() ? _friendlyName : _uniqueName;
}

GlobalShortcut* Component::getShortcutByKey(QKeySequence const& key,
                                            KGlobalAccel::MatchType type) const
{
    return _current->getShortcutByKey(key, type);
}

QList<GlobalShortcut*> Component::getShortcutsByKey(QKeySequence const& key,
                                                    KGlobalAccel::MatchType type) const
{
    QList<GlobalShortcut*> rc;
    for (GlobalShortcutContext* context : std::as_const(_contexts)) {
        auto sc = context->getShortcutByKey(key, type);
        if (sc) {
            rc.append(sc);
        }
    }
    return rc;
}

GlobalShortcut* Component::getShortcutByName(QString const& uniqueName,
                                             QString const& context) const
{
    const GlobalShortcutContext* shortcutContext = _contexts.value(context);
    return shortcutContext ? shortcutContext->_actionsMap.value(uniqueName) : nullptr;
}

QStringList Component::getShortcutContexts() const
{
    return _contexts.keys();
}

bool Component::isActive() const
{
    // The component is active if at least one of it's global shortcuts is
    // present.
    for (auto shortcut : std::as_const(_current->_actionsMap)) {
        if (shortcut->isPresent()) {
            return true;
        }
    }
    return false;
}

bool Component::isShortcutAvailable(QKeySequence const& key,
                                    QString const& component,
                                    QString const& context) const
{
    qCDebug(KWIN_INPUT) << key.toString() << component;

    // if this component asks for the key. only check the keys in the same
    // context
    if (component == uniqueName()) {
        return shortcutContext(context)->isShortcutAvailable(key);
    } else {
        for (auto it = _contexts.cbegin(), endIt = _contexts.cend(); it != endIt; ++it) {
            const GlobalShortcutContext* ctx = it.value();
            if (!ctx->isShortcutAvailable(key)) {
                return false;
            }
        }
    }
    return true;
}

GlobalShortcut* Component::registerShortcut(QString const& uniqueName,
                                            QString const& friendlyName,
                                            QString const& shortcutString,
                                            QString const& defaultShortcutString)
{
    // The shortcut will register itself with us
    auto shortcut = new GlobalShortcut(*_registry, uniqueName, friendlyName, currentContext());

    const QList<QKeySequence> keys = keysFromString(shortcutString);
    shortcut->setDefaultKeys(keysFromString(defaultShortcutString));
    shortcut->setIsFresh(false);
    QList<QKeySequence> newKeys = keys;
    for (QKeySequence const& key : keys) {
        if (!key.isEmpty()) {
            if (_registry->getShortcutByKey(key)) {
                // The shortcut is already used. The config file is
                // broken. Ignore the request.
                newKeys.removeAll(key);
                qCWarning(KWIN_INPUT) << "Shortcut found twice in kglobalshortcutsrc." << key;
            }
        }
    }
    shortcut->setKeys(keys);
    return shortcut;
}

void Component::loadSettings(KConfigGroup& configGroup)
{
    // GlobalShortcutsRegistry::loadSettings handles contexts.
    const auto listKeys = configGroup.keyList();
    for (QString const& confKey : listKeys) {
        QStringList const entry = configGroup.readEntry(confKey, QStringList());
        if (entry.size() != 3) {
            continue;
        }

        auto shortcut = registerShortcut(confKey, entry[2], entry[0], entry[1]);
        if (configGroup.name().endsWith(QLatin1String(".desktop"))) {
            shortcut->setIsPresent(true);
        }
    }
}

void Component::setFriendlyName(QString const& name)
{
    _friendlyName = name;
}

GlobalShortcutContext* Component::shortcutContext(QString const& contextName)
{
    return _contexts.value(contextName);
}

GlobalShortcutContext const* Component::shortcutContext(QString const& contextName) const
{
    return _contexts.value(contextName);
}

QStringList Component::shortcutNames(QString const& contextName) const
{
    const GlobalShortcutContext* context = _contexts.value(contextName);
    return context ? context->_actionsMap.keys() : QStringList{};
}

QString Component::uniqueName() const
{
    return _uniqueName;
}

void Component::unregisterShortcut(QString const& uniqueName)
{
    // Now wrote all contexts
    for (auto context : std::as_const(_contexts)) {
        if (context->_actionsMap.value(uniqueName)) {
            delete context->takeShortcut(context->_actionsMap.value(uniqueName));
        }
    }
}

void Component::writeSettings(KConfigGroup& configGroup) const
{
    // If we don't delete the current content global shortcut
    // registrations will never not deleted after forgetGlobalShortcut()
    configGroup.deleteGroup();

    // Now write all contexts
    for (auto context : std::as_const(_contexts)) {
        KConfigGroup contextGroup;

        if (context->uniqueName() == QLatin1String("default")) {
            contextGroup = configGroup;
            // Write the friendly name
            contextGroup.writeEntry("_k_friendly_name", friendlyName());
        } else {
            contextGroup = KConfigGroup(&configGroup, context->uniqueName());
            // Write the friendly name
            contextGroup.writeEntry("_k_friendly_name", context->friendlyName());
        }

        // qCDebug(KWIN_INPUT) << "writing group " << _uniqueName << ":" <<
        // context->uniqueName();

        for (auto const shortcut : std::as_const(context->_actionsMap)) {
            // qCDebug(KWIN_INPUT) << "writing" << shortcut->uniqueName();

            // We do not write fresh shortcuts.
            // We do not write session shortcuts
            if (shortcut->isFresh() || shortcut->isSessionShortcut()) {
                continue;
            }
            // qCDebug(KWIN_INPUT) << "really writing" << shortcut->uniqueName();

            QStringList entry(stringFromKeys(shortcut->keys()));
            entry.append(stringFromKeys(shortcut->defaultKeys()));
            entry.append(shortcut->friendlyName());

            contextGroup.writeEntry(shortcut->uniqueName(), entry);
        }
    }
}
