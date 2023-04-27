/*
    SPDX-FileCopyrightText: 2023 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "component.h"

#include <KDesktopFile>
#include <memory>

class KServiceActionComponent : public Component
{
    Q_OBJECT
public:
    ~KServiceActionComponent() override;

    void loadFromService();
    void emitGlobalShortcutPressed(const GlobalShortcut& shortcut) override;

    bool cleanUp() override;

private:
    friend class ::GlobalShortcutsRegistry;
    //! Constructs a KServiceActionComponent. This is a private constuctor, to create
    //! a KServiceActionComponent, use
    //! GlobalShortcutsRegistry::self()->createServiceActionComponent().
    KServiceActionComponent(GlobalShortcutsRegistry& registry,
                            QString const& serviceStorageId,
                            QString const& friendlyName);

    void runProcess(const KConfigGroup& group, QString const& token);

    QString m_serviceStorageId;
    std::unique_ptr<KDesktopFile> m_desktopFile;
    bool m_isInApplicationsDir = false;
};
