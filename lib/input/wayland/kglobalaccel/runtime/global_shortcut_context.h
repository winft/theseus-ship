/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KGlobalAccel>
#include <KGlobalShortcutInfo>
#include <QHash>
#include <QString>

class Component;
class GlobalShortcut;

class GlobalShortcutContext
{
public:
    /**
     * Default constructor
     */
    GlobalShortcutContext(QString const& uniqueName,
                          QString const& friendlyName,
                          Component* component);

    /**
     * Destructor
     */
    virtual ~GlobalShortcutContext();

    //! Adds @p shortcut to the context
    void addShortcut(GlobalShortcut* shortcut);

    //! Return KGlobalShortcutInfos for all shortcuts
    QList<KGlobalShortcutInfo> allShortcutInfos() const;

    /**
     * Get the name for the context
     */
    QString uniqueName() const;
    QString friendlyName() const;

    Component* component();
    Component const* component() const;

    //! Get shortcut for @p key or nullptr
    GlobalShortcut* getShortcutByKey(QKeySequence const& key, KGlobalAccel::MatchType type) const;

    //! Remove @p shortcut from the context. The shortcut is not deleted.
    GlobalShortcut* takeShortcut(GlobalShortcut* shortcut);

    // Returns true if key is not used by any global shortcuts in this context,
    // otherwise returns false
    bool isShortcutAvailable(QKeySequence const& key) const;

private:
    friend class Component;

    //! The unique name for this context
    QString _uniqueName;

    //! The unique name for this context
    QString _friendlyName;

    //! The component the context belongs to
    Component* _component = nullptr;

    //! The actions associated with this context
    QHash<QString, GlobalShortcut*> _actionsMap;
};
