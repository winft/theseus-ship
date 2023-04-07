/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "global_shortcuts_registry.h"

#include "component.h"
#include "global_shortcut.h"
#include "global_shortcut_context.h"
#include "global_shortcut_info_private.h"
#include "service_action_component.h"

#include "input/logging.h"

#include <KDesktopFile>
#include <KFileUtils>
#include <KPluginMetaData>
#include <QDBusConnection>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QPluginLoader>
#include <QStandardPaths>

static QString getConfigFile()
{
    return qEnvironmentVariableIsSet("KGLOBALACCEL_TEST_MODE")
        ? QString()
        : QStringLiteral("kglobalshortcutsrc");
}

GlobalShortcutsRegistry::GlobalShortcutsRegistry()
    : QObject()
    , _config(getConfigFile(), KConfig::SimpleConfig)
{
}

GlobalShortcutsRegistry::~GlobalShortcutsRegistry()
{
    m_components.clear();
    _active_keys.clear();
    _keys_count.clear();
}

Component* GlobalShortcutsRegistry::registerComponent(ComponentPtr component)
{
    m_components.push_back(std::move(component));
    auto comp = m_components.back().get();
    QDBusConnection conn(QDBusConnection::sessionBus());
    conn.registerObject(comp->dbusPath().path(), comp, QDBusConnection::ExportScriptableContents);
    return comp;
}

void GlobalShortcutsRegistry::activateShortcuts()
{
    for (auto& component : m_components) {
        component->activateShortcuts();
    }
}

QList<QDBusObjectPath> GlobalShortcutsRegistry::componentsDbusPaths() const
{
    QList<QDBusObjectPath> dbusPaths;
    dbusPaths.reserve(m_components.size());
    std::transform(m_components.cbegin(),
                   m_components.cend(),
                   std::back_inserter(dbusPaths),
                   [](const auto& comp) { return comp->dbusPath(); });
    return dbusPaths;
}

QList<QStringList> GlobalShortcutsRegistry::allComponentNames() const
{
    QList<QStringList> ret;
    ret.reserve(m_components.size());
    std::transform(
        m_components.cbegin(),
        m_components.cend(),
        std::back_inserter(ret),
        [](const auto& component) {
            // A string for each enumerator in KGlobalAccel::actionIdFields
            return QStringList{component->uniqueName(), component->friendlyName(), {}, {}};
        });

    return ret;
}

void GlobalShortcutsRegistry::clear()
{
    m_components.clear();

    // The shortcuts should have deregistered themselves
    Q_ASSERT(_active_keys.isEmpty());
}

void GlobalShortcutsRegistry::deactivateShortcuts(bool temporarily)
{
    for (ComponentPtr& component : m_components) {
        component->deactivateShortcuts(temporarily);
    }
}

Component* GlobalShortcutsRegistry::getComponent(QString const& uniqueName)
{
    auto it = findByName(uniqueName);
    return it != m_components.cend() ? (*it).get() : nullptr;
}

GlobalShortcut* GlobalShortcutsRegistry::getShortcutByKey(const QKeySequence& key,
                                                          KGlobalAccel::MatchType type) const
{
    for (const ComponentPtr& component : m_components) {
        auto rc = component->getShortcutByKey(key, type);
        if (rc) {
            return rc;
        }
    }
    return nullptr;
}

QList<GlobalShortcut*>
GlobalShortcutsRegistry::getShortcutsByKey(const QKeySequence& key,
                                           KGlobalAccel::MatchType type) const
{
    QList<GlobalShortcut*> rc;
    for (const ComponentPtr& component : m_components) {
        rc = component->getShortcutsByKey(key, type);
        if (!rc.isEmpty()) {
            return rc;
        }
    }
    return {};
}

bool GlobalShortcutsRegistry::isShortcutAvailable(const QKeySequence& shortcut,
                                                  QString const& componentName,
                                                  QString const& contextName) const
{
    return std::all_of(m_components.cbegin(),
                       m_components.cend(),
                       [&shortcut, &componentName, &contextName](const ComponentPtr& component) {
                           return component->isShortcutAvailable(
                               shortcut, componentName, contextName);
                       });
}

/**
 * When we are provided just a Shift key press, interpret it as "Shift" not as "Shift+Shift"
 */
static void correctKeyEvent(int& keyQt)
{
    switch (keyQt) {
    case (Qt::ShiftModifier | Qt::Key_Shift).toCombined():
        keyQt = Qt::Key_Shift;
        break;
    case (Qt::ControlModifier | Qt::Key_Control).toCombined():
        keyQt = Qt::Key_Control;
        break;
    case (Qt::AltModifier | Qt::Key_Alt).toCombined():
        keyQt = Qt::Key_Alt;
        break;
    case (Qt::MetaModifier | Qt::Key_Meta).toCombined():
        keyQt = Qt::Key_Meta;
        break;
    }
}

bool GlobalShortcutsRegistry::keyPressed(int keyQt)
{
    correctKeyEvent(keyQt);
    int keys[maxSequenceLength] = {0, 0, 0, 0};
    int count = _active_sequence.count();
    if (count == maxSequenceLength) {
        // buffer is full, rotate it
        for (int i = 1; i < count; i++) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            keys[i - 1] = _active_sequence[i].toCombined();
#else
            keys[i - 1] = _active_sequence[i];
#endif
        }
        keys[maxSequenceLength - 1] = keyQt;
    } else {
        // just append the new key
        for (int i = 0; i < count; i++) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            keys[i] = _active_sequence[i].toCombined();
#else
            keys[i] = _active_sequence[i];
#endif
        }
        keys[count] = keyQt;
    }

    _active_sequence = QKeySequence(keys[0], keys[1], keys[2], keys[3]);

    GlobalShortcut* shortcut = nullptr;
    QKeySequence tempSequence;
    for (int length = 1; length <= _active_sequence.count(); length++) {
        // We have to check all possible matches from the end since we're rotating active sequence
        // instead of cleaning it when it's full
        int sequenceToCheck[maxSequenceLength] = {0, 0, 0, 0};
        for (int i = 0; i < length; i++) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            sequenceToCheck[i]
                = _active_sequence[_active_sequence.count() - length + i].toCombined();
#else
            sequenceToCheck[i] = _active_sequence[_active_sequence.count() - length + i];
#endif
        }
        tempSequence = QKeySequence(
            sequenceToCheck[0], sequenceToCheck[1], sequenceToCheck[2], sequenceToCheck[3]);
        shortcut = getShortcutByKey(tempSequence);

        if (shortcut) {
            break;
        }
    }

    qCDebug(KWIN_INPUT) << "Pressed key" << QKeySequence(keyQt).toString() << ", current sequence"
                        << _active_sequence.toString() << "="
                        << (shortcut ? shortcut->uniqueName()
                                     : QStringLiteral("(no shortcut found)"));
    if (!shortcut) {
        // This can happen for example with the ALT-Print shortcut of kwin.
        // ALT+PRINT is SYSREQ on my keyboard. So we grab something we think
        // is ALT+PRINT but symXToKeyQt and modXToQt make ALT+SYSREQ of it
        // when pressed (correctly). We can't match that.
        qCDebug(KWIN_INPUT) << "Got unknown key" << QKeySequence(keyQt).toString();

        // In production mode just do nothing.
        return false;
    } else if (!shortcut->isActive()) {
        qCDebug(KWIN_INPUT) << "Got inactive key" << QKeySequence(keyQt).toString();

        // In production mode just do nothing.
        return false;
    }

    qCDebug(KWIN_INPUT) << QKeySequence(keyQt).toString() << "=" << shortcut->uniqueName();

    // shortcut is found, reset active sequence
    _active_sequence = QKeySequence();

    QStringList data(shortcut->context()->component()->uniqueName());
    data.append(shortcut->uniqueName());
    data.append(shortcut->context()->component()->friendlyName());
    data.append(shortcut->friendlyName());

    if (m_lastShortcut && m_lastShortcut != shortcut) {
        m_lastShortcut->context()->component()->emitGlobalShortcutReleased(*m_lastShortcut);
    }

    // Invoke the action
    shortcut->context()->component()->emitGlobalShortcutPressed(*shortcut);
    m_lastShortcut = shortcut;

    return true;
}

bool GlobalShortcutsRegistry::keyReleased(int keyQt)
{
    Q_UNUSED(keyQt)
    if (m_lastShortcut) {
        m_lastShortcut->context()->component()->emitGlobalShortcutReleased(*m_lastShortcut);
        m_lastShortcut = nullptr;
    }
    return false;
}

Component* GlobalShortcutsRegistry::createComponent(QString const& uniqueName,
                                                    QString const& friendlyName)
{
    auto it = findByName(uniqueName);
    if (it != m_components.cend()) {
        Q_ASSERT_X(false, //
                   "GlobalShortcutsRegistry::createComponent",
                   QLatin1String("A Component with the name: %1, already exists")
                       .arg(uniqueName)
                       .toUtf8()
                       .constData());
        return (*it).get();
    }

    auto c = registerComponent(
        ComponentPtr(new Component(*this, uniqueName, friendlyName), &unregisterComponent));
    return c;
}

void GlobalShortcutsRegistry::unregisterComponent(Component* component)
{
    QDBusConnection::sessionBus().unregisterObject(component->dbusPath().path());
    delete component;
}

KServiceActionComponent*
GlobalShortcutsRegistry::createServiceActionComponent(QString const& uniqueName,
                                                      QString const& friendlyName)
{
    auto it = findByName(uniqueName);
    if (it != m_components.cend()) {
        Q_ASSERT_X(false, //
                   "GlobalShortcutsRegistry::createServiceActionComponent",
                   QLatin1String("A KServiceActionComponent with the name: %1, already exists")
                       .arg(uniqueName)
                       .toUtf8()
                       .constData());
        return static_cast<KServiceActionComponent*>((*it).get());
    }

    auto c = registerComponent(ComponentPtr(
        new KServiceActionComponent(*this, uniqueName, friendlyName), &unregisterComponent));
    return static_cast<KServiceActionComponent*>(c);
}

void GlobalShortcutsRegistry::loadSettings()
{
    Q_ASSERT(m_components.empty());

    const auto groupList = _config.groupList();
    for (QString const& groupName : groupList) {
        qCDebug(KWIN_INPUT) << "Loading group " << groupName;

        Q_ASSERT(groupName.indexOf(QLatin1Char('\x1d')) == -1);

        // loadSettings isn't designed to be called in between. Only at the
        // beginning.
        Q_ASSERT(!getComponent(groupName));

        KConfigGroup configGroup(&_config, groupName);

        QString const friendlyName = configGroup.readEntry("_k_friendly_name");

        auto const isDesktop = groupName.endsWith(QLatin1String(".desktop"));
        Component* component = isDesktop ? createServiceActionComponent(groupName, friendlyName) //
                                         : createComponent(groupName, friendlyName);

        // Now load the contexts
        auto const groupList = configGroup.groupList();
        for (QString const& context : groupList) {
            // Skip the friendly name group, this was previously used instead of _k_friendly_name
            if (context == QLatin1String("Friendly Name")) {
                continue;
            }

            KConfigGroup contextGroup(&configGroup, context);
            QString contextFriendlyName = contextGroup.readEntry("_k_friendly_name");
            component->createGlobalShortcutContext(context, contextFriendlyName);
            component->activateGlobalShortcutContext(context);
            component->loadSettings(contextGroup);
        }

        // Load the default context
        component->activateGlobalShortcutContext(QStringLiteral("default"));
        component->loadSettings(configGroup);
    }

    // Load the configured KServiceActions
    auto const desktopPaths = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                                        QStringLiteral("kglobalaccel"),
                                                        QStandardPaths::LocateDirectory);

    auto const desktopFiles
        = KFileUtils::findAllUniqueFiles(desktopPaths, {QStringLiteral("*.desktop")});

    for (auto const& file : desktopFiles) {
        auto const fileName = QFileInfo(file).fileName();
        auto it = findByName(fileName);
        if (it != m_components.cend()) {
            continue;
        }

        KDesktopFile deskF(file);
        if (deskF.noDisplay()) {
            continue;
        }

        auto actionComp = createServiceActionComponent(fileName, deskF.readName());
        actionComp->activateGlobalShortcutContext(QStringLiteral("default"));
        actionComp->loadFromService();
    }
}

void GlobalShortcutsRegistry::grabKeys()
{
    activateShortcuts();
}

bool GlobalShortcutsRegistry::registerKey(const QKeySequence& key, GlobalShortcut* shortcut)
{
    if (key.isEmpty()) {
        qCDebug(KWIN_INPUT) << shortcut->uniqueName() << ": Key '" << QKeySequence(key).toString()
                            << "' already taken by " << _active_keys.value(key)->uniqueName()
                            << ".";
        return false;
    } else if (_active_keys.value(key)) {
        qCDebug(KWIN_INPUT) << shortcut->uniqueName() << ": Attempt to register key 0.";
        return false;
    }

    qCDebug(KWIN_INPUT) << "Registering key" << QKeySequence(key).toString() << "for"
                        << shortcut->context()->component()->uniqueName() << ":"
                        << shortcut->uniqueName();

    bool error = false;
    int i;
    for (i = 0; i < key.count(); i++) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        const int combined = key[i].toCombined();
#else
        const int combined(key[i]);
#endif
        ++_keys_count[combined];
    }

    if (error) {
        // Last key was not registered, rewind index by 1
        for (--i; i >= 0; i--) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
            const int combined = key[i].toCombined();
#else
            const int combined(key[i]);
#endif
            auto it = _keys_count.find(combined);
            if (it == _keys_count.end()) {
                continue;
            }

            if (it.value() == 1) {
                _keys_count.erase(it);
            } else {
                --(it.value());
            }
        }
        return false;
    }

    _active_keys.insert(key, shortcut);

    return true;
}

void GlobalShortcutsRegistry::ungrabKeys()
{
    deactivateShortcuts();
}

bool GlobalShortcutsRegistry::unregisterKey(const QKeySequence& key, GlobalShortcut* shortcut)
{
    if (_active_keys.value(key) != shortcut) {
        // The shortcut doesn't own the key or the key isn't grabbed
        return false;
    }

    for (int i = 0; i < key.count(); i++) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
        auto iter = _keys_count.find(key[i].toCombined());

#else
        auto iter = _keys_count.find(key[i]);
#endif
        if ((iter == _keys_count.end()) || (iter.value() <= 0)) {
            continue;
        }

        // Unregister if there's only one ref to given key
        // We should fail earlier when key is not registered
        if (iter.value() == 1) {
            qCDebug(KWIN_INPUT) << "Unregistering key" << QKeySequence(key[i]).toString() << "for"
                                << shortcut->context()->component()->uniqueName() << ":"
                                << shortcut->uniqueName();
            _keys_count.erase(iter);
        } else {
            qCDebug(KWIN_INPUT) << "Refused to unregister key" << QKeySequence(key[i]).toString()
                                << ": used by another global shortcut";
            --(iter.value());
        }
    }

    if (shortcut && shortcut == m_lastShortcut) {
        m_lastShortcut->context()->component()->emitGlobalShortcutReleased(*m_lastShortcut);
        m_lastShortcut = nullptr;
    }

    _active_keys.remove(key);
    return true;
}

void GlobalShortcutsRegistry::writeSettings()
{
    auto it = std::remove_if(
        m_components.begin(), m_components.end(), [this](const ComponentPtr& component) {
            KConfigGroup configGroup(&_config, component->uniqueName());
            if (component->allShortcuts().isEmpty()) {
                configGroup.deleteGroup();
                return true;
            } else {
                component->writeSettings(configGroup);
                return false;
            }
        });

    m_components.erase(it, m_components.end());
    _config.sync();
}

#include "moc_global_shortcuts_registry.cpp"
