/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "debug/support_info.h"
#include "input/platform.h"
#include "kwin_export.h"
#include "win/kill_window.h"
#include "win/placement.h"

#include <QObject>
#include <QtDBus>

namespace KWin
{

namespace win
{
class space_qobject;
}

namespace base::dbus
{

/**
 * @brief This class is a wrapper for the org.kde.KWin D-Bus interface.
 *
 * The main purpose of this class is to be exported on the D-Bus as object /KWin.
 * It is a pure wrapper to provide the deprecated D-Bus methods which have been
 * removed from Workspace which used to implement the complete D-Bus interface.
 *
 * Nowadays the D-Bus interfaces are distributed, parts of it are exported on
 * /Compositor, parts on /Effects and parts on /KWin. The implementation in this
 * class just delegates the method calls to the actual implementation in one of the
 * three singletons.
 *
 * @author Martin Gräßlin <mgraesslin@kde.org>
 */
class KWIN_EXPORT kwin : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.KWin")

public:
    explicit kwin(win::space_qobject& space);
    ~kwin() override;

public Q_SLOTS:
    int currentDesktop()
    {
        return current_desktop_impl();
    }
    Q_NOREPLY void killWindow()
    {
        kill_window_impl();
    }
    void nextDesktop()
    {
        next_desktop_impl();
    }
    void previousDesktop()
    {
        previous_desktop_impl();
    }

    Q_NOREPLY void reconfigure();

    bool setCurrentDesktop(int desktop)
    {
        return set_current_desktop_impl(desktop);
    }

    bool startActivity(const QString& in0);
    bool stopActivity(const QString& in0);

    QString supportInformation()
    {
        return support_information_impl();
    }
    QString activeOutputName()
    {
        return active_output_name_impl();
    }
    Q_NOREPLY void unclutterDesktop()
    {
        unclutter_desktop_impl();
    }

    Q_NOREPLY void showDebugConsole()
    {
        show_debug_console_impl();
    }

    void enableFtrace(bool enable);

    QVariantMap queryWindowInfo()
    {
        return query_window_info_impl();
    }
    QVariantMap getWindowInfo(QString const& uuid)
    {
        return get_window_info_impl(uuid);
    }

protected:
    virtual int current_desktop_impl() = 0;
    virtual void kill_window_impl() = 0;

    virtual void next_desktop_impl() = 0;
    virtual void previous_desktop_impl() = 0;

    virtual bool set_current_desktop_impl(int desktop) = 0;

    virtual QString support_information_impl() = 0;
    virtual QString active_output_name_impl() = 0;
    virtual void unclutter_desktop_impl() = 0;

    virtual void show_debug_console_impl() = 0;
    virtual QVariantMap query_window_info_impl() = 0;
    virtual QVariantMap get_window_info_impl(QString const& uuid) = 0;

private:
    QString m_serviceName;
    win::space_qobject& space;
};

template<typename Space, typename Input>
class kwin_impl : public kwin
{
public:
    kwin_impl(Space& space, Input* input)
        : kwin(*space.qobject)
        , space{space}
        , input{input}
    {
    }

    void kill_window_impl() override
    {
        win::start_window_killer(space);
    }

    void unclutter_desktop_impl() override
    {
        win::unclutter_desktop(space);
    }

    QString support_information_impl() override
    {
        return debug::get_support_info(space);
    }

    QString active_output_name_impl() override
    {
        auto output = win::get_current_output(space);
        if (!output) {
            return {};
        }
        return output->name();
    }

    int current_desktop_impl() override
    {
        return space.virtual_desktop_manager->current();
    }

    bool set_current_desktop_impl(int desktop) override
    {
        return space.virtual_desktop_manager->setCurrent(desktop);
    }

    void next_desktop_impl() override
    {
        space.virtual_desktop_manager->template moveTo<win::virtual_desktop_next>();
    }

    void previous_desktop_impl() override
    {
        space.virtual_desktop_manager->template moveTo<win::virtual_desktop_previous>();
    }

    void show_debug_console_impl() override
    {
        space.show_debug_console();
    }

    QVariantMap query_window_info_impl() override
    {
        if (!input) {
            return {};
        }

        m_replyQueryWindowInfo = message();
        setDelayedReply(true);

        input->start_interactive_window_selection([this](auto t) {
            if (!t) {
                QDBusConnection::sessionBus().send(m_replyQueryWindowInfo.createErrorReply(
                    QStringLiteral("org.kde.KWin.Error.UserCancel"),
                    QStringLiteral("User cancelled the query")));
                return;
            }
            if (!t->control) {
                QDBusConnection::sessionBus().send(m_replyQueryWindowInfo.createErrorReply(
                    QStringLiteral("org.kde.KWin.Error.InvalidWindow"),
                    QStringLiteral("Tried to query information about an unmanaged window")));
                return;
            }
            QDBusConnection::sessionBus().send(
                m_replyQueryWindowInfo.createReply(window_to_variant_map(t)));
        });

        return QVariantMap{};
    }

    QVariantMap get_window_info_impl(QString const& uuid) override
    {
        auto const id = QUuid::fromString(uuid);

        for (auto win : space.windows) {
            if (!win->control) {
                continue;
            }
            if (win->internal_id == id) {
                return window_to_variant_map(win);
            }
        }
        return {};
    }

private:
    QVariantMap window_to_variant_map(typename Space::window_t const* c)
    {
        return {{QStringLiteral("resourceClass"), c->resource_class},
                {QStringLiteral("resourceName"), c->resource_name},
                {QStringLiteral("desktopFile"), c->control->desktop_file_name},
                {QStringLiteral("role"), c->windowRole()},
                {QStringLiteral("caption"), c->caption.normal},
                {QStringLiteral("clientMachine"), c->wmClientMachine(true)},
                {QStringLiteral("localhost"), c->isLocalhost()},
                {QStringLiteral("type"), c->windowType()},
                {QStringLiteral("x"), c->pos().x()},
                {QStringLiteral("y"), c->pos().y()},
                {QStringLiteral("width"), c->size().width()},
                {QStringLiteral("height"), c->size().height()},
                {QStringLiteral("x11DesktopNumber"), c->desktop()},
                {QStringLiteral("minimized"), c->control->minimized},
                {QStringLiteral("shaded"), false},
                {QStringLiteral("fullscreen"), c->control->fullscreen},
                {QStringLiteral("keepAbove"), c->control->keep_above},
                {QStringLiteral("keepBelow"), c->control->keep_below},
                {QStringLiteral("noBorder"), c->noBorder()},
                {QStringLiteral("skipTaskbar"), c->control->skip_taskbar()},
                {QStringLiteral("skipPager"), c->control->skip_pager()},
                {QStringLiteral("skipSwitcher"), c->control->skip_switcher()},
                {QStringLiteral("maximizeHorizontal"),
                 static_cast<int>(c->maximizeMode() & win::maximize_mode::horizontal)},
                {QStringLiteral("maximizeVertical"),
                 static_cast<int>(c->maximizeMode() & win::maximize_mode::vertical)}};
    }

    QDBusMessage m_replyQueryWindowInfo;
    Space& space;
    Input* input;
};

}
}
