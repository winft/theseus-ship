/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <KGlobalAccel>
#include <KGlobalShortcutInfo>
#include <QList>
#include <QStringList>
#include <QtDBus>

struct KGlobalAccelDPrivate;

class KGlobalAccelD : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KGlobalAccel")

public:
    enum SetShortcutFlag {
        SetPresent = 2,
        NoAutoloading = 4,
        IsDefault = 8,
    };
    Q_ENUM(SetShortcutFlag)
    Q_DECLARE_FLAGS(SetShortcutFlags, SetShortcutFlag)
    Q_FLAG(SetShortcutFlags)

    explicit KGlobalAccelD(QObject* parent = nullptr);
    ~KGlobalAccelD() override;

    bool init();
    bool keyPressed(int keyQt);
    bool keyReleased(int keyQt);

public Q_SLOTS:
    Q_SCRIPTABLE QList<QDBusObjectPath> allComponents() const;
    Q_SCRIPTABLE QList<QStringList> allMainComponents() const;

    Q_SCRIPTABLE QList<QStringList> allActionsForComponent(QStringList const& actionId) const;
    Q_SCRIPTABLE QStringList actionList(QKeySequence const& key) const;

    Q_SCRIPTABLE QList<QKeySequence> shortcutKeys(QStringList const& actionId) const;
    Q_SCRIPTABLE QList<QKeySequence> defaultShortcutKeys(QStringList const& actionId) const;
    Q_SCRIPTABLE QDBusObjectPath getComponent(QString const& componentUnique) const;

    // to be called by main components owning the action
    Q_SCRIPTABLE QList<QKeySequence>
    setShortcutKeys(QStringList const& actionId, const QList<QKeySequence>& keys, uint flags);

    // this is used if application A wants to change shortcuts of application B
    Q_SCRIPTABLE void setForeignShortcutKeys(QStringList const& actionId,
                                             const QList<QKeySequence>& keys);

    // to be called when a KAction is destroyed. The shortcut stays in the data structures for
    // conflict resolution but won't trigger.
    Q_SCRIPTABLE void setInactive(QStringList const& actionId);
    Q_SCRIPTABLE void doRegister(QStringList const& actionId);

    Q_SCRIPTABLE void activateGlobalShortcutContext(QString const& component,
                                                    QString const& context);

    Q_SCRIPTABLE QList<KGlobalShortcutInfo>
    globalShortcutsByKey(QKeySequence const& key, KGlobalAccel::MatchType type) const;

    Q_SCRIPTABLE bool globalShortcutAvailable(QKeySequence const& key,
                                              QString const& component) const;
    Q_SCRIPTABLE bool unregister(QString const& componentUnique, QString const& shortcutUnique);
    Q_SCRIPTABLE void blockGlobalShortcuts(bool);

Q_SIGNALS:
    Q_SCRIPTABLE void yourShortcutsChanged(QStringList const& actionId,
                                           const QList<QKeySequence>& newKeys);

private:
    void scheduleWriteSettings() const;

    KGlobalAccelDPrivate* const d;
};
