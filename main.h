/********************************************************************
 KWin - the KDE window manager
 This file is part of the KDE project.

Copyright (C) 1999, 2000 Matthias Ettrich <ettrich@kde.org>
Copyright (C) 2003 Lubos Lunak <l.lunak@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

#ifndef MAIN_H
#define MAIN_H

#include "input/platform.h"

#include <kwinglobals.h>
#include <config-kwin.h>

#include <KSharedConfig>

#include <QApplication>
#include <QProcessEnvironment>

#include <memory>

class QCommandLineParser;

namespace KWin
{

namespace base
{

namespace seat
{
class session;
}

namespace wayland
{
class server;
}

namespace x11
{
class event_filter_manager;
}

class options;
class platform;

}

namespace desktop
{
class screen_locker_watcher;
}

class KWIN_EXPORT Application : public  QApplication
{
    Q_OBJECT
    Q_PROPERTY(quint32 x11Time READ x11Time WRITE setX11Time)
    Q_PROPERTY(quint32 x11RootWindow READ x11RootWindow CONSTANT)
    Q_PROPERTY(void *x11Connection READ x11Connection NOTIFY x11ConnectionChanged)
    Q_PROPERTY(int x11ScreenNumber READ x11ScreenNumber CONSTANT)
    Q_PROPERTY(KSharedConfigPtr config READ config WRITE setConfig)
public:
    /**
     * @brief This enum provides the various operation modes of KWin depending on the available
     * Windowing Systems at startup. For example whether KWin only talks to X11 or also to a Wayland
     * Compositor.
     *
     */
    enum OperationMode {
        /**
         * @brief KWin uses only X11 for managing windows and compositing
         */
        OperationModeX11,
        /**
         * @brief KWin uses only Wayland
         */
        OperationModeWaylandOnly,
        /**
         * @brief KWin uses Wayland and controls a nested Xwayland server.
         */
        OperationModeXwayland
    };

    ~Application() override;

    virtual base::platform& get_base() = 0;

    KSharedConfigPtr config() const {
        return m_config;
    }
    void setConfig(KSharedConfigPtr config) {
        m_config = std::move(config);
    }

    /**
     * @brief The operation mode used by KWin.
     *
     * @return OperationMode
     */
    OperationMode operationMode() const;
    void setOperationMode(OperationMode mode);
    bool shouldUseWaylandForCompositing() const;

    void setupEventFilters();
    void setupTranslator();
    void setupCommandLine(QCommandLineParser *parser);
    void processCommandLine(QCommandLineParser *parser);

    xcb_timestamp_t x11Time() const {
        return m_x11Time;
    }
    enum class TimestampUpdate {
        OnlyIfLarger,
        Always
    };
    void setX11Time(xcb_timestamp_t timestamp, TimestampUpdate force = TimestampUpdate::OnlyIfLarger) {
        if ((timestamp > m_x11Time || force == TimestampUpdate::Always) && timestamp != 0) {
            m_x11Time = timestamp;
        }
    }

    void update_x11_time_from_clock();
    void update_x11_time_from_event(xcb_generic_event_t *event);

    static void setCrashCount(int count);
    static bool wasCrash();
    void resetCrashesCount();

    /**
     * Creates the KAboutData object for the KWin instance and registers it as
     * KAboutData::setApplicationData.
     */
    static void createAboutData();

    /**
     * @returns the X11 Screen number. If not applicable it's set to @c -1.
     */
    int x11ScreenNumber();
    /**
     * Sets the X11 screen number of this KWin instance to @p screenNumber.
     */
    void setX11ScreenNumber(int screenNumber);

    /**
     * @returns the X11 root window.
     */
    xcb_window_t x11RootWindow() const {
        return m_rootWindow;
    }

    /**
     * Inheriting classes should use this method to set the X11 root window
     * before accessing any X11 specific code pathes.
     */
    void setX11RootWindow(xcb_window_t root) {
        m_rootWindow = root;
    }

    /**
     * @returns the X11 xcb connection
     */
    xcb_connection_t *x11Connection() const {
        return m_connection;
    }

    /**
     * Inheriting classes should use this method to set the xcb connection
     * before accessing any X11 specific code pathes.
     */
    void setX11Connection(xcb_connection_t *c, bool emit_change = true) {
        m_connection = c;
        if (emit_change) {
            Q_EMIT x11ConnectionChanged();
        }
    }

    virtual QProcessEnvironment processStartupEnvironment() const;
    virtual void setProcessStartupEnvironment(QProcessEnvironment const& environment);

    bool isTerminating() const {
        return m_terminating;
    }

    static void setupMalloc();
    static void setupLocalizedString();
    virtual void notifyKSplash() {}

    virtual bool is_screen_locked() const;

    std::unique_ptr<base::options> options;
    std::unique_ptr<base::seat::session> session;
    std::unique_ptr<base::x11::event_filter_manager> x11_event_filters;
    std::unique_ptr<desktop::screen_locker_watcher> screen_locker_watcher;

Q_SIGNALS:
    void x11ConnectionChanged();
    void x11ConnectionAboutToBeDestroyed();
    void startup_finished();
    void virtualTerminalCreated();

protected:
    Application(OperationMode mode, int &argc, char **argv);

    void prepare_start();

    void createOptions();

    void setTerminating() {
        m_terminating = true;
    }

protected:
    static int crashes;

private:
    KSharedConfigPtr m_config;
    OperationMode m_operationMode;
    int x11_screen_number{-1};
    xcb_timestamp_t m_x11Time = XCB_TIME_CURRENT_TIME;
    xcb_window_t m_rootWindow = XCB_WINDOW_NONE;
    xcb_connection_t *m_connection = nullptr;
    bool m_terminating = false;
};

inline static Application *kwinApp()
{
    return static_cast<Application*>(QCoreApplication::instance());
}

}

#endif
