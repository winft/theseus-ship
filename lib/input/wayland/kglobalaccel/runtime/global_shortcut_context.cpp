/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "global_shortcut_context.h"

#include "global_shortcut.h"
#include "sequence_helpers.h"

#include <KGlobalAccel>

GlobalShortcutContext::GlobalShortcutContext(QString const& uniqueName,
                                             QString const& friendlyName,
                                             Component* component)

    : _uniqueName(uniqueName)
    , _friendlyName(friendlyName)
    , _component(component)
{
}

GlobalShortcutContext::~GlobalShortcutContext()
{
    qDeleteAll(_actionsMap);
    _actionsMap.clear();
}

void GlobalShortcutContext::addShortcut(GlobalShortcut* shortcut)
{
    _actionsMap.insert(shortcut->uniqueName(), shortcut);
}

QList<KGlobalShortcutInfo> GlobalShortcutContext::allShortcutInfos() const
{
    QList<KGlobalShortcutInfo> rc;
    for (auto shortcut : std::as_const(_actionsMap)) {
        rc.append(static_cast<KGlobalShortcutInfo>(*shortcut));
    }
    return rc;
}

Component const* GlobalShortcutContext::component() const
{
    return _component;
}

Component* GlobalShortcutContext::component()
{
    return _component;
}

QString GlobalShortcutContext::friendlyName() const
{
    return _friendlyName;
}

GlobalShortcut* GlobalShortcutContext::getShortcutByKey(QKeySequence const& key,
                                                        KGlobalAccel::MatchType type) const
{
    if (key.isEmpty()) {
        return nullptr;
    }
    QKeySequence keyMangled = Utils::mangleKey(key);
    for (auto sc : std::as_const(_actionsMap)) {
        const auto keys = sc->keys();
        for (QKeySequence const& other : keys) {
            QKeySequence otherMangled = Utils::mangleKey(other);
            switch (type) {
            case KGlobalAccel::MatchType::Equal:
                if (otherMangled == keyMangled) {
                    return sc;
                }
                break;
            case KGlobalAccel::MatchType::Shadows:
                if (!other.isEmpty() && Utils::contains(keyMangled, otherMangled)) {
                    return sc;
                }
                break;
            case KGlobalAccel::MatchType::Shadowed:
                if (!other.isEmpty() && Utils::contains(otherMangled, keyMangled)) {
                    return sc;
                }
                break;
            }
        }
    }
    return nullptr;
}

GlobalShortcut* GlobalShortcutContext::takeShortcut(GlobalShortcut* shortcut)
{
    // Try to take the shortcut. Result could be nullptr if the shortcut doesn't
    // belong to this component.
    return _actionsMap.take(shortcut->uniqueName());
}

QString GlobalShortcutContext::uniqueName() const
{
    return _uniqueName;
}

bool GlobalShortcutContext::isShortcutAvailable(QKeySequence const& key) const
{
    for (auto it = _actionsMap.cbegin(), endIt = _actionsMap.cend(); it != endIt; ++it) {
        auto const sc = it.value();
        if (Utils::matchSequences(key, sc->keys())) {
            return false;
        }
    }
    return true;
}
