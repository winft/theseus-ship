/*
    SPDX-FileCopyrightText: 2013 Martin Gräßlin <mgraesslin@kde.org>
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "cursor.h"

#include "platform.h"
#include "pointer_redirect.h"
#include "singleton_interface.h"

#include <KConfig>
#include <KConfigGroup>
#include <QDBusConnection>
#include <QHash>

namespace KWin::input
{

cursor::cursor(KSharedConfigPtr config)
    : m_cursorTrackingCounter(0)
    , m_themeName("default")
    , m_themeSize(24)
    , config{config}
{
    singleton_interface::cursor = this;
    load_theme_settings();
    QDBusConnection::sessionBus().connect(QString(),
                                          QStringLiteral("/KGlobalSettings"),
                                          QStringLiteral("org.kde.KGlobalSettings"),
                                          QStringLiteral("notifyChange"),
                                          this,
                                          SLOT(kglobal_settings_notify_change(int, int)));
}

cursor::~cursor()
{
    singleton_interface::cursor = nullptr;
}

void cursor::load_theme_settings()
{
    QString themeName = QString::fromUtf8(qgetenv("XCURSOR_THEME"));
    bool ok = false;

    // XCURSOR_SIZE might not be set (e.g. by startkde)
    uint const themeSize = qEnvironmentVariableIntValue("XCURSOR_SIZE", &ok);

    if (!themeName.isEmpty() && ok) {
        update_theme(themeName, themeSize);
        return;
    }

    // didn't get from environment variables, read from config file
    load_theme_from_kconfig();
}

void cursor::load_theme_from_kconfig()
{
    KConfigGroup mousecfg(config, "Mouse");

    auto const themeName = mousecfg.readEntry("cursorTheme", "default");
    uint const themeSize = mousecfg.readEntry("cursorSize", 24);
    update_theme(themeName, themeSize);
}

void cursor::update_theme(QString const& name, int size)
{
    if (m_themeName != name || m_themeSize != size) {
        m_themeName = name;
        m_themeSize = size;
        Q_EMIT theme_changed();
    }
}

void cursor::kglobal_settings_notify_change(int type, int arg)
{
    Q_UNUSED(arg)
    if (type == 5 /*CursorChanged*/) {
        config->reparseConfiguration();
        load_theme_from_kconfig();

        // sync to environment
        qputenv("XCURSOR_THEME", m_themeName.toUtf8());
        qputenv("XCURSOR_SIZE", QByteArray::number(m_themeSize));
    }
}

QString const& cursor::theme_name() const
{
    return m_themeName;
}

int cursor::theme_size() const
{
    return m_themeSize;
}

QImage cursor::image() const
{
    return QImage();
}

QPoint cursor::hotspot() const
{
    return QPoint();
}

void cursor::mark_as_rendered()
{
}

bool cursor::is_hidden() const
{
    return hide_count > 0;
}

void cursor::show()
{
    hide_count--;
    if (hide_count == 0) {
        do_show();
    }
}

void cursor::hide()
{
    hide_count++;
    if (hide_count == 1) {
        do_hide();
    }
}

void cursor::do_show()
{
}

void cursor::do_hide()
{
}

QPoint const& cursor::current_pos() const
{
    return m_pos;
}

QPoint cursor::pos()
{
    do_get_pos();
    return m_pos;
}

void cursor::set_pos(QPoint const& pos)
{
    // first query the current pos to not warp to the already existing pos
    if (pos == cursor::pos()) {
        return;
    }
    m_pos = pos;
    do_set_pos();
}

void cursor::set_pos(int x, int y)
{
    cursor::set_pos(QPoint(x, y));
}

void cursor::do_set_pos()
{
    Q_EMIT pos_changed(m_pos);
}

void cursor::do_get_pos()
{
}

void cursor::update_pos(QPoint const& pos)
{
    if (m_pos == pos) {
        return;
    }
    m_pos = pos;
    Q_EMIT pos_changed(m_pos);
}

void cursor::update_pos(int x, int y)
{
    update_pos(QPoint(x, y));
}

bool cursor::is_image_tracking() const
{
    return m_cursorTrackingCounter > 0;
}

void cursor::start_image_tracking()
{
    ++m_cursorTrackingCounter;

    if (m_cursorTrackingCounter == 1) {
        do_start_image_tracking();
    }
}

void cursor::stop_image_tracking()
{
    assert(m_cursorTrackingCounter > 0);

    --m_cursorTrackingCounter;

    if (m_cursorTrackingCounter == 0) {
        do_stop_image_tracking();
    }
}

void cursor::do_start_image_tracking()
{
}

void cursor::do_stop_image_tracking()
{
}

}
