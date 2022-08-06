/*
    SPDX-FileCopyrightText: 2016 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2017 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "helpers.h"
#include "layout_policies.h"

#include "input/dbus/keyboard_layout.h"
#include "input/dbus/keyboard_layouts_v2.h"
#include "kwin_export.h"

#include <KConfigGroup>
#include <KGlobalAccel>
#include <KLocalizedString>
#include <KSharedConfig>
#include <QAction>
#include <QObject>
#include <QString>
#include <QVector>
#include <memory>
#include <string>

namespace KWin::input::xkb
{

inline QString translated_keyboard_layout(std::string const& layout)
{
    return i18nd("xkeyboard-config", layout.c_str());
}

class KWIN_EXPORT layout_manager_qobject : public QObject
{
    Q_OBJECT
public:
    layout_manager_qobject(std::function<void()> reconfigure_callback);

Q_SIGNALS:
    void layoutChanged(uint index);
    void layoutsReconfigured();

private Q_SLOTS:
    void reconfigure();

private:
    std::function<void()> reconfigure_callback;
};

template<typename Xkb>
class layout_manager
{
public:
    layout_manager(Xkb& xkb, KSharedConfigPtr const& config)
        : qobject{std::make_unique<layout_manager_qobject>([this] { reconfigure(); })}
        , xkb{xkb}
        , m_configGroup(config->group("Layout"))
    {
        auto switchKeyboardAction = new QAction(qobject.get());
        switchKeyboardAction->setObjectName(QStringLiteral("Switch to Next Keyboard Layout"));
        switchKeyboardAction->setProperty("componentName",
                                          QStringLiteral("KDE Keyboard Layout Switcher"));
        QKeySequence sequence{Qt::META | Qt::ALT | Qt::Key_K};
        KGlobalAccel::self()->setDefaultShortcut(switchKeyboardAction,
                                                 QList<QKeySequence>({sequence}));
        KGlobalAccel::self()->setShortcut(switchKeyboardAction, QList<QKeySequence>({sequence}));
        xkb.platform->setup_action_for_global_accel(switchKeyboardAction);
        QObject::connect(switchKeyboardAction, &QAction::triggered, qobject.get(), [this] {
            switchToNextLayout();
        });

        reconfigure();

        for (auto keyboard : xkb.platform->keyboards) {
            add_keyboard(keyboard);
        }

        QObject::connect(xkb.platform->qobject.get(),
                         &platform_qobject::keyboard_added,
                         qobject.get(),
                         [this](auto keys) { add_keyboard(keys); });

        init_dbus_interface_v2();
    }

    void switchToNextLayout()
    {
        get_keyboard()->switch_to_next_layout();
    }

    void switchToPreviousLayout()
    {
        get_keyboard()->switch_to_previous_layout();
    }

    std::unique_ptr<layout_manager_qobject> qobject;
    Xkb& xkb;

private:
    void reconfigure()
    {
        if (m_configGroup.isValid()) {
            m_configGroup.config()->reparseConfiguration();
            const QString policyKey
                = m_configGroup.readEntry("SwitchMode", QStringLiteral("Global"));
            xkb.reconfigure();
            if (!m_policy || m_policy->name() != policyKey) {
                m_policy = create_layout_policy(this, m_configGroup, policyKey);
            }
        } else {
            xkb.reconfigure();
        }

        auto xkb = get_keyboard();

        load_shortcuts(xkb);

        initDBusInterface();
        Q_EMIT qobject->layoutsReconfigured();
    }

    void initDBusInterface()
    {
        auto xkb = get_keyboard();

        if (xkb->layouts_count() <= 1) {
            if (dbus_interface_v1) {
                // Emit change before reset for backwards compatibility (was done so in the past).
                Q_EMIT dbus_interface_v1->layoutListChanged();
                dbus_interface_v1.reset();
            }
            return;
        }

        if (dbus_interface_v1) {
            return;
        }

        dbus_interface_v1 = std::make_unique<dbus::keyboard_layout>(
            m_configGroup, [this] { return get_primary_xkb_keyboard(*this->xkb.platform); });

        QObject::connect(qobject.get(),
                         &layout_manager_qobject::layoutChanged,
                         dbus_interface_v1.get(),
                         &dbus::keyboard_layout::layoutChanged);
        // TODO: the signal might be emitted even if the list didn't change
        QObject::connect(qobject.get(),
                         &layout_manager_qobject::layoutsReconfigured,
                         dbus_interface_v1.get(),
                         &dbus::keyboard_layout::layoutListChanged);
    }

    void init_dbus_interface_v2()
    {
        assert(!dbus_interface_v2);
        dbus_interface_v2 = std::make_unique<dbus::keyboard_layouts_v2>(xkb.platform);
    }

    void add_keyboard(input::keyboard* keyboard)
    {
        if (!keyboard->control || !keyboard->control->is_alpha_numeric_keyboard()) {
            return;
        }

        auto xkb = keyboard->xkb.get();
        QObject::connect(xkb->qobject.get(),
                         &xkb::keyboard_qobject::layout_changed,
                         qobject.get(),
                         [this, xkb] { handle_layout_change(xkb); });
    }

    void handle_layout_change(xkb::keyboard* xkb)
    {
        if (xkb != get_keyboard()) {
            // We currently only inform about changes on the primary device.
            return;
        }
        send_layout_to_osd(xkb);
        Q_EMIT qobject->layoutChanged(xkb->layout);
    }

    void send_layout_to_osd(xkb::keyboard* xkb)
    {
        QDBusMessage msg = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"),
                                                          QStringLiteral("/org/kde/osdService"),
                                                          QStringLiteral("org.kde.osdService"),
                                                          QStringLiteral("kbdLayoutChanged"));
        msg << translated_keyboard_layout(xkb->layout_name());
        QDBusConnection::sessionBus().asyncCall(msg);
    }

    void switchToLayout(xkb_layout_index_t index)
    {
        get_keyboard()->switch_to_layout(index);
    }

    void load_shortcuts(xkb::keyboard* xkb)
    {
        qDeleteAll(m_layoutShortcuts);
        m_layoutShortcuts.clear();

        const QString componentName = QStringLiteral("KDE Keyboard Layout Switcher");
        auto const count = xkb->layouts_count();

        for (uint i = 0; i < count; ++i) {
            // layout name is translated in the action name in keyboard kcm!
            const QString action
                = QStringLiteral("Switch keyboard layout to %1")
                      .arg(translated_keyboard_layout(xkb->layout_name_from_index(i)));
            const auto shortcuts = KGlobalAccel::self()->globalShortcut(componentName, action);
            if (shortcuts.isEmpty()) {
                continue;
            }
            auto a = new QAction(qobject.get());
            a->setObjectName(action);
            a->setProperty("componentName", componentName);
            QObject::connect(
                a, &QAction::triggered, qobject.get(), [this, i] { switchToLayout(i); });
            KGlobalAccel::self()->setShortcut(a, shortcuts, KGlobalAccel::Autoloading);
            m_layoutShortcuts << a;
        }
    }

    auto get_keyboard()
    {
        return get_primary_xkb_keyboard(*xkb.platform);
    }

    KConfigGroup m_configGroup;
    QVector<QAction*> m_layoutShortcuts;
    std::unique_ptr<dbus::keyboard_layout> dbus_interface_v1;
    std::unique_ptr<dbus::keyboard_layouts_v2> dbus_interface_v2;
    std::unique_ptr<xkb::layout_policy<layout_manager>> m_policy;
};

}
