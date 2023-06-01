/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "global_shortcut.h"
#include "kglobalshortcutinfo.h"

#include "kconfiggroup.h"

#include <KGlobalAccel>
#include <QHash>
#include <QObject>

class GlobalShortcut;
class GlobalShortcutContext;
class GlobalShortcutsRegistry;

class Component : public QObject
{
    Q_OBJECT

    Q_CLASSINFO("D-Bus Interface", "org.kde.kglobalaccel.Component")

    /* clang-format off */
    Q_SCRIPTABLE Q_PROPERTY(QString friendlyName READ friendlyName)
    Q_SCRIPTABLE Q_PROPERTY(QString uniqueName READ uniqueName)

public:
    ~Component() override;

    /* clang-format on */

    bool activateGlobalShortcutContext(QString const& uniqueName);

    void activateShortcuts();

    /// Returns all shortcuts in context @context
    QList<GlobalShortcut*> allShortcuts(QString const& context = QStringLiteral("default")) const;

    bool createGlobalShortcutContext(QString const& context,
                                     QString const& friendlyName = QString());

    /// Return the current context
    GlobalShortcutContext* currentContext();

    /// Return uniqueName converted to a valid dbus path
    QDBusObjectPath dbusPath() const;

    /// Deactivate all currently active shortcuts
    void deactivateShortcuts(bool temporarily = false);

    GlobalShortcut* getShortcutByKey(QKeySequence const& key, KGlobalAccel::MatchType type) const;

    GlobalShortcutContext* shortcutContext(QString const& name);
    GlobalShortcutContext const* shortcutContext(QString const& name) const;

    QList<GlobalShortcut*> getShortcutsByKey(QKeySequence const& key,
                                             KGlobalAccel::MatchType type) const;
    GlobalShortcut* getShortcutByName(QString const& uniqueName,
                                      QString const& context = QStringLiteral("default")) const;

    bool isShortcutAvailable(QKeySequence const& key,
                             QString const& component,
                             QString const& context) const;

    void loadSettings(KConfigGroup& config);

    QString friendlyName() const;
    void setFriendlyName(QString const&);
    QString uniqueName() const;

    /// Unregister @a shortcut. This will remove its siblings from all contexts
    void unregisterShortcut(QString const& uniqueName);

    void writeSettings(KConfigGroup& config) const;

protected:
    friend class ::GlobalShortcutsRegistry;

    /// Constructs a component. This is a private constructor, to create a component use
    /// GlobalShortcutsRegistry::self()->createComponent().
    Component(GlobalShortcutsRegistry& registry,
              QString const& uniqueName,
              QString const& friendlyName);

    /**
     * Create a new globalShortcut by its name
     * @param uniqueName internal unique name to identify the shortcut
     * @param friendlyName name for the shortcut to be presented to the user
     * @param shortcutString string representation of the shortcut, such as "CTRL+S"
     * @param defaultShortcutString string representation of the default shortcut,
     *                   such as "CTRL+S", when the user choses to reset to default
     *                   the keyboard shortcut will return to this one.
     */
    GlobalShortcut* registerShortcut(QString const& uniqueName,
                                     QString const& friendlyName,
                                     QString const& shortcutString,
                                     QString const& defaultShortcutString);

public Q_SLOTS:
    // For dbus Q_SCRIPTABLE has to be on slots. Scriptable methods are not
    // exported.

    /**
     * Remove all currently not used global shortcuts registrations for this
     * component and if nothing is left the component too.
     *
     * If the method returns true consider all information previously acquired
     * from this component as void.
     *
     * The method will cleanup in all contexts.
     *
     * @return @c true if a change was made, @c false if not.
     */
    Q_SCRIPTABLE virtual bool cleanUp();

    /// A component is active if at least one of it's global shortcuts is currently present.
    Q_SCRIPTABLE bool isActive() const;

    /// Get all shortcutnames living in @a context
    Q_SCRIPTABLE QStringList shortcutNames(QString const& context
                                           = QStringLiteral("default")) const;

    /// Returns all shortcut in @a context
    Q_SCRIPTABLE QList<KGlobalShortcutInfo> allShortcutInfos(QString const& context
                                                             = QStringLiteral("default")) const;

    /// Returns the shortcut contexts available for the component.
    Q_SCRIPTABLE QStringList getShortcutContexts() const;

    virtual void emitGlobalShortcutPressed(const GlobalShortcut& shortcut);
    virtual void emitGlobalShortcutReleased(const GlobalShortcut& shortcut);

    Q_SCRIPTABLE void invokeShortcut(QString const& shortcutName,
                                     QString const& context = QStringLiteral("default"));

Q_SIGNALS:
    Q_SCRIPTABLE void globalShortcutPressed(QString const& componentUnique,
                                            QString const& shortcutUnique,
                                            qlonglong timestamp);
    Q_SCRIPTABLE void globalShortcutReleased(QString const& componentUnique,
                                             QString const& shortcutUnique,
                                             qlonglong timestamp);

private:
    QString _uniqueName;

    // the name as it would be found in a magazine article about the application,
    // possibly localized if a localized name exists.
    QString _friendlyName;

    GlobalShortcutsRegistry* _registry;

    GlobalShortcutContext* _current;
    QHash<QString, GlobalShortcutContext*> _contexts;
};
