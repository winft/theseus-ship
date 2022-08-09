/*
    SPDX-FileCopyrightText: 2021 Vlad Zahorodnii <vlad.zahorodnii@kde.org>

    SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "desktop_background_item.h"
#include "base/platform.h"
#include "base/singleton_interface.h"
#include "scripting/scripting_logging.h"
#include "scripting/singleton_interface.h"
#include "scripting/space.h"

namespace KWin
{

desktop_background_item::desktop_background_item(QQuickItem* parent)
    : window_thumbnail_item(parent)
{
}

void desktop_background_item::componentComplete()
{
    window_thumbnail_item::componentComplete();
    updateWindow();
}

QString desktop_background_item::outputName() const
{
    return m_output ? m_output->name() : QString();
}

void desktop_background_item::setOutputName(QString const& name)
{
    auto const& outputs = base::singleton_interface::platform->get_outputs();
    setOutput(base::find_output(outputs, name));
}

base::output* desktop_background_item::output() const
{
    return m_output;
}

void desktop_background_item::setOutput(base::output* output)
{
    if (m_output != output) {
        m_output = output;
        updateWindow();
        Q_EMIT outputChanged();
    }
}

win::virtual_desktop* desktop_background_item::desktop() const
{
    return m_desktop;
}

void desktop_background_item::setDesktop(win::virtual_desktop* desktop)
{
    if (m_desktop != desktop) {
        m_desktop = desktop;
        updateWindow();
        Q_EMIT desktopChanged();
    }
}

QString desktop_background_item::activity() const
{
    return m_activity;
}

void desktop_background_item::setActivity(QString const& activity)
{
    if (m_activity != activity) {
        m_activity = activity;
        updateWindow();
        Q_EMIT activityChanged();
    }
}

void desktop_background_item::updateWindow()
{
    if (!isComponentComplete()) {
        return;
    }

    if (Q_UNLIKELY(!m_output)) {
        qCWarning(KWIN_SCRIPTING) << "desktop_background_item.output is required";
        return;
    }

    win::virtual_desktop* desktop = m_desktop;
    if (!desktop) {
        desktop = win::singleton_interface::virtual_desktops->current();
    }

    scripting::window* clientCandidate = nullptr;

    const auto clients = scripting::singleton_interface::qt_script_space->clientList();
    for (auto client : clients) {
        if (client->isDesktop() && client->isOnOutput(m_output) && client->isOnDesktop(desktop)) {
            // In the unlikely event there are multiple desktop windows (e.g. conky's floating panel
            // is of type "desktop") choose the one which matches the ouptut size, if possible.
            if (!clientCandidate || client->size() == m_output->geometry().size()) {
                clientCandidate = client;
            }
        }
    }

    setClient(clientCandidate);
}

} // namespace KWin

#include "moc_desktop_background_item.cpp"
