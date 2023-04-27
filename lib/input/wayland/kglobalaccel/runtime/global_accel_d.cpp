/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "global_accel_d.h"

#include "component.h"
#include "global_shortcut.h"
#include "global_shortcut_context.h"
#include "global_shortcuts_registry.h"
#include "service_action_component.h"

#include "input/logging.h"

#include <KGlobalAccel>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QMetaMethod>
#include <QTimer>

struct KGlobalAccelDPrivate {
    KGlobalAccelDPrivate(KGlobalAccelD* qq)
        : q(qq)
    {
    }

    GlobalShortcut* findAction(QStringList const& actionId) const;

    /**
     * Find the action @a shortcutUnique in @a componentUnique.
     *
     * @return the action or @c nullptr if doesn't exist
     */
    GlobalShortcut* findAction(QString const& componentUnique, QString const& shortcutUnique) const;

    GlobalShortcut* addAction(QStringList const& actionId);
    Component* component(QStringList const& actionId) const;

    void splitComponent(QString& component, QString& context) const
    {
        context = QStringLiteral("default");
        const int index = component.indexOf(QLatin1Char('|'));
        if (index != -1) {
            Q_ASSERT(component.indexOf(QLatin1Char('|'), index + 1)
                     == -1); // Only one '|' character
            context = component.mid(index + 1);
            component.truncate(index);
        }
    }

    //! Timer for delayed writing to kglobalshortcutsrc
    QTimer writeoutTimer;

    //! Our holder
    KGlobalAccelD* q;

    std::unique_ptr<GlobalShortcutsRegistry> m_registry;
};

GlobalShortcut* KGlobalAccelDPrivate::findAction(QStringList const& actionId) const
{
    // Check if actionId is valid
    if (actionId.size() != 4) {
        qCDebug(KWIN_INPUT) << "Invalid! '" << actionId << "'";
        return nullptr;
    }

    return findAction(actionId.at(KGlobalAccel::ComponentUnique),
                      actionId.at(KGlobalAccel::ActionUnique));
}

GlobalShortcut* KGlobalAccelDPrivate::findAction(QString const& _componentUnique,
                                                 QString const& shortcutUnique) const
{
    auto componentUnique = _componentUnique;

    Component* component;
    QString contextUnique;
    if (componentUnique.indexOf(QLatin1Char('|')) == -1) {
        component = m_registry->getComponent(componentUnique);
        if (component) {
            contextUnique = component->currentContext()->uniqueName();
        }
    } else {
        splitComponent(componentUnique, contextUnique);
        component = m_registry->getComponent(componentUnique);
    }

    if (!component) {
        qCDebug(KWIN_INPUT) << componentUnique << "not found";
        return nullptr;
    }

    auto shortcut = component->getShortcutByName(shortcutUnique, contextUnique);

    if (shortcut) {
        qCDebug(KWIN_INPUT) << componentUnique << contextUnique << shortcut->uniqueName();
    } else {
        qCDebug(KWIN_INPUT) << "No match for" << shortcutUnique;
    }
    return shortcut;
}

Component* KGlobalAccelDPrivate::component(QStringList const& actionId) const
{
    auto const uniqueName = actionId.at(KGlobalAccel::ComponentUnique);

    // If a component for action already exists, use that...
    if (auto c = m_registry->getComponent(uniqueName)) {
        return c;
    }

    // ... otherwise, create a new one
    auto const friendlyName = actionId.at(KGlobalAccel::ComponentFriendly);
    if (uniqueName.endsWith(QLatin1String(".desktop"))) {
        auto actionComp = m_registry->createServiceActionComponent(uniqueName, friendlyName);
        Q_ASSERT(actionComp);
        actionComp->activateGlobalShortcutContext(QStringLiteral("default"));
        actionComp->loadFromService();
        return actionComp;
    } else {
        auto comp = m_registry->createComponent(uniqueName, friendlyName);
        Q_ASSERT(comp);
        return comp;
    }
}

GlobalShortcut* KGlobalAccelDPrivate::addAction(QStringList const& actionId)
{
    Q_ASSERT(actionId.size() >= 4);

    auto componentUnique = actionId.at(KGlobalAccel::ComponentUnique);
    QString contextUnique;
    splitComponent(componentUnique, contextUnique);

    auto actionIdTmp = actionId;
    actionIdTmp.replace(KGlobalAccel::ComponentUnique, componentUnique);

    // Create the component if necessary
    auto component = this->component(actionIdTmp);
    Q_ASSERT(component);

    // Create the context if necessary
    if (component->getShortcutContexts().count(contextUnique) == 0) {
        component->createGlobalShortcutContext(contextUnique);
    }

    Q_ASSERT(!component->getShortcutByName(componentUnique, contextUnique));

    return new GlobalShortcut(*m_registry,
                              actionId.at(KGlobalAccel::ActionUnique),
                              actionId.at(KGlobalAccel::ActionFriendly),
                              component->shortcutContext(contextUnique));
}

Q_DECLARE_METATYPE(QStringList)

KGlobalAccelD::KGlobalAccelD()
    : d(new KGlobalAccelDPrivate(this))
{
    qDBusRegisterMetaType<QKeySequence>();
    qDBusRegisterMetaType<QList<QKeySequence>>();
    qDBusRegisterMetaType<QList<QDBusObjectPath>>();
    qDBusRegisterMetaType<QList<QStringList>>();
    qDBusRegisterMetaType<QStringList>();
    qDBusRegisterMetaType<KGlobalShortcutInfo>();
    qDBusRegisterMetaType<QList<KGlobalShortcutInfo>>();
    qDBusRegisterMetaType<KGlobalAccel::MatchType>();

    d->m_registry = std::make_unique<GlobalShortcutsRegistry>();

    d->writeoutTimer.setSingleShot(true);
    connect(&d->writeoutTimer,
            &QTimer::timeout,
            d->m_registry.get(),
            &GlobalShortcutsRegistry::writeSettings);

    if (!QDBusConnection::sessionBus().registerService(QLatin1String("org.kde.kglobalaccel"))) {
        throw std::runtime_error("Failed to register service org.kde.kglobalaccel");
    }

    if (!QDBusConnection::sessionBus().registerObject(
            QStringLiteral("/kglobalaccel"), this, QDBusConnection::ExportScriptableContents)) {
        throw std::runtime_error("Failed to register object kglobalaccel in org.kde.kglobalaccel");
    }

    d->m_registry->loadSettings();
}

KGlobalAccelD::~KGlobalAccelD()
{
    if (d->writeoutTimer.isActive()) {
        d->writeoutTimer.stop();
        d->m_registry->writeSettings();
    }
    d->m_registry->deactivateShortcuts();
    delete d;
}

bool KGlobalAccelD::keyPressed(int keyQt)
{
    return d->m_registry->keyPressed(keyQt);
}

bool KGlobalAccelD::keyReleased(int keyQt)
{
    return d->m_registry->keyReleased(keyQt);
}

QList<QStringList> KGlobalAccelD::allMainComponents() const
{
    return d->m_registry->allComponentNames();
}

QList<QStringList> KGlobalAccelD::allActionsForComponent(QStringList const& actionId) const
{
    // ### Would it be advantageous to sort the actions by unique name?
    QList<QStringList> ret;

    auto const component = d->m_registry->getComponent(actionId[KGlobalAccel::ComponentUnique]);
    if (!component) {
        return ret;
    }

    // ComponentUnique + ActionUnique
    QStringList partialId(actionId[KGlobalAccel::ComponentUnique]);
    partialId.append(QString());

    // Use our internal friendlyName, not the one passed in. We should have the latest data.
    // ComponentFriendly + ActionFriendly
    partialId.append(component->friendlyName());
    partialId.append(QString());

    auto const listShortcuts = component->allShortcuts();
    for (auto const shortcut : listShortcuts) {
        if (shortcut->isFresh()) {
            // isFresh is only an intermediate state, not to be reported outside.
            continue;
        }
        QStringList actionId(partialId);
        actionId[KGlobalAccel::ActionUnique] = shortcut->uniqueName();
        actionId[KGlobalAccel::ActionFriendly] = shortcut->friendlyName();
        ret.append(actionId);
    }
    return ret;
}

QStringList KGlobalAccelD::actionList(QKeySequence const& key) const
{
    auto shortcut = d->m_registry->getShortcutByKey(key);
    QStringList ret;
    if (shortcut) {
        ret.append(shortcut->context()->component()->uniqueName());
        ret.append(shortcut->uniqueName());
        ret.append(shortcut->context()->component()->friendlyName());
        ret.append(shortcut->friendlyName());
    }
    return ret;
}

void KGlobalAccelD::activateGlobalShortcutContext(QString const& component,
                                                  QString const& uniqueName)
{
    auto const comp = d->m_registry->getComponent(component);
    if (comp) {
        comp->activateGlobalShortcutContext(uniqueName);
    }
}

QList<QDBusObjectPath> KGlobalAccelD::allComponents() const
{
    return d->m_registry->componentsDbusPaths();
}

void KGlobalAccelD::blockGlobalShortcuts(bool block)
{
    qCDebug(KWIN_INPUT) << "Block global shortcuts?" << block;
    if (block) {
        d->m_registry->deactivateShortcuts(true);
    } else {
        d->m_registry->activateShortcuts();
    }
}

QList<QKeySequence> KGlobalAccelD::shortcutKeys(QStringList const& action) const
{
    auto shortcut = d->findAction(action);
    if (shortcut) {
        return shortcut->keys();
    }
    return QList<QKeySequence>();
}

QList<QKeySequence> KGlobalAccelD::defaultShortcutKeys(QStringList const& action) const
{
    auto shortcut = d->findAction(action);
    if (shortcut) {
        return shortcut->defaultKeys();
    }
    return QList<QKeySequence>();
}

// This method just registers the action. Nothing else. Shortcut has to be set
// later.
void KGlobalAccelD::doRegister(QStringList const& actionId)
{
    qCDebug(KWIN_INPUT) << actionId;

    // Check because we would not want to add a action for an invalid
    // actionId. findAction returns nullptr in that case.
    if (actionId.size() < 4) {
        return;
    }

    auto shortcut = d->findAction(actionId);
    if (!shortcut) {
        shortcut = d->addAction(actionId);
    } else {
        // a switch of locales is one common reason for a changing friendlyName
        if ((!actionId[KGlobalAccel::ActionFriendly].isEmpty())
            && shortcut->friendlyName() != actionId[KGlobalAccel::ActionFriendly]) {
            shortcut->setFriendlyName(actionId[KGlobalAccel::ActionFriendly]);
            scheduleWriteSettings();
        }
        if ((!actionId[KGlobalAccel::ComponentFriendly].isEmpty())
            && shortcut->context()->component()->friendlyName()
                != actionId[KGlobalAccel::ComponentFriendly]) {
            shortcut->context()->component()->setFriendlyName(
                actionId[KGlobalAccel::ComponentFriendly]);
            scheduleWriteSettings();
        }
    }
}

QDBusObjectPath KGlobalAccelD::getComponent(QString const& componentUnique) const
{
    qCDebug(KWIN_INPUT) << componentUnique;

    auto component = d->m_registry->getComponent(componentUnique);

    if (component) {
        return component->dbusPath();
    } else {
        sendErrorReply(QStringLiteral("org.kde.kglobalaccel.NoSuchComponent"),
                       QStringLiteral("The component '%1' doesn't exist.").arg(componentUnique));
        return QDBusObjectPath("/");
    }
}

QList<KGlobalShortcutInfo> KGlobalAccelD::globalShortcutsByKey(QKeySequence const& key,
                                                               KGlobalAccel::MatchType type) const
{
    qCDebug(KWIN_INPUT) << key;
    auto const shortcuts = d->m_registry->getShortcutsByKey(key, type);

    QList<KGlobalShortcutInfo> rc;
    rc.reserve(shortcuts.size());
    for (auto const sc : shortcuts) {
        qCDebug(KWIN_INPUT) << sc->context()->uniqueName() << sc->uniqueName();
        rc.append(static_cast<KGlobalShortcutInfo>(*sc));
    }

    return rc;
}

bool KGlobalAccelD::globalShortcutAvailable(QKeySequence const& shortcut,
                                            QString const& component) const
{
    auto realComponent = component;
    QString context;
    d->splitComponent(realComponent, context);
    return d->m_registry->isShortcutAvailable(shortcut, realComponent, context);
}

void KGlobalAccelD::setInactive(QStringList const& actionId)
{
    qCDebug(KWIN_INPUT) << actionId;

    auto shortcut = d->findAction(actionId);
    if (shortcut) {
        shortcut->setIsPresent(false);
    }
}

bool KGlobalAccelD::unregister(QString const& componentUnique, QString const& shortcutUnique)
{
    qCDebug(KWIN_INPUT) << componentUnique << shortcutUnique;

    // Stop grabbing the key
    auto shortcut = d->findAction(componentUnique, shortcutUnique);
    if (shortcut) {
        shortcut->unRegister();
        scheduleWriteSettings();
    }

    return shortcut;
}

QList<QKeySequence> KGlobalAccelD::setShortcutKeys(QStringList const& actionId,
                                                   const QList<QKeySequence>& keys,
                                                   uint flags)
{
    // spare the DBus framework some work
    const bool setPresent = (flags & SetPresent);
    const bool isAutoloading = !(flags & NoAutoloading);
    const bool isDefault = (flags & IsDefault);

    auto shortcut = d->findAction(actionId);
    if (!shortcut) {
        return QList<QKeySequence>();
    }

    // default shortcuts cannot clash because they don't do anything
    if (isDefault) {
        if (shortcut->defaultKeys() != keys) {
            shortcut->setDefaultKeys(keys);
            scheduleWriteSettings();
        }
        return keys; // doesn't matter
    }

    if (isAutoloading && !shortcut->isFresh()) {
        // the trivial and common case - synchronize the action from our data
        // and exit.
        if (!shortcut->isPresent() && setPresent) {
            shortcut->setIsPresent(true);
        }
        // We are finished here. Return the list of current active keys.
        return shortcut->keys();
    }

    // now we are actually changing the shortcut of the action
    shortcut->setKeys(keys);

    if (setPresent) {
        shortcut->setIsPresent(true);
    }

    // maybe isFresh should really only be set if setPresent, but only two things should use
    // !setPresent:
    //- the global shortcuts KCM: very unlikely to catch KWin/etc.'s actions in isFresh state
    //- KGlobalAccel::stealGlobalShortcutSystemwide(): only applies to actions with shortcuts
    //  which can never be fresh if created the usual way
    shortcut->setIsFresh(false);

    scheduleWriteSettings();

    return shortcut->keys();
}

void KGlobalAccelD::setForeignShortcutKeys(QStringList const& actionId,
                                           const QList<QKeySequence>& keys)
{
    qCDebug(KWIN_INPUT) << actionId;

    auto shortcut = d->findAction(actionId);
    if (!shortcut) {
        return;
    }

    QList<QKeySequence> newKeys = setShortcutKeys(actionId, keys, NoAutoloading);

    Q_EMIT yourShortcutsChanged(actionId, newKeys);
}

void KGlobalAccelD::scheduleWriteSettings() const
{
    if (!d->writeoutTimer.isActive()) {
        d->writeoutTimer.start(500);
    }
}

#include "moc_global_accel_d.cpp"
