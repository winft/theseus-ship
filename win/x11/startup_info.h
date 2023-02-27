/*
    SPDX-FileCopyrightText: 2022 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include <kwin_export.h>

#include <QObject>
#include <QString>
#include <QWindow>

#include <sys/types.h>

// typedef struct _XDisplay Display;

struct xcb_connection_t;

namespace KWin::win::x11
{

class startup_info_id;
class startup_info_data;

class KWIN_EXPORT startup_info : public QObject
{
    Q_OBJECT
public:
    enum {
        CleanOnCantDetect = 1 << 0,
        DisableKWinModule = 1 << 1,
        AnnounceSilenceChanges = 1 << 2,
    };

    explicit startup_info(int flags, QObject* parent = nullptr);
    ~startup_info() override;

    enum startup_t {
        NoMatch,
        Match,
        CantDetect,
    };

    startup_t checkStartup(WId w, startup_info_id& id, startup_info_data& data);
    static QByteArray windowStartupId(WId w);

    class Data;
    class Private;

private:
    Private* const d;
};

class KWIN_EXPORT startup_info_id
{
public:
    bool operator==(const startup_info_id& id) const;
    bool operator!=(const startup_info_id& id) const;
    bool isNull() const;

    const QByteArray& id() const;
    unsigned long timestamp() const;

    startup_info_id();
    startup_info_id(const startup_info_id& data);
    ~startup_info_id();
    startup_info_id& operator=(const startup_info_id& data);
    bool operator<(const startup_info_id& id) const;

private:
    explicit startup_info_id(const QString& txt);
    friend class startup_info;
    friend class startup_info::Private;
    struct Private;
    Private* const d;
};

class KWIN_EXPORT startup_info_data
{
public:
    const QString& bin() const;
    const QString& name() const;
    const QString& description() const;

    const QString& icon() const;

    int desktop() const;

    QByteArray WMClass() const;

    void addPid(pid_t pid);
    QList<pid_t> pids() const;
    bool is_pid(pid_t pid) const;

    QByteArray hostname() const;

    enum TriState {
        Yes,
        No,
        Unknown,
    };

    /**
     * Return the silence status for the startup notification.
     * @return startup_info_data::Yes if visual feedback is silenced
     */
    TriState silent() const;

    /**
     * The X11 screen on which the startup notification is happening, -1 if unknown.
     */
    int screen() const;

    /**
     * The Xinerama screen for the startup notification, -1 if unknown.
     */
    int xinerama() const;

    /**
     * The .desktop file used to initiate this startup notification, or empty. This information
     * should be used only to identify the application, not to read any additional information.
     * @since 4.5
     **/
    QString applicationId() const;

    /**
     * Updates the notification data from the given data. Some data, such as the desktop
     * or the name, won't be rewritten if already set.
     * @param data the data to update
     */
    void update(const startup_info_data& data);

    /**
     * Constructor. Initializes all the data to their default empty values.
     */
    startup_info_data();

    /**
     * Copy constructor.
     */
    startup_info_data(const startup_info_data& data);
    ~startup_info_data();
    startup_info_data& operator=(const startup_info_data& data);

private:
    explicit startup_info_data(const QString& txt);
    friend class startup_info;
    friend class startup_info::Data;
    friend class startup_info::Private;
    struct Private;
    Private* const d;
};

}
