/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "cursor.h"

#include "main.h"
#include "platform.h"
#include "utils.h"
#include "xcbutils.h"

#include <KConfig>
#include <KConfigGroup>

#include <QDBusConnection>
#include <QHash>

namespace KWin::input
{

cursor::cursor()
    : QObject()
    , m_mousePollingCounter(0)
    , m_cursorTrackingCounter(0)
    , m_themeName("default")
    , m_themeSize(24)
{
    loadThemeSettings();
    QDBusConnection::sessionBus().connect(QString(),
                                          QStringLiteral("/KGlobalSettings"),
                                          QStringLiteral("org.kde.KGlobalSettings"),
                                          QStringLiteral("notifyChange"),
                                          this,
                                          SLOT(slotKGlobalSettingsNotifyChange(int, int)));
}

void cursor::loadThemeSettings()
{
    QString themeName = QString::fromUtf8(qgetenv("XCURSOR_THEME"));
    bool ok = false;

    // XCURSOR_SIZE might not be set (e.g. by startkde)
    uint const themeSize = qEnvironmentVariableIntValue("XCURSOR_SIZE", &ok);

    if (!themeName.isEmpty() && ok) {
        updateTheme(themeName, themeSize);
        return;
    }

    // didn't get from environment variables, read from config file
    loadThemeFromKConfig();
}

void cursor::loadThemeFromKConfig()
{
    KConfigGroup mousecfg(kwinApp()->inputConfig(), "Mouse");

    auto const themeName = mousecfg.readEntry("cursorTheme", "default");
    uint const themeSize = mousecfg.readEntry("cursorSize", 24);
    updateTheme(themeName, themeSize);
}

void cursor::updateTheme(QString const& name, int size)
{
    if (m_themeName != name || m_themeSize != size) {
        m_themeName = name;
        m_themeSize = size;
        Q_EMIT themeChanged();
    }
}

void cursor::slotKGlobalSettingsNotifyChange(int type, int arg)
{
    Q_UNUSED(arg)
    if (type == 5 /*CursorChanged*/) {
        kwinApp()->inputConfig()->reparseConfiguration();
        loadThemeFromKConfig();

        // sync to environment
        qputenv("XCURSOR_THEME", m_themeName.toUtf8());
        qputenv("XCURSOR_SIZE", QByteArray::number(m_themeSize));
    }
}

QString const& cursor::themeName() const
{
    return m_themeName;
}

int cursor::themeSize() const
{
    return m_themeSize;
}

QPoint const& cursor::currentPos() const
{
    return m_pos;
}

QPoint cursor::pos()
{
    doGetPos();
    return m_pos;
}

void cursor::setPos(QPoint const& pos)
{
    // first query the current pos to not warp to the already existing pos
    if (pos == cursor::pos()) {
        return;
    }
    m_pos = pos;
    doSetPos();
}

void cursor::setPos(int x, int y)
{
    cursor::setPos(QPoint(x, y));
}

xcb_cursor_t cursor::x11Cursor([[maybe_unused]] cursor_shape shape)
{
    return XCB_CURSOR_NONE;
}

xcb_cursor_t cursor::x11Cursor([[maybe_unused]] QByteArray const& name)
{
    return XCB_CURSOR_NONE;
}

void cursor::doSetPos()
{
    Q_EMIT posChanged(m_pos);
}

void cursor::doGetPos()
{
}

void cursor::updatePos(QPoint const& pos)
{
    if (m_pos == pos) {
        return;
    }
    m_pos = pos;
    Q_EMIT posChanged(m_pos);
}

void cursor::updatePos(int x, int y)
{
    updatePos(QPoint(x, y));
}

void cursor::startMousePolling()
{
    ++m_mousePollingCounter;
    if (m_mousePollingCounter == 1) {
        doStartMousePolling();
    }
}

void cursor::stopMousePolling()
{
    assert(m_mousePollingCounter > 0);

    --m_mousePollingCounter;

    if (m_mousePollingCounter == 0) {
        doStopMousePolling();
    }
}

void cursor::doStartMousePolling()
{
}

void cursor::doStopMousePolling()
{
}

bool cursor::isCursorTracking() const
{
    return m_cursorTrackingCounter > 0;
}

void cursor::startCursorTracking()
{
    ++m_cursorTrackingCounter;

    if (m_cursorTrackingCounter == 1) {
        doStartCursorTracking();
    }
}

void cursor::stopCursorTracking()
{
    assert(m_cursorTrackingCounter > 0);

    --m_cursorTrackingCounter;

    if (m_cursorTrackingCounter == 0) {
        doStopCursorTracking();
    }
}

void cursor::doStartCursorTracking()
{
}

void cursor::doStopCursorTracking()
{
}

QVector<QByteArray> cursor::cursorAlternativeNames(QByteArray const& name) const
{
    static QHash<QByteArray, QVector<QByteArray>> const alternatives = {
        {
            QByteArrayLiteral("left_ptr"),
            {
                QByteArrayLiteral("arrow"),
                QByteArrayLiteral("dnd-none"),
                QByteArrayLiteral("op_left_arrow"),
            },
        },
        {
            QByteArrayLiteral("cross"),
            {
                QByteArrayLiteral("crosshair"),
                QByteArrayLiteral("diamond-cross"),
                QByteArrayLiteral("cross-reverse"),
            },
        },
        {
            QByteArrayLiteral("up_arrow"),
            {
                QByteArrayLiteral("center_ptr"),
                QByteArrayLiteral("sb_up_arrow"),
                QByteArrayLiteral("centre_ptr"),
            },
        },
        {
            QByteArrayLiteral("wait"),
            {
                QByteArrayLiteral("watch"),
                QByteArrayLiteral("progress"),
            },
        },
        {
            QByteArrayLiteral("ibeam"),
            {
                QByteArrayLiteral("xterm"),
                QByteArrayLiteral("text"),
            },
        },
        {
            QByteArrayLiteral("size_all"),
            {
                QByteArrayLiteral("fleur"),
            },
        },
        {
            QByteArrayLiteral("pointing_hand"),
            {
                QByteArrayLiteral("hand2"),
                QByteArrayLiteral("hand"),
                QByteArrayLiteral("hand1"),
                QByteArrayLiteral("pointer"),
                QByteArrayLiteral("e29285e634086352946a0e7090d73106"),
                QByteArrayLiteral("9d800788f1b08800ae810202380a0822"),
            },
        },
        {
            QByteArrayLiteral("size_ver"),
            {
                QByteArrayLiteral("00008160000006810000408080010102"),
                QByteArrayLiteral("sb_v_double_arrow"),
                QByteArrayLiteral("v_double_arrow"),
                QByteArrayLiteral("n-resize"),
                QByteArrayLiteral("s-resize"),
                QByteArrayLiteral("col-resize"),
                QByteArrayLiteral("top_side"),
                QByteArrayLiteral("bottom_side"),
                QByteArrayLiteral("base_arrow_up"),
                QByteArrayLiteral("base_arrow_down"),
                QByteArrayLiteral("based_arrow_down"),
                QByteArrayLiteral("based_arrow_up"),
            },
        },
        {
            QByteArrayLiteral("size_hor"),
            {
                QByteArrayLiteral("028006030e0e7ebffc7f7070c0600140"),
                QByteArrayLiteral("sb_h_double_arrow"),
                QByteArrayLiteral("h_double_arrow"),
                QByteArrayLiteral("e-resize"),
                QByteArrayLiteral("w-resize"),
                QByteArrayLiteral("row-resize"),
                QByteArrayLiteral("right_side"),
                QByteArrayLiteral("left_side"),
            },
        },
        {
            QByteArrayLiteral("size_bdiag"),
            {
                QByteArrayLiteral("fcf1c3c7cd4491d801f1e1c78f100000"),
                QByteArrayLiteral("fd_double_arrow"),
                QByteArrayLiteral("bottom_left_corner"),
                QByteArrayLiteral("top_right_corner"),
            },
        },
        {
            QByteArrayLiteral("size_fdiag"),
            {
                QByteArrayLiteral("c7088f0f3e6c8088236ef8e1e3e70000"),
                QByteArrayLiteral("bd_double_arrow"),
                QByteArrayLiteral("bottom_right_corner"),
                QByteArrayLiteral("top_left_corner"),
            },
        },
        {
            QByteArrayLiteral("whats_this"),
            {
                QByteArrayLiteral("d9ce0ab605698f320427677b458ad60b"),
                QByteArrayLiteral("left_ptr_help"),
                QByteArrayLiteral("help"),
                QByteArrayLiteral("question_arrow"),
                QByteArrayLiteral("dnd-ask"),
                QByteArrayLiteral("5c6cd98b3f3ebcb1f9c7f1c204630408"),
            },
        },
        {
            QByteArrayLiteral("split_h"),
            {
                QByteArrayLiteral("14fef782d02440884392942c11205230"),
                QByteArrayLiteral("size_hor"),
            },
        },
        {
            QByteArrayLiteral("split_v"),
            {
                QByteArrayLiteral("2870a09082c103050810ffdffffe0204"),
                QByteArrayLiteral("size_ver"),
            },
        },
        {
            QByteArrayLiteral("forbidden"),
            {
                QByteArrayLiteral("03b6e0fcb3499374a867c041f52298f0"),
                QByteArrayLiteral("circle"),
                QByteArrayLiteral("dnd-no-drop"),
                QByteArrayLiteral("not-allowed"),
            },
        },
        {
            QByteArrayLiteral("left_ptr_watch"),
            {
                QByteArrayLiteral("3ecb610c1bf2410f44200f48c40d3599"),
                QByteArrayLiteral("00000000000000020006000e7e9ffc3f"),
                QByteArrayLiteral("08e8e1c95fe2fc01f976f1e063a24ccd"),
            },
        },
        {
            QByteArrayLiteral("openhand"),
            {
                QByteArrayLiteral("9141b49c8149039304290b508d208c40"),
                QByteArrayLiteral("all_scroll"),
                QByteArrayLiteral("all-scroll"),
            },
        },
        {
            QByteArrayLiteral("closedhand"),
            {
                QByteArrayLiteral("05e88622050804100c20044008402080"),
                QByteArrayLiteral("4498f0e0c1937ffe01fd06f973665830"),
                QByteArrayLiteral("9081237383d90e509aa00f00170e968f"),
                QByteArrayLiteral("fcf21c00b30f7e3f83fe0dfd12e71cff"),
            },
        },
        {
            QByteArrayLiteral("dnd-link"),
            {
                QByteArrayLiteral("link"),
                QByteArrayLiteral("alias"),
                QByteArrayLiteral("3085a0e285430894940527032f8b26df"),
                QByteArrayLiteral("640fb0e74195791501fd1ed57b41487f"),
                QByteArrayLiteral("a2a266d0498c3104214a47bd64ab0fc8"),
            },
        },
        {
            QByteArrayLiteral("dnd-copy"),
            {
                QByteArrayLiteral("copy"),
                QByteArrayLiteral("1081e37283d90000800003c07f3ef6bf"),
                QByteArrayLiteral("6407b0e94181790501fd1e167b474872"),
                QByteArrayLiteral("b66166c04f8c3109214a4fbd64a50fc8"),
            },
        },
        {
            QByteArrayLiteral("dnd-move"),
            {
                QByteArrayLiteral("move"),
            },
        },
        {
            QByteArrayLiteral("sw-resize"),
            {
                QByteArrayLiteral("size_bdiag"),
                QByteArrayLiteral("fcf1c3c7cd4491d801f1e1c78f100000"),
                QByteArrayLiteral("fd_double_arrow"),
                QByteArrayLiteral("bottom_left_corner"),
            },
        },
        {
            QByteArrayLiteral("se-resize"),
            {
                QByteArrayLiteral("size_fdiag"),
                QByteArrayLiteral("c7088f0f3e6c8088236ef8e1e3e70000"),
                QByteArrayLiteral("bd_double_arrow"),
                QByteArrayLiteral("bottom_right_corner"),
            },
        },
        {
            QByteArrayLiteral("ne-resize"),
            {
                QByteArrayLiteral("size_bdiag"),
                QByteArrayLiteral("fcf1c3c7cd4491d801f1e1c78f100000"),
                QByteArrayLiteral("fd_double_arrow"),
                QByteArrayLiteral("top_right_corner"),
            },
        },
        {
            QByteArrayLiteral("nw-resize"),
            {
                QByteArrayLiteral("size_fdiag"),
                QByteArrayLiteral("c7088f0f3e6c8088236ef8e1e3e70000"),
                QByteArrayLiteral("bd_double_arrow"),
                QByteArrayLiteral("top_left_corner"),
            },
        },
        {
            QByteArrayLiteral("n-resize"),
            {
                QByteArrayLiteral("size_ver"),
                QByteArrayLiteral("00008160000006810000408080010102"),
                QByteArrayLiteral("sb_v_double_arrow"),
                QByteArrayLiteral("v_double_arrow"),
                QByteArrayLiteral("col-resize"),
                QByteArrayLiteral("top_side"),
            },
        },
        {
            QByteArrayLiteral("e-resize"),
            {
                QByteArrayLiteral("size_hor"),
                QByteArrayLiteral("028006030e0e7ebffc7f7070c0600140"),
                QByteArrayLiteral("sb_h_double_arrow"),
                QByteArrayLiteral("h_double_arrow"),
                QByteArrayLiteral("row-resize"),
                QByteArrayLiteral("left_side"),
            },
        },
        {
            QByteArrayLiteral("s-resize"),
            {
                QByteArrayLiteral("size_ver"),
                QByteArrayLiteral("00008160000006810000408080010102"),
                QByteArrayLiteral("sb_v_double_arrow"),
                QByteArrayLiteral("v_double_arrow"),
                QByteArrayLiteral("col-resize"),
                QByteArrayLiteral("bottom_side"),
            },
        },
        {
            QByteArrayLiteral("w-resize"),
            {
                QByteArrayLiteral("size_hor"),
                QByteArrayLiteral("028006030e0e7ebffc7f7070c0600140"),
                QByteArrayLiteral("sb_h_double_arrow"),
                QByteArrayLiteral("h_double_arrow"),
                QByteArrayLiteral("right_side"),
            },
        },
    };

    auto it = alternatives.find(name);
    if (it != alternatives.end()) {
        return it.value();
    }

    return QVector<QByteArray>();
}

cursor* get_cursor()
{
    return kwinApp()->input->cursor.get();
}

}
