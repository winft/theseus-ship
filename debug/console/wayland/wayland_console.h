/*
    SPDX-FileCopyrightText: 2021 Roman Gilg <subdiff@gmail.com>

    SPDX-License-Identifier: GPL-2.0-or-later
*/
#pragma once

#include "debug/console/console.h"

namespace KWin
{

namespace base::wayland
{
class platform;
}

namespace win::wayland
{

template<typename Base>
class space;

template<typename Space>
class window;
}

using wayland_space = win::wayland::space<base::wayland::platform>;

namespace debug
{

class input_filter;

class KWIN_EXPORT wayland_console : public console
{
    Q_OBJECT
public:
    wayland_console(wayland_space& space);
    ~wayland_console();

private:
    void update_keyboard_tab();

    std::unique_ptr<input_filter> m_inputFilter;
};

class KWIN_EXPORT wayland_console_model : public console_model
{
    Q_OBJECT
public:
    explicit wayland_console_model(win::space& space, QObject* parent = nullptr);
    ~wayland_console_model();

    bool get_client_count(int parent_id, int& count) const override;
    bool get_property_count(QModelIndex const& parent, int& count) const override;

    bool get_client_index(int row, int column, int parent_id, QModelIndex& index) const override;
    bool get_property_index(int row,
                            int column,
                            QModelIndex const& parent,
                            QModelIndex& index) const override;

    QVariant get_client_data(QModelIndex const& index, int role) const override;
    QVariant get_client_property_data(QModelIndex const& index, int role) const override;

    int topLevelRowCount() const override;
    console_window* shellClient(QModelIndex const& index) const;

    std::vector<std::unique_ptr<console_window>> m_shellClients;
};

class KWIN_EXPORT wayland_console_delegate : public console_delegate
{
    Q_OBJECT
public:
    explicit wayland_console_delegate(QObject* parent = nullptr);

    QString displayText(const QVariant& value, const QLocale& locale) const override;
};

}
}
