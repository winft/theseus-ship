/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#include "x11_console.h"

#include "ui_debug_console.h"

namespace KWin::debug
{

x11_console::x11_console(win::space& space)
    : console(space)
{
    m_ui->windowsView->setItemDelegate(new console_delegate(this));
    m_ui->windowsView->setModel(console_model::create(space, this));

    m_ui->tabWidget->setTabEnabled(1, false);
    m_ui->tabWidget->setTabEnabled(2, false);
    m_ui->tabWidget->setTabEnabled(3, false);
    m_ui->tabWidget->setTabEnabled(5, false);

    // for X11
    setWindowFlags(Qt::X11BypassWindowManagerHint);
}

}
