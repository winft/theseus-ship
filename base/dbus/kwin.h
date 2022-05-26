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
    explicit kwin(win::space& space);
    ~kwin() override;

public Q_SLOTS:
    int currentDesktop();
    Q_NOREPLY void killWindow();

    void nextDesktop();
    void previousDesktop();

    Q_NOREPLY void reconfigure();
    bool setCurrentDesktop(int desktop);

    bool startActivity(const QString& in0);
    bool stopActivity(const QString& in0);

    QString supportInformation();
    Q_NOREPLY void unclutterDesktop();
    Q_NOREPLY void showDebugConsole();
    void enableFtrace(bool enable);

    QVariantMap queryWindowInfo();
    QVariantMap getWindowInfo(const QString& uuid);

private Q_SLOTS:
    void becomeKWinService(const QString& service);

private:
    QString m_serviceName;
    QDBusMessage m_replyQueryWindowInfo;
    win::space& space;
};

}
}
