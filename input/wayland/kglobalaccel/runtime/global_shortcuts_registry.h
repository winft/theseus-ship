/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "component.h"
#include "service_action_component.h"

#include <KGlobalAccel>
#include <KSharedConfig>
#include <QDBusObjectPath>
#include <QHash>
#include <QKeySequence>
#include <QObject>

class Component;
class GlobalShortcut;
class KGlobalAccelInterface;

/**
 * Global Shortcut Registry.
 *
 * Shortcuts are registered by component. A component is for example kmail or
 * amarok.
 *
 * A component can have contexts. Currently on plasma is planned to support
 * that feature. A context enables plasma to keep track of global shortcut
 * settings when switching containments.
 *
 * A shortcut (WIN+x) can be registered by one component only. The component
 * is allowed to register it more than once in different contexts.
 */
class GlobalShortcutsRegistry : public QObject
{
    Q_OBJECT

public:
    /**
     * Use GlobalShortcutsRegistry::self()
     *
     * @internal
     */
    GlobalShortcutsRegistry();
    ~GlobalShortcutsRegistry() override;

    /**
     * Activate all shortcuts having their application present.
     */
    void activateShortcuts();

    /**
     * Returns a list of D-Bus paths of registered Components.
     *
     * The returned paths are absolute (i.e. no need to prepend anything).
     */
    QList<QDBusObjectPath> componentsDbusPaths() const;

    /**
     * Returns a list of QStringLists (one string list per registered component,
     * with each string list containing four strings, one for each enumerator in
     * KGlobalAccel::actionIdFields).
     */
    QList<QStringList> allComponentNames() const;

    /**
     * Deactivate all currently active shortcuts.
     */
    void deactivateShortcuts(bool temporarily = false);

    /**
     */
    Component* getComponent(QString const& uniqueName);

    /**
     * Get the shortcut corresponding to key. Active and inactive shortcuts
     * are considered. But if the matching application uses contexts only one
     * shortcut is returned.
     *
     * @see getShortcutsByKey(int key)
     */
    GlobalShortcut* getShortcutByKey(const QKeySequence& key,
                                     KGlobalAccel::MatchType type
                                     = KGlobalAccel::MatchType::Equal) const;

    /**
     * Get the shortcuts corresponding to key. Active and inactive shortcuts
     * are considered.
     *
     * @see getShortcutsByKey(int key)
     */
    QList<GlobalShortcut*> getShortcutsByKey(const QKeySequence& key,
                                             KGlobalAccel::MatchType type) const;

    /**
     * Checks if @p shortcut is available for @p component.
     *
     * It is available if not used by another component in any context or used
     * by @p component only in not active contexts.
     */
    bool isShortcutAvailable(const QKeySequence& shortcut,
                             QString const& component,
                             QString const& context) const;

    bool registerKey(const QKeySequence& key, GlobalShortcut* shortcut);
    bool unregisterKey(const QKeySequence& key, GlobalShortcut* shortcut);

public Q_SLOTS:

    void clear();

    void loadSettings();

    void writeSettings();

    // Grab the keys
    void grabKeys();

    // Ungrab the keys
    void ungrabKeys();

private:
    friend class KGlobalAccelD;
    friend struct KGlobalAccelDPrivate;
    friend class Component;

    Component* createComponent(QString const& uniqueName, QString const& friendlyName);
    KServiceActionComponent* createServiceActionComponent(QString const& uniqueName,
                                                          QString const& friendlyName);

    static void unregisterComponent(Component* component);
    using ComponentPtr = std::unique_ptr<Component, decltype(&unregisterComponent)>;

    Component* registerComponent(ComponentPtr component);

    // called by the implementation to inform us about key presses
    // returns true if the key was handled
    bool keyPressed(int keyQt);
    bool keyReleased(int keyQt);

    QHash<QKeySequence, GlobalShortcut*> _active_keys;
    QKeySequence _active_sequence;
    QHash<int, int> _keys_count;

    using ComponentVec = std::vector<ComponentPtr>;
    ComponentVec m_components;
    ComponentVec::const_iterator findByName(QString const& name) const
    {
        return std::find_if(
            m_components.cbegin(), m_components.cend(), [&name](const ComponentPtr& comp) {
                return comp->uniqueName() == name;
            });
    }

    mutable KConfig _config;
    GlobalShortcut* m_lastShortcut = nullptr;
};
