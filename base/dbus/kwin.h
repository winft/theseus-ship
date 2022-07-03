/*
    SPDX-FileCopyrightText: 2012 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <QObject>
#include <QtDBus>

namespace KWin
{

namespace win
{
class space;
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
class kwin : public QObject, protected QDBusContext
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
    Q_NOREPLY void unclutterDesktop()
    {
        unclutter_desktop_impl();
    }

    Q_NOREPLY void showDebugConsole();
    void enableFtrace(bool enable);

    QVariantMap queryWindowInfo()
    {
        return query_window_info_impl();
    }
    QVariantMap getWindowInfo(QString const& uuid)
    {
        return get_window_info_impl(uuid);
    }

private Q_SLOTS:
    void becomeKWinService(const QString& service);

protected:
    virtual int current_desktop_impl() = 0;
    virtual void kill_window_impl() = 0;

    virtual void next_desktop_impl() = 0;
    virtual void previous_desktop_impl() = 0;

    virtual bool set_current_desktop_impl(int desktop) = 0;

    virtual QString support_information_impl() = 0;
    virtual void unclutter_desktop_impl() = 0;

    virtual QVariantMap query_window_info_impl() = 0;
    virtual QVariantMap get_window_info_impl(QString const& uuid) = 0;

private:
    QString m_serviceName;
    win::space_qobject& space;
};

class kwin_impl : public kwin
{
public:
    explicit kwin_impl(win::space& space);

    int current_desktop_impl() override;
    void kill_window_impl() override;

    void next_desktop_impl() override;
    void previous_desktop_impl() override;

    bool set_current_desktop_impl(int desktop) override;

    QString support_information_impl() override;
    void unclutter_desktop_impl() override;

    QVariantMap query_window_info_impl() override;
    QVariantMap get_window_info_impl(QString const& uuid) override;

private:
    QDBusMessage m_replyQueryWindowInfo;
    win::space& space;
};

}
}
